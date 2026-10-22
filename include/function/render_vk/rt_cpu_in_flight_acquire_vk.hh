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
#ifndef WBE_FILE_RT_CPU_IN_FLIGHT_ACQUIRE_VK_HH
#define WBE_FILE_RT_CPU_IN_FLIGHT_ACQUIRE_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/engine_core.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/event/event_manager.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/rt_params_cpu_in_flight_acquire_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include <cstdint>
#include <future>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTCPUInFlightAcquireVK
 * @brief Starts an asynchronous CPU acquisition of a graph image on every recording.
 *
 * Every recording starts an independent acquisition into memory it allocates itself, and emits the completed result as an
 * AcquiredImage payload through the EventManager, using the source image-view resource id as the event id. Subscribe with
 * `subscribe<RTCPUInFlightAcquireVK::AcquiredImage>(image_view_id, ...)`. Acquisitions never share memory, so consecutive recordings may be in flight together;
 * wrap in RTTriggeredVK for on-demand acquisition, and use RTTriggeredVK::enabled_for() to acquire a frame sequence.
 * Instances are neither copyable nor movable.
 */
class WBE_META(WBE_RENDER_TASK(cpu_acquire_image), WBE_SERIALIZATION_ALIASES(cpu_acquire_image)) RTCPUInFlightAcquireVK final
    : public IRenderTaskVK {
public:
    /**
     * @struct AcquiredImage
     * @brief Event payload of one completed acquisition. Movable; copying duplicates the content.
     *
     * Emitted on an engine worker thread. Events of different acquisitions may be emitted concurrently and out of recording
     * order; order them by frame. An exception thrown by a subscriber is rethrown by a later recording of the task.
     */
    struct AcquiredImage {
        /**
         * @brief The renderer's current frame when the acquisition was recorded. Consecutive recorded frames have
         * consecutive values.
         */
        uint64_t frame = 0;
        /**
         * @brief The format of the source image.
         */
        VkFormat format = VK_FORMAT_UNDEFINED;
        /**
         * @brief The extent of the source image's first mip level.
         */
        VkExtent3D extent{};
        /**
         * @brief The tightly packed image bytes, laid out as ROImageVK::acquire_image_content() describes. Allocated from
         * the global allocator.
         */
        Vector<uint8_t, HeapAllocatorGlobal> content;
    };


    RTCPUInFlightAcquireVK() = delete;
    virtual ~RTCPUInFlightAcquireVK() override = default;
    RTCPUInFlightAcquireVK(const RTCPUInFlightAcquireVK&) = delete;
    RTCPUInFlightAcquireVK(RTCPUInFlightAcquireVK&&) noexcept = delete;
    RTCPUInFlightAcquireVK& operator=(const RTCPUInFlightAcquireVK&) = delete;
    RTCPUInFlightAcquireVK& operator=(RTCPUInFlightAcquireVK&&) noexcept = delete;

    /**
     * @brief Construct an in-flight CPU image-acquisition task.
     *
     * The task may be destroyed while acquisitions are in flight; their events are still emitted. The EventManager must
     * outlive every acquisition the task starts.
     *
     * @param p_render_data Base Vulkan render data.
     * @param p_image_view_id Render-graph resource id of the source image view; it is also the event id of every acquisition.
     * @throws std::runtime_error If the render data is unavailable, the id is empty, or no EventManager exists.
     */
    RTCPUInFlightAcquireVK(const RenderContextVK* p_render_data, std::string_view p_image_view_id);
    /**
     * @brief Create the task from recipe parameters; used by RenderTaskRegistryVK.
     * @param p_render_data Base Vulkan render data.
     * @param p_params The recipe parameters.
     * @param p_context The graph context the task is created for.
     * @return The task.
     * @throws std::runtime_error Under the conditions of the constructor.
     */
    WBE_META(WBE_REFLECT)
    static Unique<IRenderTaskVK> create(const RenderContextVK* p_render_data, const RTParamsCPUInFlightAcquireVK& p_params, const RendGraphContextVK& p_context);

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;

    virtual void bind_resources(const RendResViewVK& p_resources) override;

    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    const RenderContextVK* render_data = nullptr;
    std::string image_view_id;
    EventManager* event_manager = nullptr;
    Ref<RenderObjectContainerVK> image_view_container;
    Vector<std::future<void>, RenderAllocator> pending_acquisitions;
};

} // namespace WhiteBirdEngine

#endif
