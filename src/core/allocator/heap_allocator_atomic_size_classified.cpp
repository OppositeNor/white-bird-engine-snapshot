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
#include "core/allocator/heap_allocator_atomic_size_classified.hh"
#include "core/allocator/heap_allocator_atomic_arena_aligned_pool_impl_list.hh"
#include "core/allocator/i_allocator.hh"
#include <cstddef>
#include <format>
#include <limits>
#include <new>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace WhiteBirdEngine {

HeapAllocatorAtomicSizeClassified::HeapAllocatorAtomicSizeClassified(std::span<const SizeClass> p_size_classes)
    : size_class_count(p_size_classes.size()) {
    if (size_class_count == 0) {
        throw std::runtime_error("Size-classified allocator requires at least one size class.");
    }
    if (size_class_count > MAX_SIZE_CLASS_COUNT) {
        throw std::runtime_error(std::format(
            "Size-classified allocator supports at most {} size classes, but {} were given.", MAX_SIZE_CLASS_COUNT, size_class_count));
    }
    for (size_t i = 0; i < size_class_count; ++i) {
        const SizeClass& size_class = p_size_classes[i];
        if (i > 0 && size_class.max_allocation_size <= p_size_classes[i - 1].max_allocation_size) {
            throw std::runtime_error(std::format("Size class {} must have a larger max allocation size than size class {}.", i, i - 1));
        }
        if (size_class.capacity == 0) {
            throw std::runtime_error(std::format("Size class {} capacity must not be 0.", i));
        }
        if (size_class.arena_count == 0) {
            throw std::runtime_error(std::format("Size class {} arena count must not be 0.", i));
        }
        if (size_class.capacity % size_class.arena_count != 0) {
            throw std::runtime_error(std::format(
                "Size class {} capacity {} must be divisible by its arena count {}.", i, size_class.capacity, size_class.arena_count));
        }
        if ((size_class.capacity / size_class.arena_count) % HEADER_SIZE != 0) {
            throw std::runtime_error(std::format("Size class {} arena size {} must be a multiple of {}.",
                i,
                size_class.capacity / size_class.arena_count,
                HEADER_SIZE));
        }
        if (size_class.capacity > std::numeric_limits<size_t>::max() - total_size) {
            throw std::runtime_error("Size-classified allocator total capacity exceeds the address space.");
        }
        size_classes[i] = size_class;
        total_size += size_class.capacity;
    }
    if (size_classes[size_class_count - 1].max_allocation_size != std::numeric_limits<size_t>::max()) {
        throw std::runtime_error("The last size class must serve every allocation size up to the maximum size_t.");
    }

    create_class_allocators();
}

HeapAllocatorAtomicSizeClassified::~HeapAllocatorAtomicSizeClassified() {
    destroy_class_allocators();
}

MemID HeapAllocatorAtomicSizeClassified::allocate(size_t p_size, size_t p_alignment) {
    MemID result = try_allocate(p_size, p_alignment);
    if (result != MEM_NULL || p_size == 0) {
        return result;
    }
    throw std::runtime_error(std::format("Failed to allocate memory: not enough space in size class {}.\nTrying to allocate: {} bytes.\nPool status: {}",
        get_size_class_index(p_size),
        p_size,
        static_cast<std::string>(*this)));
}

MemID HeapAllocatorAtomicSizeClassified::try_allocate(size_t p_size, size_t p_alignment) {
    return get_class_allocator(get_size_class_index(p_size)).try_allocate(p_size, p_alignment);
}

void HeapAllocatorAtomicSizeClassified::deallocate(MemID p_mem) {
    if (p_mem == MEM_NULL) {
        return;
    }
    get_class_allocator(get_owning_size_class_index(p_mem)).deallocate(p_mem);
}

bool HeapAllocatorAtomicSizeClassified::is_empty() const {
    for (size_t i = 0; i < size_class_count; ++i) {
        if (!get_class_allocator(i).is_empty()) {
            return false;
        }
    }
    return true;
}

void HeapAllocatorAtomicSizeClassified::clear() {
    for (size_t i = 0; i < size_class_count; ++i) {
        get_class_allocator(i).clear();
    }
}

size_t HeapAllocatorAtomicSizeClassified::get_allocated_data_size(MemID p_mem_id) const {
    return get_class_allocator(get_owning_size_class_index(p_mem_id)).get_allocated_data_size(p_mem_id);
}

const HeapAllocatorAtomicSizeClassified::SizeClass& HeapAllocatorAtomicSizeClassified::get_size_class(size_t p_size_class_index) const {
    if (p_size_class_index >= size_class_count) {
        throw std::runtime_error(std::format("Size class index {} exceeds size class count {}.", p_size_class_index, size_class_count));
    }
    return size_classes[p_size_class_index];
}

