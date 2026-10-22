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
#ifndef WBE_FILE_DEFERRED_RECIPE_CONTEXT_HH
#define WBE_FILE_DEFERRED_RECIPE_CONTEXT_HH

#include "core/data/data_value.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/rend_recipe_context_vk.hh"
#include "function/render_vk/rend_recipe_extent_2d_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include "voxel_debug_view_toggles.hh"
#include <cstdint>
#include <string>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class DeferredRecipeFormats
 * @brief The image formats of the deferred recipe's targets, by role. Copyable and movable.
 */
struct WBE_META(WBE_REFLECT_SCRIPT) DeferredRecipeFormats {
    /**
     * @brief The format of the gbuffer albedo target.
     */
    WBE_META(WBE_REFLECT)
    ImageFormatVK gbuffer_albedo = ImageFormatVK::UNDEFINED;
    /**
     * @brief The format of the gbuffer normal target.
     */
    WBE_META(WBE_REFLECT)
    ImageFormatVK gbuffer_normal = ImageFormatVK::UNDEFINED;
    /**
     * @brief The format of the gbuffer rma target.
     */
    WBE_META(WBE_REFLECT)
    ImageFormatVK gbuffer_rma = ImageFormatVK::UNDEFINED;
    /**
     * @brief The format of the gbuffer depth target.
     */
    WBE_META(WBE_REFLECT)
    ImageFormatVK gbuffer_depth = ImageFormatVK::UNDEFINED;
    /**
     * @brief The format of the shadowmap depth target.
     */
    WBE_META(WBE_REFLECT)
    ImageFormatVK shadowmap_depth = ImageFormatVK::UNDEFINED;
    /**
     * @brief The format of the shadow moments target.
     */
    WBE_META(WBE_REFLECT)
    ImageFormatVK shadow_moments = ImageFormatVK::UNDEFINED;
    /**
     * @brief The format of the rsm flux target.
     */
    WBE_META(WBE_REFLECT)
    ImageFormatVK rsm_flux = ImageFormatVK::UNDEFINED;
    /**
     * @brief The format of the rsm normal target.
     */
    WBE_META(WBE_REFLECT)
    ImageFormatVK rsm_normal = ImageFormatVK::UNDEFINED;
    /**
     * @brief The format of the lighting output target.
     */
    WBE_META(WBE_REFLECT)
    ImageFormatVK lighting_output = ImageFormatVK::UNDEFINED;
};

/**
 * @class DeferredRecipeContext
 * @brief The context of the render_graphs.deferred recipe; see ApplicationVulkanDeferredPBRTest. Copyable and movable.
 */
struct WBE_META(WBE_REFLECT_SCRIPT) DeferredRecipeContext : public RendRecipeContextVK {
    /**
     * @brief Whether shadows are filtered with summed-area tables instead of mipmaps.
     */
    WBE_META(WBE_REFLECT)
    bool summed_area_table = false;
    /**
     * @brief The lights in the scene.
     */
    WBE_META(WBE_REFLECT)
    uint32_t spot_light_count = 0;
    /**
     * @brief The slots of the lighting shaders' lights[].
     */
    WBE_META(WBE_REFLECT)
    uint32_t max_spot_light_info_count = 0;
    /**
     * @brief The extent of every light's shadow targets.
     */
    WBE_META(WBE_REFLECT)
    RendRecipeExtent2DVK shadow_extent;
    /**
     * @brief The mip levels of every light's shadow moments.
     */
    WBE_META(WBE_REFLECT)
    uint32_t shadow_mipmap_level_count = 0;
    /**
     * @brief The extent of the bloom images.
     */
    WBE_META(WBE_REFLECT)
    RendRecipeExtent2DVK bloom_extent;
    /**
     * @brief The blur passes of the bloom.
     */
    WBE_META(WBE_REFLECT)
    uint32_t bloom_blur_pass_count = 0;
    /**
     * @brief The image formats of the targets.
     */
    WBE_META(WBE_REFLECT)
    DeferredRecipeFormats formats;
    /**
     * @brief The trigger event id of this graph's bloom acquisition.
     */
    WBE_META(WBE_REFLECT)
    std::string bloom_acquire_event;
    /**
     * @brief The trigger event id of this graph's frame sequence.
     */
    WBE_META(WBE_REFLECT)
    std::string frame_sequence_event;
    /**
     * @brief Whether the acceleration structure build task is imported.
     */
    WBE_META(WBE_REFLECT)
    bool has_mesh_acceleration_structure = false;
    /**
     * @brief The voxel structure: the RecipeFeatureParams-derived parameters of its module.
     */
    WBE_META(WBE_REFLECT)
    DataValue voxel;
    /**
     * @brief Nil, or the post-processing effect: the RecipeFeatureParams-derived parameters of its module.
     */
    WBE_META(WBE_REFLECT)
    DataValue post_processing;
    /**
     * @brief The graph ids of the dynamic mesh entities' transforms, one per drawn entity.
     */
    WBE_META(WBE_REFLECT)
    std::vector<std::string> dynamic_mesh_transform_ids;
    /**
     * @brief The RTToggleVK toggles of the tasks each voxel debug view mode group records.
     */
    WBE_META(WBE_REFLECT)
    VoxelDebugViewToggles voxel_debug_view_toggles;
};

} // namespace WhiteBirdEngine

#endif
