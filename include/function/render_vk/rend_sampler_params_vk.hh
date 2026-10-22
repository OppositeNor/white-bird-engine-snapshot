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
#ifndef WBE_FILE_REND_SAMPLER_PARAMS_VK_HH
#define WBE_FILE_REND_SAMPLER_PARAMS_VK_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <optional>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RendSamplerParamsVK
 * @brief Recipe parameters of a graph-owned sampler, converted to RendSamplerDescVK. Copyable and movable.
 *
 * The defaults match RendSamplerDescVK. Deserialization is strict: an unknown key, such as a misspelt parameter, throws instead of being ignored.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RendSamplerParamsVK {
    /**
     * @brief Minification filter.
     */
    WBE_META(WBE_REFLECT)
    FilterVK min_filter = FilterVK::LINEAR;
    /**
     * @brief Magnification filter.
     */
    WBE_META(WBE_REFLECT)
    FilterVK mag_filter = FilterVK::LINEAR;
    /**
     * @brief Mipmap sampling mode.
     */
    WBE_META(WBE_REFLECT)
    SamplerMipmapModeVK mipmap_mode = SamplerMipmapModeVK::LINEAR;
    /**
     * @brief Address mode for the U coordinate.
     */
    WBE_META(WBE_REFLECT)
    SamplerAddressModeVK address_mode_u = SamplerAddressModeVK::REPEAT;
    /**
     * @brief Address mode for the V coordinate.
     */
    WBE_META(WBE_REFLECT)
    SamplerAddressModeVK address_mode_v = SamplerAddressModeVK::REPEAT;
    /**
     * @brief Address mode for the W coordinate.
     */
    WBE_META(WBE_REFLECT)
    SamplerAddressModeVK address_mode_w = SamplerAddressModeVK::REPEAT;
    /**
     * @brief Mipmap level-of-detail bias.
     */
    WBE_META(WBE_REFLECT)
    float mip_lod_bias = 0.0F;
    /**
     * @brief Maximum anisotropy; anisotropic filtering is disabled when unset.
     */
    WBE_META(WBE_REFLECT)
    std::optional<float> max_anisotropy;
    /**
     * @brief Depth-comparison operation; comparison sampling is disabled when unset.
     */
    WBE_META(WBE_REFLECT)
    std::optional<CompareOpVK> compare_op;
    /**
     * @brief Minimum mip level of detail.
     */
    WBE_META(WBE_REFLECT)
    float min_lod = 0.0F;
    /**
     * @brief Maximum mip level of detail.
     */
    WBE_META(WBE_REFLECT)
    float max_lod = VK_LOD_CLAMP_NONE;
    /**
     * @brief Border color used by clamp-to-border address modes.
     */
    WBE_META(WBE_REFLECT)
    BorderColorVK border_color = BorderColorVK::INT_OPAQUE_BLACK;
    /**
     * @brief Whether texture coordinates are unnormalized.
     */
    WBE_META(WBE_REFLECT)
    bool unnormalized_coordinates = false;

    /**
     * @brief Convert to the sampler configuration.
     */
    RendSamplerDescVK to_desc() const;
};

} // namespace WhiteBirdEngine

#endif
