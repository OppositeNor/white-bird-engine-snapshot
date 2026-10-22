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
#include "core/allocator/heap_allocator_atomic_shared_mutex_aligned_pool.hh"
#include "core/allocator/i_allocator.hh"
#include "core/logging/log.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <algorithm>
#include <boost/thread/lock_types.hpp>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#define WBE_GET_CHUNK_SIZE(p_chunk) ((*reinterpret_cast<Header*>(p_chunk)) & MAX_TOTAL_SIZE)
#define WBE_GET_ALLOCATED_DATA_SIZE(p_mem_id) ((*reinterpret_cast<Header*>((p_mem_id) - HEADER_SIZE)) & MAX_TOTAL_SIZE)

namespace WhiteBirdEngine {

HeapAllocatorAtomicSharedMutexAlignedPool::HeapAllocatorAtomicSharedMutexAlignedPool(size_t p_size)
    : HeapAllocatorAtomicSharedMutexAlignedPool(MemoryChunk(p_size, HEADER_SIZE), 0, p_size) {
}

HeapAllocatorAtomicSharedMutexAlignedPool::HeapAllocatorAtomicSharedMutexAlignedPool(
    MemoryChunk p_memory_chunk, size_t p_start_offset, size_t p_size)
    : memory_chunk(std::move(p_memory_chunk)), size(p_size) {
    if (p_size > MAX_TOTAL_SIZE) {
        throw std::runtime_error("Failed to create pool: size: " + std::to_string(p_size) +
                                 " exceeds maximum: " + std::to_string(MAX_TOTAL_SIZE) + ".");
    }
    mem_chunk = memory_chunk.get_occupied_start(p_start_offset, p_size);
    idle_list_head = std::make_unique<IdleListNode>();
    idle_list_head->size = p_size;
    idle_list_head->next = nullptr;
    idle_list_head->mem_start = mem_chunk;
    idle_chunks_count = 1;
}

HeapAllocatorAtomicSharedMutexAlignedPool::~HeapAllocatorAtomicSharedMutexAlignedPool() {
    if (!is_empty_unguarded()) {
        stdout_log(WBE_CHANNEL_GLOBAL)->warning("Non-empty allocator destructed.");
    }
    mem_chunk = nullptr;
}

MemID HeapAllocatorAtomicSharedMutexAlignedPool::allocate(size_t p_size, size_t p_alignment) {
    if (p_alignment == 0) {
        throw std::runtime_error("Failed to allocate resource: allocation alignment must not be 0.");
    }
    if (p_alignment % alignof(Header) != 0) {
        throw std::runtime_error(std::format("Allocation alignment must be a multiple of {}", alignof(Header)));
    }
    if (p_size == 0) {
        return MEM_NULL;
    }
    // Share lock this when finding valid space for allocation, then unique lock
    // when found.
    boost::upgrade_lock lock(mutex);
    std::unique_ptr<IdleListNode>* valid_idle_node = &idle_list_head;
    // Clamp the padding size to the default alignment.
    size_t aligned_size = get_align_size(p_size, WBE_DEFAULT_ALIGNMENT) + HEADER_SIZE;
    while (*valid_idle_node != nullptr) {
        // Find the aligned starting point.
        uintptr_t proxy_mem_start_addr = reinterpret_cast<uintptr_t>((*valid_idle_node)->mem_start) + HEADER_SIZE;
        char* idle_node_mem_start = reinterpret_cast<char*>((proxy_mem_start_addr / p_alignment) * p_alignment == proxy_mem_start_addr
                                                                ? proxy_mem_start_addr
                                                                : (proxy_mem_start_addr / p_alignment + 1) * p_alignment) -
                                    HEADER_SIZE;
        // If idle node valid, insert.
        if (idle_node_mem_start + aligned_size <= (*valid_idle_node)->mem_start + (*valid_idle_node)->size) {
            // Upgrade the shared lock to unique lock.
            boost::upgrade_to_unique_lock unique_lock(lock);
            void* result_loc = acquire_memory(*valid_idle_node, idle_node_mem_start, aligned_size);
            MemID result_id = reinterpret_cast<MemID>(result_loc) + HEADER_SIZE;
            *static_cast<Header*>(result_loc) = aligned_size;
            internal_fragmentation_tracker = std::max(internal_fragmentation_tracker,
                reinterpret_cast<uintptr_t>(result_loc) + aligned_size - reinterpret_cast<uintptr_t>(mem_chunk));
            return result_id;
        }
        valid_idle_node = &((*valid_idle_node)->next);
    }
    std::string err_msg = "Failed to allocate memory: not enough space for memory pool.\n"
                          "Trying to allocate: " +
                          std::to_string(aligned_size) +
                          " bytes.\n"
                          "Pool status: " +
                          static_cast<std::string>(*this);
    throw std::runtime_error(err_msg);
}

void HeapAllocatorAtomicSharedMutexAlignedPool::deallocate(MemID p_mem) {
    // Share lock this when finding valid space for allocation, then unique lock
    // when found.
    boost::upgrade_lock lock(mutex);
    WBE_DEBUG_ASSERT(unguard_is_in_pool(p_mem));
    // The first 64 bits are used to store the header.
    char* data_loc = reinterpret_cast<char*>(p_mem - HEADER_SIZE);
    size_t data_size = WBE_GET_ALLOCATED_DATA_SIZE(p_mem);
    if (idle_list_head == nullptr || idle_list_head->mem_start > data_loc) {
        // Unique lock when insert free memory.
        boost::upgrade_to_unique_lock unique_lock(lock);
        insert_free_memory(nullptr, data_loc, data_size);
        return;
    }
    IdleListNode* insert_pos = get_idle_node_before(data_loc).get();
    // Unique lock when insert free memory: shared readers must not walk the list while nodes are merged and freed.
    boost::upgrade_to_unique_lock unique_lock(lock);
    insert_free_memory(insert_pos, data_loc, data_size);
}

void* HeapAllocatorAtomicSharedMutexAlignedPool::acquire_memory(std::unique_ptr<IdleListNode>& p_node, char* p_mem_start, size_t p_mem_size) {
    WBE_DEBUG_ASSERT(p_mem_start >= p_node->mem_start);
    WBE_DEBUG_ASSERT(p_mem_start + p_mem_size <= p_node->mem_start + p_node->size);
    if (p_node->mem_start == p_mem_start) {
        p_node->size -= p_mem_size;
        void* node_mem_start = p_node->mem_start;
        if (p_node->size == 0) {
            --idle_chunks_count;
            if (p_node->next == nullptr) {
                p_node = nullptr;
                return node_mem_start;
            }
            p_node->size = p_node->next->size;
            p_node->mem_start = p_node->next->mem_start;
            p_node->next = std::move(p_node->next->next);
        } else {
            p_node->mem_start += p_mem_size;
        }
        return node_mem_start;
    }
    size_t node_size_diff = p_mem_start - p_node->mem_start;
    if (p_mem_start + p_mem_size != p_node->mem_start + p_node->size) {
        std::unique_ptr<IdleListNode> node_next = std::make_unique<IdleListNode>();
        node_next->mem_start = p_mem_start + p_mem_size;
        node_next->size = p_node->size - node_size_diff - p_mem_size;
        node_next->next = std::move(p_node->next);
        p_node->next = std::move(node_next);
    }
    p_node->size = node_size_diff;
    return p_mem_start;
}

void HeapAllocatorAtomicSharedMutexAlignedPool::insert_free_memory(
    IdleListNode* p_node_before_insert, char* p_insert_start, size_t p_insert_size) {
    if (idle_list_head == nullptr) {
        idle_list_head = std::make_unique<IdleListNode>();
        ++idle_chunks_count;
        idle_list_head->mem_start = p_insert_start;
        idle_list_head->size = p_insert_size;
        return;
    }

    std::unique_ptr<IdleListNode> insert_node = std::make_unique<IdleListNode>();
    ++idle_chunks_count;
    insert_node->mem_start = p_insert_start;
    insert_node->size = p_insert_size;
    if (p_node_before_insert == nullptr) {
        insert_node->next = std::move(idle_list_head);
        idle_list_head = std::move(insert_node);
        combine_idle_with_next(idle_list_head.get());
        return;
    }

    insert_node->next = std::move(p_node_before_insert->next);
    p_node_before_insert->next = std::move(insert_node);
    if (combine_idle_with_next(p_node_before_insert)) {
        combine_idle_with_next(p_node_before_insert);
    } else if (p_node_before_insert->next != nullptr) {
        combine_idle_with_next(p_node_before_insert->next.get());
    }
}

bool HeapAllocatorAtomicSharedMutexAlignedPool::combine_idle_with_next(IdleListNode* p_node) {
    WBE_DEBUG_ASSERT(p_node != nullptr);
    if (p_node->next == nullptr || p_node->mem_start + p_node->size != p_node->next->mem_start) {
        return false;
    }
    p_node->size += p_node->next->size;
    p_node->next = std::move(p_node->next->next);
    --idle_chunks_count;
    return true;
}

std::unique_ptr<HeapAllocatorAtomicSharedMutexAlignedPool::IdleListNode>& HeapAllocatorAtomicSharedMutexAlignedPool::get_idle_node_before(
    void* p_loc) {
    std::unique_ptr<IdleListNode>* curr = &idle_list_head;
    while (curr != nullptr) {
        if ((*curr)->next == nullptr || (*curr)->next->mem_start > p_loc) {
            return *curr;
        }
        curr = &((*curr)->next);
    }
    throw std::runtime_error("Unreachable code.");
}

size_t HeapAllocatorAtomicSharedMutexAlignedPool::get_remain_size() const {
    boost::shared_lock lock(mutex);
    IdleListNode* node = idle_list_head.get();
    size_t total = 0;
    while (node != nullptr) {
        total += node->size;
        node = node->next.get();
    }
    return total;
}

bool HeapAllocatorAtomicSharedMutexAlignedPool::unguard_is_in_pool(MemID p_mem_id) const {
    const IdleListNode* curr = idle_list_head.get();
    char* tracker = mem_chunk;
    while (tracker < mem_chunk + size) {
        if (reinterpret_cast<MemID>(tracker) > p_mem_id) {
            return false;
        }
        if (curr != nullptr && curr->mem_start == tracker) {
            tracker += curr->size;
            curr = curr->next.get();
        } else if (p_mem_id == reinterpret_cast<MemID>(tracker) + HEADER_SIZE) {
            return true;
        } else {
            size_t chunk_size = WBE_GET_CHUNK_SIZE(tracker);
            WBE_DEBUG_ASSERT(chunk_size != 0);
            tracker += chunk_size;
        }
    }
    return false;
}

HeapAllocatorAtomicSharedMutexAlignedPool::operator std::string() const {
    boost::shared_lock lock(mutex);
    std::stringstream ss;
    ss << "{";
    ss << R"("type":"HeapAllocatorAtomicSharedMutexAlignedPool",)";
    ss << R"("total_size":)" << get_total_size() << ",";
    ss << R"("free_chunk_layout":[)";
    IdleListNode* node = idle_list_head.get();
    bool first = true;
    while (node != nullptr) {
        if (!first) {
            ss << ",";
        }
        first = false;
        ss << "{"
           << R"("begin:")" << (node->mem_start - mem_chunk) << ","
           << R"("size:")" << node->size << "}";
        node = node->next.get();
    }
    ss << "]";
    ss << "}";
    return ss.str();
}

} // namespace WhiteBirdEngine
