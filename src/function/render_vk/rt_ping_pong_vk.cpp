/* Copyright 2026 OppositeNor

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
#include "function/render_vk/rt_ping_pong_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/pipeline_descriptor_bindings_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_task_create_utils_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/rt_params_ping_pong_vk.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include <array>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

RTPingPongVK::RTPingPongVK(const RenderContextVK* p_render_data,
    Ref<ROComputePipelineVK> p_ping_pipeline,
    Ref<ROComputePipelineVK> p_pong_pipeline,
    bool p_initial_state,
    uint32_t p_iterations,
    VkExtent3D p_dispatch_group_count,
    QueueFamilyVK p_queue_family)
    : render_data(p_render_data), queue_family_id(p_queue_family), ping_pipeline(std::move(p_ping_pipeline)),
      pong_pipeline(std::move(p_pong_pipeline)), dispatch_group_count(p_dispatch_group_count),
      initial_state(p_initial_state), iterations(p_iterations) {
    if (render_data == nullptr || render_data->allocator == nullptr) {
        throw std::runtime_error("RTPingPongVK: render data and its allocator must not be null.");
    }
    if (ping_pipeline == MEM_NULL) {
        throw std::runtime_error("RTPingPongVK: ping compute pipeline is null.");
    }
    if (pong_pipeline == MEM_NULL) {
        throw std::runtime_error("RTPingPongVK: pong compute pipeline is null.");
    }
    ping_descriptor_bindings =
        make_unique<PipelineDescriptorBindingsVK>(render_data->allocator, render_data, ping_pipeline->get_descriptor_layout());
    pong_descriptor_bindings =
        make_unique<PipelineDescriptorBindingsVK>(render_data->allocator, render_data, pong_pipeline->get_descriptor_layout());
}

void RTPingPongVK::set_iteration_prefixes(
    Ref<ROComputePipelineVK> p_ping_prefix_pipeline, Ref<ROComputePipelineVK> p_pong_prefix_pipeline, VkExtent3D p_dispatch_group_count) {
    if (resources_bound) {
        throw std::runtime_error("RTPingPongVK: prefixes cannot change after graph binding.");
    }
    if (p_ping_prefix_pipeline == MEM_NULL || p_pong_prefix_pipeline == MEM_NULL) {
        throw std::runtime_error("RTPingPongVK: ping and pong prefix pipelines must not be null.");
    }
    ping_prefix_pipeline = std::move(p_ping_prefix_pipeline);
    pong_prefix_pipeline = std::move(p_pong_prefix_pipeline);
    prefix_dispatch_group_count = p_dispatch_group_count;
    ping_prefix_descriptor_bindings = make_unique<PipelineDescriptorBindingsVK>(
        render_data->allocator, render_data, ping_prefix_pipeline->get_descriptor_layout());
    pong_prefix_descriptor_bindings = make_unique<PipelineDescriptorBindingsVK>(
        render_data->allocator, render_data, pong_prefix_pipeline->get_descriptor_layout());
}

void RTPingPongVK::bind_resources(const RendResViewVK& p_resources) {
    resources_bound = true;
    ping_descriptor_bindings->bind_resources(p_resources);
    pong_descriptor_bindings->bind_resources(p_resources);
    if (ping_prefix_descriptor_bindings != MEM_NULL) {
        ping_prefix_descriptor_bindings->bind_resources(p_resources);
        pong_prefix_descriptor_bindings->bind_resources(p_resources);
    }
}

void RTPingPongVK::append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    ping_descriptor_bindings->append_accesses(p_reads, p_writes);
    pong_descriptor_bindings->append_accesses(p_reads, p_writes);
    if (!ping_prefix_descriptor_bindings.is_null()) {
        ping_prefix_descriptor_bindings->append_accesses(p_reads, p_writes);
        pong_prefix_descriptor_bindings->append_accesses(p_reads, p_writes);
    }
}

void RTPingPongVK::record(VkCommandBuffer p_command_buffer, double /*p_delta*/) {
    bool current_state = initial_state;
    for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
        if (ping_prefix_descriptor_bindings != MEM_NULL) {
            if (current_state) {
                vkCmdBindPipeline(p_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pong_prefix_pipeline->get_pipeline());
                pong_prefix_descriptor_bindings->push(p_command_buffer);
            } else {
                vkCmdBindPipeline(p_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, ping_prefix_pipeline->get_pipeline());
                ping_prefix_descriptor_bindings->push(p_command_buffer);
            }
            vkCmdDispatch(p_command_buffer,
                prefix_dispatch_group_count.width,
                prefix_dispatch_group_count.height,
                prefix_dispatch_group_count.depth);
        }

        if (ping_prefix_descriptor_bindings != MEM_NULL) {
            VkMemoryBarrier2 pass_barrier{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            };
            VkDependencyInfo dependency_info{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .memoryBarrierCount = 1,
                .pMemoryBarriers = &pass_barrier,
            };
            vkCmdPipelineBarrier2(p_command_buffer, &dependency_info);
        }

        if (current_state) {
            vkCmdBindPipeline(p_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pong_pipeline->get_pipeline());
            pong_descriptor_bindings->push(p_command_buffer);
        } else {
            vkCmdBindPipeline(p_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, ping_pipeline->get_pipeline());
            ping_descriptor_bindings->push(p_command_buffer);
        }
        vkCmdDispatch(p_command_buffer, dispatch_group_count.width, dispatch_group_count.height, dispatch_group_count.depth);
        current_state = !current_state;

        if (iteration + 1 < iterations) {
            VkMemoryBarrier2 pass_barrier{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            };
            VkDependencyInfo dependency_info{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .memoryBarrierCount = 1,
                .pMemoryBarriers = &pass_barrier,
            };
            vkCmdPipelineBarrier2(p_command_buffer, &dependency_info);
        }
    }
}

