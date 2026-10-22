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
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/debug_utils_vk.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/utils.hh"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

DebugData make_descriptor_row(std::string_view p_descriptor_type, uint32_t p_heap_index) {
    DebugData row;
    row.set_value("resource_type", "ROImageViewVK");
    row.set_value("descriptor_type", std::string(p_descriptor_type));
    row.set_value("heap", "resource");
    row.set_value("heap_index", p_heap_index);
    row.set_value("device_address", nullptr);
    return row;
}

void write_image_descriptor(const RenderContextVK* p_render_data,
    DescriptorArenaLocationVK p_arena,
    uint32_t p_index,
    VkDescriptorType p_type,
    const VkImageViewCreateInfo& p_view) {
    VkImageDescriptorInfoEXT image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT;
    image_info.pView = &p_view;
    image_info.layout = VK_IMAGE_LAYOUT_GENERAL;
    VkResourceDescriptorInfoEXT descriptor{};
    descriptor.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    descriptor.type = p_type;
    descriptor.data.pImage = &image_info;
    p_render_data->descriptor_heap_allocator->write_resource_descriptor(p_arena.heap_id, p_arena.arena_id, p_index, descriptor);
}

} // namespace

Ref<ROImageViewVK> ImageViewDescVK::create_ref(const RenderContextVK* p_render_data) const {
    return p_render_data->create_image_view(*this);
}

Unique<ROImageViewVK> ImageViewDescVK::create_unique(const RenderContextVK* p_render_data) const {
    return make_unique<ROImageViewVK>(p_render_data->allocator, p_render_data, *this);
}

TypeID ROImageViewVK::get_type_uuid() const {
    return TypeUUID<ROImageViewVK>::UUID;
}

ImageViewDescVK& ImageViewDescVK::set_image(Ref<ROImageVK> p_image) {
    image = p_image;
    return *this;
}

ImageViewDescVK& ImageViewDescVK::set_view_type(VkImageViewType p_view_type) {
    view_type = p_view_type;
    return *this;
}

ImageViewDescVK& ImageViewDescVK::set_format(VkFormat p_format) {
    format = p_format;
    return *this;
}

ImageViewDescVK& ImageViewDescVK::set_components(const VkComponentMapping& p_components) {
    components = p_components;
    return *this;
}

ImageViewDescVK& ImageViewDescVK::set_components(
    VkComponentSwizzle p_r, VkComponentSwizzle p_g, VkComponentSwizzle p_b, VkComponentSwizzle p_a) {
    components = {.r = p_r, .g = p_g, .b = p_b, .a = p_a};
    return *this;
}

ImageViewDescVK& ImageViewDescVK::set_subresource_range(const VkImageSubresourceRange& p_subresource_range) {
    subresource_range = p_subresource_range;
    return *this;
}

ImageViewDescVK& ImageViewDescVK::set_aspect_mask(VkImageAspectFlags p_aspect_mask) {
    subresource_range.aspectMask = p_aspect_mask;
    return *this;
}

ImageViewDescVK& ImageViewDescVK::set_mip_level_count(uint32_t p_level_count) {
    subresource_range.levelCount = p_level_count;
    return *this;
}

ImageViewDescVK& ImageViewDescVK::set_from_image_config(const ImageDescVK& p_image_config) {
    switch (WBE_REQUIRED(p_image_config.image_type)) {
    case VK_IMAGE_TYPE_1D:
        view_type = VK_IMAGE_VIEW_TYPE_1D;
        break;
    case VK_IMAGE_TYPE_2D:
        view_type = VK_IMAGE_VIEW_TYPE_2D;
        break;
    case VK_IMAGE_TYPE_3D:
        view_type = VK_IMAGE_VIEW_TYPE_3D;
        break;
    default:
        break;
    };
    format = WBE_REQUIRED(p_image_config.format);
    return *this;
}

ImageViewDescVK& ImageViewDescVK::set_sampled_view_index(uint32_t p_index) {
    sampled_view_index = p_index;
    return *this;
}

ImageViewDescVK& ImageViewDescVK::set_storage_view_index(uint32_t p_index) {
    storage_view_index = p_index;
    return *this;
}

VkImageView ImageViewDescVK::create_vk_object(const RenderContextVK* p_render_data) const {
    VkImageViewCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = WBE_REQUIRED(image).get_image();
    create_info.viewType = WBE_REQUIRED(view_type);
    create_info.format = WBE_REQUIRED(format);
    create_info.components = components;
    create_info.subresourceRange = subresource_range;

    VkImageView result;
    if (vkCreateImageView(p_render_data->logic_device, &create_info, p_render_data->allocation_callbacks, &result) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view.");
    }
    return result;
}

ROImageViewVK::ROImageViewVK(const RenderContextVK* p_render_data, const Builder& p_builder)
    : IRenderObjectVK(p_render_data) {
    if (p_render_data == nullptr) {
        throw std::runtime_error("Render data is nullptr.");
    }
    init(p_builder);
}

