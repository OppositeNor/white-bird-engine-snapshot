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
#ifndef WBE_FILE_RT_BUILD_ACCELERATION_STRUCTURES_VK_HH
#define WBE_FILE_RT_BUILD_ACCELERATION_STRUCTURES_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_context_vk.hh"

#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTBuildAccelerationStructuresVK
 * @brief Render task that records one prepared acceleration-structure build sequence.
 */
class RTBuildAccelerationStructuresVK final : public IRenderTaskVK {
public:
    RTBuildAccelerationStructuresVK() = delete;
    virtual ~RTBuildAccelerationStructuresVK() override = default;
    RTBuildAccelerationStructuresVK(const RTBuildAccelerationStructuresVK&) = delete;
    RTBuildAccelerationStructuresVK(RTBuildAccelerationStructuresVK&&) noexcept = delete;
    RTBuildAccelerationStructuresVK& operator=(const RTBuildAccelerationStructuresVK&) = delete;
    RTBuildAccelerationStructuresVK& operator=(RTBuildAccelerationStructuresVK&&) noexcept = delete;

    RTBuildAccelerationStructuresVK(
        const RenderContextVK* p_render_data, std::string_view p_acceleration_structure_resource_id, GPUJobVK p_record_job);

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;

    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    const RenderContextVK* render_data = nullptr;
    std::string acceleration_structure_resource_id;
    GPUJobVK record_job;
};

} // namespace WhiteBirdEngine

#endif
