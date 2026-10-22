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
#include "function/render_vk/rt_run_compute_pipelines_sequential_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/core_utils.hh"
#include "core/data/data_value.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/pipeline_descriptor_bindings_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_task_create_utils_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/rt_params_run_compute_pipelines_sequential_vk.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {
namespace {

// The entry pipeline's descriptor layout with the entry's resource renames applied to its reflected resource ids.
ShaderDescriptorHeapLayoutVK aliased_layout(const RTRunComputePipelinesSequentialVK::Entry& p_entry) {
    ShaderDescriptorHeapLayoutVK layout = p_entry.pipeline->get_descriptor_layout();
    for (const auto& [shader_resource_id, task_resource_id] : p_entry.resource_aliases) {
        const bool reflected = std::ranges::any_of(layout.fields, [&shader_resource_id](const ShaderDescriptorHeapFieldVK& p_field) {
            return p_field.resource_id == shader_resource_id;
        });
        if (!reflected) {
            throw std::runtime_error(std::format(
                "RTRunComputePipelinesSequentialVK: resource alias '{}' is not reflected by the dispatch's compute pipeline.",
                shader_resource_id));
        }
    }
    for (ShaderDescriptorHeapFieldVK& field : layout.fields) {
        auto alias = p_entry.resource_aliases.find(field.resource_id);
        if (alias != p_entry.resource_aliases.end()) {
            field.resource_id = alias->second;
        }
    }
    return layout;
}

// Copy a dispatch's recipe aliases, a map from reflected resource ids to task-local id strings, into p_entry.
void read_dispatch_aliases(const DataValue& p_aliases, size_t p_dispatch_index, RTRunComputePipelinesSequentialVK::Entry& p_entry) {
    // An empty Lua table reads as an empty list.
    if (p_aliases.is_nil() || (p_aliases.get_kind() == DataValueKind::LIST && p_aliases.as_list().empty())) {
        return;
    }
    if (p_aliases.get_kind() != DataValueKind::MAP) {
        throw std::runtime_error(
            std::format("RTRunComputePipelinesSequentialVK: aliases of dispatch {} must be a map.", p_dispatch_index));
    }
    for (const auto& [shader_resource_id, task_resource_id] : p_aliases.as_map()) {
        if (!task_resource_id.is_string()) {
            throw std::runtime_error(std::format(
                "RTRunComputePipelinesSequentialVK: alias '{}' of dispatch {} must map to a task-local resource id string.",
                shader_resource_id,
                p_dispatch_index));
        }
        p_entry.resource_aliases.emplace(shader_resource_id, task_resource_id.as_string());
    }
}

} // namespace

RTRunComputePipelinesSequentialVK::Entry::Entry(RenderAllocator* p_allocator) : resource_aliases(p_allocator) {
}

RTRunComputePipelinesSequentialVK::Dispatch::Dispatch(RenderAllocator* p_allocator) : allocator(p_allocator), entries(p_allocator) {
}

RTRunComputePipelinesSequentialVK::Dispatch& RTRunComputePipelinesSequentialVK::Dispatch::add_entry(Entry p_entry) {
    entries.push_back(std::move(p_entry));
    return *this;
}

RTRunComputePipelinesSequentialVK::Dispatch& RTRunComputePipelinesSequentialVK::Dispatch::add_entry(
    Ref<ROComputePipelineVK> p_pipeline, VkExtent3D p_group_count) {
    Entry entry(allocator);
    entry.pipeline = std::move(p_pipeline);
    entry.group_count = p_group_count;
    return add_entry(std::move(entry));
}

RTRunComputePipelinesSequentialVK::RTRunComputePipelinesSequentialVK(
    const RenderContextVK* p_render_data, const Dispatch& p_dispatch, QueueFamilyVK p_queue_family)
    : render_data(p_render_data), queue_family_id(p_queue_family), entries(p_render_data->allocator),
      descriptor_bindings(p_render_data->allocator), indirect_buffer_containers(p_render_data->allocator) {
    if (p_dispatch.entries.empty()) {
        throw std::runtime_error("RTRunComputePipelinesSequentialVK: at least one compute pipeline is required.");
    }
    entries.assign(p_dispatch.entries.begin(), p_dispatch.entries.end());
    for (const Entry& entry : entries) {
        if (entry.pipeline == MEM_NULL) {
            throw std::runtime_error("RTRunComputePipelinesSequentialVK: compute pipeline is null.");
        }
        if (entry.indirect_offset % 4 != 0) {
            throw std::runtime_error("RTRunComputePipelinesSequentialVK: indirect buffer offset is not a multiple of 4.");
        }
        if (entry.indirect_buffer_id.empty() && entry.indirect_offset != 0) {
            throw std::runtime_error("RTRunComputePipelinesSequentialVK: a direct dispatch sets an indirect buffer offset.");
        }
        descriptor_bindings.push_back(make_unique<PipelineDescriptorBindingsVK>(render_data->allocator, render_data, aliased_layout(entry)));
    }
    indirect_buffer_containers.resize(entries.size(), MEM_NULL);
}

void RTRunComputePipelinesSequentialVK::bind_resources(const RendResViewVK& p_resources) {
    for (Unique<PipelineDescriptorBindingsVK>& bindings : descriptor_bindings) {
        bindings->bind_resources(p_resources);
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].indirect_buffer_id.empty()) {
            indirect_buffer_containers[i] = p_resources.get(entries[i].indirect_buffer_id).container;
        }
    }
}

