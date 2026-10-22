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
#ifndef WBE_FILE_RT_RUN_COMPUTE_PIPELINE_VK_HH
#define WBE_FILE_RT_RUN_COMPUTE_PIPELINE_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/rt_params_run_compute_pipeline_vk.hh"
#include "function/render_vk/pipeline_descriptor_bindings_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include <cstdint>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTRunComputePipelineVK
 * @brief Render task that binds a compute pipeline and dispatches workgroups.
 * The task reports its reflected shader accesses; map reflected resource ids to graph resources with RendTaskDescVK::alias().
 */
struct WBE_META(WBE_RENDER_TASK(compute), WBE_SERIALIZATION_ALIASES(run_compute_pipeline)) RTRunComputePipelineVK final
    : public IRenderTaskVK {
    RTRunComputePipelineVK() = delete;
    virtual ~RTRunComputePipelineVK() override = default;
    RTRunComputePipelineVK(const RTRunComputePipelineVK&) = delete;
    RTRunComputePipelineVK(RTRunComputePipelineVK&&) noexcept = delete;
    RTRunComputePipelineVK& operator=(const RTRunComputePipelineVK&) = delete;
    RTRunComputePipelineVK& operator=(RTRunComputePipelineVK&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * @param p_render_data Base render data.
     * @param p_pipeline Compute pipeline retained by the task.
     * @param p_dispatch_group_count Workgroup counts passed to vkCmdDispatch.
     * @param p_queue_family Queue used to submit the compute dispatch: GRAPHICS, COMPUTE_1, or COMPUTE_2.
     */
    RTRunComputePipelineVK(const RenderContextVK* p_render_data,
        Ref<ROComputePipelineVK> p_pipeline,
        VkExtent3D p_dispatch_group_count,
        QueueFamilyVK p_queue_family = QueueFamilyVK::GRAPHICS);
    /**
     * @brief Create the task from recipe parameters; used by RenderTaskRegistryVK.
     * @param p_render_data Base render data whose RenderDatabase holds the compiled pipeline.
     * @param p_params The recipe parameters.
     * @param p_context The graph context the task is created for.
     * @return The task.
     * @throws std::runtime_error If the render data or its RenderDatabase is unavailable, or no compute pipeline is compiled
     * under p_params.pipeline.
     */
    WBE_META(WBE_REFLECT)
    static Unique<IRenderTaskVK> create(
        const RenderContextVK* p_render_data, const RTParamsRunComputePipelineVK& p_params, const RendGraphContextVK& p_context);

    /**
     * @brief Set a custom WBE_PUSH_CONSTANT value pushed with every later dispatch of the pipeline.
     *
     * Call on the rendering thread; values set before graph construction are retained.
     *
     * @param p_name Custom push-constant identifier declared by the pipeline.
     * @param p_value Value whose type must match the reflected field.
     * @throws std::runtime_error If the pipeline does not declare the identifier or the type does not match.
     */
    void set_push_constant(std::string_view p_name, PushConstantValueVK p_value);

    virtual QueueFamilyVK queue_family() const override {
        return queue_family_id;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;

    virtual void bind_resources(const RendResViewVK& p_resources) override;

    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    const RenderContextVK* render_data = nullptr;
    QueueFamilyVK queue_family_id = QueueFamilyVK::GRAPHICS;
    Ref<ROComputePipelineVK> pipeline;
    Unique<PipelineDescriptorBindingsVK> descriptor_bindings;
    VkExtent3D dispatch_group_count{.width = 1, .height = 1, .depth = 1};
};

} // namespace WhiteBirdEngine

#endif
