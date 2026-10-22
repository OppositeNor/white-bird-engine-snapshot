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
#ifndef WBE_FILE_REF_WEAK_TEST_HH
#define WBE_FILE_REF_WEAK_TEST_HH

#include "core/memory/reference_strong.hh"
#include "core/memory/reference_weak.hh"
#include "global/global.hh"
#include "mock_heap_allocator_aligned.hh"
#include "test_global.hh"
#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace WBE = WhiteBirdEngine;

class TestClassWeak {
public:
    explicit TestClassWeak(std::stringstream& p_ss) : ss(&p_ss) {
        (*ss) << "Construct.\n";
    }

    virtual ~TestClassWeak() {
        (*ss) << "Destruct.\n";
    }

    virtual void set_test_val(int p_test_val) {
        (*ss) << "Set test val: " << p_test_val << ".\n";
        test_val = p_test_val;
    }

    virtual int get_val() {
        return 1;
    }

    std::stringstream* ss;
    int test_val{0};
};

TEST(WBERefWeakTest, General) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::MockHeapAllocatorAligned pool_allocator(1024);
    std::stringstream test_ss;
    {
        WBE::Ref<TestClassWeak> ref = WBE::Ref<TestClassWeak>::make_ref(&pool_allocator, test_ss);
        {
            WBE::RefWeak<TestClassWeak> ref_weak = ref;
            ASSERT_TRUE(ref_weak.is_valid());
            ASSERT_EQ(ref_weak.lock()->test_val, 0);
            ref_weak.lock()->set_test_val(5);
            ASSERT_EQ(ref_weak.lock()->test_val, 5);
        }
        ASSERT_EQ(test_ss.str(),
            std::string("Construct.\n"
                        "Set test val: 5.\n"));
        ASSERT_EQ((*ref).test_val, 5);
    }
    ASSERT_EQ(test_ss.str(),
        std::string("Construct.\n"
                    "Set test val: 5.\n"
                    "Destruct.\n"));
    ASSERT_TRUE(pool_allocator.is_empty());
}

TEST(WBERefWeakTest, FirstStrongDestruct) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::MockHeapAllocatorAligned pool_allocator(1024);
    {
        WBE::RefWeak<int> ref_weak;
        ASSERT_FALSE(ref_weak.is_valid());
        {
            WBE::Ref<int> ref = WBE::Ref<int>::make_ref(&pool_allocator, 3);
            ref_weak = ref;
            ASSERT_TRUE(ref_weak.is_valid());
        }
        ASSERT_FALSE(ref_weak.is_valid());
    }
    ASSERT_TRUE(pool_allocator.is_empty());
}

TEST(WBERefWeakTest, FirstWeakDestruct) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::MockHeapAllocatorAligned pool_allocator(1024);
    {
        WBE::Ref<int> ref = WBE::make_ref<int>(&pool_allocator, 3);
        ASSERT_FALSE(ref.is_null());
        {
            WBE::RefWeak<int> ref_weak = ref;
            ASSERT_TRUE(ref_weak.is_valid());
            ASSERT_EQ(*ref, 3);
            ASSERT_EQ(*(ref_weak.lock()), 3);
        }
        ASSERT_FALSE(ref.is_null());
    }
    ASSERT_TRUE(pool_allocator.is_empty());
}

class TestClassWeakChild : public TestClassWeak {
public:
    explicit TestClassWeakChild(std::stringstream& p_ss) : TestClassWeak(p_ss) {
        (*ss) << "Child construct.\n";
    }

    virtual ~TestClassWeakChild() override {
        (*ss) << "Child destruct.\n";
    }

    virtual void set_test_val(int p_test_val) override {
        (*ss) << "Child set test val: " << p_test_val << ".\n";
        test_val = p_test_val;
    }

    virtual int get_val() override {
        return 2;
    }
};

