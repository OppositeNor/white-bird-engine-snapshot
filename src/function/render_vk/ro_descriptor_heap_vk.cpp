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
#include "function/render_vk/ro_descriptor_heap_vk.hh"

#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/vma_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/utils.hh"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {
VkDeviceSize checked_add(VkDeviceSize p_left, VkDeviceSize p_right) {
    if (p_right > std::numeric_limits<VkDeviceSize>::max() - p_left) {
        throw std::runtime_error("Failed to create descriptor heap: heap size overflow.");
    }
    return p_left + p_right;
}
} // namespace

RODescriptorHeapVK::Backing::Backing(
    const RenderContextVK* p_render_data, VkDeviceSize p_usable_size, VkDeviceSize p_reserved_size, VkDeviceSize p_alignment)
    : render_data(p_render_data), usable_size(p_usable_size), reserved_size(p_reserved_size) {
    if (p_render_data == nullptr || p_alignment == 0) {
        throw std::runtime_error("Failed to create descriptor heap: invalid render data or heap alignment.");
    }

    VkBufferCreateInfo buffer_create_info{};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = checked_add(usable_size, reserved_size);
    buffer_create_info.usage = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    Vector<uint32_t, RenderAllocator> queue_family_indices(
        render_data->queue_family_indices.begin(), render_data->queue_family_indices.end(), render_data->allocator);
    if (queue_family_indices.size() > 1) {
        buffer_create_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        buffer_create_info.queueFamilyIndexCount = static_cast<uint32_t>(queue_family_indices.size());
        buffer_create_info.pQueueFamilyIndices = queue_family_indices.data();
    } else {
        buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VmaAllocationCreateInfo allocation_create_info{};
    allocation_create_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_create_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VmaAllocationInfo allocation_info{};
    VkResult result = vmaCreateBufferWithAlignment(
        render_data->vma_allocator, &buffer_create_info, &allocation_create_info, p_alignment, &buffer, &allocation, &allocation_info);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to create Vulkan descriptor heap: {}.", string_VkResult(result)));
    }
    mapped_memory = allocation_info.pMappedData;
    std::memset(mapped_memory, 0, static_cast<size_t>(buffer_create_info.size));
    result = vmaFlushAllocation(render_data->vma_allocator, allocation, 0, VK_WHOLE_SIZE);
    if (result != VK_SUCCESS) {
        vmaDestroyBuffer(render_data->vma_allocator, buffer, allocation);
        buffer = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
        throw std::runtime_error(std::format("Failed to flush Vulkan descriptor heap: {}.", string_VkResult(result)));
    }

    VkBufferDeviceAddressInfo address_info{};
    address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    address_info.buffer = buffer;
    device_address = vkGetBufferDeviceAddress(render_data->logic_device, &address_info);
    if (device_address == 0 || device_address % p_alignment != 0) {
        vmaDestroyBuffer(render_data->vma_allocator, buffer, allocation);
        buffer = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to create descriptor heap: buffer device address does not satisfy heap alignment.");
    }
}

RODescriptorHeapVK::Backing::~Backing() {
    if (buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(render_data->vma_allocator, buffer, allocation);
    }
}

VkBindHeapInfoEXT RODescriptorHeapVK::Backing::get_bind_info() const {
    VkBindHeapInfoEXT result{};
    result.sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT;
    result.heapRange.address = device_address;
    result.heapRange.size = usable_size + reserved_size;
    result.reservedRangeOffset = usable_size;
    result.reservedRangeSize = reserved_size;
    return result;
}

RODescriptorHeapVK::RODescriptorHeapVK(
    const RenderContextVK* p_render_data, Ref<Backing> p_backing, DescriptorHeapTypeVK p_type, uint32_t p_first_index, uint32_t p_capacity, VkDeviceSize p_stride)
    : IRenderObjectVK(p_render_data), backing(std::move(p_backing)), type(p_type), first_index(p_first_index),
      capacity(p_capacity), stride(p_stride) {
    if (backing == MEM_NULL || capacity == 0 || stride == 0) {
        throw std::runtime_error("Failed to create descriptor heap arena: invalid backing, capacity, or stride.");
    }
    uint64_t arena_end = static_cast<uint64_t>(first_index) + capacity;
    if (arena_end > backing->usable_size / stride) {
        throw std::runtime_error("Failed to create descriptor heap arena: arena exceeds its physical heap.");
    }
}

