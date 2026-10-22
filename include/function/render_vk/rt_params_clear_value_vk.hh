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
#ifndef WBE_FILE_RT_PARAMS_CLEAR_VALUE_VK_HH
#define WBE_FILE_RT_PARAMS_CLEAR_VALUE_VK_HH

#include "core/reflection/reflection_defs.hh"
#include <cstdint>
#include <optional>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTParamsClearValueVK
 * @brief One attachment clear value in render task parameters. Copyable and movable.
 *
 * Set exactly one of color, a floating-point RGBA color, and depth, a depth-stencil value with stencil. Deserialization is
 * strict.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RTParamsClearValueVK {
    /**
     * @brief Floating-point clear color: exactly four components, red, green, blue, and alpha.
     */
    WBE_META(WBE_REFLECT)
    std::optional<std::vector<float>> color;
    /**
     * @brief Depth clear value.
     */
    WBE_META(WBE_REFLECT)
    std::optional<float> depth;
    /**
     * @brief Stencil clear value; used only with depth.
     */
    WBE_META(WBE_REFLECT)
    uint32_t stencil = 0;

    /**
     * @brief Convert to a VkClearValue.
     * @throws std::runtime_error If not exactly one of color and depth is set, or color does not have four components.
     */
    VkClearValue to_vk() const;
};

} // namespace WhiteBirdEngine

#endif
