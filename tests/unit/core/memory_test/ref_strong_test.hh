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
#ifndef WBE_FILE_REF_STRONG_TEST_HH
#define WBE_FILE_REF_STRONG_TEST_HH

#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "global/global.hh"
#include "mock_heap_allocator_aligned.hh"
#include "test_global.hh"
#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace WBE = WhiteBirdEngine;

class WBERefStrongTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;
};

TEST_F(WBERefStrongTest, Allocation) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    {
        WBE::Ref<int> ref = WBE::Ref<int>::make_ref(&allocator, 0);
        ASSERT_FALSE(allocator.is_empty());
        ASSERT_EQ(*ref, 0);
        *ref = 1;
        ASSERT_EQ(*ref, 1);
    }
    ASSERT_TRUE(allocator.is_empty());
    {
        WBE::Ref<int> ref;
    }
    ASSERT_TRUE(allocator.is_empty());
}

TEST_F(WBERefStrongTest, CopyMove) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    {
        WBE::Ref<int> ref = WBE::Ref<int>::make_ref(&allocator, 3);
        ASSERT_FALSE(allocator.is_empty());
        ASSERT_EQ(*ref, 3);
        WBE::Ref<int> ref_copy = ref;
        ASSERT_EQ(*ref_copy, 3);
        ASSERT_EQ(*ref, 3);
        *ref = 5;
        ASSERT_EQ(*ref_copy, 5);
        ASSERT_EQ(*ref, 5);
        WBE::Ref<int> ref_move = std::move(ref);
        ASSERT_EQ(*ref_move, 5);
        ASSERT_EQ(*ref_copy, 5);
        *ref_move = 2;
        ASSERT_EQ(*ref_move, 2);
        ASSERT_EQ(*ref_copy, 2);
    }
    ASSERT_TRUE(allocator.is_empty());
}

TEST_F(WBERefStrongTest, ConstructDestruct) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    int test_val = 0;
    class TestClass {
    public:
        int* modify_val;
        explicit TestClass(int* p_modify_val) : modify_val(p_modify_val) {
            ++(*modify_val);
        }
        ~TestClass() {
            *modify_val = 100;
        }
    };
    {
        WBE::Ref<TestClass> ref;
        ASSERT_EQ(test_val, 0);
        ref = WBE::Ref<TestClass>::make_ref(&allocator, &test_val);
        ASSERT_EQ(test_val, 1);
        {
            ASSERT_EQ(test_val, 1);
        }
        ASSERT_EQ(test_val, 1);
    }
    ASSERT_TRUE(allocator.is_empty());
    ASSERT_EQ(test_val, 100);
}

TEST_F(WBERefStrongTest, DynamicDispatch) {
    class A {
    public:
        A() = default;
        virtual ~A() = default;

        virtual void func() {
            num = 1;
        }

        int num = 0;
    };

    class B : public A {
    public:
        B() = default;
        virtual ~B() override = default;
        virtual void func() override {
            num = 2;
        }
    };
    WBE::MockHeapAllocatorAligned allocator(1024);
    {
        WBE::Ref<A> ref = WBE::Ref<A>::make_ref(&allocator);
        ASSERT_EQ(ref->num, 0);
        ref->func();
        ASSERT_EQ(ref->num, 1);
        WBE::Ref<A> ref_b = WBE::Ref<B>::make_ref(&allocator);
        ASSERT_EQ(ref_b->num, 0);
        ref_b->func();
        ASSERT_EQ(ref_b->num, 2);
    }
    ASSERT_TRUE(allocator.is_empty());
}

TEST_F(WBERefStrongTest, NullReference) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    WBE::Ref<int> ref(&allocator, WBE::MEM_NULL);
    ASSERT_EQ(ref, nullptr);
    ASSERT_EQ(ref, (void*)nullptr);
    ASSERT_EQ(ref, WBE::MEM_NULL);
    ASSERT_EQ(ref.get(), nullptr);
    ref = WBE::Ref<int>::make_ref(&allocator, 5);
    ASSERT_NE(ref, nullptr);
    ASSERT_NE(ref, (void*)nullptr);
    ASSERT_NE(ref, WBE::MEM_NULL);
    ASSERT_NE(ref.get(), nullptr);
}

class TestClass {
public:
    explicit TestClass(int* p_val) : val(p_val) {
        ++(*p_val);
    }
    ~TestClass() {
        *val = -1;
    }

    int* val;
};

inline int test_val1 = 0;
inline int test_val2 = 0;

