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
#include "function/render_vk/rt_toggle_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/unique.hh"
#include "function/event/event_manager.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

RTToggleVK::RTToggleVK(Unique<IRenderTaskVK> p_task, bool p_enabled) : task(std::move(p_task)), enabled(p_enabled) {
    if (task == MEM_NULL) {
        throw std::runtime_error("RTToggleVK: wrapped task must not be null.");
    }
}

RTToggleVK::RTToggleVK(Unique<IRenderTaskVK> p_task, std::string_view p_event_id, bool p_enabled)
    : RTToggleVK(std::move(p_task), p_enabled) {
    if (p_event_id.empty()) {
        throw std::runtime_error("RTToggleVK: event id is empty.");
    }
    EventManager* event_manager = EventManager::get_singleton();
    if (event_manager == nullptr) {
        throw std::runtime_error("RTToggleVK: no EventManager exists to receive toggle commands from.");
    }
    command_registration = event_manager->subscribe<Command>(p_event_id, [this](const Command& p_command) {
        set_enabled(p_command.enabled);
    });
}

RTToggleVK::RTToggleVK(Unique<IRenderTaskVK> p_task, const char* p_event_id, bool p_enabled)
    : RTToggleVK(std::move(p_task),
          p_event_id != nullptr ? std::string_view(p_event_id) : throw std::runtime_error("RTToggleVK: event id is null."),
          p_enabled) {
}

RendTaskFactoryVK RTToggleVK::wrap_factory(
    RendTaskFactoryVK p_factory, std::string p_event_id, const RenderContextVK* p_render_data, bool p_enabled) {
    if (!p_factory) {
        throw std::runtime_error("RTToggleVK: the wrapped task factory is empty.");
    }
    if (p_event_id.empty()) {
        throw std::runtime_error("RTToggleVK: event id is empty.");
    }
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error("RTToggleVK: render data is unavailable.");
    }
    if (EventManager::get_singleton() == nullptr) {
        throw std::runtime_error("RTToggleVK: no EventManager exists to receive toggle commands from.");
    }
    return [factory = std::move(p_factory), event_id = std::move(p_event_id), p_render_data, p_enabled](
               const RendGraphContextVK& p_context) -> Unique<IRenderTaskVK> {
        return make_unique<RTToggleVK>(p_render_data->allocator, factory(p_context), std::string_view(event_id), p_enabled);
    };
}

QueueFamilyVK RTToggleVK::queue_family() const {
    return task->queue_family();
}

void RTToggleVK::append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    task->append_accesses(p_reads, p_writes);
}

void RTToggleVK::bind_resources(const RendResViewVK& p_resources) {
    task->bind_resources(p_resources);
}

void RTToggleVK::record(VkCommandBuffer p_command_buffer, double p_delta) {
    if (is_enabled()) {
        task->record(p_command_buffer, p_delta);
    }
}

} // namespace WhiteBirdEngine
