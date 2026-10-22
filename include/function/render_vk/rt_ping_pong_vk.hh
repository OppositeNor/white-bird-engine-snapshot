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
#ifndef WBE_FILE_RT_PING_PONG_VK_HH
#define WBE_FILE_RT_PING_PONG_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/pipeline_descriptor_bindings_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/rt_params_ping_pong_vk.hh"
#include <cstdint>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTPingPongVK
 * @brief Render task that alternates between two compute pipelines for a fixed number of iterations.
 *
 * A false state selects the ping pipeline and a true state selects the pong pipeline. The task reports the reflected shader
 * accesses of every pipeline; map reflected resource ids to graph resources with RendTaskDescVK::alias(). Consecutive
 * dispatches are separated by a barrier that makes storage writes visible to sampled and storage reads.
 */
struct WBE_META(WBE_RENDER_TASK(ping_pong), WBE_SERIALIZATION_ALIASES(ping_pong)) RTPingPongVK final : public IRenderTaskVK {
    RTPingPongVK() = delete;
    virtual ~RTPingPongVK() override = default;
    RTPingPongVK(const RTPingPongVK&) = delete;
    RTPingPongVK(RTPingPongVK&&) noexcept = delete;
    RTPingPongVK& operator=(const RTPingPongVK&) = delete;
    RTPingPongVK& operator=(RTPingPongVK&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * @param p_render_data Base render data.
     * @param p_ping_pipeline Compute pipeline selected by a false state.
     * @param p_pong_pipeline Compute pipeline selected by a true state.
     * @param p_initial_state Selection used by the first iteration; false selects ping and true selects pong.
     * @param p_iterations Number of alternating pipeline dispatches recorded by the task.
     * @param p_dispatch_group_count Workgroup counts passed to vkCmdDispatch.
     * @param p_queue_family Queue used to submit the compute dispatch: GRAPHICS, COMPUTE_1, or COMPUTE_2.
     */
    RTPingPongVK(const RenderContextVK* p_render_data,
        Ref<ROComputePipelineVK> p_ping_pipeline,
        Ref<ROComputePipelineVK> p_pong_pipeline,
        bool p_initial_state,
        uint32_t p_iterations,
        VkExtent3D p_dispatch_group_count,
        QueueFamilyVK p_queue_family = QueueFamilyVK::GRAPHICS);

    /**
     * @brief Configure one preparatory compute dispatch before each ping or pong dispatch.
     *
     * This supports algorithms that must clear the destination state before each alternating iteration. Both prefix
     * pipelines must be provided and this method must be called before render-graph resource binding.
     *
     * @param p_ping_prefix_pipeline Pipeline recorded before every ping dispatch.
     * @param p_pong_prefix_pipeline Pipeline recorded before every pong dispatch.
     * @param p_dispatch_group_count Workgroup counts used by both prefix pipelines.
     */
    void set_iteration_prefixes(Ref<ROComputePipelineVK> p_ping_prefix_pipeline,
        Ref<ROComputePipelineVK> p_pong_prefix_pipeline,
        VkExtent3D p_dispatch_group_count);

    /**
     * @brief Set a custom WBE_PUSH_CONSTANT value for every pipeline that declares the identifier.
     *
     * The value is pushed with every later dispatch. Call on the rendering thread; values set before graph construction are
     * retained. Prefix pipelines
     * configured later by set_iteration_prefixes() do not receive earlier values.
     *
     * @param p_name Custom push-constant identifier.
     * @param p_value Value whose type must match each reflected field.
     * @throws std::runtime_error If no pipeline declares the identifier or a field type does not match.
     */
    void set_push_constant(std::string_view p_name, PushConstantValueVK p_value);

    /**
     * @brief Create the task from recipe parameters; used by RenderTaskRegistryVK.
     * @param p_render_data Base render data whose RenderDatabase holds the compiled pipelines.
     * @param p_params The recipe parameters.
     * @param p_context The graph context the task is created for.
     * @return The task.
     * @throws std::runtime_error If the render data or its RenderDatabase is unavailable, only one prefix pipeline is set,
     * or no compute pipeline is compiled under a pipeline key. The constructor's own validation also applies.
     */
    WBE_META(WBE_REFLECT)
    static Unique<IRenderTaskVK> create(
        const RenderContextVK* p_render_data, const RTParamsPingPongVK& p_params, const RendGraphContextVK& p_context);

    virtual QueueFamilyVK queue_family() const override {
        return queue_family_id;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;

    virtual void bind_resources(const RendResViewVK& p_resources) override;

    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    const RenderContextVK* render_data = nullptr;
    QueueFamilyVK queue_family_id = QueueFamilyVK::GRAPHICS;
    Ref<ROComputePipelineVK> ping_pipeline;
    Ref<ROComputePipelineVK> pong_pipeline;
    Unique<PipelineDescriptorBindingsVK> ping_descriptor_bindings;
    Unique<PipelineDescriptorBindingsVK> pong_descriptor_bindings;
    Ref<ROComputePipelineVK> ping_prefix_pipeline;
    Ref<ROComputePipelineVK> pong_prefix_pipeline;
    Unique<PipelineDescriptorBindingsVK> ping_prefix_descriptor_bindings;
    Unique<PipelineDescriptorBindingsVK> pong_prefix_descriptor_bindings;
    VkExtent3D dispatch_group_count{.width = 1, .height = 1, .depth = 1};
    VkExtent3D prefix_dispatch_group_count{.width = 1, .height = 1, .depth = 1};
    bool resources_bound = false;
    bool initial_state = false;
    uint32_t iterations = 0;
};

} // namespace WhiteBirdEngine

#endif
