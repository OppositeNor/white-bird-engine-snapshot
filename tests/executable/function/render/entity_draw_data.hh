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
#ifndef WBE_FILE_ENTITY_DRAW_DATA_HH
#define WBE_FILE_ENTITY_DRAW_DATA_HH

#include "core/memory/reference_strong.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/roc_multislots_vk.hh"
#include "glm/ext/matrix_float4x4.hpp"
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class EntityDrawData
 * @brief Test-local placement of one drawn copy of a compiled mesh: the mesh and this copy's whole-mesh transform.
 *
 * Several entities may share one MeshResourcesVK; the mesh is imported once under its own key, and each entity imports only
 * its transform. Draws of an entity bind REND_RES_MESH_TRANSFORM to `<entity key>.transform` and the other mesh ids to the
 * mesh's key. The transform lives in one buffer per frame in flight. Neither copyable nor movable; rendering thread only.
 */
class EntityDrawData final : public IBindableRenderResourceVK {
public:
    /** Member name of the per-frame whole-mesh transform. */
    static constexpr std::string_view TRANSFORM = "transform";

    EntityDrawData() = delete;
    virtual ~EntityDrawData() override = default;
    EntityDrawData(const EntityDrawData&) = delete;
    EntityDrawData(EntityDrawData&&) noexcept = delete;
    EntityDrawData& operator=(const EntityDrawData&) = delete;
    EntityDrawData& operator=(EntityDrawData&&) noexcept = delete;

    /**
     * @brief Place p_mesh at the identity transform.
     * @throws std::runtime_error If p_render_data or p_mesh is null.
     */
    EntityDrawData(const RenderContextVK* p_render_data, Ref<MeshResourcesVK> p_mesh);

    /** @brief Acquire the drawn mesh. */
    Ref<MeshResourcesVK> acquire_mesh() const {
        return mesh;
    }

    /** @brief Return the current whole-mesh transform. */
    const glm::mat4& get_transform() const {
        return transform;
    }

    /**
     * @brief Replace the whole-mesh transform for every frame slot; each slot applies the latest one when next resolved.
     * @throws std::runtime_error If p_transform is not finite or its linear part has no inverse.
     */
    void set_transform(const glm::mat4& p_transform);

    /**
     * @brief Import the transform under `<p_resource_id>.transform`; the mesh is imported separately.
     */
    virtual void bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const override;

private:
    Ref<MeshResourcesVK> mesh;
    Ref<ROCMultislotsVK> transform_buffers;
    glm::mat4 transform{1.0F};
};

} // namespace WhiteBirdEngine

#endif
