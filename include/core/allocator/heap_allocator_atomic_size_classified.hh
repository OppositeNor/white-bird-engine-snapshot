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
#ifndef WBE_FILE_HEAP_ALLOCATOR_ATOMIC_SIZE_CLASSIFIED_HH
#define WBE_FILE_HEAP_ALLOCATOR_ATOMIC_SIZE_CLASSIFIED_HH

#include "core/allocator/heap_allocator_aligned.hh"
#include "core/allocator/heap_allocator_atomic_arena_aligned_pool_impl_list.hh"
#include "core/allocator/i_allocator.hh"
#include "utils/defs.hh"
#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace WhiteBirdEngine {

template <>
struct AllocatorTrait<class HeapAllocatorAtomicSizeClassified> final : public AllocatorTrait<HeapAllocatorAligned> {
    WBE_TRAIT(AllocatorTrait<HeapAllocatorAtomicSizeClassified>);
    static constexpr bool IS_POOL = true;
    static constexpr bool IS_GURANTEED_CONTINUOUS = false;
    static constexpr bool IS_LIMITED_SIZE = true;
    static constexpr bool IS_ALLOC_FIXED_SIZE = false;
    static constexpr bool IS_ATOMIC = true;
    static constexpr bool WILL_ADDR_MOVE = false;

    WBE_TRAIT_REQUIRES(AllocatorTraitConcept);
};

/**
 * @class HeapAllocatorAtomicSizeClassified
 * @brief Atomic heap allocator with one arena allocator per allocation size class.
 *
 * Each size class owns a HeapAllocatorAtomicArenaAlignedPoolImplicitList with its own capacity and arena count, so small,
 * short-lived allocations never fragment the space of large ones, and allocations of one class rotate across that class's
 * arenas to reduce contention. An allocation never spills into another class: when every arena of its class is full,
 * allocate() throws and try_allocate() returns MEM_NULL. A single allocation of a class is limited to one arena of that
 * class. Allocation and deallocation are thread-safe. Neither copyable nor movable.
 */
class HeapAllocatorAtomicSizeClassified final : public HeapAllocatorAligned {
private:
    using ClassAllocator = HeapAllocatorAtomicArenaAlignedPoolImplicitList;

public:
    /**
     * @struct SizeClass
     * @brief The allocation sizes one size class serves, and the capacity and arena count of its allocator.
     */
    struct SizeClass {
        /**
         * @brief The largest allocation size, in bytes, of this class. A class serves the sizes above the previous class's
         * maximum up to and including this one.
         */
        size_t max_allocation_size = 0;
        /**
         * @brief The total capacity of the class's arenas in bytes. Must divide into arena_count arenas whose sizes are
         * nonzero multiples of HEADER_SIZE.
         */
        size_t capacity = 0;
        /**
         * @brief The number of arenas the class's allocations rotate across. Must not be 0.
         */
        size_t arena_count = 1;
    };

    /**
     * @brief The size of the allocated memory header.
     */
    static constexpr size_t HEADER_SIZE = ClassAllocator::HEADER_SIZE;

    /**
     * @brief The maximum number of size classes.
     */
    static constexpr size_t MAX_SIZE_CLASS_COUNT = 8;

    /**
     * @brief The largest allocation size of the default small class: allocations below 1 KiB.
     */
    static constexpr size_t DEFAULT_SMALL_MAX_ALLOCATION_SIZE = WBE_KI_B(1) - 1;

    /**
     * @brief The largest allocation size of the default medium class: allocations from 1 KiB to 1 MiB.
     */
    static constexpr size_t DEFAULT_MEDIUM_MAX_ALLOCATION_SIZE = WBE_MI_B(1);

    /**
     * @brief The default capacity of the small class.
     */
    static constexpr size_t DEFAULT_SMALL_CAPACITY = WBE_MI_B(1);

    /**
     * @brief The default capacity of the medium class.
     */
    static constexpr size_t DEFAULT_MEDIUM_CAPACITY = WBE_MI_B(15);

    /**
     * @brief The default capacity of the large class, which serves allocations above 1 MiB.
     */
    static constexpr size_t DEFAULT_LARGE_CAPACITY = WBE_MI_B(512);

    /**
     * @brief The default arena count of the small class.
     */
    static constexpr size_t DEFAULT_SMALL_ARENA_COUNT = 4;

    /**
     * @brief The default arena count of the medium class.
     */
    static constexpr size_t DEFAULT_MEDIUM_ARENA_COUNT = 4;

    /**
     * @brief The default arena count of the large class. One arena keeps the whole capacity available to one allocation.
     */
    static constexpr size_t DEFAULT_LARGE_ARENA_COUNT = 1;

