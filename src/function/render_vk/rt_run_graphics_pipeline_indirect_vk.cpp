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
#include "function/render_vk/rt_run_graphics_pipeline_indirect_vk.hh"
#include "core/allocator/i_allocator.hh"
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
#include "function/render_vk/rt_params_run_graphics_pipeline_indirect_vk.hh"
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

Unique<IRenderTaskVK> RTRunGraphicsPipelineIndirectVK::create(
    const RenderContextVK* p_render_data, const RTParamsRunGraphicsPipelineIndirectVK& p_params, const RendGraphContextVK& p_context) {
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error("RTRunGraphicsPipelineIndirectVK: render data is unavailable.");
    }
    Ref<ROGraphicsPipelineVK> pipeline =
        acquire_task_graphics_pipeline_vk(p_render_data, "RTRunGraphicsPipelineIndirectVK", p_params.pipeline);
    std::vector<VkClearValue> clear_values;
    clear_values.reserve(p_params.clear_values.size());
    for (const RTParamsClearValueVK& clear_value : p_params.clear_values) {
        clear_values.push_back(clear_value.to_vk());
    }
    Builder builder(p_render_data->allocator);
    builder.set_index_buffer(p_params.index_buffer)
        .set_indirect_buffer(p_params.indirect_buffer)
        .set_pipeline(std::move(pipeline), p_params.draw_count)
        .set_render_extent(p_params.render_extent.resolve(p_context))
        .set_clear_values(clear_values)
        .set_color_load_op(to_vk_attachment_load_op(p_params.color_load_op))
        .set_depth_load_op(to_vk_attachment_load_op(p_params.depth_load_op));
    return builder.create_unique(p_render_data);
}

RTRunGraphicsPipelineIndirectVK::Builder::Builder(RenderAllocator* p_allocator)
    : allocator(p_allocator), clear_values(p_allocator), vertex_buffer_ids(p_allocator), indirect_buffer_ids(p_allocator),
      pipeline_batches(p_allocator) {
    if (allocator == nullptr) {
        throw std::runtime_error("RTRunGraphicsPipelineIndirectVK::Builder: allocator is null.");
    }
}

RTRunGraphicsPipelineIndirectVK::Builder& RTRunGraphicsPipelineIndirectVK::Builder::set_render_extent(VkExtent2D p_render_extent) {
    render_extent = p_render_extent;
    return *this;
}

RTRunGraphicsPipelineIndirectVK::Builder& RTRunGraphicsPipelineIndirectVK::Builder::set_clear_values(
    std::span<const VkClearValue> p_clear_values) {
    clear_values.assign(p_clear_values.begin(), p_clear_values.end());
    return *this;
}

RTRunGraphicsPipelineIndirectVK::Builder& RTRunGraphicsPipelineIndirectVK::Builder::set_color_load_op(VkAttachmentLoadOp p_load_op) {
    color_load_op = p_load_op;
    return *this;
}

RTRunGraphicsPipelineIndirectVK::Builder& RTRunGraphicsPipelineIndirectVK::Builder::set_depth_load_op(VkAttachmentLoadOp p_load_op) {
    depth_load_op = p_load_op;
    return *this;
}

RTRunGraphicsPipelineIndirectVK::Builder& RTRunGraphicsPipelineIndirectVK::Builder::add_vertex_buffer(std::string_view p_resource_id) {
    if (p_resource_id.empty()) {
        throw std::runtime_error("RTRunGraphicsPipelineIndirectVK::Builder: vertex buffer resource id is empty.");
    }
    vertex_buffer_ids.emplace_back(p_resource_id);
    return *this;
}

RTRunGraphicsPipelineIndirectVK::Builder& RTRunGraphicsPipelineIndirectVK::Builder::set_index_buffer(std::string_view p_resource_id) {
    if (p_resource_id.empty()) {
        throw std::runtime_error("RTRunGraphicsPipelineIndirectVK::Builder: index buffer resource id is empty.");
    }
    index_buffer_id = p_resource_id;
    return *this;
}

RTRunGraphicsPipelineIndirectVK::Builder& RTRunGraphicsPipelineIndirectVK::Builder::set_indirect_buffer(std::string_view p_resource_id) {
    indirect_buffer_ids.clear();
    add_indirect_buffer(p_resource_id);
    return *this;
}

