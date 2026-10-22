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
#ifndef WBE_FILE_RT_GRAPHICS_DRAW_MESH_VK_HH
#define WBE_FILE_RT_GRAPHICS_DRAW_MESH_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/rt_params_graphics_draw_mesh_vk.hh"
#include "function/render_vk/rt_run_graphics_pipeline_indirect_vk.hh"
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTGraphicsDrawMeshVK
 * @brief Render task that draws parts of a compiled mesh, named by its mesh resource key, with one graphics pipeline.
 *
 * The task joins the mesh and the pipeline itself: each draw batch's vertex input is resolved from the pipeline's vertex
 * input requirement and the batch's vertex layout, the pipeline's mesh node layout is checked against the mesh's node table,
 * and the material parameter table the pipeline reads is built for its material texture contract. All parts are drawn in
 * one rendering scope, in the order of RTParamsGraphicsDrawMeshVK::parts.
 *
 * The mesh's buffers and the pipeline's mesh resource aliases must be added to the graph descriptor before the graph is
 * built; add the task with add_to_graph() or RenderTaskRegistryVK::add_task(), which call prepare_graph(). Render targets
 * are aliased by the caller as for any graphics task (`render_target_colors[<index>]`, `render_target_depth`).
 *
 * Rendering-thread confined; neither copyable nor movable.
 */
struct WBE_META(WBE_RENDER_TASK(draw_mesh), WBE_SERIALIZATION_ALIASES(graphics_draw_mesh)) RTGraphicsDrawMeshVK final : public IRenderTaskVK {
    RTGraphicsDrawMeshVK() = delete;
    virtual ~RTGraphicsDrawMeshVK() override = default;
    RTGraphicsDrawMeshVK(const RTGraphicsDrawMeshVK&) = delete;
    RTGraphicsDrawMeshVK(RTGraphicsDrawMeshVK&&) noexcept = delete;
    RTGraphicsDrawMeshVK& operator=(const RTGraphicsDrawMeshVK&) = delete;
    RTGraphicsDrawMeshVK& operator=(RTGraphicsDrawMeshVK&&) noexcept = delete;

    /**
     * @brief Wrap the indirect draw that records the mesh parts; use create().
     * @throws std::runtime_error If p_draw is null.
     */
    explicit RTGraphicsDrawMeshVK(Unique<RTRunGraphicsPipelineIndirectVK> p_draw);

    /**
     * @brief Create the task from recipe parameters; used by RenderTaskRegistryVK.
     * @param p_render_data Base render data whose RenderDatabase holds the pipeline and the compiled mesh.
     * @param p_params The recipe parameters.
     * @param p_context The graph context the task is created for.
     * @return The task.
     * @throws std::runtime_error If the pipeline or the compiled mesh is not in the RenderDatabase, parts is empty or repeats a
     * part, the mesh has no submeshes in a requested part, the pipeline's vertex input or mesh node layout does not match the
     * mesh, or the render extent or a clear value is invalid.
     */
    WBE_META(WBE_REFLECT)
    static Unique<IRenderTaskVK> create(
        const RenderContextVK* p_render_data, const RTParamsGraphicsDrawMeshVK& p_params, const RendGraphContextVK& p_context);

    /**
     * @brief Prepare a graph descriptor for the task: compile and import the mesh, and alias the pipeline's mesh resources.
     *
     * Compiles the mesh through MeshCompilerVK::acquire() if it is not registered yet, imports it under
     * RTParamsGraphicsDrawMeshVK::mesh unless already imported, builds and imports the material table for the pipeline's
     * material texture contract if the pipeline reads one, and aliases each mesh resource id the pipeline declares
     * (REND_RES_MESH_SUBMESHES, REND_RES_MESH_NODES, REND_RES_MESH_INSTANCES, REND_RES_MESH_TRANSFORM,
     * REND_RES_MESH_MATERIAL_PARAMS, REND_RES_MESH_MATERIAL_SAMPLER) on p_task. The RenderDatabase holds compiled objects
     * weakly, so p_task's factory is wrapped to keep the pipeline and the compiled mesh alive for as long as the descriptor
     * exists. Call on the rendering thread, after p_task's factory is set.
     *
     * @throws std::runtime_error If p_task has no factory, the pipeline is not compiled, the mesh cannot be compiled, the
     * pipeline reads a material table without declaring a material texture contract, or the pipeline declares
     * REND_RES_MESH_TRANSFORM and p_params.transform is unset.
     */
    static void prepare_graph(
        RendGraphDescVK& p_graph, RendTaskDescVK& p_task, const RTParamsGraphicsDrawMeshVK& p_params, const RenderContextVK* p_render_data);

    /**
     * @brief Add the task to a graph descriptor after preparing the descriptor with prepare_graph().
     * @return The added task descriptor, for render target aliases and edges; invalidated when another task is added.
     * @throws std::runtime_error Under the conditions of prepare_graph(), or if the graph already has a task with p_task_id.
     */
    static RendTaskDescVK& add_to_graph(RendGraphDescVK& p_graph,
        std::string_view p_task_id,
        const RTParamsGraphicsDrawMeshVK& p_params,
        const RenderContextVK* p_render_data);

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;

    virtual void bind_resources(const RendResViewVK& p_resources) override;

    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    Unique<RTRunGraphicsPipelineIndirectVK> draw;
};

} // namespace WhiteBirdEngine

#endif
