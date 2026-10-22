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
#ifndef WBE_FILE_HEAP_ALLOCATOR_ATOMIC_SIZE_CLASSIFIED_TEST_HH
#define WBE_FILE_HEAP_ALLOCATOR_ATOMIC_SIZE_CLASSIFIED_TEST_HH

#include "core/allocator/heap_allocator_aligned.hh"
#include "core/allocator/heap_allocator_atomic_size_classified.hh"
#include "core/allocator/i_allocator.hh"
#include "global/global.hh"
#include "test_global.hh"
#include "utils/defs.hh"
#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace WBE = WhiteBirdEngine;

class WBEHeapAllocAtomicSizeClassifiedTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;

    using SizeClass = WBE::HeapAllocatorAtomicSizeClassified::SizeClass;
    static constexpr size_t HEADER_SIZE = WBE::HeapAllocatorAtomicSizeClassified::HEADER_SIZE;
    static constexpr size_t MAX_SIZE = std::numeric_limits<size_t>::max();
    // Small: below 64 bytes; medium: 64 to 1024 bytes, over two arenas; large: above 1024 bytes.
    static constexpr std::array<SizeClass, 3> SMALL_SIZE_CLASSES{
        SizeClass{.max_allocation_size = 63, .capacity = 1024, .arena_count = 1},
        SizeClass{.max_allocation_size = 1024, .capacity = WBE_KI_B(8), .arena_count = 2},
        SizeClass{.max_allocation_size = MAX_SIZE, .capacity = WBE_KI_B(16), .arena_count = 1},
    };
};

TEST_F(WBEHeapAllocAtomicSizeClassifiedTest, TraitTest) {
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicSizeClassified>::IS_POOL);
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicSizeClassified>::IS_LIMITED_SIZE);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicSizeClassified>::IS_GURANTEED_CONTINUOUS);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicSizeClassified>::IS_ALLOC_FIXED_SIZE);
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicSizeClassified>::IS_ATOMIC);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorAtomicSizeClassified>::WILL_ADDR_MOVE);
    ASSERT_TRUE((std::is_base_of_v<WBE::HeapAllocatorAligned, WBE::HeapAllocatorAtomicSizeClassified>));
}

TEST_F(WBEHeapAllocAtomicSizeClassifiedTest, DefaultSizeClassesMatchTheRequestedCapacities) {
    constexpr std::array<SizeClass, 3> DEFAULT_SIZE_CLASSES = WBE::HeapAllocatorAtomicSizeClassified::make_default_size_classes();
    EXPECT_EQ(DEFAULT_SIZE_CLASSES[0].max_allocation_size, WBE_KI_B(1) - 1);
    EXPECT_EQ(DEFAULT_SIZE_CLASSES[0].capacity, WBE_MI_B(1));
    EXPECT_EQ(DEFAULT_SIZE_CLASSES[1].max_allocation_size, WBE_MI_B(1));
    EXPECT_EQ(DEFAULT_SIZE_CLASSES[1].capacity, WBE_MI_B(15));
    EXPECT_EQ(DEFAULT_SIZE_CLASSES[2].max_allocation_size, MAX_SIZE);
    EXPECT_EQ(DEFAULT_SIZE_CLASSES[2].capacity, WBE_MI_B(512));
    EXPECT_EQ(DEFAULT_SIZE_CLASSES[0].arena_count, WBE::HeapAllocatorAtomicSizeClassified::DEFAULT_SMALL_ARENA_COUNT);
    EXPECT_EQ(DEFAULT_SIZE_CLASSES[1].arena_count, WBE::HeapAllocatorAtomicSizeClassified::DEFAULT_MEDIUM_ARENA_COUNT);
    EXPECT_EQ(DEFAULT_SIZE_CLASSES[2].arena_count, WBE::HeapAllocatorAtomicSizeClassified::DEFAULT_LARGE_ARENA_COUNT);
}

TEST_F(WBEHeapAllocAtomicSizeClassifiedTest, RotatesAllocationsAcrossTheArenasOfAClass) {
    WBE::HeapAllocatorAtomicSizeClassified allocator(SMALL_SIZE_CLASSES);
    constexpr size_t MEDIUM_ARENA_SIZE = WBE_KI_B(4);
    constexpr size_t MEDIUM_SIZE = 1000;

    // Consecutive allocations of a class start in different arenas of that class.
    WBE::MemID first = allocator.allocate(MEDIUM_SIZE);
    WBE::MemID second = allocator.allocate(MEDIUM_SIZE);
    EXPECT_EQ(second > first ? second - first : first - second, MEDIUM_ARENA_SIZE);
    allocator.deallocate(first);
    allocator.deallocate(second);

    // Filling the class uses both arenas, so it holds more allocations than one arena can.
    std::vector<WBE::MemID> allocations;
    for (WBE::MemID mem = allocator.try_allocate(MEDIUM_SIZE); mem != WBE::MEM_NULL; mem = allocator.try_allocate(MEDIUM_SIZE)) {
        allocations.push_back(mem);
    }
    EXPECT_GT(allocations.size() * (MEDIUM_SIZE + HEADER_SIZE), MEDIUM_ARENA_SIZE);
    EXPECT_EQ(allocator.get_remain_size(0), 1024U);
    EXPECT_EQ(allocator.get_remain_size(2), WBE_KI_B(16));
    for (WBE::MemID mem : allocations) {
        allocator.deallocate(mem);
    }
    EXPECT_TRUE(allocator.is_empty());
}

