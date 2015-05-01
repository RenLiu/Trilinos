/*
// @HEADER
// ***********************************************************************
//
//          Tpetra: Templated Linear Algebra Services Package
//                 Copyright (2008) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Michael A. Heroux (maherou@sandia.gov)
//
// ************************************************************************
// @HEADER
*/

#include <Teuchos_UnitTestHarness.hpp>
#include <TpetraCore_ETIHelperMacros.h>
#include <Tpetra_Details_FixedHashTable.hpp>
#include <Kokkos_Core.hpp>

namespace { // (anonymous)

// Take care of Kokkos execution space initialization automatically.
// This ensures that each execution space gets initialized at most
// once, over all the tests in this file.  This may make the test run
// faster (esp. for CUDA), and it also simplifies the test code.
template<class ExecSpace>
struct InitExecSpace {
  InitExecSpace () {
    if (count_ == 0) {
      if (! ExecSpace::is_initialized ()) {
        ExecSpace::initialize ();
        mustFinalize_ = true;
      }
    }
    ++count_;
  }

  ~InitExecSpace () {
    --count_;
    if (count_ <= 0 && mustFinalize_ && ExecSpace::is_initialized ()) {
      ExecSpace::finalize ();
      mustFinalize_ = false;
    }
  }

  bool isInitialized () {
    return ExecSpace::is_initialized ();
  }

  static int count_;
  static bool mustFinalize_;
};

#ifdef KOKKOS_HAVE_SERIAL
  template<> int InitExecSpace<Kokkos::Serial>::count_ = 0;
  template<> bool InitExecSpace<Kokkos::Serial>::mustFinalize_ = false;
#endif // KOKKOS_HAVE_SERIAL

#ifdef KOKKOS_HAVE_OPENMP
  template<> int InitExecSpace<Kokkos::OpenMP>::count_ = 0;
  template<> bool InitExecSpace<Kokkos::OpenMP>::mustFinalize_ = false;
#endif // KOKKOS_HAVE_OPENMP

#ifdef KOKKOS_HAVE_PTHREAD
  template<> int InitExecSpace<Kokkos::Threads>::count_ = 0;
  template<> bool InitExecSpace<Kokkos::Threads>::mustFinalize_ = false;
#endif // KOKKOS_HAVE_PTHREAD

#ifdef KOKKOS_HAVE_CUDA
  template<> int InitExecSpace<Kokkos::Cuda>::count_ = 0;
  template<> bool InitExecSpace<Kokkos::Cuda>::mustFinalize_ = false;
#endif // KOKKOS_HAVE_CUDA


  template<class KeyType, class ValueType, class DeviceType>
  struct TestFixedHashTable {
    typedef Tpetra::Details::FixedHashTable<KeyType, ValueType> table_type;
    // Fix the layout, so that it's the same on all devices.  This
    // makes testing FixedHashTable's templated copy constructor
    // easier.
    typedef Kokkos::View<const KeyType*, Kokkos::LayoutLeft, DeviceType> keys_type;
    typedef Kokkos::View<const ValueType*, Kokkos::LayoutLeft, DeviceType> vals_type;
    typedef typename keys_type::size_type size_type;

    static bool
    testKeys (std::ostream& out,
              const table_type& table,
              const keys_type& keys,
              const vals_type& vals)
    {
      using std::endl;
      const size_type numKeys = keys.dimension_0 ();

      out << "Test " << numKeys << "key" << (numKeys != 1 ? "s" : "") << ":  ";
      TEUCHOS_TEST_FOR_EXCEPTION
        (numKeys != vals.dimension_0 (), std::logic_error,
         "keys and vals are not the same length!  keys.dimension_0() = "
         << numKeys << " != vals.dimension_0() = " << vals.dimension_0 ()
         << ".");

      auto keys_h = Kokkos::create_mirror_view (keys);
      Kokkos::deep_copy (keys_h, keys);
      auto vals_h = Kokkos::create_mirror_view (vals);
      Kokkos::deep_copy (vals_h, vals);
      size_type badCount = 0;
      for (size_type i = 0; i < keys.size (); ++i) {
        const KeyType key = keys_h(i);
        const ValueType expectedVal = vals_h(i);
        const ValueType actualVal = table.get (key);
        if (actualVal != expectedVal) {
          ++badCount;
        }
      }

      if (badCount == 0) {
        out << "PASSED" << endl;
        return true;
      }
      else {
        out << "FAILED (" << badCount << " out of " << numKeys << ")" << endl;
        return false;
      }
    }
  };

