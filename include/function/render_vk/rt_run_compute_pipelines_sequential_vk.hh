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
#ifndef WBE_FILE_RT_RUN_COMPUTE_PIPELINES_SEQUENTIAL_VK_HH
#define WBE_FILE_RT_RUN_COMPUTE_PIPELINES_SEQUENTIAL_VK_HH

#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/pipeline_descriptor_bindings_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/rt_params_run_compute_pipelines_sequential_vk.hh"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTRunComputePipelinesSequentialVK
 * @brief Render task that dispatches an ordered sequence of synchronized compute passes in one command buffer.
 * The task reports every pass's reflected shader accesses and indirect buffers; map reflected resource ids to graph
 * resources with RendTaskDescVK::alias(). An entry's resource aliases first rename the ids its pipeline reflects, so
 * passes may bind one shader resource to different resources. Consecutive passes are separated by a global compute
 * memory barrier, which also covers the indirect command read when the next pass is indirect.
 */
struct WBE_META(WBE_RENDER_TASK(compute_sequential), WBE_SERIALIZATION_ALIASES(run_compute_pipelines_sequential))
    RTRunComputePipelinesSequentialVK final : public IRenderTaskVK {
    /**
     * @class Entry
     * @brief One retained compute pipeline plus its workgroup counts or indirect command, and its resource aliases.
     *
     * Entries are copyable and movable. Their allocator must outlive every copy.
     */
    struct Entry {
        explicit Entry(RenderAllocator* p_allocator);
        ~Entry() = default;
        Entry(const Entry&) = default;
        Entry(Entry&&) noexcept = default;
        Entry& operator=(const Entry&) = default;
        Entry& operator=(Entry&&) noexcept = default;

        /** Compute pipeline retained by the dispatch and task. */
        Ref<ROComputePipelineVK> pipeline = MEM_NULL;
        /** Workgroup count dispatched in each dimension; unused by an indirect entry. */
        VkExtent3D group_count{.width = 1, .height = 1, .depth = 1};
        /** Task-local resource id of the indirect command buffer; empty for a direct dispatch. */
        std::string indirect_buffer_id;
        /** Byte offset of the indirect command; a multiple of 4, and 0 for a direct dispatch. */
        uint64_t indirect_offset = 0;
        /** Map from a resource id the pipeline reflects to the task-local resource id it binds; every key must be reflected. */
        Map<std::string, std::string, RenderAllocator> resource_aliases;
    };

    /**
     * @class Dispatch
     * @brief Allocator-owned ordered compute-pass entries.
     *
     * Dispatches are copyable and movable. Their allocator must outlive every copy retaining entries.
     */
    struct Dispatch {
        explicit Dispatch(RenderAllocator* p_allocator);
        ~Dispatch() = default;
        Dispatch(const Dispatch&) = default;
        Dispatch(Dispatch&&) noexcept = default;
        Dispatch& operator=(const Dispatch&) = default;
        Dispatch& operator=(Dispatch&&) noexcept = default;

        /**
         * @brief Append one compute pass.
         * @param p_entry Entry transferred into the dispatch.
         * @return This dispatch.
         */
        Dispatch& add_entry(Entry p_entry);

        /**
         * @brief Append one compute pass from its pipeline and workgroup count.
         * @param p_pipeline Compute pipeline retained by the dispatch.
         * @param p_group_count Workgroup count dispatched in each dimension.
         * @return This dispatch.
         */
        Dispatch& add_entry(Ref<ROComputePipelineVK> p_pipeline, VkExtent3D p_group_count);

        /** Allocator of the entries. */
        RenderAllocator* allocator = nullptr;
        /** Ordered compute passes. */
        Vector<Entry, RenderAllocator> entries;
    };

    RTRunComputePipelinesSequentialVK() = delete;
    virtual ~RTRunComputePipelinesSequentialVK() override = default;
    RTRunComputePipelinesSequentialVK(const RTRunComputePipelinesSequentialVK&) = delete;
    RTRunComputePipelinesSequentialVK(RTRunComputePipelinesSequentialVK&&) noexcept = delete;
    RTRunComputePipelinesSequentialVK& operator=(const RTRunComputePipelinesSequentialVK&) = delete;
    RTRunComputePipelinesSequentialVK& operator=(RTRunComputePipelinesSequentialVK&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * @param p_render_data Base render data.
     * @param p_dispatch Ordered compute entries retained by the task.
     * @param p_queue_family Queue used to submit the compute dispatches: GRAPHICS, COMPUTE_1, or COMPUTE_2.
     * @throws std::runtime_error If the dispatch is empty, a pipeline is null, an indirect offset is not a multiple of 4,
     * a direct entry sets an indirect offset, or a resource alias names an id its pipeline does not reflect.
     */
    RTRunComputePipelinesSequentialVK(
        const RenderContextVK* p_render_data, const Dispatch& p_dispatch, QueueFamilyVK p_queue_family = QueueFamilyVK::GRAPHICS);

    /**
     * @brief Set a custom WBE_PUSH_CONSTANT value for every entry that declares the identifier.
     *
     * The value is pushed with every later dispatch. Call on the rendering thread; values set before graph construction are
     * retained.
     *
     * @param p_name Custom push-constant identifier.
     * @param p_value Value whose type must match each reflected field.
     * @throws std::runtime_error If no entry declares the identifier or a field type does not match.
     */
    void set_push_constant(std::string_view p_name, PushConstantValueVK p_value);

    /**
     * @brief Set a custom WBE_PUSH_CONSTANT value for one entry.
     *
     * The value is pushed with every later dispatch of the entry. Call on the rendering thread.
     *
     * @param p_entry_index Index of the entry in dispatch order.
     * @param p_name Custom push-constant identifier declared by the entry's pipeline.
     * @param p_value Value whose type must match the reflected field.
     * @throws std::runtime_error If the index is out of range, the pipeline does not declare the identifier, or the type
     * does not match.
     */
    void set_push_constant(size_t p_entry_index, std::string_view p_name, PushConstantValueVK p_value);

    /**
     * @brief Create the task from recipe parameters; used by RenderTaskRegistryVK.
     * @param p_render_data Base render data whose RenderDatabase holds the compiled pipelines.
     * @param p_params The recipe parameters.
     * @param p_context The graph context the task is created for.
     * @return The task.
     * @throws std::runtime_error If the render data or its RenderDatabase is unavailable, p_params.dispatches is empty, or
     * no compute pipeline is compiled under a dispatch pipeline key. The constructor's own validation also applies.
     */
    WBE_META(WBE_REFLECT)
    static Unique<IRenderTaskVK> create(const RenderContextVK* p_render_data,
        const RTParamsRunComputePipelinesSequentialVK& p_params,
        const RendGraphContextVK& p_context);

    virtual QueueFamilyVK queue_family() const override {
        return queue_family_id;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;

    virtual void bind_resources(const RendResViewVK& p_resources) override;

    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    const RenderContextVK* render_data = nullptr;
    QueueFamilyVK queue_family_id = QueueFamilyVK::GRAPHICS;
    Vector<Entry, RenderAllocator> entries;
    Vector<Unique<PipelineDescriptorBindingsVK>, RenderAllocator> descriptor_bindings;
    Vector<Ref<RenderObjectContainerVK>, RenderAllocator> indirect_buffer_containers;
};

} // namespace WhiteBirdEngine

#endif
