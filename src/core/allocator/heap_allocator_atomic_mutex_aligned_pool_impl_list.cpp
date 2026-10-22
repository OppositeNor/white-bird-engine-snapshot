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
#include "core/allocator/heap_allocator_atomic_mutex_aligned_pool_impl_list.hh"
#include "core/allocator/i_allocator.hh"
#include "core/logging/log.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <algorithm>
#include <boost/thread/lock_types.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#define WBE_HAAAPMIL_GET_HEADER_SIZE(p_header) ((p_header) & TOTAL_SIZE_MASK)
#define WBE_HAAAPMIL_GET_CHUNK_SIZE(p_chunk) WBE_HAAAPMIL_GET_HEADER_SIZE(*reinterpret_cast<Header*>(p_chunk))

#define WBE_HAAAPMIL_GET_HEADER_TYPE(p_header) ((HeaderType)(((p_header) & HEADER_TYPE_MASK) >> 60))
#define WBE_HAAAPMIL_GET_CHUNK_TYPE(p_chunk) WBE_HAAAPMIL_GET_HEADER_TYPE(*reinterpret_cast<Header*>(p_chunk))

#define WBE_HAAAPMIL_SET_HEADER(p_header, p_head_type, p_size) (*(p_header) = (((Header)(p_head_type) << 60) | (p_size)))
#define WBE_HAAAPMIL_SET_CHUNK_HEADER(p_chunk, p_type, p_size)                                                              \
    WBE_HAAAPMIL_SET_HEADER(reinterpret_cast<Header*>(p_chunk), (p_type), (p_size))

#define WBE_HAAAPMIL_UPDATE_POSIBLE_VALID(update_to)                                                                                  \
    /*If possible_valid did not contain a data, could be updated.*/                                                                   \
    if (possible_valid == nullptr /*If possible_valid was occupied, could be updated.*/                                               \
        || WBE_HAAAPMIL_GET_CHUNK_TYPE(possible_valid) ==                                                                             \
               HeaderType::OCCUPIED /*Try to make it as compat as possible, so if the updated is at the front of */ /*possible_valid, \
                                                                                                                       also update                                                                                                                               \
                                                                                                                       possible valid                                                                                                                                \
                                                                                                                       to that                                                                                                                                 \
                                                                                                                       place..*/      \
        || (update_to) < possible_valid) {                                                                                            \
        possible_valid = (update_to);                                                                                                 \
    } else                                                                                                                            \
        (void(0))