  // Test that an empty table really is empty.
  //
  // ValueType and KeyType are "backwards" because they correspond to
  // LO resp. GO.  (LO, GO) is the natural order for Tpetra's test
  // macros.
  TEUCHOS_UNIT_TEST_TEMPLATE_3_DECL(FixedHashTable_ArrayView, Empty, ValueType, KeyType, DeviceType)
  {
    using std::endl;
    typedef Tpetra::Details::FixedHashTable<KeyType, ValueType> table_type;
    typedef typename Kokkos::View<const KeyType*, Kokkos::LayoutLeft, DeviceType>::size_type size_type;
    typedef typename DeviceType::execution_space execution_space;

    out << "Test empty table" << endl;
    Teuchos::OSTab tab0 (out);

    InitExecSpace<execution_space> init;
    // This avoids warnings for 'init' being unused.
    TEST_EQUALITY_CONST( init.isInitialized (), true );
    if (! init.isInitialized ()) {
      return; // avoid crashes if initialization failed
    }

    const size_type numKeys = 0;
    Kokkos::View<KeyType*, Kokkos::LayoutLeft, DeviceType> keys ("keys", numKeys);
    auto keys_h = Kokkos::create_mirror_view (keys);
    Kokkos::deep_copy (keys, keys_h);

    // Pick something other than 0, just to make sure that it works.
    const ValueType startingValue = 1;

    Teuchos::ArrayView<const KeyType> keys_av (keys_h.ptr_on_device (), numKeys);
    out << "Create table" << endl;

    Teuchos::RCP<table_type> table;
    TEST_NOTHROW( table = Teuchos::rcp (new table_type (keys_av, startingValue)) );
    if (table.is_null ()) {
      return; // above constructor must have thrown
    }

    KeyType key = 0;
    ValueType val = 0;
    TEST_NOTHROW( val = table->get (key) );
    TEST_EQUALITY( val, Teuchos::OrdinalTraits<ValueType>::invalid () );

    key = 1;
    TEST_NOTHROW( val = table->get (key) );
    TEST_EQUALITY( val, Teuchos::OrdinalTraits<ValueType>::invalid () );

    key = -1;
    TEST_NOTHROW( val = table->get (key) );
    TEST_EQUALITY( val, Teuchos::OrdinalTraits<ValueType>::invalid () );

    key = 42;
    TEST_NOTHROW( val = table->get (key) );
    TEST_EQUALITY( val, Teuchos::OrdinalTraits<ValueType>::invalid () );
  }

