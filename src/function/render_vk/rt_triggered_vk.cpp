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
#include "function/render_vk/rt_triggered_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/unique.hh"
#include "function/event/event_manager.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

RTTriggeredVK::RTTriggeredVK(Unique<IRenderTaskVK> p_task, bool p_activate)
    : task(std::move(p_task)), remaining_frames(p_activate ? 1U : 0U) {
    if (task == MEM_NULL) {
        throw std::runtime_error("RTTriggeredVK: wrapped task must not be null.");
    }
}

RTTriggeredVK::RTTriggeredVK(Unique<IRenderTaskVK> p_task, std::string_view p_event_id, bool p_activate)
    : RTTriggeredVK(std::move(p_task), p_activate) {
    EventManager* event_manager = EventManager::get_singleton();
    if (event_manager == nullptr) {
        throw std::runtime_error("RTTriggeredVK: no EventManager exists to receive trigger commands from.");
    }
    command_registration = event_manager->subscribe<Command>(p_event_id, [this](const Command& p_command) {
        apply(p_command);
    });
}

RTTriggeredVK::RTTriggeredVK(Unique<IRenderTaskVK> p_task, const char* p_event_id, bool p_activate)
    : RTTriggeredVK(std::move(p_task),
          p_event_id != nullptr ? std::string_view(p_event_id) : throw std::runtime_error("RTTriggeredVK: event id is null."),
          p_activate) {
}

RendTaskFactoryVK RTTriggeredVK::wrap_factory(
    RendTaskFactoryVK p_factory, std::string p_event_id, const RenderContextVK* p_render_data, bool p_activate) {
    if (!p_factory) {
        throw std::runtime_error("RTTriggeredVK: the wrapped task factory is empty.");
    }
    if (p_event_id.empty()) {
        throw std::runtime_error("RTTriggeredVK: event id is empty.");
    }
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error("RTTriggeredVK: render data is unavailable.");
    }
    if (EventManager::get_singleton() == nullptr) {
        throw std::runtime_error("RTTriggeredVK: no EventManager exists to receive trigger commands from.");
    }
    return [factory = std::move(p_factory), event_id = std::move(p_event_id), p_render_data, p_activate](
               const RendGraphContextVK& p_context) -> Unique<IRenderTaskVK> {
        return make_unique<RTTriggeredVK>(p_render_data->allocator, factory(p_context), std::string_view(event_id), p_activate);
    };
}

void RTTriggeredVK::apply(const Command& p_command) {
    switch (p_command.kind) {
    case Command::Kind::TRIGGER:
        trigger();
        return;
    case Command::Kind::ENABLE_FOR:
        enabled_for(p_command.frames);
        return;
    }
    throw std::runtime_error("RTTriggeredVK: unknown trigger command.");
}

void RTTriggeredVK::trigger() {
    uint32_t expected = 0;
    remaining_frames.compare_exchange_strong(expected, 1U, std::memory_order_acq_rel);
}

void RTTriggeredVK::enabled_for(uint32_t p_frames) {
    remaining_frames.store(p_frames, std::memory_order_release);
}

QueueFamilyVK RTTriggeredVK::queue_family() const {
    return task->queue_family();
}

void RTTriggeredVK::append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    task->append_accesses(p_reads, p_writes);
}

void RTTriggeredVK::bind_resources(const RendResViewVK& p_resources) {
    task->bind_resources(p_resources);
}

void RTTriggeredVK::record(VkCommandBuffer p_command_buffer, double p_delta) {
    uint32_t frames = remaining_frames.load(std::memory_order_acquire);
    while (frames > 0 && !remaining_frames.compare_exchange_weak(frames, frames - 1, std::memory_order_acq_rel)) {
    }
    if (frames > 0) {
        task->record(p_command_buffer, p_delta);
    }
}

} // namespace WhiteBirdEngine
