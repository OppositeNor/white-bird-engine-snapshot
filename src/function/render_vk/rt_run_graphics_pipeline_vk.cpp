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
#include "function/render_vk/rt_run_graphics_pipeline_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/pipeline_descriptor_bindings_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_targets_vk.hh"
#include "function/render_vk/render_task_create_utils_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/rt_params_clear_value_vk.hh"
#include "function/render_vk/rt_params_run_graphics_pipeline_vk.hh"
#include "function/render_vk/vertex_input_description_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

RTRunGraphicsPipelineVK::Builder::Builder(RenderAllocator* p_allocator) : clear_values(p_allocator) {
}

RTRunGraphicsPipelineVK::Builder& RTRunGraphicsPipelineVK::Builder::set_pipeline(Ref<ROGraphicsPipelineVK> p_pipeline) {
    pipeline = std::move(p_pipeline);
    return *this;
}

RTRunGraphicsPipelineVK::Builder& RTRunGraphicsPipelineVK::Builder::set_render_extent(VkExtent2D p_render_extent) {
    render_extent = p_render_extent;
    return *this;
}

RTRunGraphicsPipelineVK::Builder& RTRunGraphicsPipelineVK::Builder::set_clear_values(std::span<const VkClearValue> p_clear_values) {
    clear_values.assign(p_clear_values.begin(), p_clear_values.end());
    return *this;
}

RTRunGraphicsPipelineVK::Builder& RTRunGraphicsPipelineVK::Builder::set_color_load_op(VkAttachmentLoadOp p_load_op) {
    color_load_op = p_load_op;
    return *this;
}

RTRunGraphicsPipelineVK::Builder& RTRunGraphicsPipelineVK::Builder::set_depth_load_op(VkAttachmentLoadOp p_load_op) {
    depth_load_op = p_load_op;
    return *this;
}

RTRunGraphicsPipelineVK::Builder& RTRunGraphicsPipelineVK::Builder::set_indexed_draw(std::span<const VertexBufferSourceVK> p_vertex_buffers,
    std::string_view p_index_buffer_id,
    VertexInputDescriptionVK p_vertex_input,
    std::optional<IndexedDrawRegionVK> p_indexed_draw_region) {
    if (p_index_buffer_id.empty()) {
        throw std::runtime_error("RTRunGraphicsPipelineVK::Builder: index buffer resource id must not be empty.");
    }
    if (p_vertex_buffers.size() != p_vertex_input.bindings.size()) {
        throw std::runtime_error("RTRunGraphicsPipelineVK::Builder: one vertex buffer is required per vertex-input binding.");
    }
    vertex_buffer_ids.clear();
    vertex_buffer_offsets.clear();
    for (const VertexBufferSourceVK& vertex_buffer : p_vertex_buffers) {
        if (vertex_buffer.resource_id.empty()) {
            throw std::runtime_error("RTRunGraphicsPipelineVK::Builder: vertex buffer resource ids must not be empty.");
        }
        vertex_buffer_ids.emplace_back(vertex_buffer.resource_id);
        vertex_buffer_offsets.push_back(vertex_buffer.offset);
    }
    index_buffer_id = p_index_buffer_id;
    vertex_input = std::move(p_vertex_input);
    indexed_draw_region = p_indexed_draw_region;
    return *this;
}

RTRunGraphicsPipelineVK::Builder& RTRunGraphicsPipelineVK::Builder::set_draw_vertex_count(uint32_t p_draw_vertex_count) {
    draw_vertex_count = p_draw_vertex_count;
    return *this;
}

Unique<RTRunGraphicsPipelineVK> RTRunGraphicsPipelineVK::Builder::create_unique(const RenderContextVK* p_render_data) const {
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error("RTRunGraphicsPipelineVK::Builder: render data is missing.");
    }
    return make_unique<RTRunGraphicsPipelineVK>(p_render_data->allocator, p_render_data, *this);
}

namespace {

const ROGraphicsPipelineVK& require_pipeline(const Ref<ROGraphicsPipelineVK>& p_pipeline) {
    if (p_pipeline == MEM_NULL) {
        throw std::runtime_error("RTRunGraphicsPipelineVK: graphics pipeline is null.");
    }
    return *p_pipeline;
}

} // namespace

RTRunGraphicsPipelineVK::RTRunGraphicsPipelineVK(const RenderContextVK* p_render_data, const Builder& p_builder)
    : render_data(p_render_data), render_extent(p_builder.render_extent), graphics_pipeline(p_builder.pipeline),
      render_targets(p_render_data->allocator,
          require_pipeline(p_builder.pipeline),
          RenderTargetsVK::Config{
              .clear_values = p_builder.clear_values,
              .color_load_op = p_builder.color_load_op,
              .depth_load_op = p_builder.depth_load_op,
          }),
      vertex_buffer_ids(p_builder.vertex_buffer_ids), vertex_buffer_offsets(p_builder.vertex_buffer_offsets),
      index_buffer_id(p_builder.index_buffer_id), bound_vertex_buffers(p_builder.vertex_buffer_ids.size()),
      indexed_draw_region(p_builder.indexed_draw_region), vertex_input(p_builder.vertex_input),
      draw_vertex_count(p_builder.draw_vertex_count) {
    if (render_extent.width == 0 || render_extent.height == 0) {
        throw std::runtime_error("RTRunGraphicsPipelineVK: render extent is empty.");
    }
    descriptor_bindings = make_unique<PipelineDescriptorBindingsVK>(
        render_data->allocator, render_data, graphics_pipeline->get_descriptor_layout());
}