  // Test contiguous keys, with the constructor that takes a
  // Teuchos::ArrayView of keys and a single starting value.
  //
  // ValueType and KeyType are "backwards" because they correspond to
  // LO resp. GO.  (LO, GO) is the natural order for Tpetra's test
  // macros.
  TEUCHOS_UNIT_TEST_TEMPLATE_3_DECL(FixedHashTable_ArrayView, ContigKeysStartingValue, ValueType, KeyType, DeviceType)
  {
    using std::endl;
    typedef Tpetra::Details::FixedHashTable<KeyType, ValueType> table_type;
    typedef typename Kokkos::View<const KeyType*, Kokkos::LayoutLeft, DeviceType>::size_type size_type;
    typedef typename DeviceType::execution_space execution_space;

    out << "Test contiguous keys, with constructor that takes a single "
      "starting value and produces contiguous values" << endl;
    Teuchos::OSTab tab0 (out);

    InitExecSpace<execution_space> init;
    // This avoids warnings for 'init' being unused.
    TEST_EQUALITY_CONST( init.isInitialized (), true );
    if (! init.isInitialized ()) {
      return; // avoid crashes if initialization failed
    }

    const size_type numKeys = 10;
    Kokkos::View<KeyType*, Kokkos::LayoutLeft, DeviceType> keys ("keys", numKeys);
    auto keys_h = Kokkos::create_mirror_view (keys);
    // Start with some number other than 0, just to make sure that
    // it works.
    for (size_type i = 0; i < numKeys; ++i) {
      keys_h(i) = static_cast<KeyType> (i + 20);
    }
    Kokkos::deep_copy (keys, keys_h);

    // Pick something other than 0, just to make sure that it works.
    const ValueType startingValue = 1;

    // The hash table doesn't need this; we use it only for testing.
    Kokkos::View<ValueType*, Kokkos::LayoutLeft, DeviceType> vals ("vals", numKeys);
    auto vals_h = Kokkos::create_mirror_view (vals);
    for (size_type i = 0; i < numKeys; ++i) {
      vals_h(i) = static_cast<ValueType> (i) + startingValue;
    }
    Kokkos::deep_copy (vals, vals_h);

    Teuchos::ArrayView<const KeyType> keys_av (keys_h.ptr_on_device (), numKeys);
    out << " Create table" << endl;

    Teuchos::RCP<table_type> table;
    TEST_NOTHROW( table = Teuchos::rcp (new table_type (keys_av, startingValue)) );
    {
      Teuchos::OSTab tab1 (out);
      success = TestFixedHashTable<KeyType, ValueType, DeviceType>::testKeys (out, *table, keys, vals);
      TEST_EQUALITY_CONST( success, true );
    }
  }

  // Test noncontiguous keys, with the constructor that takes a
  // Teuchos::ArrayView of keys and a single starting value.
  //
  // ValueType and KeyType are "backwards" because they correspond to
  // LO resp. GO.  (LO, GO) is the natural order for Tpetra's test
  // macros.
  TEUCHOS_UNIT_TEST_TEMPLATE_3_DECL(FixedHashTable_ArrayView, NoncontigKeysStartingValue, ValueType, KeyType, DeviceType)
  {
    using std::endl;
    typedef Tpetra::Details::FixedHashTable<KeyType, ValueType> table_type;
    typedef typename Kokkos::View<const KeyType*, Kokkos::LayoutLeft, DeviceType>::size_type size_type;
    typedef typename DeviceType::execution_space execution_space;

    out << "Test noncontiguous keys, with constructor that takes a single "
      "starting value and produces contiguous values" << endl;
    Teuchos::OSTab tab0 (out);

    InitExecSpace<execution_space> init;
    // This avoids warnings for 'init' being unused.
    TEST_EQUALITY_CONST( init.isInitialized (), true );
    if (! init.isInitialized ()) {
      return; // avoid crashes if initialization failed
    }

    const size_type numKeys = 5;
    Kokkos::View<KeyType*, Kokkos::LayoutLeft, DeviceType> keys ("keys", numKeys);
    auto keys_h = Kokkos::create_mirror_view (keys);
    keys_h(0) = static_cast<KeyType> (10);
    keys_h(1) = static_cast<KeyType> (8);
    keys_h(2) = static_cast<KeyType> (12);
    keys_h(3) = static_cast<KeyType> (17);
    keys_h(4) = static_cast<KeyType> (7);
    Kokkos::deep_copy (keys, keys_h);

    // Pick something other than 0, just to make sure that it works.
    const ValueType startingValue = 1;

    // The hash table doesn't need this; we use it only for testing.
    Kokkos::View<ValueType*, Kokkos::LayoutLeft, DeviceType> vals ("vals", numKeys);
    auto vals_h = Kokkos::create_mirror_view (vals);
    for (size_type i = 0; i < numKeys; ++i) {
      vals_h(i) = static_cast<ValueType> (i) + startingValue;
    }
    Kokkos::deep_copy (vals, vals_h);

    Teuchos::ArrayView<const KeyType> keys_av (keys_h.ptr_on_device (), numKeys);
    out << " Create table" << endl;

    Teuchos::RCP<table_type> table;
    TEST_NOTHROW( table = Teuchos::rcp (new table_type (keys_av, startingValue)) );
    {
      Teuchos::OSTab tab1 (out);
      success = TestFixedHashTable<KeyType, ValueType, DeviceType>::testKeys (out, *table, keys, vals);
      TEST_EQUALITY_CONST( success, true );
    }
  }

