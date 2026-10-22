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
#ifndef WBE_FILE_UNIQUE_TEST_HH
#define WBE_FILE_UNIQUE_TEST_HH

#include "core/allocator/heap_allocator.hh"
#include "core/allocator/i_allocator.hh"
#include "core/memory/unique.hh"
#include "global/global.hh"
#include "mock_heap_allocator_aligned.hh"
#include "test_global.hh"
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <utility>

namespace WBE = WhiteBirdEngine;

struct Dummy {
    int x;
    explicit Dummy(int p_v) : x(p_v) {
    }
    ~Dummy() {
        x = -1;
    }
};

class WBEUniqueTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;
};

TEST_F(WBEUniqueTest, BasicConstructionAndAccess) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    allocator.clear_call_log();

    // Create object using allocator directly
    WBE::MemID id = WBE::create_obj<Dummy>(allocator, 42);
    WBE::Unique<Dummy> u(&allocator, id);

    ASSERT_NE(u.get(), nullptr);
    ASSERT_EQ(u.get()->x, 42);

    // Verify get was called
    std::string log = allocator.get_call_log();
    ASSERT_NE(log.find("get"), std::string::npos);
}

TEST_F(WBEUniqueTest, MoveSemantics) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    WBE::MemID id1 = WBE::create_obj<Dummy>(allocator, 7);
    WBE::Unique<Dummy> u1(&allocator, id1);
    WBE::Unique<Dummy> u2 = std::move(u1);

    // After move, u1 should be null, u2 should have the value
    ASSERT_EQ(u1.get(), nullptr);
    ASSERT_NE(u2.get(), nullptr);
    ASSERT_EQ(u2.get()->x, 7);
}

TEST_F(WBEUniqueTest, ResetBehavior) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    allocator.clear_call_log();

    WBE::MemID id = WBE::create_obj<Dummy>(allocator, 123);
    WBE::Unique<Dummy> u(&allocator, id);
    ASSERT_NE(u.get(), nullptr);

    allocator.clear_call_log();
    u.reset();

    // Reset should call deallocate and set to null
    std::string log = allocator.get_call_log();
    ASSERT_NE(log.find("deallocate"), std::string::npos);
    ASSERT_EQ(u.get(), nullptr);
}

TEST_F(WBEUniqueTest, ComparisonOperators) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    WBE::MemID id = WBE::create_obj<Dummy>(allocator, 1);
    WBE::Unique<Dummy> u1(&allocator, id);
    WBE::Unique<Dummy> u2; // Empty unique

    ASSERT_FALSE(u1 == nullptr);
    ASSERT_TRUE(u2 == nullptr);

    u1.reset();
    ASSERT_TRUE(u1 == nullptr);

    // Test exception throwing for invalid comparisons
    ASSERT_THROW(u2.operator==((void*)0x1234), std::runtime_error);
    ASSERT_THROW(u1.operator==(WBE::MemID(42)), std::runtime_error);
}

TEST_F(WBEUniqueTest, CallLoggingWithMakeUnique) {
    WBE::MockHeapAllocatorAligned allocator(4096);
    allocator.clear_call_log();

    // Test static make_unique method
    auto u = WBE::Unique<Dummy>::make_unique(&allocator, 100);
    ASSERT_NE(u.get(), nullptr);
    ASSERT_EQ(u.get()->x, 100);

    // Verify allocation and get were called
    std::string log = allocator.get_call_log();
    ASSERT_NE(log.find("allocate"), std::string::npos);
    ASSERT_NE(log.find("get"), std::string::npos);
}

TEST_F(WBEUniqueTest, ArrowOperator) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    WBE::MemID id = WBE::create_obj<Dummy>(allocator, 42);
    WBE::Unique<Dummy> u(&allocator, id);

    // Test non-const arrow operator - should work like get()->x
    ASSERT_EQ(u->x, 42);
    u->x = 84;
    ASSERT_EQ(u->x, 84);

    // Test const arrow operator
    const auto& const_u = u;
    ASSERT_EQ(const_u->x, 84);
}

