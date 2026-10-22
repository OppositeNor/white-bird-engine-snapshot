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
#ifndef WBE_FILE_OCTREE_RECIPE_PARAMS_HH
#define WBE_FILE_OCTREE_RECIPE_PARAMS_HH

#include "core/reflection/reflection_defs.hh"
#include "recipe_feature_params.hh"
#include <cstdint>
#include <string>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class OctreeGroupCount
 * @brief A 3D workgroup count. Copyable and movable.
 */
struct WBE_META(WBE_REFLECT_SCRIPT) OctreeGroupCount {
    /**
     * @brief Workgroups along x.
     */
    WBE_META(WBE_REFLECT)
    uint32_t x = 0;
    /**
     * @brief Workgroups along y.
     */
    WBE_META(WBE_REFLECT)
    uint32_t y = 0;
    /**
     * @brief Workgroups along z.
     */
    WBE_META(WBE_REFLECT)
    uint32_t z = 0;
};

/**
 * @class OctreeRecipeParams
 * @brief The parameters of the render_graph_utils.features.voxel_octree recipe module; see
 * VoxelRenderFeatureOctree::configure_recipe(). Copyable and movable.
 */
struct WBE_META(WBE_REFLECT_SCRIPT) OctreeRecipeParams : public RecipeFeatureParams {
    /**
     * @brief The import names of the application's octree textures and buffers.
     */
    WBE_META(WBE_REFLECT)
    std::vector<std::string> imports;
    /**
     * @brief The number of octree levels, which have one storage view each.
     */
    WBE_META(WBE_REFLECT)
    uint32_t level_count = 0;
    /**
     * @brief The leaf gather's workgroups.
     */
    WBE_META(WBE_REFLECT)
    OctreeGroupCount leaf_gather_group_count;
    /**
     * @brief The node gather's workgroups along each axis.
     */
    WBE_META(WBE_REFLECT)
    uint32_t node_gather_group_count = 0;
    /**
     * @brief The leaf view's indirect draw records.
     */
    WBE_META(WBE_REFLECT)
    uint32_t leaf_draw_count = 0;
};

} // namespace WhiteBirdEngine

#endif
