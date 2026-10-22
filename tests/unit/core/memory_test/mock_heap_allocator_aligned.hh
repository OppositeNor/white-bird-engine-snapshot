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
#ifndef WBE_FILE_MOCK_HEAP_ALLOCATOR_ALIGNED_HH
#define WBE_FILE_MOCK_HEAP_ALLOCATOR_ALIGNED_HH

#include "core/allocator/heap_allocator_aligned.hh"
#include "core/allocator/i_allocator.hh"
#include "utils/defs.hh"
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>

namespace WhiteBirdEngine {

class MockHeapAllocatorAligned final : public HeapAllocatorAligned {
public:
    MockHeapAllocatorAligned(size_t p_max_size = 4096) : max_size(p_max_size) {
    }

    virtual MemID allocate(size_t p_size, size_t p_alignment = WBE_DEFAULT_ALIGNMENT) override {
        call_log << "allocate(" << p_size << ", " << p_alignment << "); ";
        if (p_size == 0 || used_size + p_size > max_size) {
            return MEM_NULL;
        }
        void* ptr = malloc(p_size);
        if (!ptr) {
            return MEM_NULL;
        }
        MemID id = reinterpret_cast<MemID>(ptr);
        allocations[id] = p_size;
        used_size += p_size;
        return id;
    }

    virtual void deallocate(MemID p_mem_id) override {
        call_log << "deallocate(" << p_mem_id << "); ";
        auto it = allocations.find(p_mem_id);
        if (it != allocations.end()) {
            used_size -= it->second;
            free(reinterpret_cast<void*>(p_mem_id));
            allocations.erase(it);
        }
    }

    virtual void* get(MemID p_mem_id) const override {
        call_log << "get(" << p_mem_id << "); ";
        if (allocations.contains(p_mem_id)) {
            return reinterpret_cast<void*>(p_mem_id);
        }
        return nullptr;
    }

    size_t get_remain_size() const {
        call_log << "get_remain_size(); ";
        return max_size - used_size;
    }

    virtual void clear() override {
        call_log << "clear(); ";
        for (auto& kv : allocations) {
            free(reinterpret_cast<void*>(kv.first));
        }
        allocations.clear();
        used_size = 0;
    }

    virtual bool is_empty() const override {
        call_log << "is_empty(); ";
        return allocations.empty();
    }

    virtual size_t get_allocated_data_size(MemID p_mem_id) const override {
        call_log << "get_allocated_data_size(" << p_mem_id << "); ";
        auto it = allocations.find(p_mem_id);
        if (it != allocations.end()) {
            return it->second;
        }
        return 0;
    }

    std::string get_call_log() const {
        return call_log.str();
    }

    void clear_call_log() {
        call_log.str("");
        call_log.clear();
    }

private:
    size_t max_size;
    size_t used_size{0};
    std::unordered_map<MemID, size_t> allocations;
    mutable std::stringstream call_log;
};

} // namespace WhiteBirdEngine

#endif