inline void multithread_ref_test(WBE::Ref<TestClass> p_ref1, WBE::Ref<TestClass> p_ref2, uint32_t p_iteration_count) {
    std::vector<WBE::Ref<TestClass>> refs;
    WBE::MockHeapAllocatorAligned allocator(1024);
    refs.reserve(5);
    for (uint32_t i = 0; i < 5; ++i) {
        if (rand() & 0x1) {
            refs.push_back(p_ref1);
        } else {
            refs.push_back(p_ref2);
        }
    }
    for (uint32_t i = 0; i < p_iteration_count; ++i) {
        uint32_t rand_ref_count = rand() % 5;
        for (uint32_t j = 0; j < rand_ref_count; ++j) {
            uint32_t choice = rand() % 4;
            if (choice == 0) {
                refs[j] = p_ref1;
            } else if (choice == 1) {
                refs[j] = p_ref2;
            } else if (choice == 2) {
                refs[j] = refs[rand() % refs.size()];
            } else {
                int val = 3;
                {
                    WBE::Ref<TestClass> ref = WBE::Ref<TestClass>::make_ref(&allocator, &val);
                    ASSERT_EQ(val, 4);
                }
                ASSERT_EQ(val, -1);
            }
            ASSERT_EQ(test_val1, 1);
            ASSERT_EQ(test_val2, 1);
        }
    }
}

TEST_F(WBERefStrongTest, Multithread) {
    ASSERT_EQ(test_val1, 0);
    ASSERT_EQ(test_val2, 0);
    WBE::MockHeapAllocatorAligned allocator(1024);
    {
        WBE::Ref<TestClass> ref1 = WBE::Ref<TestClass>::make_ref(&allocator, &test_val1);
        WBE::Ref<TestClass> ref2 = WBE::Ref<TestClass>::make_ref(&allocator, &test_val2);
        ASSERT_EQ(test_val1, 1);
        ASSERT_EQ(test_val2, 1);
        std::thread thread1(multithread_ref_test, ref1, ref2, 300000);
        std::thread thread2(multithread_ref_test, ref1, ref2, 300000);
        std::thread thread3(multithread_ref_test, ref1, ref2, 300000);
        std::thread thread4(multithread_ref_test, ref1, ref2, 300000);
        std::thread thread5(multithread_ref_test, ref1, ref2, 300000);
        thread1.join();
        thread2.join();
        thread3.join();
        thread4.join();
        thread5.join();
    }
    ASSERT_EQ(test_val1, -1);
    ASSERT_EQ(test_val2, -1);
}

