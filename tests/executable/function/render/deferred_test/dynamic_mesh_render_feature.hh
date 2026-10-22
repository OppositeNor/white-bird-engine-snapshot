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
#ifndef WBE_FILE_DYNAMIC_MESH_RENDER_FEATURE_HH
#define WBE_FILE_DYNAMIC_MESH_RENDER_FEATURE_HH

#include "../entity_draw_data.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/rend_extent_2d_params_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/rt_params_graphics_draw_mesh_vk.hh"
#include "glm/ext/matrix_float4x4.hpp"
#include "resource/resource_unit/resource_unit.hh"
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {
class RendererVK;

/**
 * @class DynamicMeshRenderFeature
 * @brief Draws several moving entities of one mesh compiled once by MeshCompilerVK. Neither copyable nor movable.
 *
 * The mesh is registered in the render database under DYNAMIC_MESH_KEY, which its graphics_draw_mesh tasks import it
 * under; entity i is imported under `<DYNAMIC_MESH_ENTITY_KEY>_<i>` and needs its own draw task per pass. All methods run
 * on the rendering thread. Geometry requires reconstruction to change.
 */
class DynamicMeshRenderFeature {
public:
    DynamicMeshRenderFeature() = delete;
    /**
     * @brief Compile p_mesh against p_material_pipeline's vertex input and material contract, placing p_entity_count
     * entities at identity.
     * @throws std::runtime_error If p_entity_count is zero or compilation fails.
     */
    DynamicMeshRenderFeature(
        RendererVK* p_renderer, RefRes<RUMesh3D> p_mesh, RefRes<RUGraphicsPipeline> p_material_pipeline, size_t p_entity_count);
    ~DynamicMeshRenderFeature() = default;
    DynamicMeshRenderFeature(const DynamicMeshRenderFeature&) = delete;
    DynamicMeshRenderFeature(DynamicMeshRenderFeature&&) = delete;
    DynamicMeshRenderFeature& operator=(const DynamicMeshRenderFeature&) = delete;
    DynamicMeshRenderFeature& operator=(DynamicMeshRenderFeature&&) = delete;

    /** @brief Return the number of drawn entities. */
    size_t get_entity_count() const {
        return entities.size();
    }

    /**
     * @brief Import every entity into p_graph before graph construction; the draw tasks import the mesh.
     */
    void register_resources(RendGraphDescVK& p_graph) const;

    /**
     * @brief Get the graph id of an entity's whole-mesh transform, for graphics_draw_mesh's transform parameter.
     * @throws std::runtime_error If p_entity_index is out of range.
     */
    std::string get_entity_transform_id(size_t p_entity_index) const;

    /**
     * @brief Place one entity; every frame slot applies the latest transform.
     * @throws std::runtime_error If p_entity_index is out of range or the transform is invalid.
     */
    void set_transform(size_t p_entity_index, const glm::mat4& p_transform);

    /**
     * @brief Make the parameters of a graphics_draw_mesh task drawing one entity's opaque part; existing attachments are
     * loaded.
     * @param p_entity_index The entity.
     * @param p_pipeline_key Render database key of the compiled graphics pipeline.
     * @param p_extent The render area.
     * @throws std::runtime_error If p_entity_index is out of range.
     */
    RTParamsGraphicsDrawMeshVK make_draw_params(
        size_t p_entity_index, std::string_view p_pipeline_key, const RendExtent2DParamsVK& p_extent) const;

    /**
     * @brief Add a graphics_draw_mesh task drawing one entity (see make_draw_params()) and alias its render targets; the
     * caller declares pass-specific shader reads and dependencies.
     * @return The added task descriptor; invalidated when another task is added.
     * @throws std::runtime_error If p_entity_index is out of range, or under the conditions of
     * RTGraphicsDrawMeshVK::add_to_graph().
     */
    RendTaskDescVK& add_task(RendGraphDescVK& p_graph,
        std::string_view p_task_id,
        size_t p_entity_index,
        std::string_view p_pipeline_key,
        const RendExtent2DParamsVK& p_extent,
        std::span<const std::string_view> p_colors = {},
        std::optional<std::string_view> p_depth = std::nullopt) const;

private:
    const RenderContextVK* render_data;
    Ref<MeshResourcesVK> resources;
    std::vector<Unique<EntityDrawData>> entities;
    std::vector<std::string> entity_keys;
};

} // namespace WhiteBirdEngine
#endif