    /**
     * @brief Construct the default size classes with the given capacities and the default arena counts.
     *
     * @param p_small_capacity The capacity for allocations below 1 KiB.
     * @param p_medium_capacity The capacity for allocations from 1 KiB to 1 MiB.
     * @param p_large_capacity The capacity for allocations above 1 MiB.
     * @return The small, medium, and large size classes.
     */
    static constexpr std::array<SizeClass, 3> make_default_size_classes(
        size_t p_small_capacity = DEFAULT_SMALL_CAPACITY,
        size_t p_medium_capacity = DEFAULT_MEDIUM_CAPACITY,
        size_t p_large_capacity = DEFAULT_LARGE_CAPACITY) {
        return {
            SizeClass{.max_allocation_size = DEFAULT_SMALL_MAX_ALLOCATION_SIZE,
                .capacity = p_small_capacity,
                .arena_count = DEFAULT_SMALL_ARENA_COUNT},
            SizeClass{.max_allocation_size = DEFAULT_MEDIUM_MAX_ALLOCATION_SIZE,
                .capacity = p_medium_capacity,
                .arena_count = DEFAULT_MEDIUM_ARENA_COUNT},
            SizeClass{.max_allocation_size = std::numeric_limits<size_t>::max(),
                .capacity = p_large_capacity,
                .arena_count = DEFAULT_LARGE_ARENA_COUNT},
        };
    }

    /**
     * @brief Construct with the default size classes and capacities.
     */
    HeapAllocatorAtomicSizeClassified() : HeapAllocatorAtomicSizeClassified(make_default_size_classes()) {
    }

    /**
     * @brief Constructor.
     *
     * @param p_size_classes The size classes, ordered by strictly increasing max_allocation_size. The last class must have
     * the maximum size_t as its max_allocation_size, so every allocation size belongs to a class.
     * @throws std::runtime_error If the size classes are invalid.
     */
    explicit HeapAllocatorAtomicSizeClassified(std::span<const SizeClass> p_size_classes);

    virtual ~HeapAllocatorAtomicSizeClassified() override;
    HeapAllocatorAtomicSizeClassified(const HeapAllocatorAtomicSizeClassified&) = delete;
    HeapAllocatorAtomicSizeClassified(HeapAllocatorAtomicSizeClassified&&) = delete;
    HeapAllocatorAtomicSizeClassified& operator=(const HeapAllocatorAtomicSizeClassified&) = delete;
    HeapAllocatorAtomicSizeClassified& operator=(HeapAllocatorAtomicSizeClassified&&) = delete;

    virtual MemID allocate(size_t p_size, size_t p_alignment = HEADER_SIZE) override;
    virtual MemID try_allocate(size_t p_size, size_t p_alignment = HEADER_SIZE) override;

    virtual void deallocate(MemID p_mem) override;

    virtual void* get(MemID p_id) const override {
        if (p_id == MEM_NULL) {
            return nullptr;
        }
        WBE_DEBUG_ASSERT(is_in_pool(p_id));
        return reinterpret_cast<void*>(p_id);
    }

    virtual void* try_get(MemID p_id) const override {
        if (p_id == MEM_NULL) {
            return nullptr;
        }
        return reinterpret_cast<void*>(p_id);
    }

    virtual bool is_empty() const override;

    virtual void clear() override;

    virtual size_t get_allocated_data_size(MemID p_mem_id) const override;

    /**
     * @brief Get the number of size classes.
     *
     * @return The number of size classes.
     */
    size_t get_size_class_count() const {
        return size_class_count;
    }

    /**
     * @brief Get a size class.
     *
     * @param p_size_class_index The index of the size class.
     * @return The size class.
     */
    const SizeClass& get_size_class(size_t p_size_class_index) const;

    /**
     * @brief Get the index of the size class serving an allocation size.
     *
     * @param p_size The allocation size.
     * @return The index of the size class.
     */
    size_t get_size_class_index(size_t p_size) const;

    /**
     * @brief Get the total capacity of all arenas.
     *
     * @return The total capacity of all arenas.
     */
    size_t get_total_size() const {
        return total_size;
    }

    /**
     * @brief Get the total remaining size of all arenas.
     *
     * @return The total remaining size of all arenas.
     */
    size_t get_remain_size() const;

    /**
     * @brief Get the remaining size of one size class.
     *
     * @param p_size_class_index The index of the size class.
     * @return The total remaining size of the class's arenas.
     */
    size_t get_remain_size(size_t p_size_class_index) const;

    /**
     * @brief Get the internal fragmentation tracker for each arena.
     *
     * @return One internal fragmentation tracker value per arena, ordered by size class and then by arena within the class.
     */
    std::vector<size_t> get_internal_fragmentation_tracker() const;

    /**
     * @brief Check if a memory id belongs in any arena.
     *
     * @param p_mem_id The memory ID to check.
     * @return True if it belongs to an arena, false otherwise.
     */
    bool is_in_pool(MemID p_mem_id) const;

    /**
     * @brief Check if each arena is broken. Throws an error if any arena is broken.
     */
    void check_broken() const;

    virtual operator std::string() const override;

private:
    std::array<SizeClass, MAX_SIZE_CLASS_COUNT> size_classes{};
    size_t size_class_count = 0;
    size_t total_size = 0;
    alignas(ClassAllocator) std::byte class_allocator_storage[sizeof(ClassAllocator) * MAX_SIZE_CLASS_COUNT]{};
    size_t constructed_class_allocator_count = 0;

    void create_class_allocators();
    void destroy_class_allocators();
    ClassAllocator& get_class_allocator(size_t p_size_class_index);
    const ClassAllocator& get_class_allocator(size_t p_size_class_index) const;
    size_t get_owning_size_class_index(MemID p_mem_id) const;
};

} // namespace WhiteBirdEngine

#endif