TEST(WBERefWeakTest, Polymorphic) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::MockHeapAllocatorAligned pool_allocator(1024);
    std::stringstream test_ss;
    {
        WBE::RefWeak<TestClassWeakChild> ref_weak_child;
        {
            WBE::Ref<TestClassWeakChild> ref = WBE::Ref<TestClassWeakChild>::make_ref(&pool_allocator, test_ss);
            ref_weak_child = ref;
            {
                WBE::RefWeak<TestClassWeak> ref_weak = ref;
                ASSERT_EQ(ref->test_val, 0);
                ref_weak.lock()->set_test_val(5);
                ASSERT_EQ(test_ss.str(),
                    std::string("Construct.\n"
                                "Child construct.\n"
                                "Child set test val: 5.\n"));
                ASSERT_EQ(ref->test_val, 5);
            }
            ref_weak_child.lock()->set_test_val(10);
            ASSERT_EQ(test_ss.str(),
                std::string("Construct.\n"
                            "Child construct.\n"
                            "Child set test val: 5.\n"
                            "Child set test val: 10.\n"));
            ASSERT_EQ(ref->test_val, 10);
        }
        ASSERT_EQ(test_ss.str(),
            std::string("Construct.\n"
                        "Child construct.\n"
                        "Child set test val: 5.\n"
                        "Child set test val: 10.\n"
                        "Child destruct.\n"
                        "Destruct.\n"));
        ASSERT_FALSE(ref_weak_child.is_valid());
    }
    ASSERT_TRUE(pool_allocator.is_empty());
}

inline void multithread_ref_weak_test(WBE::Ref<TestClassWeak> p_ref1, WBE::Ref<TestClassWeak> p_ref2, uint32_t p_iteration_count) {
    std::vector<WBE::RefWeak<TestClassWeak>> ref_weaks;
    ref_weaks.reserve(5);
    for (uint32_t i = 0; i < 5; ++i) {
        if (rand() & 0x1) {
            ref_weaks.emplace_back(p_ref1);
        } else {
            ref_weaks.emplace_back(p_ref2);
        }
    }
    for (uint32_t i = 0; i < p_iteration_count; ++i) {
        uint32_t rand_ref_weak_count = rand() % 5;
        for (uint32_t j = 0; j < rand_ref_weak_count; ++j) {
            uint32_t choice = rand() % 3;
            switch (choice) {
            case 0:
                ref_weaks[j] = p_ref1;
                break;
            case 1:
                ref_weaks[j] = p_ref2;
                break;
            default:
                uint32_t pick_index = rand() % ref_weaks.size();
                ref_weaks[j] = ref_weaks[pick_index];
                break;
            }
        }
    }
}

TEST(WBERefWeak, Multithread) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::MockHeapAllocatorAligned pool_allocator(1024);
    std::stringstream test_ss1;
    std::stringstream test_ss2;
    {
        WBE::Ref<TestClassWeak> ref1 = WBE::Ref<TestClassWeak>::make_ref(&pool_allocator, test_ss1);
        WBE::Ref<TestClassWeak> ref2 = WBE::Ref<TestClassWeak>::make_ref(&pool_allocator, test_ss2);
        ASSERT_EQ(test_ss1.str(), "Construct.\n");
        ASSERT_EQ(test_ss2.str(), "Construct.\n");
        std::thread thread1(multithread_ref_weak_test, ref1, ref2, 500000);
        std::thread thread2(multithread_ref_weak_test, ref1, ref2, 500000);
        std::thread thread3(multithread_ref_weak_test, ref1, ref2, 500000);
        std::thread thread4(multithread_ref_weak_test, ref1, ref2, 500000);
        std::thread thread5(multithread_ref_weak_test, ref1, ref2, 500000);
        thread1.join();
        thread2.join();
        thread3.join();
        thread4.join();
        thread5.join();
        ASSERT_EQ(test_ss1.str(), "Construct.\n");
        ASSERT_EQ(test_ss2.str(), "Construct.\n");
    }
    ASSERT_EQ(test_ss1.str(), std::string("Construct.\nDestruct.\n"));
    ASSERT_EQ(test_ss2.str(), std::string("Construct.\nDestruct.\n"));
    ASSERT_TRUE(pool_allocator.is_empty());
}

TEST(WBERefWeak, InvaldLockTest) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::MockHeapAllocatorAligned pool_allocator(1024);
    {
        WBE::RefWeak<int> ref_weak;
        ASSERT_EQ(ref_weak.lock(), nullptr);
        {
            WBE::Ref<int> ref = WBE::Ref<int>::make_ref(&pool_allocator);
            ref_weak = ref;
            ASSERT_NE(ref_weak.lock(), nullptr);
        }
        ASSERT_EQ(ref_weak.lock(), nullptr);
    }
    ASSERT_TRUE(pool_allocator.is_empty());
}

