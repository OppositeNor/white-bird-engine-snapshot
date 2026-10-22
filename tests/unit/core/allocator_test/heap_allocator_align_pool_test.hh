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
#ifndef WBE_FILE_HEAP_ALOCATOR_ALIGNED_POOL_TEST_HH
#define WBE_FILE_HEAP_ALOCATOR_ALIGNED_POOL_TEST_HH

#include "core/allocator/heap_allocator.hh"
#include "core/allocator/heap_allocator_aligned_pool.hh"
#include "core/allocator/i_allocator.hh"
#include "global/global.hh"
#include "test_global.hh"
#include "test_utilities.hh"
#include "utils/defs.hh"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace WBE = WhiteBirdEngine;

constexpr size_t AAPT_HEADER_SIZE = WBE::HeapAllocatorAlignedPool::HEADER_SIZE;

class WBEAllocAlignedPoolTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;
};

TEST_F(WBEAllocAlignedPoolTest, IsInPoolAllocatedAndDeallocated) {
    WBE::HeapAllocatorAlignedPool pool(128);
    WBE::MemID mem1 = pool.allocate(16);
    WBE::MemID mem2 = pool.allocate(16);
    ASSERT_TRUE(pool.is_in_pool(mem1));
    ASSERT_TRUE(pool.is_in_pool(mem2));
    pool.deallocate(mem1);
    ASSERT_FALSE(pool.is_in_pool(mem1));
    ASSERT_TRUE(pool.is_in_pool(mem2));
    pool.deallocate(mem2);
    ASSERT_FALSE(pool.is_in_pool(mem2));
}

TEST_F(WBEAllocAlignedPoolTest, IsInPoolNullAndOutOfRange) {
    WBE::HeapAllocatorAlignedPool pool(128);
    ASSERT_FALSE(pool.is_in_pool(WBE::MEM_NULL));
    WBE::MemID mem = pool.allocate(16);
    uintptr_t fake_id = reinterpret_cast<uintptr_t>(mem) + 1024;
    ASSERT_FALSE(pool.is_in_pool(static_cast<WBE::MemID>(fake_id)));
    pool.deallocate(mem);
}

TEST_F(WBEAllocAlignedPoolTest, TraitTest) {
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorAlignedPool>::IS_POOL);
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorAlignedPool>::IS_LIMITED_SIZE);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorAlignedPool>::IS_GURANTEED_CONTINUOUS);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorAlignedPool>::IS_ALLOC_FIXED_SIZE);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorAlignedPool>::IS_ATOMIC);
}
// Additional tests for HeapAllocatorAlignedPool

TEST_F(WBEAllocAlignedPoolTest, ZeroSizeAllocation) {
    WBE::HeapAllocatorAlignedPool pool(128);
    WBE::MemID mem = pool.allocate(0);
    ASSERT_EQ(mem, WBE::MEM_NULL);
    ASSERT_EQ(pool.get_remain_size(), 128);
}

TEST_F(WBEAllocAlignedPoolTest, MaxAlignmentAllocation) {
    WBE::HeapAllocatorAlignedPool pool(256);
    WBE::MemID mem = pool.allocate(8, 128);
    ASSERT_NE(mem, WBE::MEM_NULL);
    ASSERT_EQ(mem % 128, 0);
    pool.deallocate(mem);
    ASSERT_EQ(pool.get_remain_size(), 256);
}

TEST_F(WBEAllocAlignedPoolTest, PoolReuseAfterClear) {
    WBE::HeapAllocatorAlignedPool pool(128);
    pool.allocate(32);
    pool.clear();
    ASSERT_EQ(pool.get_remain_size(), 128);
    WBE::MemID mem2 = pool.allocate(64);
    ASSERT_NE(mem2, WBE::MEM_NULL);
    pool.deallocate(mem2);
    ASSERT_EQ(pool.get_remain_size(), 128);
}

TEST_F(WBEAllocAlignedPoolTest, FragmentationAndCoalescing) {
    WBE::HeapAllocatorAlignedPool pool(128);
    WBE::MemID mem1 = pool.allocate(16);
    WBE::MemID mem2 = pool.allocate(16);
    WBE::MemID mem3 = pool.allocate(16);
    pool.deallocate(mem2);
    ASSERT_LT(pool.get_remain_size(), 128);
    pool.deallocate(mem1);
    pool.deallocate(mem3);
    ASSERT_EQ(pool.get_remain_size(), 128);
}

