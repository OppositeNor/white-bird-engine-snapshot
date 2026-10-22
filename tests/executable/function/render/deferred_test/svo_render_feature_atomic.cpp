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
#include "svo_render_feature_atomic.hh"
#include "../render_test.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "i_svo_render_feature.hh"
#include <atomic>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

SVORenderFeatureAtomic::SVORenderFeatureAtomic(RendererVK* p_renderer,
    Ref<MeshResourcesVK> p_static_mesh_resources,
    const RUAABB3D& p_scene_bounds,
    bool p_rebuild_static_every_frame)
    : ISVORenderFeature(p_renderer, std::move(p_static_mesh_resources), p_scene_bounds, "svo", "svo_dynamic", p_rebuild_static_every_frame) {
}

std::string_view SVORenderFeatureAtomic::get_name() const {
    return "svo (atomic publication)";
}

uint32_t SVORenderFeatureAtomic::get_reset_group_count() const {
    return 1;
}

SVORecipeVariant SVORenderFeatureAtomic::get_recipe_variant() const {
    return SVORecipeVariant::ATOMIC;
}

} // namespace WhiteBirdEngine
