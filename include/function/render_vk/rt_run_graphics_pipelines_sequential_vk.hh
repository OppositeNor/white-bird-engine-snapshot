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
#ifndef WBE_FILE_RT_RUN_GRAPHICS_PIPELINES_SEQUENTIAL_VK_HH
#define WBE_FILE_RT_RUN_GRAPHICS_PIPELINES_SEQUENTIAL_VK_HH

#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/pipeline_descriptor_bindings_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_targets_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/rt_run_graphics_pipeline_vk.hh"
#include "function/render_vk/vertex_input_description_vk.hh"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTRunGraphicsPipelinesSequentialVK
 * @brief Records ordered, synchronized graphics passes in one command buffer.
 *
 * Every pass renders into the task's render targets, `render_target_colors[<index>]` and `render_target_depth`, following its
 * pipeline's attachment layout. The task reports those accesses, its draw buffers, and every pass's reflected shader resources;
 * map them to graph resources with RendTaskDescVK::alias(). Passes that need different render targets belong in separate tasks.
 */
struct RTRunGraphicsPipelinesSequentialVK final : public IRenderTaskVK {
    /**
     * @class DispatchBase
     * @brief Attachment and pipeline state shared by regular and indirect dispatches.
     *
     * Dispatches are copyable and movable. Their allocator must outlive every copy.
     */
    struct DispatchBase {
        /**
         * @brief Construct an empty pass configuration using the supplied allocator.
         * @param p_allocator Allocator used by owned clear values.
         */
        explicit DispatchBase(RenderAllocator* p_allocator);
        ~DispatchBase() = default;
        DispatchBase(const DispatchBase&) = default;
        DispatchBase(DispatchBase&&) noexcept = default;
        DispatchBase& operator=(const DispatchBase&) = default;
        DispatchBase& operator=(DispatchBase&&) noexcept = default;

        /** Graphics pipeline retained by the task. */
        Ref<ROGraphicsPipelineVK> pipeline = MEM_NULL;
        /** Clear values for the color targets followed by the depth target. Empty clears to zero. */
        Vector<VkClearValue, RenderAllocator> clear_values;
        /** Render area for this pass. */
        VkExtent2D render_extent{};
        /** Load operation used by every color target. */
        VkAttachmentLoadOp color_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
        /** Load operation used by the depth target. */
        VkAttachmentLoadOp depth_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    };

    /**
     * @class RegularDispatch
     * @brief One regular vertex, indexed, or vertex-less graphics draw.
     */
    struct RegularDispatch final : public DispatchBase {
        explicit RegularDispatch(RenderAllocator* p_allocator) : DispatchBase(p_allocator) {
        }
        ~RegularDispatch() = default;
        RegularDispatch(const RegularDispatch&) = default;
        RegularDispatch(RegularDispatch&&) noexcept = default;
        RegularDispatch& operator=(const RegularDispatch&) = default;
        RegularDispatch& operator=(RegularDispatch&&) noexcept = default;

        /** The vertex-buffer graph-resource identifier bound for each vertex-input binding, parallel to vertex_input.bindings. */
        std::vector<std::string> vertex_buffer_ids;
        /** The byte offset bound for each vertex buffer, parallel to vertex_buffer_ids. */
        std::vector<VkDeviceSize> vertex_buffer_offsets;
        /** Optional index-buffer graph-resource identifier; vertex buffers and an indexed region require it. */
        std::optional<std::string> index_buffer_id;
        /** Optional indexed subregion; valid only when an index buffer is configured. */
        std::optional<IndexedDrawRegionVK> indexed_draw_region;
        /** Dynamic vertex-input state for indexed draws. */
        VertexInputDescriptionVK vertex_input;
        /** Vertex count used when no vertex and index buffers are configured. */
        uint32_t draw_vertex_count = 3;
    };

    /**
     * @class IndirectDispatch
     * @brief One indexed-indirect graphics draw range.
     */
    struct IndirectDispatch final : public DispatchBase {
        explicit IndirectDispatch(RenderAllocator* p_allocator) : DispatchBase(p_allocator) {
        }
        ~IndirectDispatch() = default;
        IndirectDispatch(const IndirectDispatch&) = default;
        IndirectDispatch(IndirectDispatch&&) noexcept = default;
        IndirectDispatch& operator=(const IndirectDispatch&) = default;
        IndirectDispatch& operator=(IndirectDispatch&&) noexcept = default;

        /** The vertex-buffer graph-resource identifier bound for each vertex-input binding, parallel to vertex_input.bindings. */
        std::vector<std::string> vertex_buffer_ids;
        /** The byte offset bound for each vertex buffer, parallel to vertex_buffer_ids. */
        std::vector<VkDeviceSize> vertex_buffer_offsets;
        /** Index-buffer graph-resource identifier. */
        std::string index_buffer_id;
        /** Indexed-indirect command-buffer graph-resource identifier. */
        std::string indirect_buffer_id;
        /** First tightly packed indirect command consumed by this dispatch. */
        uint32_t first_indirect_draw = 0;
        /** Number of tightly packed indirect commands consumed by this dispatch. */
        uint32_t indirect_draw_count = 0;
        /** Dynamic vertex-input state for indexed draws. */
        VertexInputDescriptionVK vertex_input;
    };

private:
    enum class DispatchType : uint8_t {
        REGULAR,
        INDIRECT,
    };