  template<class KeyType, class ValueType, class OutDeviceType, class InDeviceType>
  struct TestCopyCtor {
    typedef Tpetra::Details::FixedHashTable<KeyType, ValueType, OutDeviceType> out_table_type;
    typedef Tpetra::Details::FixedHashTable<KeyType, ValueType, InDeviceType> in_table_type;
    typedef Kokkos::View<const KeyType*, Kokkos::LayoutLeft, InDeviceType> keys_type;
    typedef Kokkos::View<const ValueType*, Kokkos::LayoutLeft, InDeviceType> vals_type;

    static void
    test (std::ostream& out,
          bool& success,
          const in_table_type& inTable,
          const keys_type& keys,
          const vals_type& vals,
          const std::string& outDeviceName,
          const std::string& inDeviceName)
    {
      using std::endl;
      Teuchos::OSTab tab1 (out);
      out << "Test FixedHashTable copy constructor from " << inDeviceName
          << " to " << outDeviceName << endl;

      // Initialize the output device's execution space, if necessary.
      InitExecSpace<typename OutDeviceType::execution_space> init;
      // This avoids warnings for 'init' being unused.
      TEST_EQUALITY_CONST( init.isInitialized (), true );
      if (! init.isInitialized ()) {
        return; // avoid crashes if initialization failed
      }

      Teuchos::RCP<out_table_type> outTable;
      TEST_NOTHROW( outTable = Teuchos::rcp (new out_table_type (inTable)) );
      if (outTable.is_null ()) {
        return;
      }

      Kokkos::View<KeyType*, Kokkos::LayoutLeft, OutDeviceType> keys_out ("keys", keys.dimension_0 ());
      Kokkos::deep_copy (keys_out, keys);
      Kokkos::View<ValueType*, Kokkos::LayoutLeft, OutDeviceType> vals_out ("vals", vals.dimension_0 ());
      Kokkos::deep_copy (vals_out, vals);

      success = TestFixedHashTable<KeyType, ValueType, OutDeviceType>::testKeys (out, *outTable, keys_out, vals_out);
      TEST_EQUALITY_CONST( success, true );
    }
  };