void RTRunGraphicsPipelineVK::append_accesses(
    Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    render_targets.append_accesses(p_reads, p_writes);
    for (size_t i = 0; i < vertex_buffer_ids.size(); ++i) {
        if (std::find(vertex_buffer_ids.begin(), vertex_buffer_ids.begin() + static_cast<std::ptrdiff_t>(i), vertex_buffer_ids[i]) ==
            vertex_buffer_ids.begin() + static_cast<std::ptrdiff_t>(i)) {
            p_reads.push_back({.res_id = vertex_buffer_ids[i], .usage = RendResUsageVK::VERTEX_BUFFER});
        }
    }
    if (index_buffer_id.has_value()) {
        p_reads.push_back({.res_id = *index_buffer_id, .usage = RendResUsageVK::INDEX_BUFFER});
    }
    descriptor_bindings->append_accesses(p_reads, p_writes);
}

void RTRunGraphicsPipelineVK::bind_resources(const RendResViewVK& p_resources) {
    descriptor_bindings->bind_resources(p_resources);
    render_targets.bind_resources(p_resources);
    vertex_buffer_containers.clear();
    for (const std::string& vertex_buffer_id : vertex_buffer_ids) {
        vertex_buffer_containers.push_back(p_resources.get(vertex_buffer_id).container);
    }
    if (index_buffer_id.has_value()) {
        index_buffer_container = p_resources.get(*index_buffer_id).container;
    }
}

void RTRunGraphicsPipelineVK::record(VkCommandBuffer p_command_buffer, double /*p_delta*/) {
    if (render_data->command_set_vertex_input == nullptr) {
        throw std::runtime_error("RTRunGraphicsPipelineVK: dynamic vertex-input command is unavailable.");
    }
    render_targets.begin_rendering(render_data, p_command_buffer, render_extent);

    vkCmdBindPipeline(p_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline->get_pipeline());
    VkViewport viewport = render_data->viewport;
    viewport.width = static_cast<float>(render_extent.width);
    viewport.height = static_cast<float>(render_extent.height);
    vkCmdSetViewport(p_command_buffer, 0, 1, &viewport);
    VkRect2D scissor{.offset = {.x = 0, .y = 0}, .extent = render_extent};
    vkCmdSetScissor(p_command_buffer, 0, 1, &scissor);

    descriptor_bindings->push(p_command_buffer);

    if (index_buffer_id.has_value()) {
        Ref<ROBufferVK> index_buffer =
            WBE_REQUIRES_VALID(index_buffer_container->get(render_data->current_frame_slot).dynamic_cast_ref<ROBufferVK>());
        if (!vertex_buffer_containers.empty()) {
            for (size_t i = 0; i < vertex_buffer_containers.size(); ++i) {
                Ref<ROBufferVK> vertex_buffer = WBE_REQUIRES_VALID(
                    vertex_buffer_containers[i]->get(render_data->current_frame_slot).dynamic_cast_ref<ROBufferVK>());
                bound_vertex_buffers[i] = vertex_buffer->get_buffer();
            }
            vkCmdBindVertexBuffers(p_command_buffer,
                VERTEX_BUFFER_BINDPOINT,
                static_cast<uint32_t>(bound_vertex_buffers.size()),
                bound_vertex_buffers.data(),
                vertex_buffer_offsets.data());
        }
        render_data->command_set_vertex_input(p_command_buffer,
            static_cast<uint32_t>(vertex_input.bindings.size()),
            vertex_input.bindings.data(),
            static_cast<uint32_t>(vertex_input.attributes.size()),
            vertex_input.attributes.data());
        vkCmdBindIndexBuffer(p_command_buffer, index_buffer->get_buffer(), 0, VK_INDEX_TYPE_UINT32);
        const uint32_t full_index_count = static_cast<uint32_t>(index_buffer->fields().buffer_size / sizeof(uint32_t));
        const IndexedDrawRegionVK region = indexed_draw_region.value_or(
            IndexedDrawRegionVK{.first_index = 0, .index_count = full_index_count, .vertex_offset = 0});
        if (region.first_index > full_index_count || region.index_count > full_index_count - region.first_index) {
            throw std::runtime_error("RTRunGraphicsPipelineVK: indexed draw region exceeds index buffer bounds.");
        }
        vkCmdDrawIndexed(p_command_buffer, region.index_count, 1, region.first_index, region.vertex_offset, 0);
    } else {
        // Vertex-less draws, such as screen triangles, generate their vertices in the shader.
        render_data->command_set_vertex_input(p_command_buffer, 0, nullptr, 0, nullptr);
        vkCmdDraw(p_command_buffer, draw_vertex_count, 1, 0, 0);
    }

    vkCmdEndRendering(p_command_buffer);
}

Unique<IRenderTaskVK> RTRunGraphicsPipelineVK::create(
    const RenderContextVK* p_render_data, const RTParamsRunGraphicsPipelineVK& p_params, const RendGraphContextVK& p_context) {
    Ref<ROGraphicsPipelineVK> pipeline = acquire_task_graphics_pipeline_vk(p_render_data, "RTRunGraphicsPipelineVK", p_params.pipeline);
    std::vector<VkClearValue> clear_values;
    clear_values.reserve(p_params.clear_values.size());
    for (const RTParamsClearValueVK& clear_value : p_params.clear_values) {
        clear_values.push_back(clear_value.to_vk());
    }
    return Builder(p_render_data->allocator)
        .set_pipeline(std::move(pipeline))
        .set_render_extent(p_params.render_extent.resolve(p_context))
        .set_clear_values(clear_values)
        .set_color_load_op(to_vk_attachment_load_op(p_params.color_load_op))
        .set_depth_load_op(to_vk_attachment_load_op(p_params.depth_load_op))
        .set_draw_vertex_count(p_params.draw_vertex_count)
        .create_unique(p_render_data);
}

} // namespace WhiteBirdEngine
