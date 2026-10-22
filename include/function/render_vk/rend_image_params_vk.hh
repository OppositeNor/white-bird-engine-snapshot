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
#ifndef WBE_FILE_REND_IMAGE_PARAMS_VK_HH
#define WBE_FILE_REND_IMAGE_PARAMS_VK_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/rend_extent_2d_params_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <cstdint>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class RendImageParamsVK
 * @brief Recipe parameters of a graph-owned image, converted to RendImageDescVK. Copyable and movable.
 *
 * The width and height follow the swap chain unless fixed; see RendExtent2DParamsVK. Deserialization is strict: an unknown key, such as a misspelt parameter, throws instead of being ignored.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RendImageParamsVK {
    /**
     * @brief Primary usage; one of the IMAGE_* usages.
     */
    WBE_META(WBE_REFLECT)
    RendResPrimaryUsageVK usage = RendResPrimaryUsageVK::IMAGE_READ_WRITE;
    /**
     * @brief Vulkan usage flags added to the flags selected by the primary usage.
     */
    WBE_META(WBE_REFLECT)
    std::vector<ImageUsageFlagVK> additional_usage;
    /**
     * @brief Image format.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    ImageFormatVK format;
    /**
     * @brief Width and height; the swap chain extent by default.
     */
    WBE_META(WBE_REFLECT)
    RendExtent2DParamsVK extent;
    /**
     * @brief Depth of the image; 1 unless image_type is TYPE_3D.
     */
    WBE_META(WBE_REFLECT)
    uint32_t depth = 1;
    /**
     * @brief Image dimensionality.
     */
    WBE_META(WBE_REFLECT)
    ImageTypeVK image_type = ImageTypeVK::TYPE_2D;
    /**
     * @brief Number of mip levels.
     */
    WBE_META(WBE_REFLECT)
    uint32_t mip_levels = 1;
    /**
     * @brief Number of array layers.
     */
    WBE_META(WBE_REFLECT)
    uint32_t layers = 1;
    /**
     * @brief Whether one image is allocated per frame in flight instead of one for the whole graph.
     */
    WBE_META(WBE_REFLECT)
    bool per_frame = false;

    /**
     * @brief Convert to the image configuration for a render graph context.
     * @throws std::runtime_error If the extent cannot be resolved or depth is zero.
     */
    RendImageDescVK to_desc(const RendGraphContextVK& p_context) const;
};

} // namespace WhiteBirdEngine

#endif
