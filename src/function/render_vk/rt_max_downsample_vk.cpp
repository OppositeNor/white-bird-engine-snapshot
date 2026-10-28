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
#include "function/render_vk/rt_max_downsample_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_task_create_utils_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "function/render_vk/rt_params_max_downsample_vk.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include "generated/type_uuid.gen.hh"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {
namespace {

constexpr uint32_t WORKGROUP_SIZE = 16;
constexpr uint32_t DESCRIPTOR_INDEX_SIZE = sizeof(uint32_t) * 2;

const ShaderDescriptorHeapFieldVK& find_descriptor_field(
    const ShaderDescriptorHeapLayoutVK& p_layout, std::string_view p_resource_id, ShaderDescriptorHeapTypeVK p_type) {
    for (const ShaderDescriptorHeapFieldVK& field : p_layout.fields) {
        if (field.resource_id != p_resource_id) {
            continue;
        }
        if (field.type != p_type) {
            throw std::runtime_error(std::format("RTMaxDownsampleVK: pipeline field '{}' has the wrong descriptor type.", p_resource_id));
        }
        if (field.offset > p_layout.size || DESCRIPTOR_INDEX_SIZE > p_layout.size - field.offset) {
            throw std::runtime_error(std::format("RTMaxDownsampleVK: pipeline field '{}' lies outside the push data.", p_resource_id));
        }
        return field;
    }
    throw std::runtime_error(std::format("RTMaxDownsampleVK: pipeline does not declare the field '{}'.", p_resource_id));
}

uint32_t mip_extent(uint32_t p_base_extent, uint32_t p_mip_level) {
    return std::max(p_base_extent >> p_mip_level, 1U);
}

void record_level_barrier(VkCommandBuffer p_command_buffer) {
    // Make the level written by the previous dispatch visible to the next dispatch, which reads it as its source.
    VkMemoryBarrier2 level_barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
    };
    VkDependencyInfo dependency_info{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &level_barrier,
    };
    vkCmdPipelineBarrier2(p_command_buffer, &dependency_info);
}

void dispatch_level(VkCommandBuffer p_command_buffer, VkExtent3D p_base_extent, uint32_t p_destination_level) {
    // Cover the destination level in 16x16 workgroups.
    const uint32_t destination_width = mip_extent(p_base_extent.width, p_destination_level);
    const uint32_t destination_height = mip_extent(p_base_extent.height, p_destination_level);
    vkCmdDispatch(p_command_buffer,
        (destination_width + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE,
        (destination_height + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE,
        1);
}

} // namespace

RTMaxDownsampleVK::RTMaxDownsampleVK(
    const RenderContextVK* p_render_data, Ref<ROComputePipelineVK> p_pipeline, std::span<const std::string> p_mip_view_ids, QueueFamilyVK p_queue_family)
    : render_data(p_render_data), queue_family_id(p_queue_family), pipeline(std::move(p_pipeline)),
      mip_view_ids(p_render_data != nullptr ? p_render_data->allocator : nullptr),
      mip_view_containers(p_render_data != nullptr ? p_render_data->allocator : nullptr),
      push_data(p_render_data != nullptr ? p_render_data->allocator : nullptr) {
    if (render_data == nullptr) {
        throw std::runtime_error("RTMaxDownsampleVK: render data is missing.");
    }
    if (render_data->allocator == nullptr) {
        throw std::runtime_error("RTMaxDownsampleVK: render allocator is missing.");
    }
    if (p_mip_view_ids.size() < 2) {
        // A mip chain needs a source level and at least one generated level.
        throw std::runtime_error("RTMaxDownsampleVK: at least two mip-level image views are required.");
    }
    for (size_t i = 0; i < p_mip_view_ids.size(); ++i) {
        if (p_mip_view_ids[i].empty()) {
            throw std::runtime_error("RTMaxDownsampleVK: mip-level image-view id is empty.");
        }
        if (std::find(p_mip_view_ids.begin(), p_mip_view_ids.begin() + static_cast<std::ptrdiff_t>(i), p_mip_view_ids[i]) !=
            p_mip_view_ids.begin() + static_cast<std::ptrdiff_t>(i)) {
            // Each view must name a distinct mip level.
            throw std::runtime_error(std::format("RTMaxDownsampleVK: mip-level image-view id '{}' is repeated.", p_mip_view_ids[i]));
        }
        mip_view_ids.emplace_back(p_mip_view_ids[i]);
    }
    if (pipeline == MEM_NULL) {
        throw std::runtime_error("RTMaxDownsampleVK: compute pipeline is null.");
    }
    // The task fills the push data itself, so it accepts only the two descriptor fields it binds per level.
    const ShaderDescriptorHeapLayoutVK& layout = pipeline->get_descriptor_layout();
    if (layout.fields.size() != 2) {
        throw std::runtime_error("RTMaxDownsampleVK: pipeline must declare exactly a source and a destination descriptor.");
    }
    if (!layout.default_fields.empty()) {
        throw std::runtime_error("RTMaxDownsampleVK: pipeline must not declare renderer-default push fields.");
    }
    source_offset = find_descriptor_field(layout, SOURCE_RESOURCE_ID, ShaderDescriptorHeapTypeVK::SAMPLED_IMAGE).offset;
    destination_offset = find_descriptor_field(layout, DESTINATION_RESOURCE_ID, ShaderDescriptorHeapTypeVK::STORAGE_IMAGE).offset;
    push_data.resize(layout.size, uint8_t{0});
    mip_view_containers.resize(mip_view_ids.size(), MEM_NULL);
}

void RTMaxDownsampleVK::append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    // Every level except the last is read as a source; every level except the first is written as a destination.
    for (size_t i = 0; i + 1 < mip_view_ids.size(); ++i) {
        p_reads.push_back({.res_id = mip_view_ids[i], .usage = RendResUsageVK::SAMPLED_IMAGE});
    }
    for (size_t i = 1; i < mip_view_ids.size(); ++i) {
        p_writes.push_back({.res_id = mip_view_ids[i], .usage = RendResUsageVK::STORAGE_IMAGE});
    }
}

