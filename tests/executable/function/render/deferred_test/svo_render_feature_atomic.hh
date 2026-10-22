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
#ifndef WBE_FILE_SVO_RENDER_FEATURE_ATOMIC_HH
#define WBE_FILE_SVO_RENDER_FEATURE_ATOMIC_HH

#include "core/memory/reference_strong.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "i_svo_render_feature.hh"
#include <atomic>
#include <cstdint>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class SVORenderFeatureAtomic
 * @brief SVO construction whose CAS owners initialize and publish child blocks immediately.
 * @details Contenders that observe an allocation in progress are retried from the deferred lists; no publication pass is
 * needed. Neither copyable nor movable; confined to the rendering thread.
 */
class SVORenderFeatureAtomic final : public ISVORenderFeature {
public:
    SVORenderFeatureAtomic() = delete;
    /**
     * @brief Compiles the atomic-publication SVO shaders and creates the shared buffers on the rendering thread.
     * @details The renderer must outlive this object and its registered graphs. Invalid dependencies throw.
     * @param p_scene_bounds Bounds of the static scene; the SVO is the cube enclosing them, see VoxelVolume::enclose().
     * @param p_rebuild_static_every_frame When true, the full static-scene construction runs every frame instead of once.
     */
    SVORenderFeatureAtomic(RendererVK* p_renderer,
        Ref<MeshResourcesVK> p_static_mesh_resources,
        const RUAABB3D& p_scene_bounds,
        bool p_rebuild_static_every_frame = false);
    virtual ~SVORenderFeatureAtomic() override = default;
    SVORenderFeatureAtomic(const SVORenderFeatureAtomic&) = delete;
    SVORenderFeatureAtomic(SVORenderFeatureAtomic&&) = delete;
    SVORenderFeatureAtomic& operator=(const SVORenderFeatureAtomic&) = delete;
    SVORenderFeatureAtomic& operator=(SVORenderFeatureAtomic&&) = delete;

    virtual std::string_view get_name() const override;

private:
    virtual uint32_t get_reset_group_count() const override;
    virtual SVORecipeVariant get_recipe_variant() const override;
};

} // namespace WhiteBirdEngine

#endif
