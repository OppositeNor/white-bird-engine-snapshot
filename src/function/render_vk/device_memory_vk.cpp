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
#include "function/render_vk/device_memory_vk.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/function_utils.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/vma_vk.hh"
#include <format>
#include <stdexcept>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

// Automatic VMA usages need the resource create info, so memory is selected by the domain's required property flags.
VmaAllocationCreateInfo allocation_create_info(DeviceDomain p_domain) {
    VmaAllocationCreateInfo result{};
    result.usage = VMA_MEMORY_USAGE_UNKNOWN;
    switch (p_domain) {
    case DeviceDomain::HOST:
        result.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        result.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    case DeviceDomain::DEVICE:
        result.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case DeviceDomain::HOST_DEVICE:
        result.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        result.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    }
    return result;
}

} // namespace

DeviceMemoryVK::Builder& DeviceMemoryVK::Builder::set_buffer_requirements(VkBuffer p_buffer) {
    buffer = p_buffer;
    return *this;
}

DeviceMemoryVK::Builder& DeviceMemoryVK::Builder::set_image_requirements(VkImage p_image) {
    image = p_image;
    return *this;
}

DeviceMemoryVK::Builder& DeviceMemoryVK::Builder::set_requirements(const VkMemoryRequirements& p_requirements) {
    requirements = p_requirements;
    return *this;
}

DeviceMemoryVK::Builder& DeviceMemoryVK::Builder::set_domain(DeviceDomain p_domain) {
    domain = p_domain;
    return *this;
}

Ref<DeviceMemoryVK> DeviceMemoryVK::Builder::create_ref(const RenderContextVK* p_render_data, bool p_is_dedicated) const {
    return make_ref<DeviceMemoryVK>(p_render_data->allocator, p_render_data, *this, p_is_dedicated);
}

DeviceMemoryVK::DeviceMemoryVK(const RenderContextVK* p_render_data, const Builder& p_builder, bool p_is_dedicated)
    : render_data(p_render_data), is_dedicated(p_is_dedicated) {
    const int source_count = static_cast<int>(p_builder.buffer != VK_NULL_HANDLE) + static_cast<int>(p_builder.image != VK_NULL_HANDLE) +
                             static_cast<int>(p_builder.requirements.has_value());
    if (source_count != 1) {
        throw std::runtime_error("Failed to allocate device memory: exactly one of a buffer, an image, or explicit requirements is required.");
    }
    exposed_fields.domain = WBE_REQUIRED(p_builder.domain);
    VmaAllocationCreateInfo create_info = allocation_create_info(exposed_fields.domain);
    create_info.flags |= is_dedicated ? VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT : VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
    VmaAllocationInfo allocation_info{};
    VkResult result = VK_SUCCESS;
    if (p_builder.buffer != VK_NULL_HANDLE) {
        result = vmaAllocateMemoryForBuffer(render_data->vma_allocator, p_builder.buffer, &create_info, &allocation, &allocation_info);
    } else if (p_builder.image != VK_NULL_HANDLE) {
        result = vmaAllocateMemoryForImage(render_data->vma_allocator, p_builder.image, &create_info, &allocation, &allocation_info);
    } else {
        result = vmaAllocateMemory(render_data->vma_allocator, &*p_builder.requirements, &create_info, &allocation, &allocation_info);
    }
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to allocate device memory: {}.", string_VkResult(result)));
    }
    exposed_fields.device_memory = allocation_info.deviceMemory;
    exposed_fields.offset = allocation_info.offset;
    exposed_fields.size = allocation_info.size;
    exposed_fields.memory_type_index = allocation_info.memoryType;
    exposed_fields.mapped_data = allocation_info.pMappedData;
}

DeviceMemoryVK::~DeviceMemoryVK() {
    vmaFreeMemory(render_data->vma_allocator, allocation);
}

VkBuffer DeviceMemoryVK::create_buffer(const VkBufferCreateInfo& p_create_info, VkDeviceSize p_offset) const {
    if (is_dedicated) {
        throw std::runtime_error("Failed to create aliasing buffer: the memory allocation is dedicated.");
    }
    const VkDeviceBufferMemoryRequirements requirements_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS,
        .pCreateInfo = &p_create_info,
    };
    VkMemoryDedicatedRequirements dedicated_requirements{.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryRequirements2 requirements{.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, .pNext = &dedicated_requirements};
    vkGetDeviceBufferMemoryRequirements(render_data->logic_device, &requirements_info, &requirements);
    if (dedicated_requirements.requiresDedicatedAllocation == VK_TRUE) {
        throw std::runtime_error("Failed to create aliasing buffer: the buffer requires dedicated memory.");
    }
    if (!is_compatible(requirements.memoryRequirements, p_offset)) {
        throw std::runtime_error(std::format("Failed to create aliasing buffer: device memory cannot hold {} bytes at offset {}.",
            requirements.memoryRequirements.size,
            p_offset));
    }
    VkBuffer buffer = VK_NULL_HANDLE;
    VkResult result = vmaCreateAliasingBuffer2(render_data->vma_allocator, allocation, p_offset, &p_create_info, &buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to create aliasing buffer: {}.", string_VkResult(result)));
    }
    return buffer;
}

VkImage DeviceMemoryVK::create_image(const VkImageCreateInfo& p_create_info, VkDeviceSize p_offset) const {
    if (is_dedicated) {
        throw std::runtime_error("Failed to create aliasing image: the memory allocation is dedicated.");
    }
    const VkDeviceImageMemoryRequirements requirements_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS,
        .pCreateInfo = &p_create_info,
    };
    VkMemoryDedicatedRequirements dedicated_requirements{.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryRequirements2 requirements{.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, .pNext = &dedicated_requirements};
    vkGetDeviceImageMemoryRequirements(render_data->logic_device, &requirements_info, &requirements);
    if (dedicated_requirements.requiresDedicatedAllocation == VK_TRUE) {
        throw std::runtime_error("Failed to create aliasing image: the image requires dedicated memory.");
    }
    if (!is_compatible(requirements.memoryRequirements, p_offset)) {
        throw std::runtime_error(std::format("Failed to create aliasing image: device memory cannot hold {} bytes at offset {}.",
            requirements.memoryRequirements.size,
            p_offset));
    }
    VkImage image = VK_NULL_HANDLE;
    VkResult result = vmaCreateAliasingImage2(render_data->vma_allocator, allocation, p_offset, &p_create_info, &image);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to create aliasing image: {}.", string_VkResult(result)));
    }
    return image;
}

bool DeviceMemoryVK::is_compatible(const VkMemoryRequirements& p_requirements, VkDeviceSize p_offset) const {
    const bool memory_type_allowed = (p_requirements.memoryTypeBits & (1U << exposed_fields.memory_type_index)) != 0;
    const bool aligned = p_requirements.alignment == 0 || (exposed_fields.offset + p_offset) % p_requirements.alignment == 0;
    const bool fits = p_offset <= exposed_fields.size && p_requirements.size <= exposed_fields.size - p_offset;
    return !is_dedicated && memory_type_allowed && aligned && fits;
}

void DeviceMemoryVK::flush(VkDeviceSize p_offset, VkDeviceSize p_size) const {
    VkResult result = vmaFlushAllocation(render_data->vma_allocator, allocation, p_offset, p_size);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to flush device memory: {}.", string_VkResult(result)));
    }
}

} // namespace WhiteBirdEngine
