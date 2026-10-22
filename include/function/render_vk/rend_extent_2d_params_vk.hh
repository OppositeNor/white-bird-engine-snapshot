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
#ifndef WBE_FILE_REND_EXTENT_2D_PARAMS_VK_HH
#define WBE_FILE_REND_EXTENT_2D_PARAMS_VK_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include <cstdint>
#include <optional>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RendExtent2DParamsVK
 * @brief A two-dimensional extent in render task and resource parameters, fixed or relative to the swap chain. Copyable and movable.
 *
 * With width and height both set the extent is fixed. With neither set it is the swap chain extent scaled by
 * swap_chain_scale, so it follows swap chain resizes; each scaled dimension is rounded down and is at least 1.
 * Deserialization is strict.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RendExtent2DParamsVK {
    /**
     * @brief Fixed width in pixels; set together with height.
     */
    WBE_META(WBE_REFLECT)
    std::optional<uint32_t> width;
    /**
     * @brief Fixed height in pixels; set together with width.
     */
    WBE_META(WBE_REFLECT)
    std::optional<uint32_t> height;
    /**
     * @brief Scale applied to the swap chain extent when width and height are unset.
     */
    WBE_META(WBE_REFLECT)
    float swap_chain_scale = 1.0F;

    /**
     * @brief Resolve the extent against a render graph context.
     * @throws std::runtime_error If only one of width and height is set, a fixed dimension is zero, or swap_chain_scale is
     * not positive and finite.
     */
    VkExtent2D resolve(const RendGraphContextVK& p_context) const;
};

} // namespace WhiteBirdEngine

#endif
