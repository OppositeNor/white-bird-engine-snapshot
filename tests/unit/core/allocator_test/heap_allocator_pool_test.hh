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
#ifndef WBE_FILE_HEAP_ALOCATOR_POOL_TEST_HH
#define WBE_FILE_HEAP_ALOCATOR_POOL_TEST_HH

#include "core/allocator/heap_allocator.hh"
#include "core/allocator/heap_allocator_pool.hh"
#include "core/allocator/i_allocator.hh"
#include "global/global.hh"
#include "test_global.hh"
#include "test_utilities.hh"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <type_traits>

namespace WBE = WhiteBirdEngine;

constexpr size_t APT_HEADER_SIZE = WBE::HeapAllocatorPool::HEADER_SIZE;
static_assert(std::is_same_v<WBE::HeapAllocatorPool::Header, uint64_t>);

TEST(WBEAllocPoolTest, TraitTest) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorPool>::IS_POOL);
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorPool>::IS_LIMITED_SIZE);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorPool>::IS_GURANTEED_CONTINUOUS);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorPool>::IS_ALLOC_FIXED_SIZE);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorPool>::IS_ATOMIC);
}

TEST(WBEAllocPoolTest, ToString) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::HeapAllocatorPool pool(1024);
    WBEAllocPoolBehavTestClass behv_test("HeapAllocatorPool", 1024, APT_HEADER_SIZE);
    std::string exp_1 = behv_test({{false, 1024}});
    ASSERT_EQ(static_cast<std::string>(pool), exp_1);
    WBE::MemID mem1 = pool.allocate(2);
    std::string exp_2 = behv_test({{true, 2}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp_2);
    WBE::MemID mem2 = pool.allocate(8);
    std::string exp_3 = behv_test({{true, 2}, {true, 8}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp_3);
    pool.deallocate(mem1);
    std::string exp_4 = behv_test({{false, 2 + APT_HEADER_SIZE}, {true, 8}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp_4);
    pool.deallocate(mem2);
    ASSERT_EQ(static_cast<std::string>(pool), exp_1);
    auto mem3 = pool.allocate(1024 - APT_HEADER_SIZE);
    std::string exp_5 = behv_test({{true, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp_5);
    pool.deallocate(mem3);
    ASSERT_EQ(static_cast<std::string>(pool), exp_1);
}

TEST(WBEAllocPoolTest, RemoveIdleFront) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::HeapAllocatorPool pool(1024);
    WBEAllocPoolBehavTestClass behv_test("HeapAllocatorPool", 1024, APT_HEADER_SIZE);
    WBE::MemID mem1 = pool.allocate(4);
    WBE::MemID mem2 = pool.allocate(8);
    WBE::MemID mem3 = pool.allocate(4);
    WBE::MemID mem4 = pool.allocate(4);
    pool.deallocate(mem3);
    std::string exp1 = behv_test({{true, 4}, {true, 8}, {false, 4 + APT_HEADER_SIZE}, {true, 4}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp1);
    pool.deallocate(mem2);
    std::string exp2 = behv_test({{true, 4}, {false, 12 + APT_HEADER_SIZE * 2}, {true, 4}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp2);
    pool.deallocate(mem4);
    std::string exp3 = behv_test({{true, 4}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp3);
    pool.deallocate(mem1);
    std::string exp4 = behv_test({{false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp4);
}

TEST(WBEAllocPoolTest, RemoveIdleBack) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::HeapAllocatorPool pool(1024);
    WBEAllocPoolBehavTestClass behv_test("HeapAllocatorPool", 1024, APT_HEADER_SIZE);
    WBE::MemID mem1 = pool.allocate(4);
    WBE::MemID mem2 = pool.allocate(8);
    WBE::MemID mem3 = pool.allocate(4);
    WBE::MemID mem4 = pool.allocate(12);
    pool.deallocate(mem2);
    std::string exp1 = behv_test({{true, 4}, {false, 8 + APT_HEADER_SIZE}, {true, 4}, {true, 12}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp1);
    pool.deallocate(mem3);
    std::string exp2 = behv_test({{true, 4}, {false, 12 + APT_HEADER_SIZE * 2}, {true, 12}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp2);
    pool.deallocate(mem4);
    std::string exp3 = behv_test({{true, 4}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp3);
    pool.deallocate(mem1);
    std::string exp4 = behv_test({{false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp4);
}

TEST(WBEAllocPoolTest, RemoveIdleMiddle) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::HeapAllocatorPool pool(1024);
    WBEAllocPoolBehavTestClass behv_test("HeapAllocatorPool", 1024, APT_HEADER_SIZE);
    WBE::MemID mem1 = pool.allocate(4);
    WBE::MemID mem2 = pool.allocate(8);
    WBE::MemID mem3 = pool.allocate(4);
    WBE::MemID mem4 = pool.allocate(12);
    WBE::MemID mem5 = pool.allocate(128);
    pool.deallocate(mem2);
    std::string exp1 = behv_test({{true, 4}, {false, 8 + APT_HEADER_SIZE}, {true, 4}, {true, 12}, {true, 128}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp1);
    pool.deallocate(mem4);
    std::string exp2 =
        behv_test({{true, 4}, {false, 8 + APT_HEADER_SIZE}, {true, 4}, {false, 12 + APT_HEADER_SIZE}, {true, 128}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp2);
    pool.deallocate(mem3);
    std::string exp3 = behv_test({{true, 4}, {false, 24 + APT_HEADER_SIZE * 3}, {true, 128}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp3);
    pool.deallocate(mem1);
    std::string exp4 = behv_test({{false, 28 + APT_HEADER_SIZE * 4}, {true, 128}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp4);
    pool.deallocate(mem5);
    std::string exp5 = behv_test({{false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp5);
}

TEST(WBEAllocPoolTest, RemoveIdleEnd) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WBE::HeapAllocatorPool pool(1024);
    WBEAllocPoolBehavTestClass behv_test("HeapAllocatorPool", 1024, APT_HEADER_SIZE);
    WBE::MemID mem1 = pool.allocate(4);
    WBE::MemID mem2 = pool.allocate(8);
    WBE::MemID mem3 = pool.allocate(1012 - 3 * APT_HEADER_SIZE);
    std::string exp1 = behv_test({{true, 4}, {true, 8}, {true, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp1);
    pool.deallocate(mem3);
    std::string exp2 = behv_test({{true, 4}, {true, 8}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp2);
    pool.deallocate(mem2);
    std::string exp3 = behv_test({{true, 4}, {false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp3);
    pool.deallocate(mem1);
    std::string exp4 = behv_test({{false, -1}});
    ASSERT_EQ(static_cast<std::string>(pool), exp4);
}

TEST(WBEAllocPoolTest, RemainSize) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();

    WhiteBirdEngine::HeapAllocatorPool pool(1024);
    WBE::MemID mem = pool.allocate(2);
    ASSERT_EQ(pool.get_remain_size(), 1022 - APT_HEADER_SIZE);
    pool.deallocate(mem);
    ASSERT_EQ(pool.get_remain_size(), 1024);
    WBE::MemID all_mem = pool.allocate(1024 - APT_HEADER_SIZE);
    ASSERT_EQ(pool.get_remain_size(), 0);
    pool.deallocate(all_mem);
    ASSERT_EQ(pool.get_remain_size(), 1024);
}

TEST(WBEAllocPoolTest, ConstructDestructCall) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    uint32_t test_val = 0;
    class TestClass {
    public:
        explicit TestClass(uint32_t* p_test_val) {
            test_val_ptr = p_test_val;
            *test_val_ptr = 1;
        }
        ~TestClass() {
            *test_val_ptr = 2;
        }
        uint32_t* test_val_ptr;
    };
    WhiteBirdEngine::HeapAllocatorPool allocator = WhiteBirdEngine::HeapAllocatorPool();
    WBE::MemID test_obj = WhiteBirdEngine::create_obj<TestClass>(allocator, &test_val);
    ASSERT_EQ(test_val, 1);
    WBE::destroy_obj<TestClass>(allocator, test_obj);
    ASSERT_EQ(test_val, 2);
}

TEST(WBEAllocPoolTest, WithDiffInstances) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    class TestBase {
    public:
        TestBase() {
            (void)placeholder;
        }
        uint32_t placeholder{};
    };
    class TestChild1 : public TestBase {
    public:
        TestChild1() {
            (void)child_placeholder;
        }
        uint32_t child_placeholder{};
    };
    class TestChild2 : public TestBase {
    public:
        TestChild2() {
            (void)child_placeholder1;
            (void)child_placeholder2;
        }
        uint32_t child_placeholder1{};
        uint32_t child_placeholder2{};
    };

    WhiteBirdEngine::HeapAllocatorPool allocator = WhiteBirdEngine::HeapAllocatorPool(1024);
    WBE::MemID test_obj = WhiteBirdEngine::create_obj<TestBase>(allocator);
    ASSERT_EQ(allocator.get_remain_size(), 1024 - sizeof(TestBase) - APT_HEADER_SIZE);
    WBE::MemID test_obj1 = WhiteBirdEngine::create_obj<TestChild1>(allocator);
    ASSERT_EQ(allocator.get_remain_size(), 1024 - sizeof(TestBase) - sizeof(TestChild1) - 2 * APT_HEADER_SIZE);
    WBE::MemID test_obj2 = WhiteBirdEngine::create_obj<TestChild2>(allocator);
    ASSERT_EQ(allocator.get_remain_size(), 1024 - sizeof(TestBase) - sizeof(TestChild1) - sizeof(TestChild2) - 3 * APT_HEADER_SIZE);
    WhiteBirdEngine::destroy_obj<TestBase>(allocator, test_obj);
    ASSERT_EQ(allocator.get_remain_size(), 1024 - sizeof(TestChild1) - sizeof(TestChild2) - 2 * APT_HEADER_SIZE);
    WhiteBirdEngine::destroy_obj<TestChild1>(allocator, test_obj1);
    ASSERT_EQ(allocator.get_remain_size(), 1024 - sizeof(TestChild2) - APT_HEADER_SIZE);
    WhiteBirdEngine::destroy_obj<TestChild2>(allocator, test_obj2);
    ASSERT_EQ(allocator.get_remain_size(), 1024);
}

TEST(WBEAllocPoolTest, GetMemoryAddress) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WhiteBirdEngine::HeapAllocatorPool allocator = WhiteBirdEngine::HeapAllocatorPool(1024);
    WBE::MemID mem1 = allocator.allocate(16);
    WBE::MemID mem2 = allocator.allocate(32);
    WBE::MemID mem3 = allocator.allocate(64);

    void* addr1 = allocator.get(mem1);
    void* addr2 = allocator.get(mem2);
    void* addr3 = allocator.get(mem3);

    ASSERT_NE(addr1, nullptr);
    ASSERT_NE(addr2, nullptr);
    ASSERT_NE(addr3, nullptr);

    ASSERT_EQ(std::bit_cast<WBE::MemID>(addr1), mem1);
    ASSERT_EQ(std::bit_cast<WBE::MemID>(addr2), mem2);
    ASSERT_EQ(std::bit_cast<WBE::MemID>(addr3), mem3);

    allocator.deallocate(mem1);
    allocator.deallocate(mem2);
    allocator.deallocate(mem3);
}

TEST(WBEAllocPoolTest, GetNullMemoryAddress) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WhiteBirdEngine::HeapAllocatorPool allocator = WhiteBirdEngine::HeapAllocatorPool(1024);
    void* addr = allocator.get(WBE::MEM_NULL);
    ASSERT_EQ(addr, nullptr);
}

TEST(WBEAllocPoolTest, MaxDataSizeTracker) {
    std::unique_ptr<WBE::Global> global = WBE::make_test_global();
    WhiteBirdEngine::HeapAllocatorPool allocator = WhiteBirdEngine::HeapAllocatorPool(1024);
    ASSERT_EQ(allocator.get_max_data_size(), 0);
    WBE::MemID mem1 = allocator.allocate(100);
    ASSERT_EQ(allocator.get_max_data_size(), 100 + APT_HEADER_SIZE);
    WBE::MemID mem2 = allocator.allocate(200);
    ASSERT_EQ(allocator.get_max_data_size(), 300 + 2 * APT_HEADER_SIZE);
    allocator.deallocate(mem1);
    ASSERT_EQ(allocator.get_max_data_size(), 300 + 2 * APT_HEADER_SIZE);
    WBE::MemID mem3 = allocator.allocate(50);
    ASSERT_EQ(allocator.get_max_data_size(), 300 + 2 * APT_HEADER_SIZE);
    allocator.deallocate(mem2);
    ASSERT_EQ(allocator.get_max_data_size(), 300 + 2 * APT_HEADER_SIZE);
    allocator.deallocate(mem3);
    ASSERT_EQ(allocator.get_max_data_size(), 300 + 2 * APT_HEADER_SIZE);
}

#endif