TEST_F(WBEAllocAlignedPoolTest, RemoveIdleFront) {
    WBE::HeapAllocatorAlignedPool pool(1024);
    WBE::MemID mem1 = pool.allocate(4);
    WBE::MemID mem2 = pool.allocate(8);
    WBE::MemID mem3 = pool.allocate(4);
    WBE::MemID mem4 = pool.allocate(4);
    pool.deallocate(mem1); // Remove front
    ASSERT_FALSE(pool.is_in_pool(mem1));
    ASSERT_TRUE(pool.is_in_pool(mem2));
    ASSERT_TRUE(pool.is_in_pool(mem3));
    ASSERT_TRUE(pool.is_in_pool(mem4));
    pool.clear();
    ASSERT_EQ(pool.get_remain_size(), 1024);
}

TEST_F(WBEAllocAlignedPoolTest, RemoveIdleBack) {
    WBE::HeapAllocatorAlignedPool pool(1024);
    WBE::MemID mem1 = pool.allocate(4);
    WBE::MemID mem2 = pool.allocate(8);
    WBE::MemID mem3 = pool.allocate(4);
    WBE::MemID mem4 = pool.allocate(12);
    pool.deallocate(mem4); // Remove back
    ASSERT_TRUE(pool.is_in_pool(mem1));
    ASSERT_TRUE(pool.is_in_pool(mem2));
    ASSERT_TRUE(pool.is_in_pool(mem3));
    ASSERT_FALSE(pool.is_in_pool(mem4));
    pool.clear();
    ASSERT_EQ(pool.get_remain_size(), 1024);
}

TEST_F(WBEAllocAlignedPoolTest, RemoveIdleMiddle) {
    WBE::HeapAllocatorAlignedPool pool(1024);
    WBE::MemID mem1 = pool.allocate(4);
    WBE::MemID mem2 = pool.allocate(8);
    WBE::MemID mem3 = pool.allocate(4);
    WBE::MemID mem4 = pool.allocate(12);
    WBE::MemID mem5 = pool.allocate(128);
    pool.deallocate(mem3); // Remove middle
    ASSERT_TRUE(pool.is_in_pool(mem1));
    ASSERT_TRUE(pool.is_in_pool(mem2));
    ASSERT_FALSE(pool.is_in_pool(mem3));
    ASSERT_TRUE(pool.is_in_pool(mem4));
    ASSERT_TRUE(pool.is_in_pool(mem5));
    pool.clear();
    ASSERT_EQ(pool.get_remain_size(), 1024);
}

TEST_F(WBEAllocAlignedPoolTest, RemoveIdleEnd) {
    WBE::HeapAllocatorAlignedPool pool(1024);
    WBE::MemID mem1 = pool.allocate(4);
    WBE::MemID mem2 = pool.allocate(8);
    WBE::MemID mem3 = pool.allocate(pool.get_remain_size() - AAPT_HEADER_SIZE);
    pool.deallocate(mem3); // Remove end
    ASSERT_TRUE(pool.is_in_pool(mem1));
    ASSERT_TRUE(pool.is_in_pool(mem2));
    ASSERT_FALSE(pool.is_in_pool(mem3));
    pool.clear();
    ASSERT_EQ(pool.get_remain_size(), 1024);
}

TEST_F(WBEAllocAlignedPoolTest, RemainSize) {
    WhiteBirdEngine::HeapAllocatorAlignedPool pool(1024);
    WBE::MemID mem = pool.allocate(2);
    ASSERT_LE(pool.get_remain_size(), 1024 - 2);
    pool.deallocate(mem);
    ASSERT_EQ(pool.get_remain_size(), 1024);
    WBE::MemID all_mem = pool.allocate(1024 - AAPT_HEADER_SIZE);
    ASSERT_EQ(pool.get_remain_size(), 0);
    pool.deallocate(all_mem);
    ASSERT_EQ(pool.get_remain_size(), 1024);
}

TEST_F(WBEAllocAlignedPoolTest, ConstructDestructCall) {
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
        TestClass(const TestClass&) noexcept = default;
        TestClass(TestClass&&) noexcept = default;
        TestClass& operator=(const TestClass&) noexcept = default;
        TestClass& operator=(TestClass&&) noexcept = default;
        uint32_t* test_val_ptr;
    };
    WhiteBirdEngine::HeapAllocatorAlignedPool allocator = WhiteBirdEngine::HeapAllocatorAlignedPool();
    WBE::MemID test_obj = WhiteBirdEngine::create_obj<TestClass>(allocator, &test_val);
    ASSERT_EQ(test_val, 1);
    WBE::destroy_obj<TestClass>(allocator, test_obj);
    ASSERT_EQ(test_val, 2);
}

