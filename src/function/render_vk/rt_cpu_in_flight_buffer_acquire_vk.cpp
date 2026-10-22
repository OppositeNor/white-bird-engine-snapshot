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
#include "function/render_vk/rt_cpu_in_flight_buffer_acquire_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/engine_core.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/event/event_manager.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_staging_buffer_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/defs.hh"
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

RTCPUInFlightBufferAcquireVK::RTCPUInFlightBufferAcquireVK(const RenderContextVK* p_render_data, std::string_view p_buffer_id)
    : render_data(p_render_data), buffer_id(p_buffer_id), event_manager(EventManager::get_singleton()) {
    if (render_data == nullptr || render_data->allocator == nullptr) {
        throw std::runtime_error("RTCPUInFlightBufferAcquireVK: render data is unavailable.");
    }
    if (buffer_id.empty()) {
        throw std::runtime_error("RTCPUInFlightBufferAcquireVK: buffer resource id is empty.");
    }
    if (event_manager == nullptr) {
        throw std::runtime_error("RTCPUInFlightBufferAcquireVK: no EventManager exists to emit acquisitions through.");
    }
}

Unique<IRenderTaskVK> RTCPUInFlightBufferAcquireVK::create(
    const RenderContextVK* p_render_data, const RTParamsCPUInFlightBufferAcquireVK& p_params, const RendGraphContextVK& /*p_context*/) {
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error("RTCPUInFlightBufferAcquireVK: render data is unavailable.");
    }
    return make_unique<RTCPUInFlightBufferAcquireVK>(p_render_data->allocator, p_render_data, p_params.buffer);
}

void RTCPUInFlightBufferAcquireVK::append_accesses(
    Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    static_cast<void>(p_writes);
    p_reads.push_back({.res_id = buffer_id, .usage = RendResUsageVK::TRANSFER_SRC});
}

void RTCPUInFlightBufferAcquireVK::bind_resources(const RendResViewVK& p_resources) {
    buffer_container = p_resources.get(buffer_id).container;
    if (buffer_container == MEM_NULL || buffer_container->get_resource_type_id() != TypeUUID<ROBufferVK>::UUID) {
        throw std::runtime_error("RTCPUInFlightBufferAcquireVK: buffer resource has the wrong type.");
    }
}

void RTCPUInFlightBufferAcquireVK::record(VkCommandBuffer p_command_buffer, double /*p_delta*/) {
    if (buffer_container == MEM_NULL) {
        throw std::runtime_error("RTCPUInFlightBufferAcquireVK: source resource has not been bound.");
    }

    Ref<ROBufferVK> buffer = buffer_container->get(render_data->current_frame_slot).dynamic_cast_ref<ROBufferVK>();
    if (buffer == MEM_NULL) {
        throw std::runtime_error("RTCPUInFlightBufferAcquireVK: buffer resource is unavailable.");
    }
    const size_t buffer_size = buffer->fields().buffer_size;

    const size_t staging_buffer_size = ROStagingBufferVK::required_buffer_size(buffer_size);
    Ref<ROStagingBufferVK> staging_buffer =
        render_data->create_staging_buffer(ROStagingBufferVK::Builder().set_size(staging_buffer_size).set_usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    const ROStagingBufferVK::Allocation staging_allocation = staging_buffer->allocate_region(buffer_size);
    const VkBufferCopy copy_region{
        .srcOffset = 0,
        .dstOffset = staging_allocation.offset,
        .size = buffer_size,
    };
    vkCmdCopyBuffer(p_command_buffer, buffer->get_buffer(), staging_buffer->get_buffer(), 1, &copy_region);

    const VkBufferMemoryBarrier2 destination_barrier =
        ROBufferVK::MemoryBarrierBuilder()
            .set_buffer(staging_buffer->get_buffer(), staging_buffer->fields().buffer_size)
            .set_src_access(VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT)
            .set_dst_access(VK_ACCESS_2_HOST_READ_BIT, VK_PIPELINE_STAGE_2_HOST_BIT)
            .set_data_region(staging_allocation.offset, buffer_size)
            .build();
    VkDependencyInfo destination_dependency{};
    destination_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    destination_dependency.bufferMemoryBarrierCount = 1;
    destination_dependency.pBufferMemoryBarriers = &destination_barrier;
    vkCmdPipelineBarrier2(p_command_buffer, &destination_dependency);

    // Each acquisition owns its memory, which its event carries, so it never waits for the ones still in flight.
    AcquiredBuffer acquired_buffer{
        .frame = render_data->current_frame,
        .content = Vector<uint8_t, HeapAllocatorGlobal>(buffer_size, global_allocator()),
    };
    WBE_DEBUG_ASSERT(render_data->frames_in_flight > 0);
    // The copy belongs to the current frame slot. Reaching this callback after the other frame slots have advanced means the
    // renderer has wrapped to this slot and waited for its fence, so host reads cannot race the recorded transfer. This is one
    // frame earlier than the staging allocation's normal delayed release, keeping its mapped region reserved during the copy.
    render_data->register_delayed_call(
        [staging_buffer,
            mapped_memory = staging_allocation.data,
            buffer_size,
            acquired_buffer = std::move(acquired_buffer),
            event_manager = event_manager,
            event_id = buffer_id]() mutable {
            std::memcpy(acquired_buffer.content.data(), mapped_memory, buffer_size);
            event_manager->emit(event_id, acquired_buffer);
        },
        render_data->frames_in_flight - 1);
}

} // namespace WhiteBirdEngine