  // Test FixedHashTable's templated copy constructor.
  //
  // ValueType and KeyType are "backwards" because they correspond to
  // LO resp. GO.  (LO, GO) is the natural order for Tpetra's test
  // macros.
  TEUCHOS_UNIT_TEST_TEMPLATE_3_DECL(FixedHashTable_ArrayView, CopyCtor, ValueType, KeyType, DeviceType)
  {
    using std::endl;
    typedef Tpetra::Details::FixedHashTable<KeyType, ValueType> table_type;
    typedef typename Kokkos::View<const KeyType*, DeviceType>::size_type size_type;
    typedef typename DeviceType::execution_space execution_space;

    out << "Test FixedHashTable's templated copy constructor" << endl;
    Teuchos::OSTab tab0 (out);

    InitExecSpace<execution_space> init;
    // This avoids warnings for 'init' being unused.
    TEST_EQUALITY_CONST( init.isInitialized (), true );
    if (! init.isInitialized ()) {
      return; // avoid crashes if initialization failed
    }

    // Create the same set of key,value pairs as in the previous test.
    const size_type numKeys = 5;
    Kokkos::View<KeyType*, DeviceType> keys ("keys", numKeys);
    auto keys_h = Kokkos::create_mirror_view (keys);
    keys_h(0) = static_cast<KeyType> (10);
    keys_h(1) = static_cast<KeyType> (8);
    keys_h(2) = static_cast<KeyType> (12);
    keys_h(3) = static_cast<KeyType> (17);
    keys_h(4) = static_cast<KeyType> (7);
    Kokkos::deep_copy (keys, keys_h);

    // Pick something other than 0, just to make sure that it works.
    const ValueType startingValue = 1;

    // The hash table doesn't need this; we use it only for testing.
    Kokkos::View<ValueType*, DeviceType> vals ("vals", numKeys);
    auto vals_h = Kokkos::create_mirror_view (vals);
    for (size_type i = 0; i < numKeys; ++i) {
      vals_h(i) = static_cast<ValueType> (i) + startingValue;
    }
    Kokkos::deep_copy (vals, vals_h);

    Teuchos::ArrayView<const KeyType> keys_av (keys_h.ptr_on_device (), numKeys);
    out << " Create table" << endl;

    Teuchos::RCP<table_type> table;
    TEST_NOTHROW( table = Teuchos::rcp (new table_type (keys_av, startingValue)) );
    {
      Teuchos::OSTab tab1 (out);
      success = TestFixedHashTable<KeyType, ValueType, DeviceType>::testKeys (out, *table, keys, vals);
      TEST_EQUALITY_CONST( success, true );
    }

    // Print a warning if only one execution space is enabled in
    // Kokkos, because this means that we can't test the templated
    // copy constructor.
    bool testedAtLeastOnce = false;

#ifdef KOKKOS_HAVE_SERIAL
    if (! std::is_same<execution_space, Kokkos::Serial>::value) {
      // The test initializes the output device's execution space if necessary.
      TestCopyCtor<KeyType, ValueType, Kokkos::Device<Kokkos::Serial, Kokkos::HostSpace>,
        DeviceType>::test (out, success, *table, keys, vals,
                           "Kokkos::Device<Kokkos::Serial, Kokkos::HostSpace>",
                           typeid(DeviceType).name ());
      testedAtLeastOnce = true;
    }
#endif // KOKKOS_HAVE_SERIAL

#ifdef KOKKOS_HAVE_OPENMP
    if (! std::is_same<execution_space, Kokkos::OpenMP>::value) {
      TestCopyCtor<KeyType, ValueType, Kokkos::Device<Kokkos::OpenMP, Kokkos::HostSpace>,
        DeviceType>::test (out, success, *table, keys, vals,
                           "Kokkos::Device<Kokkos::OpenMP, Kokkos::HostSpace>",
                           typeid(DeviceType).name ());
      testedAtLeastOnce = true;
    }
#endif // KOKKOS_HAVE_OPENMP

#ifdef KOKKOS_HAVE_PTHREAD
    if (! std::is_same<execution_space, Kokkos::Threads>::value) {
      TestCopyCtor<KeyType, ValueType, Kokkos::Device<Kokkos::Threads, Kokkos::HostSpace>,
        DeviceType>::test (out, success, *table, keys, vals,
                           "Kokkos::Device<Kokkos::Threads, Kokkos::HostSpace>",
                           typeid(DeviceType).name ());
      testedAtLeastOnce = true;
    }
#endif // KOKKOS_HAVE_PTHREAD

#ifdef KOKKOS_HAVE_CUDA
    {
      if (! std::is_same<DeviceType::memory_space, Kokkos::CudaSpace>::value) {
        TestCopyCtor<KeyType, ValueType, Kokkos::Device<Kokkos::Cuda, Kokkos::CudaSpace>,
          DeviceType>::test (out, success, *table, keys, vals,
                             "Kokkos::Device<Kokkos::Cuda, Kokkos::CudaSpace>",
                             typeid(DeviceType).name ());
        testedAtLeastOnce = true;
      }
      if (! std::is_same<DeviceType::memory_space, Kokkos::CudaUVMSpace>::value) {
        TestCopyCtor<KeyType, ValueType, Kokkos::Device<Kokkos::Cuda, Kokkos::CudaUVMSpace>,
          DeviceType>::test (out, success, *table, keys, vals,
                             "Kokkos::Device<Kokkos::Cuda, Kokkos::CudaUVMSpace>",
                             typeid(DeviceType).name ());
        testedAtLeastOnce = true;
      }
    }
#endif // KOKKOS_HAVE_CUDA

    if (! testedAtLeastOnce) {
      out << "*** WARNING: Did not actually test FixedHashTable's templated "
        "copy constructor, since only one Kokkos execution space is enabled!"
        " ***" << endl;
    }
  }

  //
  // Instantiations of the templated unit test(s) above.
  //