TEST_F(WBEAllocAlignedPoolTest, AlignmentTest) {
    WhiteBirdEngine::HeapAllocatorAlignedPool allocator = WhiteBirdEngine::HeapAllocatorAlignedPool(WBE_MI_B(0.5));
    constexpr size_t ALIGN_REQ = alignof(WhiteBirdEngine::HeapAllocatorAlignedPool::Header);
    WBE::MemID mem1 = allocator.allocate(1, ALIGN_REQ);
    ASSERT_EQ(mem1 % ALIGN_REQ, 0);
    WBE::MemID mem2 = allocator.allocate(1, ALIGN_REQ * 2);
    ASSERT_EQ(mem2 % (2 * ALIGN_REQ), 0);
    WBE::MemID mem3 = allocator.allocate(1, ALIGN_REQ * 3);
    ASSERT_EQ(mem3 % (3 * ALIGN_REQ), 0);
    WBE::MemID mem4 = allocator.allocate(1, ALIGN_REQ * 4);
    ASSERT_EQ(mem4 % (4 * ALIGN_REQ), 0);
    WBE::MemID mem5 = allocator.allocate(1, ALIGN_REQ * 5);
    ASSERT_EQ(mem5 % (5 * ALIGN_REQ), 0);
    WBE::MemID mem6 = allocator.allocate(1, ALIGN_REQ * 6);
    ASSERT_EQ(mem6 % (6 * ALIGN_REQ), 0);
    WBE::MemID mem7 = allocator.allocate(1, ALIGN_REQ * 7);
    ASSERT_EQ(mem7 % (7 * ALIGN_REQ), 0);
    WBE::MemID mem8 = allocator.allocate(1, ALIGN_REQ * 8);
    ASSERT_EQ(mem8 % (8 * ALIGN_REQ), 0);
    WBE::MemID mem9 = allocator.allocate(1, ALIGN_REQ * 9);
    ASSERT_EQ(mem9 % (9 * ALIGN_REQ), 0);
    WBE::MemID mem10 = allocator.allocate(1, ALIGN_REQ * 10);
    ASSERT_EQ(mem10 % (10 * ALIGN_REQ), 0);

    // False alignments
    ASSERT_THROW(allocator.allocate(3, 0), std::runtime_error);
    ASSERT_THROW(allocator.allocate(10, ALIGN_REQ * 10 + 1), std::runtime_error);

    allocator.deallocate(mem1);
    allocator.deallocate(mem2);
    allocator.deallocate(mem3);
    allocator.deallocate(mem4);
    allocator.deallocate(mem5);
    allocator.deallocate(mem6);
    allocator.deallocate(mem7);
    allocator.deallocate(mem8);
    allocator.deallocate(mem9);
    allocator.deallocate(mem10);

    ASSERT_EQ(allocator.get_remain_size(), WBE_MI_B(0.5));
}

TEST_F(WBEAllocAlignedPoolTest, ManySmallAllocations) {
    const int num_allocs = 64;
    WhiteBirdEngine::HeapAllocatorAlignedPool allocator = WhiteBirdEngine::HeapAllocatorAlignedPool(1024 + AAPT_HEADER_SIZE * num_allocs);
    WBEAllocPoolBehavTestClass behv_test("HeapAllocatorAlignedPool", 1024 + AAPT_HEADER_SIZE * num_allocs, AAPT_HEADER_SIZE);
    std::vector<WBE::MemID> mems(num_allocs);
    for (uint64_t& mem : mems) {
        mem = allocator.allocate(16);
        ASSERT_NE(mem, WBE::MEM_NULL);
        ASSERT_EQ(mem % 8, 0);
    }
    ASSERT_EQ(allocator.get_remain_size(), 0);
    std::string exp = behv_test({
        {true, -1},
    });
    ASSERT_EQ(static_cast<std::string>(allocator), exp);
    for (uint64_t mem : mems) {
        allocator.deallocate(mem);
    }
    ASSERT_EQ(allocator.get_remain_size(), 1024 + AAPT_HEADER_SIZE * num_allocs);
}

TEST_F(WBEAllocAlignedPoolTest, GetMemoryAddress) {
    WhiteBirdEngine::HeapAllocatorAlignedPool allocator = WhiteBirdEngine::HeapAllocatorAlignedPool(1024);
    WBE::MemID mem1 = allocator.allocate(16, 16);
    WBE::MemID mem2 = allocator.allocate(32, 16);
    WBE::MemID mem3 = allocator.allocate(64, 32);

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

#endif