TEST_F(WBERefStrongTest, DynamicCastRef) {
    WBE::MockHeapAllocatorAligned allocator(1024);

    // Test classes for polymorphic casting
    class BaseClass {
    public:
        virtual ~BaseClass() = default;
        int base_value = 42;
        virtual int get_type() const {
            return 1;
        }
    };

    class DerivedClass : public BaseClass {
    public:
        int derived_value = 123;
        virtual int get_type() const override {
            return 2;
        }
    };

    class AnotherClass : public BaseClass {
    public:
        int another_value = 456;
        virtual int get_type() const override {
            return 3;
        }
    };

    class UnrelatedClass {
    public:
        virtual ~UnrelatedClass() = default;
        int unrelated_value = 789;
    };

    // Test successful downcast (Base -> Derived)
    {
        WBE::Ref<BaseClass> base_ref = WBE::Ref<DerivedClass>::make_ref(&allocator);
        dynamic_cast<DerivedClass*>(base_ref.get())->derived_value = 999;

        WBE::Ref<DerivedClass> derived_ref = base_ref.dynamic_cast_ref<DerivedClass>();
        ASSERT_NE(derived_ref, nullptr);
        ASSERT_EQ(derived_ref->base_value, 42);
        ASSERT_EQ(derived_ref->derived_value, 999);
        ASSERT_EQ(derived_ref->get_type(), 2);

        // Verify they point to the same object
        ASSERT_EQ(base_ref.get(), derived_ref.get());
    }

    // Test successful upcast (Derived -> Base)
    {
        WBE::Ref<DerivedClass> derived_ref = WBE::Ref<DerivedClass>::make_ref(&allocator);
        derived_ref->derived_value = 777;

        WBE::Ref<BaseClass> base_ref = derived_ref.dynamic_cast_ref<BaseClass>();
        ASSERT_NE(base_ref, nullptr);
        ASSERT_EQ(base_ref->base_value, 42);
        ASSERT_EQ(base_ref->get_type(), 2);

        // Verify they point to the same object
        ASSERT_EQ(derived_ref.get(), base_ref.get());
    }

    // Test failed downcast (Base -> wrong Derived)
    {
        WBE::Ref<BaseClass> base_ref = WBE::Ref<DerivedClass>::make_ref(&allocator);

        WBE::Ref<AnotherClass> another_ref = base_ref.dynamic_cast_ref<AnotherClass>();
        ASSERT_EQ(another_ref, nullptr);
        ASSERT_EQ(another_ref, WBE::MEM_NULL);
    }

    // Test cast with null reference
    {
        WBE::Ref<BaseClass> null_ref;
        ASSERT_EQ(null_ref, nullptr);

        WBE::Ref<DerivedClass> derived_ref = null_ref.dynamic_cast_ref<DerivedClass>();
        ASSERT_EQ(derived_ref, nullptr);
        ASSERT_EQ(derived_ref, WBE::MEM_NULL);
    }

    // Test cast between unrelated types (should fail)
    {
        WBE::Ref<DerivedClass> derived_ref = WBE::Ref<DerivedClass>::make_ref(&allocator);

        WBE::Ref<UnrelatedClass> unrelated_ref = derived_ref.dynamic_cast_ref<UnrelatedClass>();
        ASSERT_EQ(unrelated_ref, nullptr);
        ASSERT_EQ(unrelated_ref, WBE::MEM_NULL);
    }

    // Test side cast (Derived1 -> Derived2 through common base)
    {
        WBE::Ref<BaseClass> base_ref = WBE::Ref<AnotherClass>::make_ref(&allocator);
        dynamic_cast<AnotherClass*>(base_ref.get())->another_value = 555;

        // This should fail since AnotherClass is not DerivedClass
        WBE::Ref<DerivedClass> derived_ref = base_ref.dynamic_cast_ref<DerivedClass>();
        ASSERT_EQ(derived_ref, nullptr);

        // But casting to AnotherClass should succeed
        WBE::Ref<AnotherClass> another_ref = base_ref.dynamic_cast_ref<AnotherClass>();
        ASSERT_NE(another_ref, nullptr);
        ASSERT_EQ(another_ref->another_value, 555);
        ASSERT_EQ(another_ref->get_type(), 3);
    }

    // Test reference counting is preserved after casting
    {
        WBE::Ref<BaseClass> base_ref1 = WBE::Ref<DerivedClass>::make_ref(&allocator);
        WBE::Ref<BaseClass> base_ref2 = base_ref1; // Create another reference

        WBE::Ref<DerivedClass> derived_ref = base_ref1.dynamic_cast_ref<DerivedClass>();
        ASSERT_NE(derived_ref, nullptr);

        // All three references should point to the same object
        ASSERT_EQ(base_ref1.get(), base_ref2.get());
        ASSERT_EQ(base_ref1.get(), derived_ref.get());

        // Object should still be alive after releasing one reference
        base_ref1 = nullptr;
        ASSERT_NE(base_ref2, nullptr);
        ASSERT_NE(derived_ref, nullptr);
        ASSERT_EQ(base_ref2.get(), derived_ref.get());
    }

    ASSERT_TRUE(allocator.is_empty());
}

TEST_F(WBERefStrongTest, IsNullMethod_BasicBehavior) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    {
        // Default constructed reference should be null
        WBE::Ref<int> default_ref;
        ASSERT_TRUE(default_ref.is_null());

        // Reference constructed with MEM_NULL via allocator should be null
        WBE::Ref<int> null_ref(&allocator, WBE::MEM_NULL);
        ASSERT_TRUE(null_ref.is_null());
        ASSERT_EQ(null_ref.get(), nullptr);

        // make_ref should produce a non-null reference
        WBE::Ref<int> valid_ref = WBE::Ref<int>::make_ref(&allocator, 42);
        ASSERT_FALSE(valid_ref.is_null());
        ASSERT_NE(valid_ref.get(), nullptr);

        // Assigning nullptr should make it null again
        valid_ref = nullptr;
        ASSERT_TRUE(valid_ref.is_null());
    }
    ASSERT_TRUE(allocator.is_empty());
}

TEST_F(WBERefStrongTest, IsNullMethod_MoveAndLifetime) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    {
        // Move semantics: moved-from reference becomes null
        WBE::Ref<int> a = WBE::Ref<int>::make_ref(&allocator, 7);
        WBE::Ref<int> b = std::move(a);
        ASSERT_TRUE(a.is_null());
        ASSERT_FALSE(b.is_null());

        // After resetting the last strong reference, the reference should be null
        b = nullptr;
        ASSERT_TRUE(b.is_null());
    }

    ASSERT_TRUE(allocator.is_empty());
}

#endif