namespace WhiteBirdEngine {

HeapAllocatorAtomicMutexAlignedPoolImplicitList::HeapAllocatorAtomicMutexAlignedPoolImplicitList(size_t p_size)
    : HeapAllocatorAtomicMutexAlignedPoolImplicitList(MemoryChunk(p_size, HEADER_SIZE), 0, p_size) {
}

HeapAllocatorAtomicMutexAlignedPoolImplicitList::HeapAllocatorAtomicMutexAlignedPoolImplicitList(
    MemoryChunk p_memory_chunk, size_t p_start_offset, size_t p_size)
    : memory_chunk(std::move(p_memory_chunk)), size(p_size) {
    if (p_size > TOTAL_SIZE_MASK) {
        throw std::runtime_error("Failed to create pool: size: " + std::to_string(p_size) +
                                 " exceeds maximum: " + std::to_string(TOTAL_SIZE_MASK) + ".");
    }
    mem_chunk = memory_chunk.get_occupied_start(p_start_offset, p_size);
    memset(mem_chunk, 0, p_size);
    WBE_HAAAPMIL_SET_CHUNK_HEADER(mem_chunk, HeaderType::IDLE, size);
    possible_valid = mem_chunk;
}

HeapAllocatorAtomicMutexAlignedPoolImplicitList::~HeapAllocatorAtomicMutexAlignedPoolImplicitList() {
    if (!is_empty()) {
        stdout_log(WBE_CHANNEL_GLOBAL)->warning("Non-empty allocator destructed. Allocator status: " + static_cast<std::string>(*this));
    }
    mem_chunk = nullptr;
}

MemID HeapAllocatorAtomicMutexAlignedPoolImplicitList::allocate(size_t p_size, size_t p_alignment) {
    MemID result = try_allocate(p_size, p_alignment);
    if (result != MEM_NULL || p_size == 0) {
        return result;
    }
    std::string err_msg = "Failed to allocate memory: not enough space for memory pool.\n"
                          "Trying to allocate: " +
                          std::to_string(get_align_size(p_size, HEADER_SIZE) + HEADER_SIZE) +
                          " bytes.\n"
                          "Pool status: " +
                          static_cast<std::string>(*this);
    throw std::runtime_error(err_msg);
}

MemID HeapAllocatorAtomicMutexAlignedPoolImplicitList::try_allocate(size_t p_size, size_t p_alignment) {
    WBE_DEBUG_ASSERT(!(mutex.is_unique_locked_by_current_thread()));
    if (p_alignment == 0) {
        throw std::runtime_error("Allocation alignment must not be 0.");
    }
    if (p_size == 0) {
        return MEM_NULL;
    }
    if (p_alignment % HEADER_SIZE) {
        throw std::runtime_error(std::format("Allocation alignment must be a multiple of {}.", HEADER_SIZE));
    }
    // Clamp the padding size to the default alignment.
    size_t aligned_size = get_align_size(p_size, HEADER_SIZE) + HEADER_SIZE;
    boost::unique_lock lock(mutex);
    MemID result = find_valid_chunk<false>(aligned_size, p_alignment);
    if (result == MEM_NULL) {
        result = find_valid_chunk<true>(aligned_size, p_alignment);
    }
    return result;
}

template <bool CoalesceEnabled>
MemID HeapAllocatorAtomicMutexAlignedPoolImplicitList::check_posible_free(size_t p_aligned_size, size_t p_alignment) {
    WBE_DEBUG_ASSERT(mutex.is_unique_locked_by_current_thread());
    if constexpr (CoalesceEnabled) {
        if (possible_valid != nullptr) {
            coalesce_chunk(possible_valid);
        }
    }
    WBE_DEBUG_ASSERT(possible_valid == nullptr || WBE_HAAAPMIL_GET_CHUNK_TYPE(possible_valid) == HeaderType::IDLE);
    if (possible_valid == nullptr) {
        return MEM_NULL;
    }
    uintptr_t proxy_mem_start_addr = reinterpret_cast<uintptr_t>(possible_valid) + HEADER_SIZE;
    char* idle_mem_start = reinterpret_cast<char*>(proxy_mem_start_addr % p_alignment == 0
                                                       ?
                                                       // Already aligned.
                                                       proxy_mem_start_addr
                                                       // Not aligned, align by finding the next value that is a
                                                       // multiple of p_alignment.
                                                       : (proxy_mem_start_addr / p_alignment + 1) * p_alignment) -
                           HEADER_SIZE;
    // If idle node valid, insert.
    if (idle_mem_start + p_aligned_size <= possible_valid + WBE_HAAAPMIL_GET_CHUNK_SIZE(possible_valid)) {
        void* result_loc = acquire_memory(possible_valid, idle_mem_start, p_aligned_size);
        MemID result_id = reinterpret_cast<MemID>(result_loc) + HEADER_SIZE;
        *static_cast<Header*>(result_loc) = p_aligned_size;
        internal_fragmentation_tracker = std::max(internal_fragmentation_tracker,
            reinterpret_cast<uintptr_t>(result_loc) + p_aligned_size - reinterpret_cast<size_t>(mem_chunk));
        return result_id;
    }
    return MEM_NULL;
}

template <bool CoalesceEnabled>
MemID HeapAllocatorAtomicMutexAlignedPoolImplicitList::find_valid_chunk(size_t p_aligned_size, size_t p_alignment) {
    WBE_DEBUG_ASSERT(mutex.is_unique_locked_by_current_thread());
    MemID possible_result = check_posible_free<CoalesceEnabled>(p_aligned_size, p_alignment);
    if (possible_result != MEM_NULL) {
        return possible_result;
    }
    char* free_memory = get_next_free_memory<true, CoalesceEnabled>(mem_chunk);
    while (free_memory != nullptr) {
        // Find the aligned starting point.
        uintptr_t proxy_mem_start_addr = reinterpret_cast<uintptr_t>(free_memory) + HEADER_SIZE;
        char* idle_mem_start = reinterpret_cast<char*>((proxy_mem_start_addr / p_alignment) * p_alignment == proxy_mem_start_addr
                                                           ? proxy_mem_start_addr
                                                           : (proxy_mem_start_addr / p_alignment + 1) * p_alignment) -
                               HEADER_SIZE;
        // If idle node valid, insert.
        if (idle_mem_start + p_aligned_size <= free_memory + WBE_HAAAPMIL_GET_CHUNK_SIZE(free_memory)) {
            void* result_loc = acquire_memory(free_memory, idle_mem_start, p_aligned_size);
            MemID result_id = reinterpret_cast<MemID>(result_loc) + HEADER_SIZE;
            *static_cast<Header*>(result_loc) = p_aligned_size;
            internal_fragmentation_tracker = std::max(internal_fragmentation_tracker,
                reinterpret_cast<uintptr_t>(result_loc) + p_aligned_size - reinterpret_cast<size_t>(mem_chunk));
            return result_id;
        }
        free_memory = get_next_free_memory<false, CoalesceEnabled>(free_memory);
    }
    // If not found, return null.
    return MEM_NULL;
}

void HeapAllocatorAtomicMutexAlignedPoolImplicitList::deallocate(MemID p_mem) {
    WBE_DEBUG_ASSERT(!(mutex.is_unique_locked_by_current_thread()));
    if (p_mem == MEM_NULL) {
        return;
    }
    char* data_loc = reinterpret_cast<char*>(p_mem - HEADER_SIZE);
    boost::unique_lock lock(mutex);
    WBE_DEBUG_ASSERT(unguarded_is_in_pool(p_mem));
    size_t data_size = WBE_HAAAPMIL_GET_HEADER_SIZE(*reinterpret_cast<Header*>((p_mem - HEADER_SIZE)));
    insert_free_memory(data_loc, data_size);
}

template <bool CheckFirst, bool CoalesceEnabled>
char* HeapAllocatorAtomicMutexAlignedPoolImplicitList::get_next_free_memory(char* p_from) {
    WBE_DEBUG_ASSERT(mutex.is_unique_locked_by_current_thread());
    if constexpr (CheckFirst) {
        while (WBE_HAAAPMIL_GET_CHUNK_TYPE(p_from) != HeaderType::IDLE) {
            p_from += WBE_HAAAPMIL_GET_CHUNK_SIZE(p_from);
            if (p_from >= mem_chunk + size) {
                return nullptr;
            }
        }
    } else {
        do {
            p_from += WBE_HAAAPMIL_GET_CHUNK_SIZE(p_from);
            if (p_from >= mem_chunk + size) {
                return nullptr;
            }
        } while (WBE_HAAAPMIL_GET_CHUNK_TYPE(p_from) != HeaderType::IDLE);
    }
    if constexpr (CoalesceEnabled) {
        coalesce_chunk(p_from);
    }
    return p_from;
}

void* HeapAllocatorAtomicMutexAlignedPoolImplicitList::acquire_memory(char* p_idle_chunk, char* p_mem_start, size_t p_mem_size) {
    WBE_DEBUG_ASSERT(mutex.is_unique_locked_by_current_thread());
    WBE_DEBUG_ASSERT(p_mem_start >= mem_chunk && p_mem_start + p_mem_size <= p_idle_chunk + WBE_HAAAPMIL_GET_CHUNK_SIZE(p_idle_chunk));
    WBE_DEBUG_ASSERT(WBE_HAAAPMIL_GET_CHUNK_TYPE(p_idle_chunk) == HeaderType::IDLE);
    size_t idle_chunk_size = WBE_HAAAPMIL_GET_CHUNK_SIZE(p_idle_chunk);
    size_t idle_before_size = p_mem_start - p_idle_chunk;
    // Get the inserted memory.
    WBE_HAAAPMIL_SET_CHUNK_HEADER(p_mem_start, HeaderType::OCCUPIED, p_mem_size);
    char* next_chunk = p_idle_chunk + p_mem_size;
    // Insert the idle memory before the acquired memory chunk.
    if (p_idle_chunk != p_mem_start) {
        WBE_HAAAPMIL_SET_CHUNK_HEADER(p_idle_chunk, HeaderType::IDLE, idle_before_size);
        WBE_HAAAPMIL_UPDATE_POSIBLE_VALID(p_idle_chunk);
    }
    // Insert the idle memory after the acquired memory chunk.
    if (p_mem_start + p_mem_size < p_idle_chunk + idle_chunk_size) {
        // The memory after the acquired should be size: idle_chunk_size -
        // idle_size_before - inserted_memory_size
        WBE_HAAAPMIL_SET_CHUNK_HEADER(p_mem_start + p_mem_size, HeaderType::IDLE, idle_chunk_size - idle_before_size - p_mem_size);
        WBE_HAAAPMIL_UPDATE_POSIBLE_VALID(p_mem_start + p_mem_size);
    }
    // If this acquire uses up the possible_valid,
    if (possible_valid == nullptr || WBE_HAAAPMIL_GET_CHUNK_TYPE(possible_valid) == HeaderType::OCCUPIED) {
        if (next_chunk < mem_chunk + size && WBE_HAAAPMIL_GET_CHUNK_TYPE(next_chunk) == HeaderType::IDLE) {
            possible_valid = next_chunk;
        } else {
            possible_valid = nullptr;
        }
    }
    return p_mem_start;
}

void HeapAllocatorAtomicMutexAlignedPoolImplicitList::insert_free_memory(char* p_insert_start, size_t p_insert_size) {
    WBE_DEBUG_ASSERT(mutex.is_unique_locked_by_current_thread());
    WBE_HAAAPMIL_SET_CHUNK_HEADER(p_insert_start, HeaderType::IDLE, p_insert_size);
    coalesce_chunk(p_insert_start);
}

size_t HeapAllocatorAtomicMutexAlignedPoolImplicitList::get_remain_size() const {
    boost::unique_lock lock(mutex);
    return unguarded_get_remain_size();
}

size_t HeapAllocatorAtomicMutexAlignedPoolImplicitList::unguarded_get_remain_size() const {
    char* curr = mem_chunk;
    size_t total = 0;
    while (curr < mem_chunk + size) {
        if (WBE_HAAAPMIL_GET_CHUNK_TYPE(curr) != HeaderType::OCCUPIED) {
            total += WBE_HAAAPMIL_GET_CHUNK_SIZE(curr);
        }
        curr += WBE_HAAAPMIL_GET_CHUNK_SIZE(curr);
    }
    if (curr != mem_chunk + size) {
        throw std::runtime_error("Bad pool.");
    }
    return total;
}

bool HeapAllocatorAtomicMutexAlignedPoolImplicitList::is_in_pool(MemID p_mem_id) const {
    boost::unique_lock lock(mutex);
    return unguarded_is_in_pool(p_mem_id);
}

void HeapAllocatorAtomicMutexAlignedPoolImplicitList::unguarded_check_broken() const {
    char* curr = mem_chunk;
    if (possible_valid != nullptr && WBE_HAAAPMIL_GET_CHUNK_TYPE(possible_valid) != HeaderType::IDLE) {
        throw std::runtime_error("Broken possible_valid.");
    }
    while (curr < mem_chunk + size) {
        if (WBE_HAAAPMIL_GET_CHUNK_SIZE(curr) == 0) {
            throw std::runtime_error("Size is 0.");
        }
        curr += WBE_HAAAPMIL_GET_CHUNK_SIZE(curr);
    }
    if (curr != mem_chunk + size) {
        throw std::runtime_error("Bad pool.");
    }
}

void HeapAllocatorAtomicMutexAlignedPoolImplicitList::coalesce_all() const {
    WBE_DEBUG_ASSERT(mutex.is_unique_locked_by_current_thread());
    char* curr = mem_chunk;
    do {
        coalesce_chunk(curr);
        curr += WBE_HAAAPMIL_GET_CHUNK_SIZE(curr);
    } while (curr < mem_chunk + size);
}

void HeapAllocatorAtomicMutexAlignedPoolImplicitList::coalesce_chunk(char* p_chunk) const {
    WBE_DEBUG_ASSERT(mutex.is_unique_locked_by_current_thread());
    if (WBE_HAAAPMIL_GET_CHUNK_TYPE(p_chunk) != HeaderType::IDLE) {
        return;
    }
    // Coalesce is not really changing the internal data. So here it allows to
    // be called in a const function.
    size_t chunk_size = WBE_HAAAPMIL_GET_CHUNK_SIZE(p_chunk);
    while (p_chunk + chunk_size < mem_chunk + size && WBE_HAAAPMIL_GET_CHUNK_TYPE(p_chunk + chunk_size) == HeaderType::IDLE) {
        WBE_HAAAPMIL_SET_CHUNK_HEADER(p_chunk, HeaderType::IDLE, chunk_size + WBE_HAAAPMIL_GET_CHUNK_SIZE(p_chunk + chunk_size));
        chunk_size = WBE_HAAAPMIL_GET_CHUNK_SIZE(p_chunk);
    }
    // Possible free might be merged with another chunk. So, we have to update
    // it to the coalesced chunk.
    WBE_HAAAPMIL_UPDATE_POSIBLE_VALID(p_chunk);
}

HeapAllocatorAtomicMutexAlignedPoolImplicitList::operator std::string() const {
    boost::unique_lock lock(mutex);
    return unguarded_to_string();
}

bool HeapAllocatorAtomicMutexAlignedPoolImplicitList::unguarded_is_in_pool(MemID p_mem_id) const {
    char* curr = mem_chunk;
    char* mem_ptr = reinterpret_cast<char*>(p_mem_id);
    while (curr <= mem_ptr && curr < mem_chunk + size - HEADER_SIZE) {
        if (curr + HEADER_SIZE == mem_ptr && WBE_HAAAPMIL_GET_CHUNK_TYPE(curr) == HeaderType::OCCUPIED) {
            return true;
        }
        curr += WBE_HAAAPMIL_GET_CHUNK_SIZE(curr);
    }
    return false;
}

std::string HeapAllocatorAtomicMutexAlignedPoolImplicitList::unguarded_to_string() const {
    std::stringstream ss;
    ss << "{";
    ss << R"("type":"HeapAllocatorAtomicMutexAlignedPoolImplicitList",)";
    ss << "\"total_size\":" << size << ",";
    ss << "\"chunk_layout\":[";
    char* curr = mem_chunk;
    bool first = true;
    uint64_t fragments_size = 0;
    while (curr < mem_chunk + size) {
        if (!first) {
            ss << ",";
        }
        first = false;
        bool occupied = WBE_HAAAPMIL_GET_CHUNK_TYPE(curr) == HeaderType::OCCUPIED;
        ss << "{"
           << "\"occupied\":" << std::to_string(occupied) << ","
           << "\"begin\":" << (curr - mem_chunk) << ","
           << "\"size\":" << WBE_HAAAPMIL_GET_CHUNK_SIZE(curr) << "}";
        curr += WBE_HAAAPMIL_GET_CHUNK_SIZE(curr);
        if (curr < mem_chunk + size && !occupied) {
            fragments_size += size;
        }
    }
    if (curr != mem_chunk + size) {
        throw std::runtime_error("Bad pool.");
    }
    ss << "],";
    ss << "\"fragments_size:\"" << fragments_size;
    ss << "}";
    return ss.str();
}
} // namespace WhiteBirdEngine