  // Magic that makes Tpetra tests work.  (Not _quite_ magic; it
  // relates to macros not liking arguments with commas or spaces in
  // them.  It defines some typedefs to avoid this.)
  TPETRA_ETI_MANGLING_TYPEDEFS()

  // Set of all unit tests, templated on all three template parameters.
#define UNIT_TEST_GROUP_3( LO, GO, DEVICE ) \
  TEUCHOS_UNIT_TEST_TEMPLATE_3_INSTANT( FixedHashTable_ArrayView, ContigKeysStartingValue, LO, GO, DEVICE ) \
  TEUCHOS_UNIT_TEST_TEMPLATE_3_INSTANT( FixedHashTable_ArrayView, NoncontigKeysStartingValue, LO, GO, DEVICE ) \
  TEUCHOS_UNIT_TEST_TEMPLATE_3_INSTANT( FixedHashTable_ArrayView, Empty, LO, GO, DEVICE ) \
  TEUCHOS_UNIT_TEST_TEMPLATE_3_INSTANT( FixedHashTable_ArrayView, CopyCtor, LO, GO, DEVICE )

  // The typedefs below are there because macros don't like arguments
  // with commas in them.

#ifdef KOKKOS_HAVE_SERIAL
  typedef Kokkos::Device<Kokkos::Serial, Kokkos::HostSpace> serial_device_type;

#define UNIT_TEST_GROUP_SERIAL( LO, GO ) \
  UNIT_TEST_GROUP_3( LO, GO, serial_device_type )

  TPETRA_INSTANTIATE_LG( UNIT_TEST_GROUP_SERIAL )

#else
#  define UNIT_TEST_GROUP_SERIAL( LO, GO )
#endif // KOKKOS_HAVE_SERIAL


#ifdef KOKKOS_HAVE_OPENMP
  typedef Kokkos::Device<Kokkos::OpenMP, Kokkos::HostSpace> openmp_device_type;

#define UNIT_TEST_GROUP_OPENMP( LO, GO ) \
  UNIT_TEST_GROUP_3( LO, GO, openmp_device_type )

  TPETRA_INSTANTIATE_LG( UNIT_TEST_GROUP_OPENMP )

#else
#  define UNIT_TEST_GROUP_OPENMP( LO, GO )
#endif // KOKKOS_HAVE_OPENMP


#ifdef KOKKOS_HAVE_PTHREAD
  typedef Kokkos::Device<Kokkos::Threads, Kokkos::HostSpace> threads_device_type;

#define UNIT_TEST_GROUP_PTHREAD( LO, GO ) \
  UNIT_TEST_GROUP_3( LO, GO, threads_device_type )

  TPETRA_INSTANTIATE_LG( UNIT_TEST_GROUP_PTHREAD )

#else
#  define UNIT_TEST_GROUP_PTHREAD( LO, GO )
#endif // KOKKOS_HAVE_PTHREAD


#ifdef KOKKOS_HAVE_CUDA
  typedef Kokkos::Device<Kokkos::Cuda, Kokkos::CudaSpace> cuda_device_type;

#define UNIT_TEST_GROUP_CUDA( LO, GO ) \
  UNIT_TEST_GROUP_3( LO, GO, cuda_device_type )

  TPETRA_INSTANTIATE_LG( UNIT_TEST_GROUP_CUDA )

#else
#  define UNIT_TEST_GROUP_CUDA( LO, GO )
#endif // KOKKOS_HAVE_CUDA


#ifdef KOKKOS_HAVE_CUDA
  typedef Kokkos::Device<Kokkos::Cuda, Kokkos::CudaUVMSpace> cuda_uvm_device_type;

#define UNIT_TEST_GROUP_CUDA_UVM( LO, GO ) \
  UNIT_TEST_GROUP_3( LO, GO, cuda_uvm_device_type )

  TPETRA_INSTANTIATE_LG( UNIT_TEST_GROUP_CUDA_UVM )

#else
#  define UNIT_TEST_GROUP_CUDA_UVM( LO, GO )
#endif // KOKKOS_HAVE_CUDA

} // namespace (anonymous)