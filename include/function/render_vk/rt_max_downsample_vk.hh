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
#ifndef WBE_FILE_RT_MAX_DOWNSAMPLE_VK_HH
#define WBE_FILE_RT_MAX_DOWNSAMPLE_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/rt_params_max_downsample_vk.hh"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

class ROImageViewVK;

/**
 * @class RTMaxDownsampleVK
 * @brief Generates an image's mip chain by writing each texel as the per-channel maximum of a 2x2 region of the previous level.
 *
 * The task dispatches its compute pipeline once per generated mip level. The pipeline must be compiled from the
 * `max_downsample_shader` resource or another shader with the same push-data contract: exactly one sampled-image field
 * reflected as `max_downsample_source` and one storage-image field reflected as `max_downsample_destination`, and no
 * renderer-default fields. The task binds these logical resources itself for every level, so they are not aliased.
 *
 * The task takes one render-graph image view per mip level. Each view must cover exactly one mip level of the same image,
 * and the views must cover consecutive levels in order; the first view is only read, the last is only written, and every
 * other view is both. Destination extents follow the Vulkan mip rule max(source / 2, 1); on an odd source axis the last
 * destination texel also covers the remaining source column or row, so every source texel contributes to the next level.
 * Consecutive levels are separated by a barrier that makes the written level visible to the next dispatch. The view
 * layout is validated while recording, because the graph may reallocate the views. Instances are neither copyable nor
 * movable.
 */
struct WBE_META(WBE_RENDER_TASK, WBE_SERIALIZATION_ALIASES(max_downsample)) RTMaxDownsampleVK final : public IRenderTaskVK {
    RTMaxDownsampleVK() = delete;
    virtual ~RTMaxDownsampleVK() override = default;
    RTMaxDownsampleVK(const RTMaxDownsampleVK&) = delete;
    RTMaxDownsampleVK(RTMaxDownsampleVK&&) noexcept = delete;
    RTMaxDownsampleVK& operator=(const RTMaxDownsampleVK&) = delete;
    RTMaxDownsampleVK& operator=(RTMaxDownsampleVK&&) noexcept = delete;

    /** Reflected resource id of the sampled-image field that receives the previous mip level. */
    static constexpr std::string_view SOURCE_RESOURCE_ID = "max_downsample_source";
    /** Reflected resource id of the storage-image field that receives the generated mip level. */
    static constexpr std::string_view DESTINATION_RESOURCE_ID = "max_downsample_destination";

    /**
     * @brief Construct a max-downsample task.
     *
     * @param p_render_data Base Vulkan render data.
     * @param p_pipeline Max-downsample compute pipeline retained by the task.
     * @param p_mip_view_ids Render-graph resource ids of the single-level image views, ordered from the source level to
     * the last generated level. At least two distinct, non-empty ids are required.
     * @param p_queue_family Queue used to submit the dispatches: GRAPHICS, COMPUTE_1, or COMPUTE_2.
     * @throws std::runtime_error If the render data or pipeline is missing, the view ids are invalid, or the pipeline does
     * not have the push-data contract described by the class.
     */
    RTMaxDownsampleVK(const RenderContextVK* p_render_data,
        Ref<ROComputePipelineVK> p_pipeline,
        std::span<const std::string> p_mip_view_ids,
        QueueFamilyVK p_queue_family = QueueFamilyVK::GRAPHICS);

    /**
     * @brief Create the task from recipe parameters; used by RenderTaskRegistryVK.
     * @param p_render_data Base render data whose RenderDatabase holds the compiled pipelines.
     * @param p_params The recipe parameters.
     * @param p_context The graph context the task is created for.
     * @return The task.
     * @throws std::runtime_error If the render data or its RenderDatabase is unavailable, or no compute pipeline is compiled
     * under p_params.pipeline. The constructor's own validation also applies.
     */
    WBE_META(WBE_REFLECT)
    static Unique<IRenderTaskVK> create(
        const RenderContextVK* p_render_data, const RTParamsMaxDownsampleVK& p_params, const RendGraphContextVK& p_context);

    virtual QueueFamilyVK queue_family() const override {
        return queue_family_id;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;

    virtual void bind_resources(const RendResViewVK& p_resources) override;

    /**
     * @brief Record one dispatch per generated mip level.
     *
     * @param p_command_buffer The command buffer to record into.
     * @param p_delta The frame delta in seconds.
     * @throws std::runtime_error If the views are unbound, or do not cover consecutive single mip levels of one image.
     */
    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    const RenderContextVK* render_data = nullptr;
    QueueFamilyVK queue_family_id = QueueFamilyVK::GRAPHICS;
    Ref<ROComputePipelineVK> pipeline;
    Vector<std::string, RenderAllocator> mip_view_ids;
    Vector<Ref<RenderObjectContainerVK>, RenderAllocator> mip_view_containers;
    Vector<uint8_t, RenderAllocator> push_data;
    uint32_t source_offset = 0;
    uint32_t destination_offset = 0;

    Vector<Ref<ROImageViewVK>, RenderAllocator> resolve_mip_views() const;
    void push_level_descriptors(VkCommandBuffer p_command_buffer, const ROImageViewVK& p_source, const ROImageViewVK& p_destination);
};

} // namespace WhiteBirdEngine

#endif
