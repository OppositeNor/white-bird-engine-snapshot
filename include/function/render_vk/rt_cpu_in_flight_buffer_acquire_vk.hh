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
#ifndef WBE_FILE_RT_CPU_IN_FLIGHT_BUFFER_ACQUIRE_VK_HH
#define WBE_FILE_RT_CPU_IN_FLIGHT_BUFFER_ACQUIRE_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/engine_core.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/event/event_manager.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/rt_params_cpu_in_flight_buffer_acquire_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include <cstdint>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTCPUInFlightBufferAcquireVK
 * @brief Starts an asynchronous CPU acquisition of a graph buffer on every recording.
 *
 * Every recording starts an independent acquisition into memory it allocates itself, and emits the completed result as an
 * AcquiredBuffer payload through the EventManager, using the source buffer resource id as the event id. Subscribe with
 * `subscribe<RTCPUInFlightBufferAcquireVK::AcquiredBuffer>(buffer_id, ...)`. Acquisitions never share memory, so consecutive recordings may be in flight
 * together. Wrap in RTTriggeredVK for on-demand acquisition. Instances are neither copyable nor movable.
 */
class WBE_META(WBE_RENDER_TASK, WBE_SERIALIZATION_ALIASES(cpu_acquire_buffer)) RTCPUInFlightBufferAcquireVK final : public IRenderTaskVK {
public:
    /**
     * @struct AcquiredBuffer
     * @brief Event payload of one completed acquisition. Movable; copying duplicates the content.
     *
     * Emitted on the rendering thread once the frame slot that recorded the copy is reused, so subscribers must not block
     * it; hand long work to a worker. An exception thrown by a subscriber propagates to the renderer.
     */
    struct AcquiredBuffer {
        /**
         * @brief The renderer's current frame when the acquisition was recorded.
         */
        uint64_t frame = 0;
        /**
         * @brief The complete buffer contents. Allocated from the global allocator.
         */
        Vector<uint8_t, HeapAllocatorGlobal> content;
    };

    RTCPUInFlightBufferAcquireVK() = delete;
    virtual ~RTCPUInFlightBufferAcquireVK() override = default;
    RTCPUInFlightBufferAcquireVK(const RTCPUInFlightBufferAcquireVK&) = delete;
    RTCPUInFlightBufferAcquireVK(RTCPUInFlightBufferAcquireVK&&) noexcept = delete;
    RTCPUInFlightBufferAcquireVK& operator=(const RTCPUInFlightBufferAcquireVK&) = delete;
    RTCPUInFlightBufferAcquireVK& operator=(RTCPUInFlightBufferAcquireVK&&) noexcept = delete;

    /**
     * @brief Construct an in-flight CPU buffer-acquisition task.
     *
     * The source buffer must have transfer-source usage. The task may be destroyed while acquisitions are in flight; their
     * events are still emitted. The EventManager must outlive every acquisition the task starts.
     *
     * @param p_render_data Base Vulkan render data.
     * @param p_buffer_id Render-graph resource id of the source buffer; it is also the event id of every acquisition.
     * @throws std::runtime_error If the render data is unavailable, the id is empty, or no EventManager exists.
     */
    RTCPUInFlightBufferAcquireVK(const RenderContextVK* p_render_data, std::string_view p_buffer_id);
    /**
     * @brief Create the task from recipe parameters; used by RenderTaskRegistryVK.
     * @param p_render_data Base Vulkan render data.
     * @param p_params The recipe parameters.
     * @param p_context The graph context the task is created for.
     * @return The task.
     * @throws std::runtime_error Under the conditions of the constructor.
     */
    WBE_META(WBE_REFLECT)
    static Unique<IRenderTaskVK> create(const RenderContextVK* p_render_data, const RTParamsCPUInFlightBufferAcquireVK& p_params, const RendGraphContextVK& p_context);

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;

    virtual void bind_resources(const RendResViewVK& p_resources) override;

    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    const RenderContextVK* render_data = nullptr;
    std::string buffer_id;
    EventManager* event_manager = nullptr;
    Ref<RenderObjectContainerVK> buffer_container;
};

} // namespace WhiteBirdEngine

#endif