uint32_t RTRunGraphicsPipelineIndirectVK::Builder::add_indirect_buffer(std::string_view p_resource_id) {
    if (p_resource_id.empty()) {
        throw std::runtime_error("RTRunGraphicsPipelineIndirectVK::Builder: indirect buffer resource id is empty.");
    }
    indirect_buffer_ids.emplace_back(p_resource_id);
    return static_cast<uint32_t>(indirect_buffer_ids.size() - 1);
}

RTRunGraphicsPipelineIndirectVK::Builder& RTRunGraphicsPipelineIndirectVK::Builder::set_pipeline(
    Ref<ROGraphicsPipelineVK> p_pipeline, uint32_t p_draw_count) {
    pipeline_batches.clear();
    return add_pipeline_batch(std::move(p_pipeline), 0, p_draw_count);
}

RTRunGraphicsPipelineIndirectVK::Builder& RTRunGraphicsPipelineIndirectVK::Builder::add_pipeline_batch(
    Ref<ROGraphicsPipelineVK> p_pipeline, uint32_t p_first_draw, uint32_t p_draw_count) {
    PipelineBatch batch;
    batch.graphics_pipeline = std::move(p_pipeline);
    batch.first_draw = p_first_draw;
    batch.draw_count = p_draw_count;
    return add_pipeline_batch(std::move(batch));
}

RTRunGraphicsPipelineIndirectVK::Builder& RTRunGraphicsPipelineIndirectVK::Builder::add_pipeline_batch(PipelineBatch p_batch) {
    if (p_batch.graphics_pipeline == MEM_NULL) {
        throw std::runtime_error("RTRunGraphicsPipelineIndirectVK::Builder: graphics pipeline is null.");
    }
    pipeline_batches.push_back(std::move(p_batch));
    return *this;
}

Unique<RTRunGraphicsPipelineIndirectVK> RTRunGraphicsPipelineIndirectVK::Builder::create_unique(const RenderContextVK* p_render_data) const {
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error("RTRunGraphicsPipelineIndirectVK::Builder: render data is missing.");
    }
    return make_unique<RTRunGraphicsPipelineIndirectVK>(p_render_data->allocator, p_render_data, *this);
}

namespace {

const ROGraphicsPipelineVK& first_pipeline(const Vector<RTRunGraphicsPipelineIndirectVK::PipelineBatch, RenderAllocator>& p_batches) {
    if (p_batches.empty()) {
        throw std::runtime_error("RTRunGraphicsPipelineIndirectVK::Builder: no pipeline batches were configured.");
    }
    return *p_batches.front().graphics_pipeline;
}

} // namespace

RTRunGraphicsPipelineIndirectVK::RTRunGraphicsPipelineIndirectVK(const RenderContextVK* p_render_data, const Builder& p_builder)
    : render_data(p_render_data), render_extent(p_builder.render_extent),
      pipeline_batches(p_builder.pipeline_batches.begin(), p_builder.pipeline_batches.end(), p_render_data->allocator),
      render_targets(p_render_data->allocator,
          first_pipeline(pipeline_batches),
          RenderTargetsVK::Config{
              .clear_values = p_builder.clear_values,
              .color_load_op = p_builder.color_load_op,
              .depth_load_op = p_builder.depth_load_op,
          }),
      vertex_buffer_ids(p_builder.vertex_buffer_ids.begin(), p_builder.vertex_buffer_ids.end(), p_render_data->allocator),
      index_buffer_id(p_builder.index_buffer_id),
      indirect_buffer_ids(p_builder.indirect_buffer_ids.begin(), p_builder.indirect_buffer_ids.end(), p_render_data->allocator),
      vertex_buffer_containers(p_render_data->allocator), bound_vertex_buffers(p_render_data->allocator),
      bound_vertex_buffer_offsets(p_render_data->allocator), indirect_buffer_containers(p_render_data->allocator),
      descriptor_bindings(p_render_data->allocator) {
    if (render_extent.width == 0 || render_extent.height == 0) {
        throw std::runtime_error("RTRunGraphicsPipelineIndirectVK::Builder: render extent is empty.");
    }
    if (index_buffer_id.empty() || indirect_buffer_ids.empty()) {
        throw std::runtime_error("RTRunGraphicsPipelineIndirectVK::Builder: index and indirect buffers require resource ids.");
    }
    size_t max_vertex_binding_count = 0;
    for (const PipelineBatch& batch : pipeline_batches) {
        if (batch.vertex_buffers.size() != batch.vertex_input.bindings.size()) {
            throw std::runtime_error("RTRunGraphicsPipelineIndirectVK::Builder: a batch must bind one vertex buffer per vertex-input binding.");
        }
        if (batch.indirect_buffer_index >= indirect_buffer_ids.size()) {
            throw std::runtime_error(
                "RTRunGraphicsPipelineIndirectVK::Builder: a batch references an indirect buffer that was not configured.");
        }
        for (const VertexBufferBindingVK& vertex_buffer : batch.vertex_buffers) {
            if (vertex_buffer.buffer_index >= vertex_buffer_ids.size()) {
                throw std::runtime_error(
                    "RTRunGraphicsPipelineIndirectVK::Builder: a batch references a vertex buffer that was not configured.");
            }
        }
        max_vertex_binding_count = std::max(max_vertex_binding_count, batch.vertex_buffers.size());
    }
    bound_vertex_buffers.resize(max_vertex_binding_count);
    bound_vertex_buffer_offsets.resize(max_vertex_binding_count);
    initialize_descriptor_bindings();
}

