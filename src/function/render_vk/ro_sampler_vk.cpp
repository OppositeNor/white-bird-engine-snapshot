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
#include "function/render_vk/ro_sampler_vk.hh"
#include "function/render_vk/debug_utils_vk.hh"
#include <string_view>
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/utils.hh"
#include <cstdint>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

DebugData make_descriptor_row(uint32_t p_heap_index) {
    DebugData row;
    row.set_value("resource_type", "ROSamplerVK");
    row.set_value("descriptor_type", "sampler");
    row.set_value("heap", "sampler");
    row.set_value("heap_index", p_heap_index);
    row.set_value("device_address", nullptr);
    return row;
}

} // namespace

Ref<ROSamplerVK> SamplerDescVK::create_ref(const RenderContextVK* p_render_data) const {
    return p_render_data->create_sampler(*this);
}

Unique<ROSamplerVK> SamplerDescVK::create_unique(const RenderContextVK* p_render_data) const {
    return make_unique<ROSamplerVK>(p_render_data->allocator, p_render_data, *this);
}

TypeID ROSamplerVK::get_type_uuid() const {
    return TypeUUID<ROSamplerVK>::UUID;
}

SamplerDescVK& SamplerDescVK::set_filter(VkFilter p_min_filter, VkFilter p_mag_filter) {
    min_filter = p_min_filter;
    mag_filter = p_mag_filter;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_mag_filter(VkFilter p_mag_filter) {
    mag_filter = p_mag_filter;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_min_filter(VkFilter p_min_filter) {
    min_filter = p_min_filter;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_mipmap_mode(VkSamplerMipmapMode p_mipmap_mode) {
    mipmap_mode = p_mipmap_mode;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_address_mode(VkSamplerAddressMode p_address_mode) {
    return set_address_mode(p_address_mode, p_address_mode, p_address_mode);
}

SamplerDescVK& SamplerDescVK::set_address_mode(
    VkSamplerAddressMode p_address_mode_u, VkSamplerAddressMode p_address_mode_v, VkSamplerAddressMode p_address_mode_w) {
    address_mode_u = p_address_mode_u;
    address_mode_v = p_address_mode_v;
    address_mode_w = p_address_mode_w;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_address_mode_u(VkSamplerAddressMode p_address_mode_u) {
    address_mode_u = p_address_mode_u;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_address_mode_v(VkSamplerAddressMode p_address_mode_v) {
    address_mode_v = p_address_mode_v;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_address_mode_w(VkSamplerAddressMode p_address_mode_w) {
    address_mode_w = p_address_mode_w;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_mip_lod_bias(float p_mip_lod_bias) {
    mip_lod_bias = p_mip_lod_bias;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_anisotropy(VkBool32 p_anisotropy_enable, float p_max_anisotropy) {
    anisotropy_enable = p_anisotropy_enable;
    max_anisotropy = p_max_anisotropy;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_anisotropy_enable(VkBool32 p_anisotropy_enable) {
    anisotropy_enable = p_anisotropy_enable;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_max_anisotropy(float p_max_anisotropy) {
    max_anisotropy = p_max_anisotropy;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_compare(VkBool32 p_compare_enable, VkCompareOp p_compare_op) {
    compare_enable = p_compare_enable;
    compare_op = p_compare_op;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_compare_enable(VkBool32 p_compare_enable) {
    compare_enable = p_compare_enable;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_compare_op(VkCompareOp p_compare_op) {
    compare_op = p_compare_op;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_lod_range(float p_min_lod, float p_max_lod) {
    min_lod = p_min_lod;
    max_lod = p_max_lod;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_min_lod(float p_min_lod) {
    min_lod = p_min_lod;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_max_lod(float p_max_lod) {
    max_lod = p_max_lod;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_border_color(VkBorderColor p_border_color) {
    border_color = p_border_color;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_unnormalized_coordinates(VkBool32 p_unnormalized_coordinates) {
    unnormalized_coordinates = p_unnormalized_coordinates;
    return *this;
}

SamplerDescVK& SamplerDescVK::set_sampler_index(uint32_t p_index) {
    sampler_index = p_index;
    return *this;
}

VkSampler SamplerDescVK::create_vk_object(const RenderContextVK* p_render_data) const {
    VkSamplerCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    create_info.magFilter = mag_filter;
    create_info.minFilter = min_filter;
    create_info.mipmapMode = mipmap_mode;
    create_info.addressModeU = address_mode_u;
    create_info.addressModeV = address_mode_v;
    create_info.addressModeW = address_mode_w;
    create_info.mipLodBias = mip_lod_bias;
    create_info.anisotropyEnable = anisotropy_enable;
    create_info.maxAnisotropy = max_anisotropy;
    create_info.compareEnable = compare_enable;
    create_info.compareOp = compare_op;
    create_info.minLod = min_lod;
    create_info.maxLod = max_lod;
    create_info.borderColor = border_color;
    create_info.unnormalizedCoordinates = unnormalized_coordinates;

    VkSampler result = VK_NULL_HANDLE;
    if (vkCreateSampler(p_render_data->logic_device, &create_info, p_render_data->allocation_callbacks, &result) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan sampler.");
    }
    return result;
}

ROSamplerVK::ROSamplerVK(const RenderContextVK* p_render_data, const Builder& p_builder) : IRenderObjectVK(p_render_data) {
    create_info = VkSamplerCreateInfo{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = p_builder.mag_filter,
        .minFilter = p_builder.min_filter,
        .mipmapMode = p_builder.mipmap_mode,
        .addressModeU = p_builder.address_mode_u,
        .addressModeV = p_builder.address_mode_v,
        .addressModeW = p_builder.address_mode_w,
        .mipLodBias = p_builder.mip_lod_bias,
        .anisotropyEnable = p_builder.anisotropy_enable,
        .maxAnisotropy = p_builder.max_anisotropy,
        .compareEnable = p_builder.compare_enable,
        .compareOp = p_builder.compare_op,
        .minLod = p_builder.min_lod,
        .maxLod = p_builder.max_lod,
        .borderColor = p_builder.border_color,
        .unnormalizedCoordinates = p_builder.unnormalized_coordinates};
    if (p_builder.sampler_index.has_value()) {
        if (render_data->descriptor_heap_allocator == nullptr ||
            !render_data->descriptor_heap_allocator->owns_descriptor(
                render_data->sampler_descriptor_arena.heap_id, render_data->sampler_descriptor_arena.arena_id, *p_builder.sampler_index)) {
            throw std::runtime_error("Failed to adopt sampler descriptor index: the index is not allocated by the sampler arena.");
        }
    }
    sampler = p_builder.create_vk_object(p_render_data);
    if (p_builder.sampler_index.has_value()) {
        sampler_index = p_builder.sampler_index;
        const DescriptorArenaLocationVK arena = render_data->sampler_descriptor_arena;
        render_data->descriptor_heap_allocator->write_sampler_descriptor(arena.heap_id, arena.arena_id, *sampler_index, create_info);
    }
}

ROSamplerVK::~ROSamplerVK() {
    render_data->register_delayed_call(
        [render_data = render_data, sampler = sampler, sampler_index = sampler_index]() {
            if (sampler_index.has_value()) {
                const DescriptorArenaLocationVK arena = render_data->sampler_descriptor_arena;
                render_data->descriptor_heap_allocator->deallocate_descriptor(arena.heap_id, arena.arena_id, *sampler_index);
            }
            vkDestroySampler(render_data->logic_device, sampler, render_data->allocation_callbacks);
        },
        render_data->frames_in_flight);
}

uint32_t ROSamplerVK::get_sampler_index() const {
    if (!sampler_index.has_value()) {
        DescriptorHeapAllocatorVK* allocator = render_data->descriptor_heap_allocator;
        if (allocator == nullptr) {
            throw std::runtime_error("Failed to create sampler descriptor: descriptor heap allocator is unavailable.");
        }
        const DescriptorArenaLocationVK arena = render_data->sampler_descriptor_arena;
        sampler_index = allocator->allocate_descriptor(arena.heap_id, arena.arena_id);
        allocator->write_sampler_descriptor(arena.heap_id, arena.arena_id, *sampler_index, create_info);
    }
    return *sampler_index;
}

DebugDataRows ROSamplerVK::descriptor_debug_data() const {
    DebugDataRows rows(render_data->allocator);
    if (sampler_index.has_value()) {
        rows.emplace_back(make_descriptor_row(*sampler_index));
    }
    return rows;
}

void ROSamplerVK::bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const {
    bind_render_object(p_graph, p_resource_id, acquire_this_ref());
}

void ROSamplerVK::apply_debug_name(std::string_view p_name) {
    set_debug_object_name_vk(render_data, VK_OBJECT_TYPE_SAMPLER, sampler, p_name);
}

} // namespace WhiteBirdEngine
