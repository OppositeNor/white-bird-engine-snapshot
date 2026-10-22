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
#ifndef WBE_FILE_REND_RECIPE_CONTEXT_VK_HH
#define WBE_FILE_REND_RECIPE_CONTEXT_VK_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/rend_recipe_extent_2d_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <cstdint>

namespace WhiteBirdEngine {

/**
 * @class RendRecipeContextVK
 * @brief The renderer facts every render graph recipe's `build(context)` receives; see RendGraphScriptVK::make_context().
 *
 * An application that adds context entries for its recipe derives a reflected struct from this one and serializes it with
 * RendGraphScriptVK::serialize_context(), so the recipe's context type is generated from it. Copyable and movable.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_REFLECT_SCRIPT) RendRecipeContextVK {
    /**
     * @brief The number of frames the renderer records ahead.
     */
    WBE_META(WBE_REFLECT)
    uint32_t frames_in_flight = 0;
    /**
     * @brief The format of the swap chain images.
     */
    WBE_META(WBE_REFLECT)
    ImageFormatVK swap_chain_format = ImageFormatVK::UNDEFINED;
    /**
     * @brief The extent of the swap chain images.
     */
    WBE_META(WBE_REFLECT)
    RendRecipeExtent2DVK swap_chain_extent;
};

} // namespace WhiteBirdEngine

#endif