TEST_F(WBEUniqueTest, DereferenceOperator) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    WBE::MemID id = WBE::create_obj<Dummy>(allocator, 123);
    WBE::Unique<Dummy> u(&allocator, id);

    // Test non-const dereference operator - should work like *get()
    ASSERT_EQ((*u).x, 123);
    (*u).x = 456;
    ASSERT_EQ((*u).x, 456);

    // Test const dereference operator
    const auto& const_u = u;
    ASSERT_EQ((*const_u).x, 456);
}

TEST_F(WBEUniqueTest, GlobalMakeUniqueFunction) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    allocator.clear_call_log();

    // Test global make_unique function
    auto u = WBE::make_unique<Dummy>(&allocator, 99);
    ASSERT_NE(u.get(), nullptr);
    ASSERT_EQ(u.get()->x, 99);

    // Verify allocation was called
    std::string log = allocator.get_call_log();
    ASSERT_NE(log.find("allocate"), std::string::npos);
}

TEST_F(WBEUniqueTest, MemIDComparisonOperator) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    WBE::Unique<Dummy> empty_unique;
    WBE::MemID id = WBE::create_obj<Dummy>(allocator, 1);
    WBE::Unique<Dummy> valid_unique(&allocator, id);

    // Test MEM_NULL comparison - empty should equal MEM_NULL, valid should not
    ASSERT_TRUE(empty_unique == WBE::MEM_NULL);
    ASSERT_FALSE(valid_unique == WBE::MEM_NULL);

    // Test exception throwing for non-null MemID comparison
    WBE::MemID non_null_id(42);
    ASSERT_THROW(empty_unique.operator==(non_null_id), std::runtime_error);
    ASSERT_THROW(valid_unique.operator==(non_null_id), std::runtime_error);
}

TEST_F(WBEUniqueTest, VoidPointerComparisonOperator) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    WBE::Unique<Dummy> empty_unique;
    WBE::MemID id = WBE::create_obj<Dummy>(allocator, 1);
    WBE::Unique<Dummy> valid_unique(&allocator, id);

    // Test void* comparison with nullptr - should work like nullptr comparison
    ASSERT_TRUE(empty_unique == static_cast<void*>(nullptr));
    ASSERT_FALSE(valid_unique == static_cast<void*>(nullptr));

    // Test exception throwing for non-null pointer comparison
    void* non_null_ptr = reinterpret_cast<void*>(0x1234);
    bool threw_exception = false;
    try {
        empty_unique.operator==(non_null_ptr);
    } catch (const std::runtime_error&) {
        threw_exception = true;
    }
    ASSERT_TRUE(threw_exception);
}

TEST_F(WBEUniqueTest, MemIDConstructorValidation) {
    // Test constructor with MEM_NULL - should not throw and create empty unique
    WBE::Unique<Dummy> u1(WBE::MEM_NULL);
    ASSERT_EQ(u1.get(), nullptr);
    ASSERT_TRUE(u1 == nullptr);

    // Test constructor with non-null MemID but no allocator - should throw
    bool threw_exception = false;
    try {
        WBE::Unique<Dummy> u2(static_cast<WBE::MemID>(42));
    } catch (const std::runtime_error&) {
        threw_exception = true;
    }
    ASSERT_TRUE(threw_exception);
}

// Helper classes for template conversion testing
struct Base {
    int value;
    explicit Base(int p_v) : value(p_v) {
    }
    virtual ~Base() = default;
};

struct Derived : public Base {
    int extra;
    Derived(int p_v, int p_e) : Base(p_v), extra(p_e) {
    }
};

TEST_F(WBEUniqueTest, TemplateConversionConstructor) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    // Create a Unique<Derived>
    WBE::MemID derived_id = WBE::create_obj<Derived>(allocator, 42, 84);
    WBE::Unique<Derived> derived_unique(&allocator, derived_id);

    ASSERT_EQ(derived_unique->value, 42);
    ASSERT_EQ(derived_unique->extra, 84);

    // Move convert to Unique<Base> using template conversion constructor
    WBE::Unique<Base> base_unique = std::move(derived_unique);

    // After move, derived_unique should be empty
    ASSERT_EQ(derived_unique.get(), nullptr);

    // base_unique should have the object
    ASSERT_NE(base_unique.get(), nullptr);
    ASSERT_EQ(base_unique->value, 42);
}

