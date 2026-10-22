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
#ifndef WBE_FILE_RT_TRIGGERED_VK_HH
#define WBE_FILE_RT_TRIGGERED_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/unique.hh"
#include "function/event/event_registration.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTTriggeredVK
 * @brief Owns a task and records it while it has remaining enabled frames. Neither copyable nor movable.
 * @details Binding, initialization, queue selection and resource declarations always reach the child.
 * Inactive frames record no child commands; the graph still submits its synchronization and any
 * required layout transitions. Each recording of an enabled frame consumes one remaining frame before
 * recording the child, so a request made during recording applies to later recordings. Repeated triggers
 * coalesce into one frame, not a queue.
 *
 * A task constructed with an event id is controlled through the EventManager: emit a Command under that id, from any
 * thread, instead of holding the task. Several tasks may share one id, for example a present task and an acquisition task
 * that must run on the same frames; emit from the rendering thread between frames so they observe the command on the same
 * recording. trigger(), enabled_for(), and get_remaining_frames() are atomic and may also be called from any thread. Every
 * other method follows the render-thread confinement of render tasks.
 */
class RTTriggeredVK final : public IRenderTaskVK {
public:
    /**
     * @struct Command
     * @brief Event payload that controls every RTTriggeredVK subscribed to the event id it is emitted under. Copyable.
     */
    struct Command {
        /**
         * @brief What the command does.
         */
        enum class Kind : uint8_t {
            /**
             * @brief Behave like trigger().
             */
            TRIGGER,
            /**
             * @brief Behave like enabled_for(frames).
             */
            ENABLE_FOR,
        };
        /**
         * @brief What the command does.
         */
        Kind kind = Kind::TRIGGER;
        /**
         * @brief The number of frames for ENABLE_FOR; ignored by TRIGGER.
         */
        uint32_t frames = 0;

        /**
         * @brief Make a command that behaves like trigger().
         */
        static Command trigger() {
            return Command{.kind = Kind::TRIGGER, .frames = 0};
        }

        /**
         * @brief Make a command that behaves like enabled_for(p_frames).
         */
        static Command enable_for(uint32_t p_frames) {
            return Command{.kind = Kind::ENABLE_FOR, .frames = p_frames};
        }
    };

    RTTriggeredVK() = delete;
    virtual ~RTTriggeredVK() override = default;
    RTTriggeredVK(const RTTriggeredVK&) = delete;
    RTTriggeredVK(RTTriggeredVK&&) noexcept = delete;
    RTTriggeredVK& operator=(const RTTriggeredVK&) = delete;
    RTTriggeredVK& operator=(RTTriggeredVK&&) noexcept = delete;

    /**
     * @brief Take ownership of a non-null child. Starts inactive unless explicitly activated.
     * @param p_task Task owned exclusively by this wrapper; null throws.
     * @param p_activate Whether the next recording should execute the child.
     */
    explicit RTTriggeredVK(Unique<IRenderTaskVK> p_task, bool p_activate = false);
    /**
     * @brief Take ownership of a non-null child and subscribe to Command events under p_event_id.
     *
     * The subscription ends when the task is destroyed. Starts inactive unless explicitly activated.
     * @param p_task Task owned exclusively by this wrapper; null throws.
     * @param p_event_id Event id the task receives Command events under; non-empty.
     * @param p_activate Whether the next recording should execute the child.
     * @throws std::runtime_error If p_task is null, p_event_id is empty, or no EventManager exists.
     */
    RTTriggeredVK(Unique<IRenderTaskVK> p_task, std::string_view p_event_id, bool p_activate = false);
    /**
     * @brief Event-controlled constructor for string literals, which would otherwise convert to the bool p_activate.
     * @throws std::runtime_error If p_event_id is null, or under the conditions of the std::string_view overload.
     */
    RTTriggeredVK(Unique<IRenderTaskVK> p_task, const char* p_event_id, bool p_activate = false);
    /**
     * @brief Wrap a task factory so every task it creates is an event-controlled RTTriggeredVK.
     *
     * The returned factory calls p_factory with the graph context and wraps its task with the event-id constructor. Every
     * evaluation, for example after a resize, creates a new wrapper that starts in its initial state and subscribes again.
     * @param p_factory Factory of the wrapped task; empty throws.
     * @param p_event_id Event id every created wrapper receives Command events under; non-empty.
     * @param p_render_data Render data whose allocator owns the wrappers; it must outlive the returned factory.
     * @param p_activate Whether each created wrapper executes its child on its first recording.
     * @return The wrapping factory, suitable for RendTaskDescVK::factory.
     * @throws std::runtime_error If p_factory is empty, p_event_id is empty, the render data or its allocator is null, or no
     * EventManager exists.
     */
    static RendTaskFactoryVK wrap_factory(
        RendTaskFactoryVK p_factory, std::string p_event_id, const RenderContextVK* p_render_data, bool p_activate = false);

    /**
     * @brief Execute the child on its next graph recording.
     *
     * Sets the remaining frames to at least one, so a pending enabled_for() period is never shortened.
     */
    void trigger();

    /**
     * @brief Execute the child on each of its next p_frames graph recordings.
     *
     * Replaces the remaining frames, so it can extend or shorten a pending period. Zero disables the child.
     *
     * @param p_frames The number of recordings to execute the child on.
     */
    void enabled_for(uint32_t p_frames);

    /**
     * @brief Get the number of upcoming recordings that execute the child.
     *
     * @return The remaining enabled frames.
     */
    uint32_t get_remaining_frames() const {
        return remaining_frames.load(std::memory_order_acquire);
    }

    virtual QueueFamilyVK queue_family() const override;
    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;
    virtual void bind_resources(const RendResViewVK& p_resources) override;
    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    void apply(const Command& p_command);

    Unique<IRenderTaskVK> task;
    std::atomic<uint32_t> remaining_frames;
    // Declared last, so the subscription ends before the members its callback uses are destroyed.
    EventRegistration command_registration;
};

} // namespace WhiteBirdEngine

#endif
