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
#ifndef WBE_FILE_HEAP_ALLOCATOR_ATOMIC_ARENA_ALIGNED_POOL_IMPL_LIST_HH
#define WBE_FILE_HEAP_ALLOCATOR_ATOMIC_ARENA_ALIGNED_POOL_IMPL_LIST_HH

#include "core/allocator/heap_allocator_aligned.hh"
#include "core/allocator/heap_allocator_atomic_mutex_aligned_pool_impl_list.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/memory_chunk.hh"
#include "utils/defs.hh"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace WhiteBirdEngine {

template <>
struct AllocatorTrait<class HeapAllocatorAtomicArenaAlignedPoolImplicitList> final : public AllocatorTrait<HeapAllocatorAligned> {
    WBE_TRAIT(AllocatorTrait<HeapAllocatorAtomicArenaAlignedPoolImplicitList>);
    static constexpr bool IS_POOL = true;
    static constexpr bool IS_GURANTEED_CONTINUOUS = false;
    static constexpr bool IS_LIMITED_SIZE = true;
    static constexpr bool IS_ALLOC_FIXED_SIZE = false;
    static constexpr bool IS_ATOMIC = true;
    static constexpr bool WILL_ADDR_MOVE = false;

    WBE_TRAIT_REQUIRES(AllocatorTraitConcept);
};

/**
 * @class HeapAllocatorAtomicArenaAlignedPoolImplicitList
 * @brief Atomic heap allocator split into fixed arenas backed by one memory chunk.
 */
class HeapAllocatorAtomicArenaAlignedPoolImplicitList final : public HeapAllocatorAligned {
private:
    using ArenaAllocator = HeapAllocatorAtomicMutexAlignedPoolImplicitList;

public:
    /**
     * @brief The size of the allocated memory header.
     */
    static constexpr size_t HEADER_SIZE = ArenaAllocator::HEADER_SIZE;

    /**
     * @brief The default number of arenas.
     */
    static constexpr size_t DEFAULT_ARENA_COUNT = 4;

    HeapAllocatorAtomicArenaAlignedPoolImplicitList() : HeapAllocatorAtomicArenaAlignedPoolImplicitList(WBE_KI_B(64)) {
    }
    virtual ~HeapAllocatorAtomicArenaAlignedPoolImplicitList() override;
    HeapAllocatorAtomicArenaAlignedPoolImplicitList(const HeapAllocatorAtomicArenaAlignedPoolImplicitList&) = delete;
    HeapAllocatorAtomicArenaAlignedPoolImplicitList(HeapAllocatorAtomicArenaAlignedPoolImplicitList&&) = delete;
    HeapAllocatorAtomicArenaAlignedPoolImplicitList& operator=(const HeapAllocatorAtomicArenaAlignedPoolImplicitList&) = delete;
    HeapAllocatorAtomicArenaAlignedPoolImplicitList& operator=(HeapAllocatorAtomicArenaAlignedPoolImplicitList&&) = delete;

    /**
     * @brief Constructor.
     *
     * @param p_size The total size of all arenas.
     */
    HeapAllocatorAtomicArenaAlignedPoolImplicitList(size_t p_size);

    /**
     * @brief Constructor.
     *
     * @param p_size The total size of all arenas.
     * @param p_arena_count The number of fixed arenas.
     */
    HeapAllocatorAtomicArenaAlignedPoolImplicitList(size_t p_size, size_t p_arena_count);

    virtual MemID allocate(size_t p_size, size_t p_alignment = HEADER_SIZE) override;
    virtual MemID try_allocate(size_t p_size, size_t p_alignment = HEADER_SIZE) override;

    /**
     * @brief Allocate memory from a specific arena.
     *
     * @param p_size The size to allocate.
     * @param p_arena_index The arena index to allocate from.
     * @return The memory ID of the allocated resource.
     */
    MemID allocate_in_arena(size_t p_size, size_t p_arena_index) {
        return allocate_in_arena(p_size, HEADER_SIZE, p_arena_index);
    }

    /**
     * @brief Allocate aligned memory from a specific arena.
     *
     * @param p_size The size to allocate.
     * @param p_alignment Alignment.
     * @param p_arena_index The arena index to allocate from.
     * @return The memory ID of the allocated resource.
     */
    MemID allocate_in_arena(size_t p_size, size_t p_alignment, size_t p_arena_index);

    /**
     * @brief Try to allocate aligned memory from a specific arena.
     *
     * @param p_size The size to allocate.
     * @param p_alignment Alignment.
     * @param p_arena_index The arena index to allocate from.
     * @return The memory ID of the allocated resource, or MEM_NULL if no space is available.
     */
    MemID try_allocate_in_arena(size_t p_size, size_t p_alignment, size_t p_arena_index);

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
     * @brief Get the total size of all arenas.
     *
     * @return The total size of all arenas.
     */
    size_t get_total_size() const {
        return total_size;
    }

    /**
     * @brief Get the number of arenas.
     *
     * @return The number of arenas.
     */
    size_t get_arena_count() const {
        return arena_count;
    }

    /**
     * @brief Get the size of each arena.
     *
     * @return The size of each arena.
     */
    size_t get_arena_size() const {
        return arena_size;
    }

    /**
     * @brief Get the total remaining size of all arenas.
     *
     * @return The total remaining size of all arenas.
     */
    size_t get_remain_size() const;

    /**
     * @brief Get the internal fragmentation tracker for each arena.
     *
     * @return One internal fragmentation tracker value per arena.
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
     * @brief Check if a memory id lies inside this allocator's memory chunk, without locking or validating the allocation.
     *
     * @param p_mem_id The memory ID to check.
     * @return True if p_mem_id lies inside the memory chunk, false otherwise.
     */
    bool is_in_memory_chunk(MemID p_mem_id) const;

    /**
     * @brief Check if each arena is broken. Throws an error if any arena is broken.
     */
    void check_broken() const;

    virtual operator std::string() const override;

private:
    MemoryChunk memory_chunk;
    size_t total_size = 0;
    size_t arena_count = 0;
    size_t arena_size = 0;
    std::byte* arena_storage = nullptr;
    std::atomic_size_t next_arena_index = 0;

    void create_arenas();
    void destroy_arenas();
    ArenaAllocator& get_arena(size_t p_arena_index);
    const ArenaAllocator& get_arena(size_t p_arena_index) const;
    size_t get_arena_index(MemID p_mem_id) const;
};

} // namespace WhiteBirdEngine

#endif