size_t HeapAllocatorAtomicSizeClassified::get_size_class_index(size_t p_size) const {
    // The last class serves every size, so the search always finds a class.
    size_t index = 0;
    while (p_size > size_classes[index].max_allocation_size) {
        ++index;
    }
    WBE_DEBUG_ASSERT(index < size_class_count);
    return index;
}

size_t HeapAllocatorAtomicSizeClassified::get_remain_size() const {
    size_t remain_size = 0;
    for (size_t i = 0; i < size_class_count; ++i) {
        remain_size += get_class_allocator(i).get_remain_size();
    }
    return remain_size;
}

size_t HeapAllocatorAtomicSizeClassified::get_remain_size(size_t p_size_class_index) const {
    if (p_size_class_index >= size_class_count) {
        throw std::runtime_error(std::format("Size class index {} exceeds size class count {}.", p_size_class_index, size_class_count));
    }
    return get_class_allocator(p_size_class_index).get_remain_size();
}

std::vector<size_t> HeapAllocatorAtomicSizeClassified::get_internal_fragmentation_tracker() const {
    std::vector<size_t> result;
    for (size_t i = 0; i < size_class_count; ++i) {
        std::vector<size_t> class_trackers = get_class_allocator(i).get_internal_fragmentation_tracker();
        result.insert(result.end(), class_trackers.begin(), class_trackers.end());
    }
    return result;
}

bool HeapAllocatorAtomicSizeClassified::is_in_pool(MemID p_mem_id) const {
    for (size_t i = 0; i < size_class_count; ++i) {
        if (get_class_allocator(i).is_in_memory_chunk(p_mem_id)) {
            return get_class_allocator(i).is_in_pool(p_mem_id);
        }
    }
    return false;
}

void HeapAllocatorAtomicSizeClassified::check_broken() const {
    for (size_t i = 0; i < size_class_count; ++i) {
        get_class_allocator(i).check_broken();
    }
}

HeapAllocatorAtomicSizeClassified::operator std::string() const {
    std::stringstream ss;
    ss << "{";
    ss << R"("type":"HeapAllocatorAtomicSizeClassified",)";
    ss << "\"total_size\":" << total_size << ",";
    ss << "\"size_classes\":[";
    for (size_t i = 0; i < size_class_count; ++i) {
        if (i != 0) {
            ss << ",";
        }
        ss << "{\"max_allocation_size\":" << size_classes[i].max_allocation_size << ",";
        ss << "\"allocator\":" << static_cast<std::string>(get_class_allocator(i)) << "}";
    }
    ss << "]";
    ss << "}";
    return ss.str();
}

void HeapAllocatorAtomicSizeClassified::create_class_allocators() {
    try {
        for (; constructed_class_allocator_count < size_class_count; ++constructed_class_allocator_count) {
            const SizeClass& size_class = size_classes[constructed_class_allocator_count];
            new (&get_class_allocator(constructed_class_allocator_count)) ClassAllocator(size_class.capacity, size_class.arena_count);
        }
    } catch (...) {
        destroy_class_allocators();
        throw;
    }
}

void HeapAllocatorAtomicSizeClassified::destroy_class_allocators() {
    for (; constructed_class_allocator_count > 0; --constructed_class_allocator_count) {
        get_class_allocator(constructed_class_allocator_count - 1).~ClassAllocator();
    }
}

HeapAllocatorAtomicSizeClassified::ClassAllocator& HeapAllocatorAtomicSizeClassified::get_class_allocator(size_t p_size_class_index) {
    WBE_DEBUG_ASSERT(p_size_class_index < size_class_count);
    return *std::launder(reinterpret_cast<ClassAllocator*>(class_allocator_storage + p_size_class_index * sizeof(ClassAllocator)));
}

const HeapAllocatorAtomicSizeClassified::ClassAllocator& HeapAllocatorAtomicSizeClassified::get_class_allocator(
    size_t p_size_class_index) const {
    WBE_DEBUG_ASSERT(p_size_class_index < size_class_count);
    return *std::launder(
        reinterpret_cast<const ClassAllocator*>(class_allocator_storage + p_size_class_index * sizeof(ClassAllocator)));
}

size_t HeapAllocatorAtomicSizeClassified::get_owning_size_class_index(MemID p_mem_id) const {
    for (size_t i = 0; i < size_class_count; ++i) {
        if (get_class_allocator(i).is_in_memory_chunk(p_mem_id)) {
            return i;
        }
    }
    throw std::runtime_error("Memory id does not belong to this size-classified allocator.");
}

} // namespace WhiteBirdEngine