RODescriptorHeapVK::~RODescriptorHeapVK() = default;

TypeID RODescriptorHeapVK::get_type_uuid() const {
    return TypeUUID<RODescriptorHeapVK>::UUID;
}

bool RODescriptorHeapVK::owns_range(uint32_t p_first_index, uint32_t p_count) const {
    if (p_count == 0 || p_first_index < first_index) {
        return false;
    }
    uint64_t range_end = static_cast<uint64_t>(p_first_index) + p_count;
    uint64_t arena_end = static_cast<uint64_t>(first_index) + capacity;
    return range_end <= arena_end;
}

void RODescriptorHeapVK::write_resource_descriptor(uint32_t p_index, const VkResourceDescriptorInfoEXT& p_descriptor) const {
    if (type == DescriptorHeapTypeVK::SAMPLER || !owns_range(p_index)) {
        throw std::runtime_error("Failed to write resource descriptor: descriptor index does not belong to the arena.");
    }
    VkDeviceSize offset = static_cast<VkDeviceSize>(p_index) * stride;
    auto* destination = static_cast<uint8_t*>(backing->mapped_memory) + offset;
    std::memset(destination, 0, static_cast<size_t>(stride));
    VkHostAddressRangeEXT descriptor_range{.address = destination, .size = static_cast<size_t>(stride)};
    VkResult result = render_data->write_resource_descriptors(render_data->logic_device, 1, &p_descriptor, &descriptor_range);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to write Vulkan resource descriptor: {}.", string_VkResult(result)));
    }
    result = vmaFlushAllocation(render_data->vma_allocator, backing->allocation, offset, stride);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to flush Vulkan resource descriptor: {}.", string_VkResult(result)));
    }
}

void RODescriptorHeapVK::write_sampler_descriptor(uint32_t p_index, const VkSamplerCreateInfo& p_sampler) const {
    if (type != DescriptorHeapTypeVK::SAMPLER || !owns_range(p_index)) {
        throw std::runtime_error("Failed to write sampler descriptor: descriptor index does not belong to the sampler arena.");
    }
    VkDeviceSize offset = static_cast<VkDeviceSize>(p_index) * stride;
    auto* destination = static_cast<uint8_t*>(backing->mapped_memory) + offset;
    std::memset(destination, 0, static_cast<size_t>(stride));
    VkHostAddressRangeEXT descriptor_range{.address = destination, .size = static_cast<size_t>(stride)};
    VkResult result = render_data->write_sampler_descriptors(render_data->logic_device, 1, &p_sampler, &descriptor_range);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to write Vulkan sampler descriptor: {}.", string_VkResult(result)));
    }
    result = vmaFlushAllocation(render_data->vma_allocator, backing->allocation, offset, stride);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to flush Vulkan sampler descriptor: {}.", string_VkResult(result)));
    }
}

void RODescriptorHeapVK::clear_descriptor(uint32_t p_index) const {
    if (!owns_range(p_index)) {
        throw std::runtime_error("Failed to clear descriptor: descriptor index does not belong to the arena.");
    }
    VkDeviceSize offset = static_cast<VkDeviceSize>(p_index) * stride;
    std::memset(static_cast<uint8_t*>(backing->mapped_memory) + offset, 0, static_cast<size_t>(stride));
    VkResult result = vmaFlushAllocation(render_data->vma_allocator, backing->allocation, offset, stride);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to flush cleared Vulkan descriptor: {}.", string_VkResult(result)));
    }
}

VkBindHeapInfoEXT RODescriptorHeapVK::get_bind_info() const {
    return backing->get_bind_info();
}

} // namespace WhiteBirdEngine
