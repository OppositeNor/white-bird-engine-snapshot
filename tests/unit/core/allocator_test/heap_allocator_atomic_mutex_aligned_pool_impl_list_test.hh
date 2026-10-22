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
#ifndef WBE_FILE_HEAP_ALLOCATOR_ATOMIC_MUTEX_ALIGNED_POOL_IMPL_LIST_TEST_HH
#define WBE_FILE_HEAP_ALLOCATOR_ATOMIC_MUTEX_ALIGNED_POOL_IMPL_LIST_TEST_HH

#include "core/allocator/heap_allocator_atomic_mutex_aligned_pool_impl_list.hh"
#include "core/allocator/i_allocator.hh"
#include "global/global.hh"
#include "test_global.hh"
#include "utils/defs.hh"
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace WBE = WhiteBirdEngine;

class WBEHeapAllocAtomicMutexAlignedPoolImplicitListTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;

    static constexpr size_t HEADER_SIZE = WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList::HEADER_SIZE;
};

TEST_F(WBEHeapAllocAtomicMutexAlignedPoolImplicitListTest, TraitAndDefaultAliasTest) {
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList>::IS_POOL);
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList>::IS_LIMITED_SIZE);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList>::IS_GURANTEED_CONTINUOUS);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList>::IS_ALLOC_FIXED_SIZE);
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList>::IS_ATOMIC);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList>::WILL_ADDR_MOVE);
}

TEST_F(WBEHeapAllocAtomicMutexAlignedPoolImplicitListTest, AllocatesAndDeallocates) {
    WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList pool(256);

    WBE::MemID mem1 = pool.allocate(16);
    WBE::MemID mem2 = pool.allocate(32);
    ASSERT_TRUE(pool.is_in_pool(mem1));
    ASSERT_TRUE(pool.is_in_pool(mem2));
    ASSERT_GE(pool.get_allocated_data_size(mem1), 16);
    ASSERT_GE(pool.get_allocated_data_size(mem2), 32);

    memset(pool.get(mem1), 0xAB, 16);
    memset(pool.get(mem2), 0xCD, 32);

    pool.deallocate(mem1);
    ASSERT_FALSE(pool.is_in_pool(mem1));
    ASSERT_TRUE(pool.is_in_pool(mem2));

    pool.deallocate(mem2);
    ASSERT_FALSE(pool.is_in_pool(mem2));
    ASSERT_EQ(pool.get_remain_size(), 256);
}

TEST_F(WBEHeapAllocAtomicMutexAlignedPoolImplicitListTest, HandlesAlignmentAndInvalidAlignment) {
    WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList pool(WBE_KI_B(4));

    WBE::MemID mem1 = pool.allocate(8, HEADER_SIZE);
    WBE::MemID mem2 = pool.allocate(8, HEADER_SIZE * 4);
    WBE::MemID mem3 = pool.allocate(8, HEADER_SIZE * 8);
    ASSERT_EQ(mem1 % HEADER_SIZE, 0);
    ASSERT_EQ(mem2 % (HEADER_SIZE * 4), 0);
    ASSERT_EQ(mem3 % (HEADER_SIZE * 8), 0);

    ASSERT_THROW(pool.allocate(8, 0), std::runtime_error);
    ASSERT_THROW(pool.allocate(8, HEADER_SIZE + 1), std::runtime_error);

    pool.deallocate(mem1);
    pool.deallocate(mem2);
    pool.deallocate(mem3);
    ASSERT_EQ(pool.get_remain_size(), WBE_KI_B(4));
}

TEST_F(WBEHeapAllocAtomicMutexAlignedPoolImplicitListTest, CoalescesFragmentsAndCanClear) {
    WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList pool(512);

    WBE::MemID mem1 = pool.allocate(32);
    WBE::MemID mem2 = pool.allocate(32);
    WBE::MemID mem3 = pool.allocate(32);
    pool.deallocate(mem2);
    ASSERT_LT(pool.get_remain_size(), 512);

    pool.deallocate(mem1);
    pool.deallocate(mem3);
    ASSERT_EQ(pool.get_remain_size(), 512);
    ASSERT_NO_THROW(pool.check_broken());

    WBE::MemID mem4 = pool.allocate(128);
    ASSERT_TRUE(pool.is_in_pool(mem4));
    pool.clear();
    ASSERT_FALSE(pool.is_in_pool(mem4));
    ASSERT_EQ(pool.get_remain_size(), 512);
}

TEST_F(WBEHeapAllocAtomicMutexAlignedPoolImplicitListTest, ConcurrentAllocationsProduceUniqueValidIds) {
    WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList pool(WBE_MI_B(4));
    constexpr int THREAD_COUNT = 8;
    constexpr int ALLOCS_PER_THREAD = 100;

    std::vector<std::thread> threads;
    std::vector<std::vector<WBE::MemID>> thread_mems(THREAD_COUNT);
    std::atomic<int> success_count{0};
    std::barrier sync_point(THREAD_COUNT);

    threads.reserve(THREAD_COUNT);
    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&, i] {
            sync_point.arrive_and_wait();
            std::mt19937 rng(i + 37);
            std::uniform_int_distribution<int> size_dist(16, 128);

            for (int j = 0; j < ALLOCS_PER_THREAD; ++j) {
                int size = size_dist(rng);
                WBE::MemID mem = pool.allocate(size);
                if (mem != WBE::MEM_NULL) {
                    memset(pool.get(mem), i & 0xFF, static_cast<size_t>(size));
                    thread_mems[i].push_back(mem);
                    success_count.fetch_add(1);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    std::set<WBE::MemID> all_mems;
    for (const auto& thread_mem : thread_mems) {
        for (WBE::MemID mem : thread_mem) {
            ASSERT_TRUE(pool.is_in_pool(mem));
            ASSERT_TRUE(all_mems.insert(mem).second);
        }
    }
    ASSERT_EQ(all_mems.size(), static_cast<size_t>(success_count.load()));
    ASSERT_GT(success_count.load(), 0);

    for (const auto& thread_mem : thread_mems) {
        for (WBE::MemID mem : thread_mem) {
            pool.deallocate(mem);
        }
    }
    ASSERT_EQ(pool.get_remain_size(), WBE_MI_B(4));
}

#endif
