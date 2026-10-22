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
#ifndef WBE_FILE_PIXEL_SORT_RECIPE_PARAMS_HH
#define WBE_FILE_PIXEL_SORT_RECIPE_PARAMS_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include "recipe_feature_params.hh"
#include <cstdint>
#include <string>

namespace WhiteBirdEngine {

/**
 * @enum PixelSortDirection
 * @brief The lines of the image that the pixel sort sorts.
 */
enum class WBE_META(WBE_REFLECT_SCRIPT) PixelSortDirection : uint8_t {
    /** Sort every column, from the top. */
    VERTICAL WBE_META(WBE_SERIALIZATION_ALIASES(vertical)),
    /** Sort every row, from the left. */
    HORIZONTAL WBE_META(WBE_SERIALIZATION_ALIASES(horizontal)),
};

/**
 * @enum PixelSortScope
 * @brief The pixels each sort of the pixel sort covers.
 * @details The values must match the WBE_PIXEL_SORT_SCOPE_ macros in pixel_sort_utils.slang.
 */
enum class WBE_META(WBE_REFLECT_SCRIPT) PixelSortScope : uint32_t {
    /** Sort all masked pixels of every line together, compacting them across unmasked gaps. */
    LINE WBE_META(WBE_SERIALIZATION_ALIASES(line)) = 0,
    /** Sort every span, a maximal run of consecutive masked pixels of a line, independently. */
    SPAN WBE_META(WBE_SERIALIZATION_ALIASES(span)) = 1,
};

/**
 * @class PixelSortRecipeParams
 * @brief The parameters of the render_graph_utils.features.pixel_sort recipe module; see PixelSort::configure_recipe().
 * @details Copyable and movable.
 */
struct WBE_META(WBE_REFLECT_SCRIPT) PixelSortRecipeParams : public RecipeFeatureParams {
    /**
     * @brief The lines sorted.
     */
    WBE_META(WBE_REFLECT)
    PixelSortDirection direction = PixelSortDirection::VERTICAL;
    /**
     * @brief Whether each sort covers the whole line or each masked span.
     */
    WBE_META(WBE_REFLECT)
    PixelSortScope scope = PixelSortScope::LINE;
    /**
     * @brief The format of the sorted color image.
     */
    WBE_META(WBE_REFLECT)
    ImageFormatVK color_format = ImageFormatVK::UNDEFINED;
    /**
     * @brief The queue every task runs on.
     */
    WBE_META(WBE_REFLECT)
    QueueFamilyVK queue_family = QueueFamilyVK::COMPUTE_1;
    /**
     * @brief The import name of the application's contrast uniform buffer.
     */
    WBE_META(WBE_REFLECT)
    std::string contrast_import;
    /**
     * @brief The event id the effect's toggle receives RTToggleVK commands under.
     */
    WBE_META(WBE_REFLECT)
    std::string toggle_event;
    /**
     * @brief Whether the effect starts enabled.
     */
    WBE_META(WBE_REFLECT)
    bool enabled = false;
};

} // namespace WhiteBirdEngine

#endif
