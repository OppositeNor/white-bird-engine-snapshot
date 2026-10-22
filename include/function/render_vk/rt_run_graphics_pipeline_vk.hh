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
#ifndef WBE_FILE_RT_RUN_GRAPHICS_PIPELINE_VK_HH
#define WBE_FILE_RT_RUN_GRAPHICS_PIPELINE_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/pipeline_descriptor_bindings_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_targets_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/rt_params_run_graphics_pipeline_vk.hh"
#include "function/render_vk/vertex_input_description_vk.hh"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

struct IndexedDrawRegionVK {
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    int32_t vertex_offset = 0;
};

/**
 * @struct VertexBufferSourceVK
 * @brief A task-local vertex buffer resource and the byte offset bound for it. Copyable; the id must outlive the call it is
 * passed to.
 */
struct VertexBufferSourceVK {
    /** Task-local vertex buffer resource id. */
    std::string_view resource_id;
    /** Byte offset bound for the buffer. */
    VkDeviceSize offset = 0;
};

/**
 * @class RTRunGraphicsPipelineVK
 * @brief Render task that runs one graphics pipeline draw.
 *
 * The task renders into `render_target_colors[<index>]` and `render_target_depth` following the pipeline's attachment
 * layout, and reports those accesses, its vertex and index buffers, and its reflected shader resources. Map them to graph
 * resources with RendTaskDescVK::alias().
 */
struct WBE_META(WBE_RENDER_TASK(draw), WBE_SERIALIZATION_ALIASES(run_graphics_pipeline)) RTRunGraphicsPipelineVK final : public IRenderTaskVK {
    /**
     * @class Builder
     * @brief Configures a graphics draw. Copyable and movable; its allocator must outlive every copy.
     */
    struct Builder {
        Builder() = delete;
        ~Builder() = default;
        Builder(const Builder&) = default;
        Builder(Builder&&) noexcept = default;
        Builder& operator=(const Builder&) = default;
        Builder& operator=(Builder&&) noexcept = default;

        /**
         * @brief Construct allocator-owned builder storage.
         */
        explicit Builder(RenderAllocator* p_allocator);

        /**
         * @brief Set the graphics pipeline retained by the task.
         */
        Builder& set_pipeline(Ref<ROGraphicsPipelineVK> p_pipeline);

        /**
         * @brief Set the dynamic-rendering area.
         */
        Builder& set_render_extent(VkExtent2D p_render_extent);

        /**
         * @brief Set clear values for the color targets followed by the depth target. Empty clears to zero.
         */
        Builder& set_clear_values(std::span<const VkClearValue> p_clear_values);

        /**
         * @brief Set the load operation shared by color targets.
         */
        Builder& set_color_load_op(VkAttachmentLoadOp p_load_op);

        /**
         * @brief Set the load operation of the depth target.
         */
        Builder& set_depth_load_op(VkAttachmentLoadOp p_load_op);

        /**
         * @brief Draw indexed vertices read from task-local vertex buffers, one per vertex-input binding.
         * @param p_vertex_buffers The buffer bound for each vertex-input binding, in binding order; there must be one per
         * binding of p_vertex_input.
         * @param p_index_buffer_id Index buffer resource.
         * @param p_vertex_input Dynamic vertex-input state.
         * @param p_indexed_draw_region Optional subregion; the whole index buffer is drawn when omitted.
         * @throws std::runtime_error If a resource id is empty or the buffer and binding counts differ.
         */
        Builder& set_indexed_draw(std::span<const VertexBufferSourceVK> p_vertex_buffers,
            std::string_view p_index_buffer_id,
            VertexInputDescriptionVK p_vertex_input,
            std::optional<IndexedDrawRegionVK> p_indexed_draw_region = std::nullopt);

        /**
         * @brief Set the vertex count of a draw without vertex and index buffers. Defaults to three.
         */
        Builder& set_draw_vertex_count(uint32_t p_draw_vertex_count);

        /**
         * @brief Create an allocator-owned render task.
         * @throws std::runtime_error If the pipeline or render extent is missing, or clear values are too few.
         */
        Unique<RTRunGraphicsPipelineVK> create_unique(const RenderContextVK* p_render_data) const;

    private:
        friend RTRunGraphicsPipelineVK;

        Ref<ROGraphicsPipelineVK> pipeline;
        VkExtent2D render_extent{};
        Vector<VkClearValue, RenderAllocator> clear_values;
        VkAttachmentLoadOp color_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentLoadOp depth_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
        std::vector<std::string> vertex_buffer_ids;
        std::vector<VkDeviceSize> vertex_buffer_offsets;
        std::optional<std::string> index_buffer_id;
        std::optional<IndexedDrawRegionVK> indexed_draw_region;
        VertexInputDescriptionVK vertex_input;
        uint32_t draw_vertex_count = 3;
    };

    RTRunGraphicsPipelineVK() = delete;
    virtual ~RTRunGraphicsPipelineVK() override = default;
    RTRunGraphicsPipelineVK(const RTRunGraphicsPipelineVK&) = delete;
    RTRunGraphicsPipelineVK(RTRunGraphicsPipelineVK&&) noexcept = delete;
    RTRunGraphicsPipelineVK& operator=(const RTRunGraphicsPipelineVK&) = delete;
    RTRunGraphicsPipelineVK& operator=(RTRunGraphicsPipelineVK&&) noexcept = delete;

    /**
     * @brief Construct a task from a complete builder configuration.
     */
    RTRunGraphicsPipelineVK(const RenderContextVK* p_render_data, const Builder& p_builder);

    /**
     * @brief Create the task from recipe parameters; used by RenderTaskRegistryVK.
     * @param p_render_data Base render data whose RenderDatabase holds the compiled pipelines.
     * @param p_params The recipe parameters.
     * @param p_context The graph context the task is created for.
     * @return The task.
     * @throws std::runtime_error If the render data or its RenderDatabase is unavailable, no graphics pipeline is compiled under p_params.pipeline, or the render extent or a clear value is invalid. The constructor's own
     * validation also applies.
     */
    WBE_META(WBE_REFLECT)
    static Unique<IRenderTaskVK> create(
        const RenderContextVK* p_render_data, const RTParamsRunGraphicsPipelineVK& p_params, const RendGraphContextVK& p_context);

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void bind_resources(const RendResViewVK& p_resources) override;

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;

    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    const RenderContextVK* render_data = nullptr;
    VkExtent2D render_extent{};
    Ref<ROGraphicsPipelineVK> graphics_pipeline;
    RenderTargetsVK render_targets;
    Unique<PipelineDescriptorBindingsVK> descriptor_bindings;
    std::vector<std::string> vertex_buffer_ids;
    std::vector<VkDeviceSize> vertex_buffer_offsets;
    std::optional<std::string> index_buffer_id;
    std::vector<Ref<RenderObjectContainerVK>> vertex_buffer_containers;
    std::vector<VkBuffer> bound_vertex_buffers;
    Ref<RenderObjectContainerVK> index_buffer_container;
    std::optional<IndexedDrawRegionVK> indexed_draw_region;
    VertexInputDescriptionVK vertex_input;
    uint32_t draw_vertex_count = 3;
};

} // namespace WhiteBirdEngine

#endif