ROImageViewVK::~ROImageViewVK() {
    // Deallocate resource after frames in flight.
    render_data->register_delayed_call(
        [render_data = render_data, image_view = image_view, sampled_view_index = sampled_view_index, storage_view_index = storage_view_index]() {
            if (sampled_view_index.has_value()) {
                const DescriptorArenaLocationVK arena = render_data->sampled_image_descriptor_arena;
                render_data->descriptor_heap_allocator->deallocate_descriptor(arena.heap_id, arena.arena_id, *sampled_view_index);
            }
            if (storage_view_index.has_value()) {
                const DescriptorArenaLocationVK arena = render_data->storage_image_descriptor_arena;
                render_data->descriptor_heap_allocator->deallocate_descriptor(arena.heap_id, arena.arena_id, *storage_view_index);
            }
            vkDestroyImageView(render_data->logic_device, image_view, render_data->allocation_callbacks);
        },
        render_data->frames_in_flight);
}

void ROImageViewVK::init(const Builder& p_builder) {
    exposed_fields.image = p_builder.image;
    exposed_fields.subresource_range = p_builder.subresource_range;
    create_info = VkImageViewCreateInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = WBE_REQUIRED(p_builder.image).get_image(),
        .viewType = WBE_REQUIRED(p_builder.view_type),
        .format = WBE_REQUIRED(p_builder.format),
        .components = p_builder.components,
        .subresourceRange = p_builder.subresource_range};
    if (p_builder.sampled_view_index.has_value()) {
        if ((exposed_fields.image->fields().usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0) {
            throw std::runtime_error("Failed to adopt sampled-image descriptor index: image does not have sampled usage.");
        }
        if (render_data->descriptor_heap_allocator == nullptr ||
            !render_data->descriptor_heap_allocator->owns_descriptor(render_data->sampled_image_descriptor_arena.heap_id,
                render_data->sampled_image_descriptor_arena.arena_id,
                *p_builder.sampled_view_index)) {
            throw std::runtime_error("Failed to adopt sampled-image descriptor index: the index is not allocated by the sampled-image arena.");
        }
    }
    if (p_builder.storage_view_index.has_value()) {
        if ((exposed_fields.image->fields().usage & VK_IMAGE_USAGE_STORAGE_BIT) == 0) {
            throw std::runtime_error("Failed to adopt storage-image descriptor index: image does not have storage usage.");
        }
        if (render_data->descriptor_heap_allocator == nullptr ||
            !render_data->descriptor_heap_allocator->owns_descriptor(render_data->storage_image_descriptor_arena.heap_id,
                render_data->storage_image_descriptor_arena.arena_id,
                *p_builder.storage_view_index)) {
            throw std::runtime_error("Failed to adopt storage-image descriptor index: the index is not allocated by the storage-image arena.");
        }
    }
    image_view = p_builder.create_vk_object(render_data);
    if (p_builder.sampled_view_index.has_value()) {
        sampled_view_index = p_builder.sampled_view_index;
        write_image_descriptor(
            render_data, render_data->sampled_image_descriptor_arena, *sampled_view_index, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, create_info);
    }
    if (p_builder.storage_view_index.has_value()) {
        storage_view_index = p_builder.storage_view_index;
        write_image_descriptor(
            render_data, render_data->storage_image_descriptor_arena, *storage_view_index, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, create_info);
    }
}

uint32_t ROImageViewVK::get_sampled_view_index() const {
    if ((exposed_fields.image->fields().usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0) {
        throw std::runtime_error("Failed to create sampled-image descriptor: image does not have sampled usage.");
    }
    if (!sampled_view_index.has_value()) {
        DescriptorHeapAllocatorVK* allocator = render_data->descriptor_heap_allocator;
        if (allocator == nullptr) {
            throw std::runtime_error("Failed to create sampled-image descriptor: descriptor heap allocator is unavailable.");
        }
        const DescriptorArenaLocationVK arena = render_data->sampled_image_descriptor_arena;
        sampled_view_index = allocator->allocate_descriptor(arena.heap_id, arena.arena_id);
        write_image_descriptor(render_data, arena, *sampled_view_index, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, create_info);
    }
    return *sampled_view_index;
}

uint32_t ROImageViewVK::get_storage_view_index() const {
    if ((exposed_fields.image->fields().usage & VK_IMAGE_USAGE_STORAGE_BIT) == 0) {
        throw std::runtime_error("Failed to create storage-image descriptor: image does not have storage usage.");
    }
    if (!storage_view_index.has_value()) {
        DescriptorHeapAllocatorVK* allocator = render_data->descriptor_heap_allocator;
        if (allocator == nullptr) {
            throw std::runtime_error("Failed to create storage-image descriptor: descriptor heap allocator is unavailable.");
        }
        const DescriptorArenaLocationVK arena = render_data->storage_image_descriptor_arena;
        storage_view_index = allocator->allocate_descriptor(arena.heap_id, arena.arena_id);
        write_image_descriptor(render_data, arena, *storage_view_index, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, create_info);
    }
    return *storage_view_index;
}

DebugDataRows ROImageViewVK::descriptor_debug_data() const {
    DebugDataRows rows(render_data->allocator);
    if (sampled_view_index.has_value()) {
        rows.emplace_back(make_descriptor_row("sampled_image", *sampled_view_index));
    }
    if (storage_view_index.has_value()) {
        rows.emplace_back(make_descriptor_row("storage_image", *storage_view_index));
    }
    return rows;
}

void ROImageViewVK::bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const {
    bind_render_object(p_graph, p_resource_id, acquire_this_ref());
}

void ROImageViewVK::apply_debug_name(std::string_view p_name) {
    // Names only the view; the viewed image is named by whoever registers or allocates it.
    set_debug_object_name_vk(render_data, VK_OBJECT_TYPE_IMAGE_VIEW, image_view, p_name);
}

} // namespace WhiteBirdEngine
