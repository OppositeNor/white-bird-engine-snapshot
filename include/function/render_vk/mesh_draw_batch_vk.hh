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
#ifndef WBE_FILE_MESH_DRAW_BATCH_VK_HH
#define WBE_FILE_MESH_DRAW_BATCH_VK_HH

#include "function/render/render_resource_unit.hh"
#include "function/render/vertex_input_requirement.hh"
#include "function/render_vk/vertex_input_description_vk.hh"
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @struct MeshDrawBatchVK
 * @brief One range of a compiled mesh's indexed-indirect commands whose draws share one vertex layout and pipeline variant.
 * Copyable and movable.
 *
 * The batch is independent of any shader: it records the mesh's vertex layout, and a pipeline's vertex input requirement
 * is matched against it when the batch is drawn; see resolve_vertex_input().
 */
struct MeshDrawBatchVK {
    MeshDrawBatchVK() = default;
    ~MeshDrawBatchVK() = default;
    MeshDrawBatchVK(const MeshDrawBatchVK&) = default;
    MeshDrawBatchVK(MeshDrawBatchVK&&) noexcept = default;
    MeshDrawBatchVK& operator=(const MeshDrawBatchVK&) = default;
    MeshDrawBatchVK& operator=(MeshDrawBatchVK&&) noexcept = default;

    /** First tightly packed indirect command in the batch buffer. */
    uint32_t first_draw = 0;
    /** Number of tightly packed indirect commands. */
    uint32_t draw_count = 0;
    /**
     * @brief The vertex layout shared by every draw of the batch: the vertex bindings of one of its submeshes, viewing
     * chunk-owned storage of the mesh the batch was compiled from.
     */
    std::span<const RUVertexView> vertex_bindings;
    /**
     * @brief The mesh vertex buffer, by MeshResourcesVK vertex buffer index, and byte offset bound for each vertex binding;
     * parallel to vertex_bindings. Command vertex offsets are relative to these bases.
     */
    std::vector<VertexBufferBindingVK> vertex_buffers;
    /**
     * @brief The pipeline variant of every draw's material; a draw task draws the batch with the pipeline it maps the
     * variant to.
     */
    std::string pipeline_variant;

    /**
     * @brief Match a pipeline's vertex input requirement against the batch's vertex layout.
     * @param p_requirement The pipeline's vertex input, e.g. ROGraphicsPipelineVK::get_vertex_input_requirement().
     * @param p_vertex_buffers Receives the vertex buffer and offset to bind for each binding of the result, parallel to its
     * bindings; only the vertex bindings the requirement reads are bound.
     * @return The dynamic vertex input of the batch's draws.
     * @throws std::runtime_error If a required role is missing from the layout, has a different format, or is declared by more
     * than one binding, or vertex_buffers does not parallel vertex_bindings.
     */
    VertexInputDescriptionVK resolve_vertex_input(
        const VertexInputRequirement& p_requirement, std::vector<VertexBufferBindingVK>& p_vertex_buffers) const;
};

} // namespace WhiteBirdEngine

#endif