TEST_F(WBEHeapAllocAtomicSizeClassifiedTest, ClassifiesAllocationSizesByInclusiveMaximum) {
    WBE::HeapAllocatorAtomicSizeClassified allocator(SMALL_SIZE_CLASSES);
    ASSERT_EQ(allocator.get_size_class_count(), 3U);
    ASSERT_EQ(allocator.get_total_size(), 1024 + WBE_KI_B(8) + WBE_KI_B(16));
    EXPECT_EQ(allocator.get_size_class_index(1), 0U);
    EXPECT_EQ(allocator.get_size_class_index(63), 0U);
    EXPECT_EQ(allocator.get_size_class_index(64), 1U);
    EXPECT_EQ(allocator.get_size_class_index(1024), 1U);
    EXPECT_EQ(allocator.get_size_class_index(1025), 2U);
    EXPECT_EQ(allocator.get_size_class_index(MAX_SIZE), 2U);
}

TEST_F(WBEHeapAllocAtomicSizeClassifiedTest, AllocatesEachSizeInItsOwnArena) {
    WBE::HeapAllocatorAtomicSizeClassified allocator(SMALL_SIZE_CLASSES);

    WBE::MemID small = allocator.allocate(32);
    WBE::MemID medium = allocator.allocate(512);
    WBE::MemID large = allocator.allocate(4096);
    ASSERT_TRUE(allocator.is_in_pool(small));
    ASSERT_TRUE(allocator.is_in_pool(medium));
    ASSERT_TRUE(allocator.is_in_pool(large));
    EXPECT_LT(allocator.get_remain_size(0), 1024U);
    EXPECT_LT(allocator.get_remain_size(1), WBE_KI_B(8));
    EXPECT_LT(allocator.get_remain_size(2), WBE_KI_B(16));
    EXPECT_GE(allocator.get_allocated_data_size(small), 32U);
    EXPECT_GE(allocator.get_allocated_data_size(medium), 512U);
    EXPECT_GE(allocator.get_allocated_data_size(large), 4096U);

    allocator.deallocate(medium);
    EXPECT_EQ(allocator.get_remain_size(1), WBE_KI_B(8));
    allocator.deallocate(small);
    allocator.deallocate(large);
    EXPECT_TRUE(allocator.is_empty());
    EXPECT_EQ(allocator.get_remain_size(), allocator.get_total_size());
    EXPECT_NO_THROW(allocator.check_broken());
}

TEST_F(WBEHeapAllocAtomicSizeClassifiedTest, FullClassDoesNotSpillIntoOtherClasses) {
    WBE::HeapAllocatorAtomicSizeClassified allocator(SMALL_SIZE_CLASSES);

    std::vector<WBE::MemID> small_allocations;
    for (WBE::MemID mem = allocator.try_allocate(48); mem != WBE::MEM_NULL; mem = allocator.try_allocate(48)) {
        small_allocations.push_back(mem);
    }
    ASSERT_FALSE(small_allocations.empty());
    EXPECT_EQ(allocator.get_remain_size(1), WBE_KI_B(8));
    EXPECT_EQ(allocator.get_remain_size(2), WBE_KI_B(16));
    EXPECT_THROW(allocator.allocate(48), std::runtime_error);

    // The other classes still serve their own sizes.
    WBE::MemID medium = allocator.allocate(512);
    allocator.deallocate(medium);
    for (WBE::MemID mem : small_allocations) {
        allocator.deallocate(mem);
    }
    EXPECT_TRUE(allocator.is_empty());
}

TEST_F(WBEHeapAllocAtomicSizeClassifiedTest, HandlesAlignmentAndZeroSize) {
    WBE::HeapAllocatorAtomicSizeClassified allocator(SMALL_SIZE_CLASSES);

    WBE::MemID mem = allocator.allocate(8, HEADER_SIZE * 4);
    ASSERT_EQ(mem % (HEADER_SIZE * 4), 0U);
    EXPECT_EQ(allocator.allocate(0), WBE::MEM_NULL);
    allocator.deallocate(WBE::MEM_NULL);
    allocator.deallocate(mem);
    EXPECT_TRUE(allocator.is_empty());
}

