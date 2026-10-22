/* Copyright 2025 OppositeNor

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#ifndef WBE_FILE_REF_RAW_TEST_HH
#define WBE_FILE_REF_RAW_TEST_HH

#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_raw.hh"
#include "mock_heap_allocator_aligned.hh"
#include <functional>
#include <gtest/gtest.h>
#include <stdexcept>
#include <utility>

namespace WBE = WhiteBirdEngine;

struct TestObject {
    int value;
    inline static int instance_count = 0;

    TestObject(int p_v) : value(p_v) {
        instance_count++;
    }
    ~TestObject() {
        instance_count--;
        value = -1; // Mark as destroyed
    }
};

class DerivedTestObject : public TestObject {
public:
    DerivedTestObject(int p_v = 0) : TestObject(p_v) {
    }
};

TEST(WBERefRawTest, DefaultConstruction) {
    // Test default construction creates empty reference
    WBE::RefRaw<TestObject> ref;

    ASSERT_EQ(ref.get(), nullptr);
    ASSERT_TRUE(ref == nullptr);
    ASSERT_TRUE(ref == WBE::MEM_NULL);
    ASSERT_TRUE(ref == static_cast<void*>(nullptr));
}

TEST(WBERefRawTest, StaticNewRefCreation) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;

    // Test static new_ref method
    using OwningRef = WBE::RefRaw<TestObject, WBE::MockHeapAllocatorAligned, true>;
    auto ref = OwningRef::new_ref(&allocator, 42);

    ASSERT_NE(ref.get(), nullptr);
    ASSERT_EQ(ref->value, 42);
    ASSERT_EQ(TestObject::instance_count, 1);
    ASSERT_FALSE(ref == nullptr);

    // Manual cleanup
    OwningRef::delete_ref(std::move(ref));
    ASSERT_EQ(TestObject::instance_count, 0);
}

TEST(WBERefRawTest, GlobalNewRefCreation) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;

    // Test global new_ref function
    auto ref = WBE::new_ref<TestObject>(&allocator, 123);

    ASSERT_NE(ref.get(), nullptr);
    ASSERT_EQ(ref->value, 123);
    ASSERT_EQ(TestObject::instance_count, 1);

    // Manual cleanup
    WBE::delete_ref(std::move(ref));
    ASSERT_EQ(TestObject::instance_count, 0);
}

TEST(WBERefRawTest, NullAllocatorThrows) {
    TestObject::instance_count = 0;

    // Test that null allocator throws exception
    ASSERT_THROW(WBE::RefRaw<TestObject>::new_ref(nullptr, 42), std::runtime_error);
    ASSERT_THROW((WBE::new_ref<TestObject>(static_cast<WBE::MockHeapAllocatorAligned*>(nullptr), 42)), std::runtime_error);

    ASSERT_EQ(TestObject::instance_count, 0);
}

TEST(WBERefRawTest, CopyConstructor) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;

    auto ref1 = WBE::new_ref<TestObject>(&allocator, 99);
    WBE::RefRaw<TestObject, WBE::MockHeapAllocatorAligned> ref2(ref1); // Copy constructor

    ASSERT_EQ(ref1.get(), ref2.get());
    ASSERT_EQ(ref1->value, ref2->value);
    ASSERT_EQ(ref2->value, 99);
    ASSERT_EQ(TestObject::instance_count, 1); // Still only one object

    // Both references point to same object
    ref1->value = 77;
    ASSERT_EQ(ref2->value, 77);

    // Manual cleanup through one reference
    WBE::delete_ref(std::move(ref1));
    ASSERT_EQ(TestObject::instance_count, 0);

    // Note: ref2 is now dangling but this is expected behavior for raw references
}

TEST(WBERefRawTest, MoveConstructor) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;

    auto ref1 = WBE::new_ref<TestObject>(&allocator, 88);
    TestObject* original_ptr = ref1.get();

    decltype(ref1) ref2(std::move(ref1)); // Move constructor

    ASSERT_EQ(ref1.get(), nullptr);      // ref1 is now empty
    ASSERT_EQ(ref2.get(), original_ptr); // ref2 owns the object
    ASSERT_EQ(ref2->value, 88);
    ASSERT_EQ(TestObject::instance_count, 1);

    WBE::delete_ref(std::move(ref2));
    ASSERT_EQ(TestObject::instance_count, 0);
}

TEST(WBERefRawTest, CopyAssignment) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;

    auto ref1 = WBE::new_ref<TestObject>(&allocator, 55);
    WBE::RefRaw<TestObject, WBE::MockHeapAllocatorAligned> ref2;

    ASSERT_EQ(ref2.get(), nullptr);

    ref2 = ref1; // Copy assignment

    ASSERT_EQ(ref1.get(), ref2.get());
    ASSERT_EQ(ref2->value, 55);
    ASSERT_EQ(TestObject::instance_count, 1);

    WBE::delete_ref(std::move(ref1));
    ASSERT_EQ(TestObject::instance_count, 0);
}

TEST(WBERefRawTest, MoveAssignment) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;

    auto ref1 = WBE::new_ref<TestObject>(&allocator, 66);
    TestObject* original_ptr = ref1.get();
    decltype(ref1) ref2;

    ref2 = std::move(ref1); // Move assignment

    ASSERT_EQ(ref1.get(), nullptr);
    ASSERT_EQ(ref2.get(), original_ptr);
    ASSERT_EQ(ref2->value, 66);
    ASSERT_EQ(TestObject::instance_count, 1);

    WBE::delete_ref(std::move(ref2));
    ASSERT_EQ(TestObject::instance_count, 0);
}

TEST(WBERefRawTest, ArrowOperator) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;

    auto ref = WBE::new_ref<TestObject>(&allocator, 100);

    // Test non-const arrow operator
    ASSERT_EQ(ref->value, 100);
    ref->value = 200;
    ASSERT_EQ(ref->value, 200);

    // Test const arrow operator
    const auto& const_ref = ref;
    ASSERT_EQ(const_ref->value, 200);

    WBE::delete_ref(std::move(ref));
    ASSERT_EQ(TestObject::instance_count, 0);
}

TEST(WBERefRawTest, DereferenceOperator) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;

    auto ref = WBE::new_ref<TestObject>(&allocator, 300);

    // Test non-const dereference operator
    ASSERT_EQ((*ref).value, 300);
    (*ref).value = 400;
    ASSERT_EQ((*ref).value, 400);

    // Test const dereference operator
    const auto& const_ref = ref;
    ASSERT_EQ((*const_ref).value, 400);

    WBE::delete_ref(std::move(ref));
    ASSERT_EQ(TestObject::instance_count, 0);
}

TEST(WBERefRawTest, GetMethod) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;

    WBE::RefRaw<TestObject> empty_ref;
    ASSERT_EQ(empty_ref.get(), nullptr);

    auto ref = WBE::new_ref<TestObject>(&allocator, 500);
    TestObject* ptr = ref.get();
    ASSERT_NE(ptr, nullptr);
    ASSERT_EQ(ptr->value, 500);

    // Test const get method
    const auto& const_ref = ref;
    const TestObject* const_ptr = const_ref.get();
    ASSERT_EQ(const_ptr, ptr);
    ASSERT_EQ(const_ptr->value, 500);

    WBE::delete_ref(std::move(ref));
    ASSERT_EQ(TestObject::instance_count, 0);
}

TEST(WBERefRawTest, EqualityOperators) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;

    WBE::RefRaw<TestObject> empty_ref1;
    WBE::RefRaw<TestObject> empty_ref2;
    auto ref1 = WBE::new_ref<TestObject>(&allocator, 111);
    auto ref2 = WBE::new_ref<TestObject>(&allocator, 222);
    auto ref1_copy = ref1; // Copy of ref1

    // Test equality between empty references
    ASSERT_TRUE(empty_ref1 == empty_ref2);
    ASSERT_FALSE(empty_ref1 != empty_ref2);

    // Test equality with nullptr
    ASSERT_TRUE(empty_ref1 == nullptr);
    ASSERT_FALSE(ref1 == nullptr);

    // Test equality with MEM_NULL
    ASSERT_TRUE(empty_ref1 == WBE::MEM_NULL);
    ASSERT_FALSE(ref1 == WBE::MEM_NULL);

    // Test equality with void* nullptr
    ASSERT_TRUE(empty_ref1 == static_cast<void*>(nullptr));
    ASSERT_FALSE(ref1 == static_cast<void*>(nullptr));

    // Test equality between different objects
    ASSERT_FALSE(ref1 == ref2);
    ASSERT_TRUE(ref1 != ref2);

    // Test equality with copy
    ASSERT_TRUE(ref1 == ref1_copy);
    ASSERT_FALSE(ref1 != ref1_copy);

    WBE::delete_ref(std::move(ref1));
    WBE::delete_ref(std::move(ref2));
    ASSERT_EQ(TestObject::instance_count, 0);
}

TEST(WBERefRawTest, ComparisonExceptions) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;

    WBE::RefRaw<TestObject> ref;

    // Test exception for non-null pointer comparison
    void* non_null_ptr = reinterpret_cast<void*>(0x1234);
    ASSERT_THROW(ref.operator==(non_null_ptr), std::runtime_error);

    // Test exception for non-null MemID comparison
    WBE::MemID non_null_id(42);
    ASSERT_THROW(ref.operator==(non_null_id), std::runtime_error);

    ASSERT_EQ(TestObject::instance_count, 0);
}

TEST(WBERefRawTest, LessOperatorOrdersByAllocatorThenMemID) {
    WBE::MockHeapAllocatorAligned allocator1(1024);
    WBE::MockHeapAllocatorAligned allocator2(1024);
    WBE::RefRaw<TestObject, WBE::MockHeapAllocatorAligned> lower_mem_id_ref(1, &allocator1);
    WBE::RefRaw<TestObject, WBE::MockHeapAllocatorAligned> higher_mem_id_ref(2, &allocator1);
    WBE::RefRaw<TestObject, WBE::MockHeapAllocatorAligned> other_allocator_ref(0, &allocator2);

    ASSERT_TRUE(lower_mem_id_ref < higher_mem_id_ref);
    ASSERT_FALSE(higher_mem_id_ref < lower_mem_id_ref);
    ASSERT_FALSE(lower_mem_id_ref < lower_mem_id_ref);

    const void* allocator1_address = static_cast<const void*>(&allocator1);
    const void* allocator2_address = static_cast<const void*>(&allocator2);
    std::less<> address_less;
    ASSERT_EQ(lower_mem_id_ref < other_allocator_ref, address_less(allocator1_address, allocator2_address));
    ASSERT_EQ(other_allocator_ref < lower_mem_id_ref, address_less(allocator2_address, allocator1_address));
}

// Helper classes for template conversion testing
struct BaseClass {
    int base_value;
    BaseClass(int p_v) : base_value(p_v) {
    }
    virtual ~BaseClass() = default;
};

struct DerivedClass : public BaseClass {
    int derived_value;
    DerivedClass(int p_b, int p_d) : BaseClass(p_b), derived_value(p_d) {
    }
};

TEST(WBERefRawTest, TemplateConversionConstructor) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    // Create RefRaw<DerivedClass>
    auto derived_ref = WBE::new_ref<DerivedClass>(&allocator, 10, 20);
    ASSERT_EQ(derived_ref->base_value, 10);
    ASSERT_EQ(derived_ref->derived_value, 20);

    // Convert to RefRaw<BaseClass> using template copy constructor
    WBE::RefRaw<BaseClass> base_ref(derived_ref);

    ASSERT_EQ(base_ref->base_value, 10);
    ASSERT_EQ(derived_ref->base_value, 10); // Original still valid

    // Both should point to the same object
    derived_ref->base_value = 30;
    ASSERT_EQ(base_ref->base_value, 30);

    WBE::delete_ref(std::move(derived_ref));
}

TEST(WBERefRawTest, TemplateConversionMoveConstructor) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    auto derived_ref = WBE::new_ref<DerivedClass>(&allocator, 40, 50);
    DerivedClass* original_ptr = derived_ref.get();

    // Move convert to RefRaw<BaseClass>
    WBE::RefRaw<BaseClass> base_ref(std::move(derived_ref));

    ASSERT_EQ(derived_ref.get(), nullptr); // Moved from
    ASSERT_EQ(base_ref.get(), reinterpret_cast<BaseClass*>(original_ptr));
    ASSERT_EQ(base_ref->base_value, 40);

    WBE::delete_ref(std::move(base_ref));
}

TEST(WBERefRawTest, TemplateConversionAssignment) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    auto derived_ref = WBE::new_ref<DerivedClass>(&allocator, 60, 70);
    WBE::RefRaw<BaseClass> base_ref;

    ASSERT_EQ(base_ref.get(), nullptr);

    // Copy assign derived to base
    base_ref = derived_ref;

    ASSERT_NE(base_ref.get(), nullptr);
    ASSERT_EQ(base_ref->base_value, 60);
    ASSERT_EQ(derived_ref->base_value, 60); // Both valid

    WBE::delete_ref(std::move(derived_ref));
}

TEST(WBERefRawTest, TemplateConversionMoveAssignment) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    auto derived_ref = WBE::new_ref<DerivedClass>(&allocator, 80, 90);
    WBE::RefRaw<BaseClass> base_ref;

    // Move assign derived to base
    base_ref = std::move(derived_ref);

    ASSERT_EQ(derived_ref.get(), nullptr); // Moved from
    ASSERT_NE(base_ref.get(), nullptr);
    ASSERT_EQ(base_ref->base_value, 80);

    WBE::delete_ref(std::move(base_ref));
}

TEST(WBERefRawTest, ManualResourceManagement) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;
    allocator.clear_call_log();

    // Create reference
    auto ref = WBE::new_ref<TestObject>(&allocator, 999);
    ASSERT_EQ(TestObject::instance_count, 1);

    // Verify allocation happened
    std::string log = allocator.get_call_log();
    ASSERT_NE(log.find("allocate"), std::string::npos);

    allocator.clear_call_log();

    // Manual deletion
    WBE::delete_ref(std::move(ref));
    ASSERT_EQ(TestObject::instance_count, 0);
    ASSERT_EQ(ref.get(), nullptr); // Reference should be nulled

    // Verify deallocation happened
    log = allocator.get_call_log();
    ASSERT_NE(log.find("deallocate"), std::string::npos);
}

TEST(WBERefRawTest, MultipleReferencesToSameObject) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    TestObject::instance_count = 0;

    auto ref1 = WBE::new_ref<TestObject>(&allocator, 777);
    auto ref2 = ref1; // Copy - both point to same object
    auto ref3 = ref1; // Another copy

    ASSERT_EQ(TestObject::instance_count, 1); // Only one object
    ASSERT_EQ(ref1.get(), ref2.get());
    ASSERT_EQ(ref1.get(), ref3.get());

    // Modifying through any reference affects all
    ref2->value = 888;
    ASSERT_EQ(ref1->value, 888);
    ASSERT_EQ(ref3->value, 888);

    // Delete through one reference - others become dangling (expected behavior)
    WBE::delete_ref(std::move(ref1));
    ASSERT_EQ(TestObject::instance_count, 0);

    // ref2 and ref3 are now dangling - this is the manual management aspect
}

TEST(WBERefRawTest, GetMethodWithNullAllocator) {
    WBE::RefRaw<TestObject> empty_ref;

    // Test that get() returns nullptr for empty reference (all indices)
    ASSERT_EQ(empty_ref.get(), nullptr);

    // Test const version
    const auto& const_empty_ref = empty_ref;
    ASSERT_EQ(const_empty_ref.get(), nullptr);
}

TEST(WBERefRawTest, IsNullMethod_BasicBehavior) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    // Default constructed should be null
    WBE::RefRaw<TestObject> default_ref;
    ASSERT_TRUE(default_ref.is_null());

    // After creating with allocator, should not be null
    auto r = WBE::new_ref<TestObject>(&allocator, 11);
    ASSERT_FALSE(r.is_null());
    ASSERT_NE(r.get(), nullptr);

    // After delete_ref, should be null again
    WBE::delete_ref(std::move(r));
    ASSERT_TRUE(r.is_null());
    ASSERT_EQ(r.get(), nullptr);

    // const version
    WBE::RefRaw<const TestObject> const_default_ref;
    ASSERT_TRUE(const_default_ref.is_null());
}

#endif
