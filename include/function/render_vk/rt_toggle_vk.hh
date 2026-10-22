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
#ifndef WBE_FILE_RT_TOGGLE_VK_HH
#define WBE_FILE_RT_TOGGLE_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/unique.hh"
#include "function/event/event_registration.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include <atomic>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTToggleVK
 * @brief Owns a task and records it on every frame while it is enabled. Neither copyable nor movable.
 * @details Binding, queue selection and resource declarations always reach the child. Disabled frames record no child
 * commands; the graph still submits its synchronization and any required layout transitions. Unlike RTTriggeredVK, the
 * state persists until it is changed instead of counting down.
 *
 * A task constructed with an event id is controlled through the EventManager: emit a Command under that id, from any
 * thread, instead of holding the task. Several tasks may share one id, for example every task of an effect, so one command
 * switches them together; emit from the rendering thread between frames so they observe it on the same recording. A
 * recreated task, for example after a graph rebuild, starts in the state it is constructed with, so pass the current state
 * when wrapping. set_enabled() and is_enabled() are atomic and may also be called from any thread. Every other method
 * follows the render-thread confinement of render tasks.
 */
class RTToggleVK final : public IRenderTaskVK {
public:
    /**
     * @struct Command
     * @brief Event payload that sets the state of every RTToggleVK subscribed to the event id it is emitted under.
     * Copyable.
     */
    struct Command {
        /**
         * @brief Whether the tasks record their child.
         */
        bool enabled = false;
        /**
         * @brief Make a command that enables the tasks.
         */
        static Command enable() {
            return Command{.enabled = true};
        }
        /**
         * @brief Make a command that disables the tasks.
         */
        static Command disable() {
            return Command{.enabled = false};
        }
    };

    RTToggleVK() = delete;
    virtual ~RTToggleVK() override = default;
    RTToggleVK(const RTToggleVK&) = delete;
    RTToggleVK(RTToggleVK&&) noexcept = delete;
    RTToggleVK& operator=(const RTToggleVK&) = delete;
    RTToggleVK& operator=(RTToggleVK&&) noexcept = delete;

    /**
     * @brief Take ownership of a non-null child.
     * @param p_task Task owned exclusively by this wrapper; null throws.
     * @param p_enabled Whether the child records until the state is changed.
     */
    explicit RTToggleVK(Unique<IRenderTaskVK> p_task, bool p_enabled = false);

    /**
     * @brief Take ownership of a non-null child and subscribe to Command events under p_event_id.
     *
     * The subscription ends when the task is destroyed.
     * @param p_task Task owned exclusively by this wrapper; null throws.
     * @param p_event_id Event id the task receives Command events under; non-empty.
     * @param p_enabled Whether the child records until a command changes the state.
     * @throws std::runtime_error If p_task is null, p_event_id is empty, or no EventManager exists.
     */
    RTToggleVK(Unique<IRenderTaskVK> p_task, std::string_view p_event_id, bool p_enabled = false);

    /**
     * @brief Event-controlled constructor for string literals, which would otherwise convert to the bool p_enabled.
     * @throws std::runtime_error If p_event_id is null, or under the conditions of the std::string_view overload.
     */
    RTToggleVK(Unique<IRenderTaskVK> p_task, const char* p_event_id, bool p_enabled = false);

    /**
     * @brief Wrap a task factory so every task it creates is an event-controlled RTToggleVK.
     *
     * The returned factory calls p_factory with the graph context and wraps its task with the event-id constructor. Every
     * evaluation, for example after a resize, creates a new wrapper in state p_enabled that subscribes again.
     * @param p_factory Factory of the wrapped task; empty throws.
     * @param p_event_id Event id every created wrapper receives Command events under; non-empty.
     * @param p_render_data Render data whose allocator owns the wrappers; it must outlive the returned factory.
     * @param p_enabled The state every created wrapper starts in.
     * @return The wrapping factory, suitable for RendTaskDescVK::factory.
     * @throws std::runtime_error If p_factory is empty, p_event_id is empty, the render data or its allocator is null, or no
     * EventManager exists.
     */
    static RendTaskFactoryVK wrap_factory(
        RendTaskFactoryVK p_factory, std::string p_event_id, const RenderContextVK* p_render_data, bool p_enabled = false);

    /**
     * @brief Set whether the child records, from the next graph recording on.
     */
    void set_enabled(bool p_enabled) {
        enabled.store(p_enabled, std::memory_order_release);
    }

    /**
     * @brief Get whether the child records.
     */
    bool is_enabled() const {
        return enabled.load(std::memory_order_acquire);
    }

    virtual QueueFamilyVK queue_family() const override;
    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;
    virtual void bind_resources(const RendResViewVK& p_resources) override;
    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    Unique<IRenderTaskVK> task;
    std::atomic<bool> enabled;
    // Declared last, so the subscription ends before the members its callback uses are destroyed.
    EventRegistration command_registration;
};

} // namespace WhiteBirdEngine

#endif
