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
#include "function/render_vk/rt_generate_mipmaps_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "function/render_vk/rt_params_generate_mipmaps_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/defs.hh"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

// Number of mip levels, including the base level, until the largest dimension reaches 1 at the downsample rate.
uint32_t full_mipmap_level_count(VkExtent2D p_base_extent, uint32_t p_downsample_rate) {
    WBE_DEBUG_ASSERT(p_downsample_rate >= 2);
    uint32_t level_count = 1;
    for (uint32_t size = std::max(p_base_extent.width, p_base_extent.height); size > 1; size /= p_downsample_rate) {
        ++level_count;
    }
    return level_count;
}

} // namespace

RTGenerateMipmapsVK::RTGenerateMipmapsVK(const RenderContextVK* p_render_data,
    std::string_view p_source_view_id,
    std::string_view p_mip_chain_view_id,
    VkExtent2D p_base_extent,
    uint32_t p_mipmap_level_count,
    VkFormat p_format,
    uint32_t p_downsample_rate)
    : render_data(p_render_data), source_view_id(p_source_view_id), mip_chain_view_id(p_mip_chain_view_id),
      base_extent(p_base_extent), mipmap_level_count(p_mipmap_level_count), downsample_rate(p_downsample_rate) {
    if (render_data == nullptr || render_data->physical_device == VK_NULL_HANDLE) {
        // Format capabilities cannot be queried without a valid physical device.
        throw std::runtime_error("RTGenerateMipmapsVK: physical device is missing.");
    }
    if (base_extent.width == 0 || base_extent.height == 0 || mipmap_level_count == 0) {
        // Empty images and mip chains cannot produce a valid blit sequence.
        throw std::runtime_error("RTGenerateMipmapsVK: image extent and mip level count must be non-zero.");
    }
    if (downsample_rate < 2) {
        // A rate below 2 would never shrink the levels.
        throw std::runtime_error("RTGenerateMipmapsVK: downsample rate must be at least 2.");
    }
    // Limit the mip chain to the number of levels supported by the base extent.
    const uint32_t maximum_mipmap_level_count = full_mipmap_level_count(base_extent, downsample_rate);
    if (mipmap_level_count > maximum_mipmap_level_count) {
        // A longer chain would address mip levels that do not exist for this extent.
        throw std::runtime_error("RTGenerateMipmapsVK: mip level count exceeds the levels supported by the base extent.");
    }
    // Require every format feature used by the linear blit mip-generation path.
    VkFormatProperties format_properties{};
    vkGetPhysicalDeviceFormatProperties(render_data->physical_device, p_format, &format_properties);
    constexpr VkFormatFeatureFlags REQUIRED_FEATURES =
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((format_properties.optimalTilingFeatures & REQUIRED_FEATURES) != REQUIRED_FEATURES) {
        // Linear mip generation requires the format to support both blit directions and filtering.
        throw std::runtime_error("RTGenerateMipmapsVK: image format does not support filtered mipmap blits.");
    }
}

void RTGenerateMipmapsVK::append_accesses(
    Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    // The first blit reads mip level zero; later blits read and write the generated levels of the mip chain.
    p_reads.push_back({.res_id = source_view_id, .usage = RendResUsageVK::TRANSFER_SRC});
    p_reads.push_back({.res_id = mip_chain_view_id, .usage = RendResUsageVK::TRANSFER_SRC});
    p_writes.push_back({.res_id = mip_chain_view_id, .usage = RendResUsageVK::TRANSFER_DST});
}

void RTGenerateMipmapsVK::bind_resources(const RendResViewVK& p_resources) {
    auto bind_view = [&p_resources](std::string_view p_view_id) {
        Ref<RenderObjectContainerVK> container = p_resources.get(p_view_id).container;
        if (container == MEM_NULL || container->get_resource_type_id() != TypeUUID<ROImageViewVK>::UUID) {
            throw std::runtime_error("RTGenerateMipmapsVK: image-view resource has the wrong type.");
        }
        return container;
    };
    source_view_container = bind_view(source_view_id);
    mip_chain_view_container = bind_view(mip_chain_view_id);
}