void RTRunComputePipelinesSequentialVK::append_accesses(
    Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    for (const Entry& entry : entries) {
        if (!entry.indirect_buffer_id.empty()) {
            p_reads.push_back({.res_id = entry.indirect_buffer_id, .usage = RendResUsageVK::INDIRECT_BUFFER});
        }
    }
    for (const Unique<PipelineDescriptorBindingsVK>& bindings : descriptor_bindings) {
        bindings->append_accesses(p_reads, p_writes);
    }
}

void RTRunComputePipelinesSequentialVK::record(VkCommandBuffer p_command_buffer, double /*p_delta*/) {
    for (size_t i = 0; i < entries.size(); ++i) {
        Entry& entry = entries[i];
        vkCmdBindPipeline(p_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, entry.pipeline->get_pipeline());
        descriptor_bindings[i]->push(p_command_buffer);
        if (entry.indirect_buffer_id.empty()) {
            vkCmdDispatch(p_command_buffer, entry.group_count.width, entry.group_count.height, entry.group_count.depth);
        } else {
            Ref<ROBufferVK> indirect_buffer =
                WBE_REQUIRES_VALID(indirect_buffer_containers[i]->get(render_data->current_frame_slot).dynamic_cast_ref<ROBufferVK>());
            if (indirect_buffer->fields().buffer_size < entry.indirect_offset + sizeof(VkDispatchIndirectCommand)) {
                throw std::runtime_error("RTRunComputePipelinesSequentialVK: indirect buffer is too small for the dispatch command.");
            }
            vkCmdDispatchIndirect(p_command_buffer, indirect_buffer->get_buffer(), entry.indirect_offset);
        }

        if (i + 1 >= entries.size()) {
            continue;
        }

        // An earlier pass may write the next pass's indirect command.
        const bool next_indirect = !entries[i + 1].indirect_buffer_id.empty();
        VkMemoryBarrier2 pass_barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | (next_indirect ? VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT : VK_PIPELINE_STAGE_2_NONE),
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                             (next_indirect ? VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT : VK_ACCESS_2_NONE),
        };
        VkDependencyInfo dependency_info{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &pass_barrier,
        };
        vkCmdPipelineBarrier2(p_command_buffer, &dependency_info);
    }
}

void RTRunComputePipelinesSequentialVK::set_push_constant(std::string_view p_name, PushConstantValueVK p_value) {
    bool declared = false;
    for (Unique<PipelineDescriptorBindingsVK>& bindings : descriptor_bindings) {
        declared = bindings->try_set_push_constant(p_name, p_value) || declared;
    }
    if (!declared) {
        throw std::runtime_error(
            std::format("RTRunComputePipelinesSequentialVK: no compute pipeline declares push constant '{}'.", p_name));
    }
}

void RTRunComputePipelinesSequentialVK::set_push_constant(size_t p_entry_index, std::string_view p_name, PushConstantValueVK p_value) {
    if (p_entry_index >= descriptor_bindings.size()) {
        throw std::runtime_error("RTRunComputePipelinesSequentialVK: push-constant entry index is out of range.");
    }
    descriptor_bindings[p_entry_index]->set_push_constant(p_name, p_value);
}

Unique<IRenderTaskVK> RTRunComputePipelinesSequentialVK::create(const RenderContextVK* p_render_data,
    const RTParamsRunComputePipelinesSequentialVK& p_params,
    const RendGraphContextVK& /*p_context*/) {
    if (p_params.dispatches.empty()) {
        throw std::runtime_error("RTRunComputePipelinesSequentialVK: at least one dispatch is required.");
    }
    // Validates the render data before its allocator is used.
    Ref<ROComputePipelineVK> first_pipeline =
        acquire_task_compute_pipeline_vk(p_render_data, "RTRunComputePipelinesSequentialVK", p_params.dispatches.front().pipeline);
    Dispatch dispatch(p_render_data->allocator);
    // Each dispatch's push constants are typed by its own pipeline.
    std::vector<std::vector<TaskPushConstantVK>> push_constants;
    push_constants.reserve(p_params.dispatches.size());
    for (size_t i = 0; i < p_params.dispatches.size(); ++i) {
        const RTParamsComputeDispatchVK& entry = p_params.dispatches[i];
        Ref<ROComputePipelineVK> pipeline =
            i == 0 ? first_pipeline
                   : acquire_task_compute_pipeline_vk(p_render_data, "RTRunComputePipelinesSequentialVK", entry.pipeline);
        const std::array<const ShaderDescriptorHeapLayoutVK*, 1> layouts{&pipeline->get_descriptor_layout()};
        push_constants.push_back(resolve_task_push_constants_vk(entry.push_constants, layouts, "RTRunComputePipelinesSequentialVK"));
        Entry dispatch_entry(p_render_data->allocator);
        dispatch_entry.pipeline = std::move(pipeline);
        dispatch_entry.group_count = VkExtent3D{.width = entry.group_count_x, .height = entry.group_count_y, .depth = entry.group_count_z};
        dispatch_entry.indirect_buffer_id = entry.indirect_buffer;
        dispatch_entry.indirect_offset = entry.indirect_offset;
        read_dispatch_aliases(entry.aliases, i, dispatch_entry);
        dispatch.add_entry(std::move(dispatch_entry));
    }
    Unique<RTRunComputePipelinesSequentialVK> task =
        make_unique<RTRunComputePipelinesSequentialVK>(p_render_data->allocator, p_render_data, dispatch, p_params.queue_family);
    for (size_t i = 0; i < push_constants.size(); ++i) {
        for (const TaskPushConstantVK& push_constant : push_constants[i]) {
            task->set_push_constant(i, push_constant.name, push_constant.value);
        }
    }
    return task;
}

} // namespace WhiteBirdEngine