void RTRunGraphicsPipelineIndirectVK::initialize_descriptor_bindings() {
    descriptor_bindings.reserve(pipeline_batches.size());
    for (const PipelineBatch& batch : pipeline_batches) {
        if (!render_targets.matches(*batch.graphics_pipeline)) {
            throw std::runtime_error("RTRunGraphicsPipelineIndirectVK: pipeline batches do not share one attachment layout.");
        }
        descriptor_bindings.push_back(make_unique<PipelineDescriptorBindingsVK>(
            render_data->allocator, render_data, batch.graphics_pipeline->get_descriptor_layout()));
    }
}

void RTRunGraphicsPipelineIndirectVK::append_accesses(
    Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    render_targets.append_accesses(p_reads, p_writes);
    for (size_t i = 0; i < vertex_buffer_ids.size(); ++i) {
        if (std::find(vertex_buffer_ids.begin(), vertex_buffer_ids.begin() + static_cast<std::ptrdiff_t>(i), vertex_buffer_ids[i]) ==
            vertex_buffer_ids.begin() + static_cast<std::ptrdiff_t>(i)) {
            p_reads.push_back({.res_id = vertex_buffer_ids[i], .usage = RendResUsageVK::VERTEX_BUFFER});
        }
    }
    p_reads.push_back({.res_id = index_buffer_id, .usage = RendResUsageVK::INDEX_BUFFER});
    for (size_t i = 0; i < indirect_buffer_ids.size(); ++i) {
        if (std::find(indirect_buffer_ids.begin(), indirect_buffer_ids.begin() + static_cast<std::ptrdiff_t>(i), indirect_buffer_ids[i]) ==
            indirect_buffer_ids.begin() + static_cast<std::ptrdiff_t>(i)) {
            p_reads.push_back({.res_id = indirect_buffer_ids[i], .usage = RendResUsageVK::INDIRECT_BUFFER});
        }
    }
    for (const Unique<PipelineDescriptorBindingsVK>& bindings : descriptor_bindings) {
        bindings->append_accesses(p_reads, p_writes);
    }
}

void RTRunGraphicsPipelineIndirectVK::bind_resources(const RendResViewVK& p_resources) {
    for (Unique<PipelineDescriptorBindingsVK>& bindings : descriptor_bindings) {
        bindings->bind_resources(p_resources);
    }
    render_targets.bind_resources(p_resources);
    vertex_buffer_containers.clear();
    for (const std::string& vertex_buffer_id : vertex_buffer_ids) {
        vertex_buffer_containers.push_back(p_resources.get(vertex_buffer_id).container);
    }
    index_buffer_container = p_resources.get(index_buffer_id).container;
    indirect_buffer_containers.clear();
    for (const std::string& indirect_buffer_id : indirect_buffer_ids) {
        indirect_buffer_containers.push_back(p_resources.get(indirect_buffer_id).container);
    }
}

