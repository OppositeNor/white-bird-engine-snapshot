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
#ifndef WBE_FILE_DESCRIPTOR_HEAP_ALLOCATOR_VK_HH
#define WBE_FILE_DESCRIPTOR_HEAP_ALLOCATOR_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_descriptor_heap_vk.hh"

#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @struct DescriptorHeapConfigurationVK
 * @brief Size and Vulkan bind domain of one physical descriptor heap.
 */
struct DescriptorHeapConfigurationVK {
    /** Vulkan bind domain. */
    DescriptorHeapTypeVK type = DescriptorHeapTypeVK::RESOURCE;
    /** Bytes available for descriptor arenas. */
    VkDeviceSize usable_size = 0;
    /** Bytes reserved for the implementation after the usable range. */
    VkDeviceSize reserved_size = 0;
    /** Required physical heap address alignment. */
    VkDeviceSize alignment = 0;

    /**
     * @brief Append an aligned arena range and return its byte offset.
     * @throws std::runtime_error If the range cannot be represented.
     */
    VkDeviceSize reserve_arena_bytes(uint32_t p_descriptor_count, VkDeviceSize p_stride, VkDeviceSize p_alignment);
};

/**
 * @struct DescriptorArenaConfigurationVK
 * @brief Location and indexing rules for an arena in one descriptor heap.
 */
struct DescriptorArenaConfigurationVK {
    /** Byte offset from the beginning of the physical heap. */
    VkDeviceSize byte_offset = 0;
    /** Bytes between consecutive descriptors in this arena. */
    VkDeviceSize descriptor_stride = 0;
    /** Number of descriptors in the arena. */
    uint32_t descriptor_count = 0;
    /** Required alignment of each descriptor in this arena. */
    VkDeviceSize descriptor_alignment = 0;
};

/**
 * @class DescriptorHeapAllocatorVK
 * @brief Owns physical Vulkan descriptor heaps and their logical arenas.
 *
 * Heap IDs identify physical backing buffers; arena IDs identify typed index
 * ranges inside one heap. Callers define layout and descriptor types. The
 * allocator is confined to the rendering thread.
 *
 * This object is neither copyable nor movable.
 */
class DescriptorHeapAllocatorVK final {
public:
    DescriptorHeapAllocatorVK() = delete;
    ~DescriptorHeapAllocatorVK() = default;
    DescriptorHeapAllocatorVK(const DescriptorHeapAllocatorVK&) = delete;
    DescriptorHeapAllocatorVK(DescriptorHeapAllocatorVK&&) noexcept = delete;
    DescriptorHeapAllocatorVK& operator=(const DescriptorHeapAllocatorVK&) = delete;
    DescriptorHeapAllocatorVK& operator=(DescriptorHeapAllocatorVK&&) noexcept = delete;

    /**
     * @brief Construct an empty descriptor-heap allocator.
     */
    DescriptorHeapAllocatorVK(RenderAllocator* p_allocator, const RenderContextVK* p_render_data);

    /**
     * @brief Create a physical descriptor heap and return its identifier.
     * @throws std::runtime_error If the configuration is invalid or the backing cannot be created.
     */
    DescriptorHeapIDVK create_heap(const DescriptorHeapConfigurationVK& p_configuration);
    /**
     * @brief Reserve an arena inside an existing physical heap.
     * @throws std::runtime_error If the arena overlaps another arena or exceeds the heap.
     */
    DescriptorArenaIDVK allocate_arena(DescriptorHeapIDVK p_heap_id, const DescriptorArenaConfigurationVK& p_configuration);
    /**
     * @brief Allocate a contiguous descriptor range and return its shader-visible index.
     */
    uint32_t allocate_descriptor(DescriptorHeapIDVK p_heap_id, DescriptorArenaIDVK p_arena_id, uint32_t p_count = 1);
    /**
     * @brief Release a previously allocated descriptor range.
     */
    void deallocate_descriptor(
        DescriptorHeapIDVK p_heap_id, DescriptorArenaIDVK p_arena_id, uint32_t p_first_index, uint32_t p_count = 1);
    /**
     * @brief Check whether a range belongs to an arena and is currently allocated.
     */
    bool owns_descriptor(DescriptorHeapIDVK p_heap_id, DescriptorArenaIDVK p_arena_id, uint32_t p_first_index, uint32_t p_count = 1) const;

    /**
     * @brief Encode a resource descriptor into an allocated arena slot.
     */
    void write_resource_descriptor(
        DescriptorHeapIDVK p_heap_id, DescriptorArenaIDVK p_arena_id, uint32_t p_index, const VkResourceDescriptorInfoEXT& p_descriptor) const;
    /**
     * @brief Encode a sampler descriptor into an allocated arena slot.
     */
    void write_sampler_descriptor(
        DescriptorHeapIDVK p_heap_id, DescriptorArenaIDVK p_arena_id, uint32_t p_index, const VkSamplerCreateInfo& p_sampler) const;

    /**
     * @brief Bind one resource heap and one sampler heap to a command buffer.
     */
    void bind_heaps(VkCommandBuffer p_command_buffer, DescriptorHeapIDVK p_resource_heap_id, DescriptorHeapIDVK p_sampler_heap_id) const;

private:
    struct FreeRange {
        uint32_t first_index = 0;
        uint32_t count = 0;
    };

    using RangeVector = Vector<FreeRange, RenderAllocator>;

    struct Arena {
        Arena() = delete;
        ~Arena() = default;
        Arena(const Arena&) = delete;
        Arena& operator=(const Arena&) = delete;
        Arena(Arena&&) noexcept = default;
        Arena& operator=(Arena&&) noexcept = default;
        Arena(RenderAllocator* p_allocator, Ref<RODescriptorHeapVK> p_view, VkDeviceSize p_byte_offset, VkDeviceSize p_byte_size);

        Ref<RODescriptorHeapVK> view;
        VkDeviceSize byte_offset = 0;
        VkDeviceSize byte_size = 0;
        RangeVector free_ranges;
    };

    struct Heap {
        Heap() = delete;
        ~Heap() = default;
        Heap(const Heap&) = delete;
        Heap& operator=(const Heap&) = delete;
        Heap(Heap&&) noexcept = default;
        Heap& operator=(Heap&&) noexcept = default;
        Heap(RenderAllocator* p_allocator, DescriptorHeapTypeVK p_type, Ref<RODescriptorHeapVK::Backing> p_backing, VkDeviceSize p_usable_size);

        DescriptorHeapTypeVK type = DescriptorHeapTypeVK::RESOURCE;
        Ref<RODescriptorHeapVK::Backing> backing;
        VkDeviceSize usable_size = 0;
        Vector<Arena, RenderAllocator> arenas;
    };

    Heap& get_heap(DescriptorHeapIDVK p_heap_id);
    const Heap& get_heap(DescriptorHeapIDVK p_heap_id) const;
    Arena& get_arena(DescriptorHeapIDVK p_heap_id, DescriptorArenaIDVK p_arena_id);
    const Arena& get_arena(DescriptorHeapIDVK p_heap_id, DescriptorArenaIDVK p_arena_id) const;
    static bool is_allocated(const Arena& p_arena, uint32_t p_first_index, uint32_t p_count);

    const RenderContextVK* render_data = nullptr;
    RenderAllocator* allocator = nullptr;
    Vector<Heap, RenderAllocator> heaps;
};

} // namespace WhiteBirdEngine

#endif