void RTMaxDownsampleVK::bind_resources(const RendResViewVK& p_resources) {
    for (size_t i = 0; i < mip_view_ids.size(); ++i) {
        Ref<RenderObjectContainerVK> container = p_resources.get(mip_view_ids[i]).container;
        if (container == MEM_NULL) {
            throw std::runtime_error(std::format("RTMaxDownsampleVK: image-view resource '{}' has no container.", mip_view_ids[i]));
        }
        if (container->get_resource_type_id() != TypeUUID<ROImageViewVK>::UUID) {
            throw std::runtime_error(std::format("RTMaxDownsampleVK: resource '{}' is not an image view.", mip_view_ids[i]));
        }
        mip_view_containers[i] = std::move(container);
    }
}

void RTMaxDownsampleVK::record(VkCommandBuffer p_command_buffer, double /*p_delta*/) {
    Vector<Ref<ROImageViewVK>, RenderAllocator> mip_views = resolve_mip_views();
    const VkExtent3D base_extent = mip_views[0]->fields().image->fields().extent;
    if (base_extent.width == 0 || base_extent.height == 0) {
        throw std::runtime_error("RTMaxDownsampleVK: image extent is unknown or empty.");
    }
    if (render_data->command_push_data == nullptr) {
        throw std::runtime_error("RTMaxDownsampleVK: push-data command is unavailable.");
    }

    vkCmdBindPipeline(p_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->get_pipeline());
    for (size_t i = 1; i < mip_views.size(); ++i) {
        if (i > 1) {
            record_level_barrier(p_command_buffer);
        }
        push_level_descriptors(p_command_buffer, *mip_views[i - 1], *mip_views[i]);
        dispatch_level(p_command_buffer, base_extent, mip_views[i]->fields().subresource_range.baseMipLevel);
    }
}

Vector<Ref<ROImageViewVK>, RenderAllocator> RTMaxDownsampleVK::resolve_mip_views() const {
    // Resolve the current frame's views and check that they describe consecutive single levels of one image.
    Vector<Ref<ROImageViewVK>, RenderAllocator> mip_views(render_data->allocator);
    mip_views.reserve(mip_view_containers.size());
    for (size_t i = 0; i < mip_view_containers.size(); ++i) {
        if (mip_view_containers[i] == MEM_NULL) {
            throw std::runtime_error(std::format("RTMaxDownsampleVK: image-view resource '{}' is not bound.", mip_view_ids[i]));
        }
        Ref<ROImageViewVK> view = mip_view_containers[i]->get(render_data->current_frame_slot).dynamic_cast_ref<ROImageViewVK>();
        if (view == MEM_NULL) {
            throw std::runtime_error(std::format("RTMaxDownsampleVK: image-view resource '{}' is missing.", mip_view_ids[i]));
        }
        if (view->fields().image == MEM_NULL) {
            throw std::runtime_error(std::format("RTMaxDownsampleVK: image view '{}' has no image.", mip_view_ids[i]));
        }
        if (view->fields().subresource_range.levelCount != 1) {
            throw std::runtime_error(
                std::format("RTMaxDownsampleVK: image view '{}' must cover exactly one mip level.", mip_view_ids[i]));
        }
        if (i > 0 && view->fields().image != mip_views[0]->fields().image) {
            throw std::runtime_error(std::format("RTMaxDownsampleVK: image view '{}' references a different image.", mip_view_ids[i]));
        }
        if (i > 0 && view->fields().subresource_range.baseMipLevel != mip_views[i - 1]->fields().subresource_range.baseMipLevel + 1) {
            throw std::runtime_error(std::format(
                "RTMaxDownsampleVK: image view '{}' does not view the mip level after the previous view.", mip_view_ids[i]));
        }
        mip_views.push_back(std::move(view));
    }
    return mip_views;
}

void RTMaxDownsampleVK::push_level_descriptors(
    VkCommandBuffer p_command_buffer, const ROImageViewVK& p_source, const ROImageViewVK& p_destination) {
    // Push the previous level as the source and the generated level as the destination.
    const uint32_t source_indices[2] = {p_source.get_sampled_view_index(), 0};
    const uint32_t destination_indices[2] = {p_destination.get_storage_view_index(), 0};
    std::memcpy(push_data.data() + source_offset, source_indices, DESCRIPTOR_INDEX_SIZE);
    std::memcpy(push_data.data() + destination_offset, destination_indices, DESCRIPTOR_INDEX_SIZE);
    VkPushDataInfoEXT push_data_info{
        .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
        .data = {.address = push_data.data(), .size = push_data.size()},
    };
    render_data->command_push_data(p_command_buffer, &push_data_info);
}

Unique<IRenderTaskVK> RTMaxDownsampleVK::create(
    const RenderContextVK* p_render_data, const RTParamsMaxDownsampleVK& p_params, const RendGraphContextVK& /*p_context*/) {
    Ref<ROComputePipelineVK> pipeline = acquire_task_compute_pipeline_vk(p_render_data, "RTMaxDownsampleVK", p_params.pipeline);
    return make_unique<RTMaxDownsampleVK>(
        p_render_data->allocator, p_render_data, std::move(pipeline), p_params.mip_views, p_params.queue_family);
}

} // namespace WhiteBirdEngine