void RTRunGraphicsPipelineIndirectVK::record( // NOLINT
    VkCommandBuffer p_command_buffer,
    double /*p_delta*/) {
    Ref<ROBufferVK> resolved_index_buffer =
        WBE_REQUIRES_VALID(index_buffer_container->get(render_data->current_frame_slot).dynamic_cast_ref<ROBufferVK>());
    if (pipeline_batches.empty()) {
        throw std::runtime_error("RTRunGraphicsPipelineIndirectVK: indirect command buffer is empty or too small.");
    }
    for (size_t buffer_index = 0; buffer_index < indirect_buffer_containers.size(); ++buffer_index) {
        Ref<ROBufferVK> indirect_buffer = WBE_REQUIRES_VALID(
            indirect_buffer_containers[buffer_index]->get(render_data->current_frame_slot).dynamic_cast_ref<ROBufferVK>());
        uint64_t required_draw_count = 0;
        for (const auto& batch : pipeline_batches) {
            if (batch.indirect_buffer_index == buffer_index) {
                required_draw_count = std::max(required_draw_count, static_cast<uint64_t>(batch.first_draw) + batch.draw_count);
            }
        }
        if (indirect_buffer->fields().buffer_size < required_draw_count * sizeof(VkDrawIndexedIndirectCommand)) {
            throw std::runtime_error("RTRunGraphicsPipelineIndirectVK: indirect command buffer is empty or too small.");
        }
    }

    render_targets.begin_rendering(render_data, p_command_buffer, render_extent);

    VkViewport viewport = render_data->viewport;
    viewport.width = static_cast<float>(render_extent.width);
    viewport.height = static_cast<float>(render_extent.height);
    vkCmdSetViewport(p_command_buffer, 0, 1, &viewport);
    VkRect2D scissor{.offset = {.x = 0, .y = 0}, .extent = render_extent};
    vkCmdSetScissor(p_command_buffer, 0, 1, &scissor);
    vkCmdBindIndexBuffer(p_command_buffer, resolved_index_buffer->get_buffer(), 0, VK_INDEX_TYPE_UINT32);
    if (render_data->command_set_vertex_input == nullptr) {
        throw std::runtime_error("RTRunGraphicsPipelineIndirectVK: dynamic vertex-input command is unavailable.");
    }
    for (size_t batch_index = 0; batch_index < pipeline_batches.size(); ++batch_index) {
        const PipelineBatch& batch = pipeline_batches[batch_index];
        vkCmdBindPipeline(p_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch.graphics_pipeline->get_pipeline());
        if (!batch.vertex_buffers.empty()) {
            for (size_t i = 0; i < batch.vertex_buffers.size(); ++i) {
                const VertexBufferBindingVK& vertex_buffer = batch.vertex_buffers[i];
                Ref<ROBufferVK> resolved_vertex_buffer = WBE_REQUIRES_VALID(
                    vertex_buffer_containers[vertex_buffer.buffer_index]->get(render_data->current_frame_slot).dynamic_cast_ref<ROBufferVK>());
                bound_vertex_buffers[i] = resolved_vertex_buffer->get_buffer();
                bound_vertex_buffer_offsets[i] = vertex_buffer.offset;
            }
            vkCmdBindVertexBuffers(p_command_buffer,
                VERTEX_BUFFER_BINDPOINT,
                static_cast<uint32_t>(batch.vertex_buffers.size()),
                bound_vertex_buffers.data(),
                bound_vertex_buffer_offsets.data());
        }
        render_data->command_set_vertex_input(p_command_buffer,
            static_cast<uint32_t>(batch.vertex_input.bindings.size()),
            batch.vertex_input.bindings.data(),
            static_cast<uint32_t>(batch.vertex_input.attributes.size()),
            batch.vertex_input.attributes.data());
        descriptor_bindings[batch_index]->push(p_command_buffer);
        Ref<ROBufferVK> indirect_buffer = WBE_REQUIRES_VALID(
            indirect_buffer_containers[batch.indirect_buffer_index]->get(render_data->current_frame_slot).dynamic_cast_ref<ROBufferVK>());
        VkDeviceSize indirect_offset = static_cast<VkDeviceSize>(batch.first_draw) * sizeof(VkDrawIndexedIndirectCommand);
        vkCmdDrawIndexedIndirect(
            p_command_buffer, indirect_buffer->get_buffer(), indirect_offset, batch.draw_count, sizeof(VkDrawIndexedIndirectCommand));
    }
    vkCmdEndRendering(p_command_buffer);
}

} // namespace WhiteBirdEngine
