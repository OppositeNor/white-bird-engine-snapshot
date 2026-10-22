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
#ifndef WBE_FILE_VOXEL_DEBUG_VIEW_TOGGLES_HH
#define WBE_FILE_VOXEL_DEBUG_VIEW_TOGGLES_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/rend_graph_recipe_params_vk.hh"

namespace WhiteBirdEngine {

/**
 * @class VoxelDebugViewToggles
 * @brief The RTToggleVK toggles of the tasks each voxel debug view mode group records; see
 * ApplicationVulkanDeferredPBRTest::is_voxel_debug_view_toggle_enabled(). Copyable and movable.
 */
struct WBE_META(WBE_REFLECT_SCRIPT) VoxelDebugViewToggles {
    /**
     * @brief Enabled while the voxel debug view is disabled.
     */
    WBE_META(WBE_REFLECT)
    RendRecipeToggleVK disabled;
    /**
     * @brief Enabled while any voxel debug view is shown.
     */
    WBE_META(WBE_REFLECT)
    RendRecipeToggleVK enabled;
    /**
     * @brief Enabled while the post-construction view is shown.
     */
    WBE_META(WBE_REFLECT)
    RendRecipeToggleVK post_svo_build;
    /**
     * @brief Enabled while the pre-propagation view is shown.
     */
    WBE_META(WBE_REFLECT)
    RendRecipeToggleVK pre_deferred_propagation;
};

} // namespace WhiteBirdEngine

#endif
