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
#ifndef WBE_FILE_PIXEL_SORTING_RECIPE_CONTEXT_HH
#define WBE_FILE_PIXEL_SORTING_RECIPE_CONTEXT_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/rend_recipe_context_vk.hh"
#include <cstdint>

namespace WhiteBirdEngine {

/**
 * @enum PixelSortOrientation
 * @brief The lines of the image the pixel sort sorts.
 */
enum class WBE_META(WBE_REFLECT_SCRIPT) PixelSortOrientation : uint8_t {
    /** Sort every column, from the top. */
    VERTICAL WBE_META(WBE_SERIALIZATION_ALIASES(vertical)),
    /** Sort every row, from the left. */
    HORIZONTAL WBE_META(WBE_SERIALIZATION_ALIASES(horizontal)),
};

/**
 * @class PixelSortingRecipeContext
 * @brief The context of the render_graphs.pixel_sorting recipe; see ApplicationVulkanPixelSortingTest. Copyable and movable.
 */
struct WBE_META(WBE_REFLECT_SCRIPT) PixelSortingRecipeContext : public RendRecipeContextVK {
    /**
     * @brief The width of the source image, in pixels.
     */
    WBE_META(WBE_REFLECT)
    uint32_t source_width = 0;
    /**
     * @brief The height of the source image, in pixels.
     */
    WBE_META(WBE_REFLECT)
    uint32_t source_height = 0;
    /**
     * @brief The lines of the image sorted.
     */
    WBE_META(WBE_REFLECT)
    PixelSortOrientation orientation = PixelSortOrientation::VERTICAL;
};

} // namespace WhiteBirdEngine

#endif
