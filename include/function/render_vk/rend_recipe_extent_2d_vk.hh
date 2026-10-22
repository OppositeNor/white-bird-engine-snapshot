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
#ifndef WBE_FILE_REND_RECIPE_EXTENT_2D_VK_HH
#define WBE_FILE_REND_RECIPE_EXTENT_2D_VK_HH

#include "core/reflection/reflection_defs.hh"
#include <cstdint>

namespace WhiteBirdEngine {

/**
 * @class RendRecipeExtent2DVK
 * @brief A fixed 2D extent in pixels that an application hands a render graph recipe. Copyable and movable.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_REFLECT_SCRIPT) RendRecipeExtent2DVK {
    /**
     * @brief The width in pixels.
     */
    WBE_META(WBE_REFLECT)
    uint32_t width = 0;
    /**
     * @brief The height in pixels.
     */
    WBE_META(WBE_REFLECT)
    uint32_t height = 0;
};

} // namespace WhiteBirdEngine

#endif
