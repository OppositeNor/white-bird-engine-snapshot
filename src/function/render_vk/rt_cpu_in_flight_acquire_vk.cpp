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
#include "function/render_vk/rt_cpu_in_flight_acquire_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/async/worker.hh"
#include "core/engine_core.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/event/event_manager.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/defs.hh"
#include <chrono>
#include <cstdint>
#include <future>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

// The member containers allocate from the render data, so it must be validated before they are constructed.
RenderAllocator* get_render_data_allocator(const RenderContextVK* p_render_data) {
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error("RTCPUInFlightAcquireVK: render data is unavailable.");
    }
    return p_render_data->allocator;
}

} // namespace

RTCPUInFlightAcquireVK::RTCPUInFlightAcquireVK(const RenderContextVK* p_render_data, std::string_view p_image_view_id)
    : render_data(p_render_data), image_view_id(p_image_view_id), event_manager(EventManager::get_singleton()),
      pending_acquisitions(get_render_data_allocator(p_render_data)) {
    if (image_view_id.empty()) {
        throw std::runtime_error("RTCPUInFlightAcquireVK: image-view resource id is empty.");
    }
    if (event_manager == nullptr) {
        throw std::runtime_error("RTCPUInFlightAcquireVK: no EventManager exists to emit acquisitions through.");
    }
}

Unique<IRenderTaskVK> RTCPUInFlightAcquireVK::create(
    const RenderContextVK* p_render_data, const RTParamsCPUInFlightAcquireVK& p_params, const RendGraphContextVK& /*p_context*/) {
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error("RTCPUInFlightAcquireVK: render data is unavailable.");
    }
    return make_unique<RTCPUInFlightAcquireVK>(p_render_data->allocator, p_render_data, p_params.image_view);
}

void RTCPUInFlightAcquireVK::append_accesses(
    Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    static_cast<void>(p_writes);
    p_reads.push_back({.res_id = image_view_id, .usage = RendResUsageVK::TRANSFER_SRC});
}

void RTCPUInFlightAcquireVK::bind_resources(const RendResViewVK& p_resources) {
    image_view_container = p_resources.get(image_view_id).container;
    if (image_view_container == MEM_NULL || image_view_container->get_resource_type_id() != TypeUUID<ROImageViewVK>::UUID) {
        throw std::runtime_error("RTCPUInFlightAcquireVK: image-view resource has the wrong type.");
    }
}

void RTCPUInFlightAcquireVK::record(VkCommandBuffer /*p_command_buffer*/, double /*p_delta*/) {
    // Observe completed acquisitions so asynchronous failures follow the engine's crash-on-failure convention.
    std::erase_if(pending_acquisitions, [](std::future<void>& p_acquisition) {
        if (p_acquisition.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return false;
        }
        p_acquisition.get();
        return true;
    });

    if (image_view_container == MEM_NULL) {
        throw std::runtime_error("RTCPUInFlightAcquireVK: source resource has not been bound.");
    }

    Ref<ROImageViewVK> image_view = image_view_container->get(render_data->current_frame_slot).dynamic_cast_ref<ROImageViewVK>();
    if (image_view == MEM_NULL || image_view->fields().image == MEM_NULL) {
        throw std::runtime_error("RTCPUInFlightAcquireVK: image-view resource is unavailable.");
    }
    const Ref<ROImageVK>& image = image_view->fields().image;

    // Each acquisition owns its memory, which its event carries, so it never waits for the ones still in flight.
    AcquiredImage acquired_image{
        .frame = render_data->current_frame,
        .format = image->fields().format,
        .extent = image->fields().extent,
        .content = Vector<uint8_t, HeapAllocatorGlobal>(image->get_content_size(), global_allocator()),
    };
    // The image schedules the GPU copy and worker-side population without blocking graph recording. Moving the content
    // into the continuation keeps its storage, so the population target stays valid.
    std::future<void> acquire_future = image->acquire_image_content(std::span<uint8_t>(acquired_image.content));
    Worker* worker = Worker::get_singleton();
    WBE_DEBUG_ASSERT(worker != nullptr);
    pending_acquisitions.push_back(worker->submit_task(
        [acquire_future = std::move(acquire_future),
            acquired_image = std::move(acquired_image),
            event_manager = event_manager,
            event_id = image_view_id]() mutable {
            acquire_future.get();
            event_manager->emit(event_id, acquired_image);
        }));
}

} // namespace WhiteBirdEngine
