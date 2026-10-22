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
#ifndef WBE_FILE_RT_PARAMS_GENERATE_MIPMAPS_VK_HH
#define WBE_FILE_RT_PARAMS_GENERATE_MIPMAPS_VK_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/rend_extent_2d_params_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <cstdint>
#include <optional>
#include <string>

namespace WhiteBirdEngine {

/**
 * @class RTParamsGenerateMipmapsVK
 * @brief Recipe parameters of RTGenerateMipmapsVK. Copyable and movable.
 *
 * Deserialization is strict: an unknown key, such as a misspelt parameter, throws instead of being ignored.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RTParamsGenerateMipmapsVK {
    /**
     * @brief Graph resource id of the view of the base mip level.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED, WBE_RECIPE_RESOURCE(graph))
    std::string source_view;
    /**
     * @brief Graph resource id of the view covering the whole mip chain.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED, WBE_RECIPE_RESOURCE(graph))
    std::string mip_chain_view;
    /**
     * @brief Extent of the base mip level; the swap chain extent by default.
     */
    WBE_META(WBE_REFLECT)
    RendExtent2DParamsVK base_extent;
    /**
     * @brief Number of mip levels including the base level; the full chain of base_extent at downsample_rate by default.
     */
    WBE_META(WBE_REFLECT)
    std::optional<uint32_t> mipmap_level_count;
    /**
     * @brief Factor by which each mip level shrinks every dimension of the previous one; at least 2.
     *
     * A rate of 2 halves each dimension, combining 2x2 texels (2x2x2 for 3D images) into one; a rate of 3 combines 3x3
     * (3x3x3) texels.
     */
    WBE_META(WBE_REFLECT)
    uint32_t downsample_rate = 2;
    /**
     * @brief Format of the image, which must support filtered blits.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    ImageFormatVK format;
};

} // namespace WhiteBirdEngine

#endif