inline void multithread_ref_strong_and_weak_test(
    WBE::Ref<TestClassWeak> p_ref1, WBE::Ref<TestClassWeakChild> p_ref2, uint32_t p_iteration_count) {
    std::vector<WBE::RefWeak<TestClassWeak>> ref_weaks;
    std::vector<WBE::Ref<TestClassWeak>> ref_strongs;
    ref_weaks.reserve(5);
    for (uint32_t i = 0; i < 5; ++i) {
        if (rand() & 0x1) {
            ref_weaks.emplace_back(p_ref1);
        } else {
            ref_weaks.emplace_back(p_ref2);
        }
        if (rand() & 0x1) {
            ref_strongs.push_back(p_ref1);
        } else {
            ref_strongs.emplace_back(p_ref2);
        }
    }
    for (uint32_t i = 0; i < p_iteration_count; ++i) {
        uint32_t rand_ref_weak_count = rand() % 5;
        for (uint32_t j = 0; j < rand_ref_weak_count; ++j) {
            if (rand() % 0x1) {
                uint32_t choice = rand() % 3;
                switch (choice) {
                case 0:
                    ref_weaks[j] = p_ref1;
                    if (rand() & 0x1) {
                        ASSERT_EQ(ref_weaks[j].lock()->get_val(), 1);
                    }
                    break;
                case 1:
                    ref_weaks[j] = p_ref2;
                    if (rand() & 0x1) {
                        ASSERT_EQ(ref_weaks[j].lock()->get_val(), 2);
                    }
                    break;
                default:
                    ref_weaks[j] = ref_weaks[rand() % ref_weaks.size()];
                }
            } else {
                uint32_t choice = rand() % 4;
                switch (choice) {
                case 0:
                    ref_strongs[j] = p_ref1;
                    break;
                case 1:
                    ref_strongs[j] = p_ref2;
                    break;
                case 2:
                    ref_weaks[j] = ref_strongs[rand() % ref_strongs.size()];
                default:
                    ref_strongs[j] = ref_strongs[rand() % ref_strongs.size()];
                }
            }
        }
    }
}

TEST(WBERefStrongWeak, Multithread) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::MockHeapAllocatorAligned pool_allocator(1024);
    std::stringstream test_ss1;
    std::stringstream test_ss2;
    {
        WBE::Ref<TestClassWeak> ref1 = WBE::Ref<TestClassWeak>::make_ref(&pool_allocator, test_ss1);
        WBE::Ref<TestClassWeakChild> ref2 = WBE::Ref<TestClassWeakChild>::make_ref(&pool_allocator, test_ss2);
        ASSERT_EQ(test_ss1.str(), "Construct.\n");
        ASSERT_EQ(test_ss2.str(), "Construct.\nChild construct.\n");
        std::thread thread1(multithread_ref_strong_and_weak_test, ref1, ref2, 500000);
        std::thread thread2(multithread_ref_strong_and_weak_test, ref1, ref2, 500000);
        std::thread thread3(multithread_ref_strong_and_weak_test, ref1, ref2, 500000);
        std::thread thread4(multithread_ref_strong_and_weak_test, ref1, ref2, 500000);
        std::thread thread5(multithread_ref_strong_and_weak_test, ref1, ref2, 500000);
        thread1.join();
        thread2.join();
        thread3.join();
        thread4.join();
        thread5.join();
        ASSERT_EQ(test_ss1.str(), "Construct.\n");
        ASSERT_EQ(test_ss2.str(), "Construct.\nChild construct.\n");
    }
    ASSERT_EQ(test_ss1.str(), std::string("Construct.\nDestruct.\n"));
    ASSERT_EQ(test_ss2.str(), std::string("Construct.\nChild construct.\nChild destruct.\nDestruct.\n"));
    ASSERT_TRUE(pool_allocator.is_empty());
}

TEST(WBERefWeakTest, IsNullMethod_Behavior) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::MockHeapAllocatorAligned pool_allocator(1024);
    {
        // Default-constructed weak should not be NULL.
        WBE::RefWeak<int> weak_default;
        ASSERT_TRUE(weak_default.is_null());

        WBE::RefWeak<int> weak;
        {
            WBE::Ref<int> strong = WBE::Ref<int>::make_ref(&pool_allocator, 7);
            weak = strong;
            ASSERT_TRUE(weak.is_valid());
            // While valid, is_null() delegates to lock()->is_null() which is false for a live Ref
            ASSERT_FALSE(weak.is_null());
        }

        // After strong ref goes out of scope, weak becomes invalid; implementation returns false for invalid
        ASSERT_FALSE(weak.is_valid());
        ASSERT_TRUE(weak.is_null());
    }
    ASSERT_TRUE(pool_allocator.is_empty());
}
#endif