void RTGenerateMipmapsVK::record(VkCommandBuffer p_command_buffer, double /*p_delta*/) {
    // Resolve the graph image view and its underlying image for this frame.
    auto source_view = source_view_container->get(render_data->current_frame_slot).dynamic_cast_ref<ROImageViewVK>();
    auto mip_chain_view = mip_chain_view_container->get(render_data->current_frame_slot).dynamic_cast_ref<ROImageViewVK>();
    if (source_view == MEM_NULL || mip_chain_view == MEM_NULL || mip_chain_view->fields().image == MEM_NULL) {
        // Commands cannot be recorded without bound image views and an image.
        throw std::runtime_error("RTGenerateMipmapsVK: image-view resource is missing.");
    }
    if (source_view->fields().image != mip_chain_view->fields().image) {
        // The declared accesses only describe the blits when both views reference the same image.
        throw std::runtime_error("RTGenerateMipmapsVK: source and mip-chain views reference different images.");
    }
    // Generate each destination mip from the preceding level, shrunk by the downsample rate.
    Ref<ROImageVK> image = mip_chain_view->fields().image;
    const int32_t rate = static_cast<int32_t>(downsample_rate);
    int32_t source_width = static_cast<int32_t>(base_extent.width);
    int32_t source_height = static_cast<int32_t>(base_extent.height);
    int32_t source_depth =
        image->fields().view_type == VK_IMAGE_VIEW_TYPE_3D ? static_cast<int32_t>(image->fields().extent.depth) : 1;
    for (uint32_t mip_level = 1; mip_level < mipmap_level_count; ++mip_level) {
        if (mip_level > 1) {
            // Make a previously generated source mip visible before reading it in the next blit.
            ROImageVK::MemoryBarrierBuilder()
                .set_src_access(VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT)
                .set_dst_access(VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT)
                .set_subresource_range(VkImageSubresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = mip_level - 1,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                })
                .set_new_layout(VK_IMAGE_LAYOUT_GENERAL)
                .set_image(image)
                .apply_barrier(p_command_buffer);
        }
        // Discard and transition the destination mip before completely overwriting it.
        ROImageVK::MemoryBarrierBuilder()
            .set_src_access(VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE)
            .set_dst_access(VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT)
            .set_subresource_range(VkImageSubresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = mip_level,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            })
            .set_new_layout(VK_IMAGE_LAYOUT_GENERAL)
            .set_image(image, true)
            .apply_barrier(p_command_buffer);

        // Describe the linear blit from the source mip into the next smaller level.
        const int32_t destination_width = std::max(source_width / rate, 1);
        const int32_t destination_height = std::max(source_height / rate, 1);
        const int32_t destination_depth = std::max(source_depth / rate, 1);
        VkImageBlit2 image_blit{};
        image_blit.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        image_blit.srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = mip_level - 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        image_blit.srcOffsets[1] = {.x = source_width, .y = source_height, .z = source_depth};
        image_blit.dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = mip_level,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        image_blit.dstOffsets[1] = {.x = destination_width, .y = destination_height, .z = destination_depth};
        // Record the filtered blit with both subresources in the general layout.
        VkBlitImageInfo2 blit_info{};
        blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blit_info.srcImage = image->get_image();
        blit_info.srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        blit_info.dstImage = image->get_image();
        blit_info.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        blit_info.regionCount = 1;
        blit_info.pRegions = &image_blit;
        blit_info.filter = VK_FILTER_LINEAR;
        vkCmdBlitImage2(p_command_buffer, &blit_info);
        // Advance the source dimensions for the next mip-generation iteration.
        source_width = destination_width;
        source_height = destination_height;
        source_depth = destination_depth;
    }
}

Unique<IRenderTaskVK> RTGenerateMipmapsVK::create(
    const RenderContextVK* p_render_data, const RTParamsGenerateMipmapsVK& p_params, const RendGraphContextVK& p_context) {
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error("RTGenerateMipmapsVK: render data is unavailable.");
    }
    const VkExtent2D base_extent = p_params.base_extent.resolve(p_context);
    if (p_params.downsample_rate < 2) {
        throw std::runtime_error("RTGenerateMipmapsVK: downsample rate must be at least 2.");
    }
    const uint32_t mipmap_level_count =
        p_params.mipmap_level_count.value_or(full_mipmap_level_count(base_extent, p_params.downsample_rate));
    return make_unique<RTGenerateMipmapsVK>(p_render_data->allocator,
        p_render_data,
        p_params.source_view,
        p_params.mip_chain_view,
        base_extent,
        mipmap_level_count,
        to_vk_format(p_params.format),
        p_params.downsample_rate);
}

} // namespace WhiteBirdEngine
