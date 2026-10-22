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
#ifndef WBE_FILE_HEAP_ALLOCATOR_ATOMIC_ARENA_ALIGNED_POOL_IMPL_LIST_TEST_HH
#define WBE_FILE_HEAP_ALLOCATOR_ATOMIC_ARENA_ALIGNED_POOL_IMPL_LIST_TEST_HH

#include "core/allocator/heap_allocator_aligned.hh"
#include "core/allocator/heap_allocator_atomic_arena_aligned_pool_impl_list.hh"
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
#include <set>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace WBE = WhiteBirdEngine;

class WBEHeapAllocAtomicArenaAlignedPoolImplicitListTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;

    static constexpr size_t HEADER_SIZE = WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList::HEADER_SIZE;
};

TEST_F(WBEHeapAllocAtomicArenaAlignedPoolImplicitListTest, TraitTest) {
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList>::IS_POOL);
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList>::IS_LIMITED_SIZE);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList>::IS_GURANTEED_CONTINUOUS);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList>::IS_ALLOC_FIXED_SIZE);
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList>::IS_ATOMIC);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList>::WILL_ADDR_MOVE);
    ASSERT_TRUE((std::is_base_of_v<WBE::HeapAllocatorAligned, WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList>));
}

TEST_F(WBEHeapAllocAtomicArenaAlignedPoolImplicitListTest, SplitsMemoryChunkIntoIndexedArenas) {
    WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList allocator(1024, 4);
    ASSERT_EQ(allocator.get_total_size(), 1024);
    ASSERT_EQ(allocator.get_arena_count(), 4);
    ASSERT_EQ(allocator.get_arena_size(), 256);

    WBE::MemID mem0 = allocator.allocate_in_arena(32, 0);
    WBE::MemID mem1 = allocator.allocate_in_arena(32, 1);
    WBE::MemID mem2 = allocator.allocate_in_arena(32, 2);
    WBE::MemID mem3 = allocator.allocate_in_arena(32, 3);

    ASSERT_EQ(mem1 - mem0, allocator.get_arena_size());
    ASSERT_EQ(mem2 - mem1, allocator.get_arena_size());
    ASSERT_EQ(mem3 - mem2, allocator.get_arena_size());
    ASSERT_TRUE(allocator.is_in_pool(mem0));
    ASSERT_TRUE(allocator.is_in_pool(mem1));
    ASSERT_TRUE(allocator.is_in_pool(mem2));
    ASSERT_TRUE(allocator.is_in_pool(mem3));

    allocator.deallocate(mem0);
    allocator.deallocate(mem1);
    allocator.deallocate(mem2);
    allocator.deallocate(mem3);
    ASSERT_EQ(allocator.get_remain_size(), 1024);
}

TEST_F(WBEHeapAllocAtomicArenaAlignedPoolImplicitListTest, DeallocatesByMemoryOffset) {
    WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList allocator(1024, 4);

    WBE::MemID front = allocator.allocate_in_arena(64, 0);
    WBE::MemID back = allocator.allocate_in_arena(64, 3);
    ASSERT_TRUE(allocator.is_in_pool(front));
    ASSERT_TRUE(allocator.is_in_pool(back));

    allocator.deallocate(back);
    ASSERT_TRUE(allocator.is_in_pool(front));
    ASSERT_FALSE(allocator.is_in_pool(back));

    allocator.deallocate(front);
    ASSERT_FALSE(allocator.is_in_pool(front));
    ASSERT_EQ(allocator.get_remain_size(), 1024);
}

TEST_F(WBEHeapAllocAtomicArenaAlignedPoolImplicitListTest, HandlesAlignmentAndInvalidArenaInputs) {
    WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList allocator(WBE_KI_B(4), 4);

    WBE::MemID mem = allocator.allocate_in_arena(8, HEADER_SIZE * 4, 2);
    ASSERT_EQ(mem % (HEADER_SIZE * 4), 0);
    ASSERT_GE(allocator.get_allocated_data_size(mem), 8);

    ASSERT_THROW(allocator.allocate_in_arena(8, HEADER_SIZE, 4), std::runtime_error);
    ASSERT_THROW(allocator.allocate_in_arena(8, 0, 0), std::runtime_error);
    ASSERT_THROW(WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList allocator(1024, 0), std::runtime_error);
    ASSERT_THROW(WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList allocator(1025, 4), std::runtime_error);
    ASSERT_THROW(WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList allocator(48, 4), std::runtime_error);

    allocator.deallocate(mem);
    ASSERT_EQ(allocator.get_remain_size(), WBE_KI_B(4));
}

TEST_F(WBEHeapAllocAtomicArenaAlignedPoolImplicitListTest, TracksInternalFragmentationPerArena) {
    WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList allocator(1024, 4);

    WBE::MemID arena_1_mem = allocator.allocate_in_arena(7, 1);
    WBE::MemID arena_3_mem = allocator.allocate_in_arena(11, 3);

    std::vector<size_t> trackers = allocator.get_internal_fragmentation_tracker();
    ASSERT_EQ(trackers.size(), 4U);
    EXPECT_EQ(trackers[0], 0U);
    EXPECT_GT(trackers[1], 0U);
    EXPECT_EQ(trackers[2], 0U);
    EXPECT_GT(trackers[3], 0U);

    allocator.deallocate(arena_1_mem);
    allocator.deallocate(arena_3_mem);
}

TEST_F(WBEHeapAllocAtomicArenaAlignedPoolImplicitListTest, ConcurrentAllocationsProduceUniqueValidIds) {
    WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList allocator(WBE_MI_B(2), 8);
    constexpr int THREAD_COUNT = 8;
    constexpr int ALLOCS_PER_THREAD = 64;

    std::vector<std::thread> threads;
    std::vector<std::vector<WBE::MemID>> thread_mems(THREAD_COUNT);
    std::atomic<int> success_count{0};
    std::barrier sync_point(THREAD_COUNT);

    threads.reserve(THREAD_COUNT);
    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&, i] {
            sync_point.arrive_and_wait();
            for (int j = 0; j < ALLOCS_PER_THREAD; ++j) {
                WBE::MemID mem = allocator.allocate(32);
                memset(allocator.get(mem), i & 0xFF, 32);
                thread_mems[i].push_back(mem);
                success_count.fetch_add(1);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    std::set<WBE::MemID> all_mems;
    for (const auto& thread_mem : thread_mems) {
        for (WBE::MemID mem : thread_mem) {
            ASSERT_TRUE(allocator.is_in_pool(mem));
            ASSERT_TRUE(all_mems.insert(mem).second);
        }
    }
    ASSERT_EQ(all_mems.size(), static_cast<size_t>(success_count.load()));

    threads.clear();
    std::barrier deallocate_sync_point(THREAD_COUNT);
    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&, i] {
            deallocate_sync_point.arrive_and_wait();
            for (WBE::MemID mem : thread_mems[i]) {
                allocator.deallocate(mem);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_EQ(allocator.get_remain_size(), WBE_MI_B(2));
    ASSERT_NO_THROW(allocator.check_broken());
}

#endif
