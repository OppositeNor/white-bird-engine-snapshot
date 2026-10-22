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
#ifndef WBE_FILE_REND_IMAGE_VIEW_PARAMS_VK_HH
#define WBE_FILE_REND_IMAGE_VIEW_PARAMS_VK_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <cstdint>
#include <optional>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class RendImageViewParamsVK
 * @brief Recipe parameters of a graph-owned image view, converted to RendImageViewDescVK. Copyable and movable.
 *
 * Unset optional fields are derived from the viewed image. Set mip_level_count or layer_count to 4294967295 (VK_REMAINING_MIP_LEVELS, VK_REMAINING_ARRAY_LAYERS) to view every remaining level or layer. Deserialization is strict: an unknown key, such as a misspelt parameter, throws instead of being ignored.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RendImageViewParamsVK {
    /**
     * @brief View type; the image's default view type when unset.
     */
    WBE_META(WBE_REFLECT)
    std::optional<ImageViewTypeVK> view_type;
    /**
     * @brief View format; the image format when unset.
     */
    WBE_META(WBE_REFLECT)
    std::optional<ImageFormatVK> format;
    /**
     * @brief Aspect mask; depth for depth formats and color otherwise when unset. Must not be empty when set.
     */
    WBE_META(WBE_REFLECT)
    std::optional<std::vector<ImageAspectFlagVK>> aspect_mask;
    /**
     * @brief First viewed mip level.
     */
    WBE_META(WBE_REFLECT)
    uint32_t base_mip_level = 0;
    /**
     * @brief Number of viewed mip levels.
     */
    WBE_META(WBE_REFLECT)
    uint32_t mip_level_count = 1;
    /**
     * @brief First viewed array layer.
     */
    WBE_META(WBE_REFLECT)
    uint32_t base_array_layer = 0;
    /**
     * @brief Number of viewed array layers.
     */
    WBE_META(WBE_REFLECT)
    uint32_t layer_count = 1;

    /**
     * @brief Convert to the image view configuration.
     * @throws std::runtime_error If aspect_mask is set but empty.
     */
    RendImageViewDescVK to_desc() const;
};

} // namespace WhiteBirdEngine

#endif