    struct Dispatch {
        Dispatch(RenderAllocator* p_allocator, RegularDispatch p_dispatch);
        Dispatch(RenderAllocator* p_allocator, IndirectDispatch p_dispatch);
        ~Dispatch() = default;
        Dispatch(const Dispatch&) = default;
        Dispatch(Dispatch&&) noexcept = default;
        Dispatch& operator=(const Dispatch&) = default;
        Dispatch& operator=(Dispatch&&) noexcept = default;

        DispatchBase base;
        DispatchType type = DispatchType::REGULAR;
        std::vector<std::string> vertex_buffer_ids;
        std::vector<VkDeviceSize> vertex_buffer_offsets;
        std::optional<std::string> index_buffer_id;
        std::string indirect_buffer_id;
        uint32_t first_indirect_draw = 0;
        uint32_t indirect_draw_count = 0;
        std::optional<IndexedDrawRegionVK> indexed_draw_region;
        VertexInputDescriptionVK vertex_input;
        uint32_t draw_vertex_count = 3;
    };

public:
    /**
     * @class Sequence
     * @brief Allocator-owned ordered graphics dispatches.
     */
    struct Sequence {
        explicit Sequence(RenderAllocator* p_allocator);
        ~Sequence() = default;
        Sequence(const Sequence&) = default;
        Sequence(Sequence&&) noexcept = default;
        Sequence& operator=(const Sequence&) = default;
        Sequence& operator=(Sequence&&) noexcept = default;

        /**
         * @brief Append one regular graphics dispatch.
         * @param p_dispatch Dispatch transferred into the sequence.
         * @return This sequence.
         */
        Sequence& add_dispatch(RegularDispatch p_dispatch);

        /**
         * @brief Append one indexed-indirect graphics dispatch.
         * @param p_dispatch Dispatch transferred into the sequence.
         * @return This sequence.
         */
        Sequence& add_dispatch(IndirectDispatch p_dispatch);

        /** @brief Return the number of ordered dispatches. */
        size_t get_dispatch_count() const {
            return dispatches.size();
        }

        /** @brief Return whether one ordered dispatch is indirect. */
        bool is_indirect(size_t p_index) const;

    private:
        friend struct RTRunGraphicsPipelinesSequentialVK;

        RenderAllocator* allocator = nullptr;
        Vector<Dispatch, RenderAllocator> dispatches;
    };

    RTRunGraphicsPipelinesSequentialVK() = delete;
    virtual ~RTRunGraphicsPipelinesSequentialVK() override = default;
    RTRunGraphicsPipelinesSequentialVK(const RTRunGraphicsPipelinesSequentialVK&) = delete;
    RTRunGraphicsPipelinesSequentialVK(RTRunGraphicsPipelinesSequentialVK&&) noexcept = delete;
    RTRunGraphicsPipelinesSequentialVK& operator=(const RTRunGraphicsPipelinesSequentialVK&) = delete;
    RTRunGraphicsPipelinesSequentialVK& operator=(RTRunGraphicsPipelinesSequentialVK&&) noexcept = delete;

    /**
     * @brief Construct an ordered graphics-pass task.
     * @param p_render_data Renderer state used to retain and record the passes.
     * @param p_sequence Ordered dispatch configurations copied into task-owned storage.
     * @throws std::runtime_error When no pass is provided or a pass configuration is invalid.
     */
    RTRunGraphicsPipelinesSequentialVK(const RenderContextVK* p_render_data, const Sequence& p_sequence);

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void bind_resources(const RendResViewVK& p_resources) override;
    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;

    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    struct BoundEntry {
        BoundEntry(const RenderContextVK* p_render_data, const Dispatch& p_dispatch);
        ~BoundEntry() = default;
        BoundEntry(const BoundEntry&) = delete;
        BoundEntry(BoundEntry&&) noexcept = default;
        BoundEntry& operator=(const BoundEntry&) = delete;
        BoundEntry& operator=(BoundEntry&&) noexcept = default;

        Dispatch dispatch;
        RenderTargetsVK render_targets;
        std::vector<Ref<RenderObjectContainerVK>> vertex_buffer_containers;
        std::vector<VkBuffer> bound_vertex_buffers;
        Ref<RenderObjectContainerVK> index_buffer_container;
        Ref<RenderObjectContainerVK> indirect_buffer_container;
        Unique<PipelineDescriptorBindingsVK> descriptor_bindings;
    };

    const RenderContextVK* render_data = nullptr;
    Vector<BoundEntry, RenderAllocator> entries;

    void bind_vertex_buffers(VkCommandBuffer p_command_buffer, BoundEntry& p_bound_entry) const;
};

} // namespace WhiteBirdEngine

#endif