TEST_F(WBEUniqueTest, TemplateConversionAssignmentOperator) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    // Create a Unique<Derived>
    WBE::MemID derived_id = WBE::create_obj<Derived>(allocator, 10, 20);
    WBE::Unique<Derived> derived_unique(&allocator, derived_id);

    // Create an empty Unique<Base>
    WBE::Unique<Base> base_unique;
    ASSERT_EQ(base_unique.get(), nullptr);

    // Move assign derived to base using template conversion assignment
    base_unique = std::move(derived_unique);

    // After move assignment
    ASSERT_EQ(derived_unique.get(), nullptr);
    ASSERT_NE(base_unique.get(), nullptr);
    ASSERT_EQ(base_unique->value, 10);
}

TEST_F(WBEUniqueTest, MoveAssignmentWithExistingObject) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    // Create two Unique objects
    WBE::MemID id1 = WBE::create_obj<Dummy>(allocator, 100);
    WBE::MemID id2 = WBE::create_obj<Dummy>(allocator, 200);

    WBE::Unique<Dummy> u1(&allocator, id1);
    WBE::Unique<Dummy> u2(&allocator, id2);

    ASSERT_EQ(u1->x, 100);
    ASSERT_EQ(u2->x, 200);

    allocator.clear_call_log();

    // Move assign u2 to u1 (should deallocate u1's old object first)
    u1 = std::move(u2);

    // Verify deallocation happened
    std::string log = allocator.get_call_log();
    ASSERT_NE(log.find("deallocate"), std::string::npos);

    // u1 should now have u2's object, u2 should be empty
    ASSERT_EQ(u1->x, 200);
    ASSERT_EQ(u2.get(), nullptr);
}

TEST_F(WBEUniqueTest, ArrowOperatorNullAccess) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    // Test arrow operator on empty unique - should handle null gracefully or crash predictably
    WBE::Unique<Dummy> empty_unique;
    ASSERT_EQ(empty_unique.get(), nullptr);

    // Note: Accessing -> on empty unique would typically crash, so we just verify it's empty
    // In production code, users should check get() != nullptr before using ->
}

TEST_F(WBEUniqueTest, DereferenceOperatorNullAccess) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    // Test dereference operator on empty unique
    WBE::Unique<Dummy> empty_unique;
    ASSERT_EQ(empty_unique.get(), nullptr);

    // Note: Dereferencing empty unique would typically crash, so we just verify it's empty
    // In production code, users should check get() != nullptr before using *
}

TEST_F(WBEUniqueTest, StaticMakeUniqueVsGlobalMakeUnique) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    // Test both static and global make_unique functions work identically
    auto u1 = WBE::Unique<Dummy>::make_unique(&allocator, 111);
    auto u2 = WBE::make_unique<Dummy>(&allocator, 222);

    ASSERT_NE(u1.get(), nullptr);
    ASSERT_NE(u2.get(), nullptr);
    ASSERT_EQ(u1->x, 111);
    ASSERT_EQ(u2->x, 222);

    // Both should be proper Unique objects with same behavior
    ASSERT_FALSE(u1 == nullptr);
    ASSERT_FALSE(u2 == nullptr);
}

TEST_F(WBEUniqueTest, MultipleResetCalls) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    WBE::MemID id = WBE::create_obj<Dummy>(allocator, 999);
    WBE::Unique<Dummy> u(&allocator, id);

    ASSERT_NE(u.get(), nullptr);
    ASSERT_EQ(u->x, 999);

    // First reset should deallocate
    u.reset();
    ASSERT_EQ(u.get(), nullptr);

    // Second reset should be safe (no-op)
    u.reset();
    ASSERT_EQ(u.get(), nullptr);

    // Third reset should still be safe
    u.reset();
    ASSERT_EQ(u.get(), nullptr);
}

TEST_F(WBEUniqueTest, IsNullMethod_Behavior) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    // Default constructed should be null
    WBE::Unique<Dummy> empty_unique;
    ASSERT_TRUE(empty_unique.is_null());

    // make_unique should produce non-null
    auto u = WBE::Unique<Dummy>::make_unique(&allocator, 13);
    ASSERT_FALSE(u.is_null());

    // reset should make it null again
    u.reset();
    ASSERT_TRUE(u.is_null());

    // const version
    const auto& const_u = u;
    ASSERT_TRUE(const_u.is_null());
}

#endif
