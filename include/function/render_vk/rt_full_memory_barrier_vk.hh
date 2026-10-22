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
#ifndef WBE_FILE_RT_FULL_MEMORY_BARRIER_VK_HH
#define WBE_FILE_RT_FULL_MEMORY_BARRIER_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/rt_params_full_memory_barrier_vk.hh"
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTFullMemoryBarrierVK
 * @brief Records a full memory barrier on the graphics queue, ordering every earlier command before every later one.
 * @details Reports an all-commands write of one resource, which the task the barrier must precede is connected to. Use it
 * to order a frame's mutation of persistent state after the previous frame's consumers, which the graph does not see as
 * edges. Neither copyable nor movable; confined to the rendering thread.
 */
class WBE_META(WBE_RENDER_TASK(full_memory_barrier), WBE_SERIALIZATION_ALIASES(full_memory_barrier))
    RTFullMemoryBarrierVK final : public IRenderTaskVK {
public:
    RTFullMemoryBarrierVK() = delete;
    /**
     * @brief Create the barrier.
     * @param p_resource_id Graph resource carrying the edge to the task ordered after the barrier.
     * @throws std::runtime_error If p_resource_id is empty.
     */
    explicit RTFullMemoryBarrierVK(std::string_view p_resource_id);
    virtual ~RTFullMemoryBarrierVK() override = default;
    RTFullMemoryBarrierVK(const RTFullMemoryBarrierVK&) = delete;
    RTFullMemoryBarrierVK(RTFullMemoryBarrierVK&&) noexcept = delete;
    RTFullMemoryBarrierVK& operator=(const RTFullMemoryBarrierVK&) = delete;
    RTFullMemoryBarrierVK& operator=(RTFullMemoryBarrierVK&&) noexcept = delete;

    /**
     * @brief Create the task from recipe parameters; used by RenderTaskRegistryVK.
     * @throws std::runtime_error If the render data is unavailable, or under the conditions of the constructor.
     */
    WBE_META(WBE_REFLECT)
    static Unique<IRenderTaskVK> create(
        const RenderContextVK* p_render_data, const RTParamsFullMemoryBarrierVK& p_params, const RendGraphContextVK& p_context);

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }
    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;
    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    std::string resource_id;
};

} // namespace WhiteBirdEngine

#endif