void RTPingPongVK::set_push_constant(std::string_view p_name, PushConstantValueVK p_value) {
    bool declared = ping_descriptor_bindings->try_set_push_constant(p_name, p_value);
    declared = pong_descriptor_bindings->try_set_push_constant(p_name, p_value) || declared;
    if (ping_prefix_descriptor_bindings != MEM_NULL) {
        declared = ping_prefix_descriptor_bindings->try_set_push_constant(p_name, p_value) || declared;
        declared = pong_prefix_descriptor_bindings->try_set_push_constant(p_name, p_value) || declared;
    }
    if (!declared) {
        throw std::runtime_error(std::format("RTPingPongVK: no compute pipeline declares push constant '{}'.", p_name));
    }
}

Unique<IRenderTaskVK> RTPingPongVK::create(
    const RenderContextVK* p_render_data, const RTParamsPingPongVK& p_params, const RendGraphContextVK& /*p_context*/) {
    if (p_params.ping_prefix_pipeline.has_value() != p_params.pong_prefix_pipeline.has_value()) {
        throw std::runtime_error("RTPingPongVK: ping_prefix_pipeline and pong_prefix_pipeline must be set together.");
    }
    Ref<ROComputePipelineVK> ping_pipeline = acquire_task_compute_pipeline_vk(p_render_data, "RTPingPongVK", p_params.ping_pipeline);
    Ref<ROComputePipelineVK> pong_pipeline = acquire_task_compute_pipeline_vk(p_render_data, "RTPingPongVK", p_params.pong_pipeline);
    Ref<ROComputePipelineVK> ping_prefix_pipeline = MEM_NULL;
    Ref<ROComputePipelineVK> pong_prefix_pipeline = MEM_NULL;
    if (p_params.ping_prefix_pipeline.has_value()) {
        ping_prefix_pipeline = acquire_task_compute_pipeline_vk(p_render_data, "RTPingPongVK", *p_params.ping_prefix_pipeline);
        pong_prefix_pipeline = acquire_task_compute_pipeline_vk(p_render_data, "RTPingPongVK", *p_params.pong_prefix_pipeline);
    }
    // set_push_constant() applies a value to every pipeline of the task that declares its identifier.
    const auto layout_of = [](const Ref<ROComputePipelineVK>& p_pipeline) {
        return p_pipeline == MEM_NULL ? nullptr : &p_pipeline->get_descriptor_layout();
    };
    const std::array<const ShaderDescriptorHeapLayoutVK*, 4> layouts{
        layout_of(ping_pipeline),
        layout_of(pong_pipeline),
        layout_of(ping_prefix_pipeline),
        layout_of(pong_prefix_pipeline),
    };
    const std::vector<TaskPushConstantVK> push_constants =
        resolve_task_push_constants_vk(p_params.push_constants, layouts, "RTPingPongVK");
    Unique<RTPingPongVK> task = make_unique<RTPingPongVK>(p_render_data->allocator,
        p_render_data,
        std::move(ping_pipeline),
        std::move(pong_pipeline),
        p_params.initial_state,
        p_params.iterations,
        VkExtent3D{.width = p_params.group_count_x, .height = p_params.group_count_y, .depth = p_params.group_count_z},
        p_params.queue_family);
    if (ping_prefix_pipeline != MEM_NULL) {
        task->set_iteration_prefixes(std::move(ping_prefix_pipeline),
            std::move(pong_prefix_pipeline),
            VkExtent3D{.width = p_params.prefix_group_count_x, .height = p_params.prefix_group_count_y, .depth = p_params.prefix_group_count_z});
    }
    for (const TaskPushConstantVK& push_constant : push_constants) {
        task->set_push_constant(push_constant.name, push_constant.value);
    }
    return task;
}

} // namespace WhiteBirdEngine