TEST_F(WBEHeapAllocAtomicSizeClassifiedTest, RejectsInvalidSizeClasses) {
    const std::array<SizeClass, 2> unordered{
        SizeClass{.max_allocation_size = 128, .capacity = 1024},
        SizeClass{.max_allocation_size = 128, .capacity = 1024},
    };
    const std::array<SizeClass, 2> bounded_last{
        SizeClass{.max_allocation_size = 64, .capacity = 1024},
        SizeClass{.max_allocation_size = 128, .capacity = 1024},
    };
    const std::array<SizeClass, 1> zero_capacity{SizeClass{.max_allocation_size = MAX_SIZE, .capacity = 0}};
    const std::array<SizeClass, 1> unaligned_capacity{SizeClass{.max_allocation_size = MAX_SIZE, .capacity = HEADER_SIZE + 1}};
    const std::array<SizeClass, 1> zero_arenas{SizeClass{.max_allocation_size = MAX_SIZE, .capacity = 1024, .arena_count = 0}};
    const std::array<SizeClass, 1> indivisible_arenas{SizeClass{.max_allocation_size = MAX_SIZE, .capacity = 1024, .arena_count = 3}};
    const std::array<SizeClass, 1> unaligned_arenas{SizeClass{.max_allocation_size = MAX_SIZE, .capacity = HEADER_SIZE * 2, .arena_count = 4}};
    std::array<SizeClass, WBE::HeapAllocatorAtomicSizeClassified::MAX_SIZE_CLASS_COUNT + 1> too_many{};
    for (size_t i = 0; i < too_many.size(); ++i) {
        too_many[i] = SizeClass{.max_allocation_size = i + 1, .capacity = 1024};
    }
    too_many.back().max_allocation_size = MAX_SIZE;

    EXPECT_THROW(WBE::HeapAllocatorAtomicSizeClassified(std::span<const SizeClass>()), std::runtime_error);
    EXPECT_THROW(WBE::HeapAllocatorAtomicSizeClassified{unordered}, std::runtime_error);
    EXPECT_THROW(WBE::HeapAllocatorAtomicSizeClassified{bounded_last}, std::runtime_error);
    EXPECT_THROW(WBE::HeapAllocatorAtomicSizeClassified{zero_capacity}, std::runtime_error);
    EXPECT_THROW(WBE::HeapAllocatorAtomicSizeClassified{unaligned_capacity}, std::runtime_error);
    EXPECT_THROW(WBE::HeapAllocatorAtomicSizeClassified{zero_arenas}, std::runtime_error);
    EXPECT_THROW(WBE::HeapAllocatorAtomicSizeClassified{indivisible_arenas}, std::runtime_error);
    EXPECT_THROW(WBE::HeapAllocatorAtomicSizeClassified{unaligned_arenas}, std::runtime_error);
    EXPECT_THROW(WBE::HeapAllocatorAtomicSizeClassified{too_many}, std::runtime_error);

    WBE::HeapAllocatorAtomicSizeClassified allocator(SMALL_SIZE_CLASSES);
    EXPECT_THROW(allocator.get_size_class(3), std::runtime_error);
    EXPECT_THROW(allocator.get_remain_size(3), std::runtime_error);
}

TEST_F(WBEHeapAllocAtomicSizeClassifiedTest, TracksInternalFragmentationPerArena) {
    WBE::HeapAllocatorAtomicSizeClassified allocator(SMALL_SIZE_CLASSES);

    WBE::MemID small = allocator.allocate(7);
    WBE::MemID large = allocator.allocate(1031);

    // One small arena, two medium arenas, and one large arena.
    std::vector<size_t> trackers = allocator.get_internal_fragmentation_tracker();
    ASSERT_EQ(trackers.size(), 4U);
    EXPECT_GT(trackers[0], 0U);
    EXPECT_EQ(trackers[1], 0U);
    EXPECT_EQ(trackers[2], 0U);
    EXPECT_GT(trackers[3], 0U);

    allocator.deallocate(small);
    allocator.deallocate(large);
}

TEST_F(WBEHeapAllocAtomicSizeClassifiedTest, ConcurrentAllocationsProduceUniqueValidIds) {
    WBE::HeapAllocatorAtomicSizeClassified allocator(WBE::HeapAllocatorAtomicSizeClassified::make_default_size_classes(
        WBE_KI_B(512), WBE_MI_B(2), WBE_MI_B(8)));
    constexpr int THREAD_COUNT = 8;
    constexpr int ALLOCS_PER_THREAD = 64;
    // Every thread mixes all three classes.
    constexpr std::array<size_t, 3> ALLOCATION_SIZES{32, WBE_KI_B(2), WBE_MI_B(1) + 1};
    constexpr int LARGE_ALLOCS_PER_THREAD = 2;

    std::vector<std::thread> threads;
    std::vector<std::vector<WBE::MemID>> thread_mems(THREAD_COUNT);
    std::barrier sync_point(THREAD_COUNT);

    threads.reserve(THREAD_COUNT);
    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&, i] {
            sync_point.arrive_and_wait();
            for (int j = 0; j < ALLOCS_PER_THREAD; ++j) {
                const size_t size = j < LARGE_ALLOCS_PER_THREAD && i < 2 ? ALLOCATION_SIZES[2] : ALLOCATION_SIZES[j % 2];
                WBE::MemID mem = allocator.allocate(size);
                memset(allocator.get(mem), i & 0xFF, size);
                thread_mems[i].push_back(mem);
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

    ASSERT_TRUE(allocator.is_empty());
    ASSERT_EQ(allocator.get_remain_size(), allocator.get_total_size());
    ASSERT_NO_THROW(allocator.check_broken());
}

#endif
