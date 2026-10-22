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
#ifndef WBE_FILE_SVO_RENDER_FEATURE_PASS_PUBLISHED_HH
#define WBE_FILE_SVO_RENDER_FEATURE_PASS_PUBLISHED_HH

#include "core/memory/reference_strong.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "i_svo_render_feature.hh"
#include <atomic>
#include <cstdint>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class SVORenderFeaturePassPublished
 * @brief SVO construction whose child blocks become visible only at publication passes between construction rounds.
 * @details Traversal reads only published blocks, so construction passes need no acquire ordering; each deferred round is
 * followed by a publication pass. Neither copyable nor movable; confined to the rendering thread.
 */
class SVORenderFeaturePassPublished final : public ISVORenderFeature {
public:
    SVORenderFeaturePassPublished() = delete;
    /**
     * @brief Compiles the pass-published SVO shaders and creates the shared buffers on the rendering thread.
     * @details The renderer must outlive this object and its registered graphs. Invalid dependencies throw.
     * @param p_scene_bounds Bounds of the static scene; the SVO is the cube enclosing them, see VoxelVolume::enclose().
     * @param p_rebuild_static_every_frame When true, the full static-scene construction runs every frame instead of once.
     */
    SVORenderFeaturePassPublished(RendererVK* p_renderer,
        Ref<MeshResourcesVK> p_static_mesh_resources,
        const RUAABB3D& p_scene_bounds,
        bool p_rebuild_static_every_frame = false);
    virtual ~SVORenderFeaturePassPublished() override = default;
    SVORenderFeaturePassPublished(const SVORenderFeaturePassPublished&) = delete;
    SVORenderFeaturePassPublished(SVORenderFeaturePassPublished&&) = delete;
    SVORenderFeaturePassPublished& operator=(const SVORenderFeaturePassPublished&) = delete;
    SVORenderFeaturePassPublished& operator=(SVORenderFeaturePassPublished&&) = delete;

    virtual std::string_view get_name() const override;

private:
    virtual uint32_t get_reset_group_count() const override;
    virtual SVORecipeVariant get_recipe_variant() const override;

    // The publication pipelines, which the render database holds weakly and the recipe names.
    Ref<ROComputePipelineVK> update_states_pipeline;
    Ref<ROComputePipelineVK> dynamic_publication_pipeline;
};

} // namespace WhiteBirdEngine

#endif
