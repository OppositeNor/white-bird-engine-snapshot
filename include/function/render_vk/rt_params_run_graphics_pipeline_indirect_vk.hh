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
#ifndef WBE_FILE_RT_PARAMS_RUN_GRAPHICS_PIPELINE_INDIRECT_VK_HH
#define WBE_FILE_RT_PARAMS_RUN_GRAPHICS_PIPELINE_INDIRECT_VK_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/rend_extent_2d_params_vk.hh"
#include "function/render_vk/rt_params_clear_value_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <cstdint>
#include <string>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class RTParamsRunGraphicsPipelineIndirectVK
 * @brief Recipe parameters of RTRunGraphicsPipelineIndirectVK: indexed indirect draws of one pipeline without vertex
 * buffers, which reads its geometry from the index buffer and its own resources. Copyable and movable.
 *
 * Draw meshes with graphics_draw_mesh instead. Deserialization is strict: an unknown key, such as a misspelt parameter,
 * throws instead of being ignored.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RTParamsRunGraphicsPipelineIndirectVK {
    /**
     * @brief Resource key of the compiled graphics pipeline in the renderer's RenderDatabase; it must take no vertex input.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED, WBE_RECIPE_RESOURCE(database, ROGraphicsPipelineVK))
    std::string pipeline;
    /**
     * @brief Graph resource id of the index buffer (32-bit indices).
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED, WBE_RECIPE_RESOURCE(graph))
    std::string index_buffer;
    /**
     * @brief Graph resource id of the buffer of VkDrawIndexedIndirectCommand records.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED, WBE_RECIPE_RESOURCE(graph))
    std::string indirect_buffer;
    /**
     * @brief Number of indirect draw records, from the start of the indirect buffer.
     */
    WBE_META(WBE_REFLECT)
    uint32_t draw_count = 1;
    /**
     * @brief Render area; the swap chain extent by default.
     */
    WBE_META(WBE_REFLECT)
    RendExtent2DParamsVK render_extent;
    /**
     * @brief Clear values of the render targets, colors in index order followed by depth.
     */
    WBE_META(WBE_REFLECT)
    std::vector<RTParamsClearValueVK> clear_values;
    /**
     * @brief Load operation of the color render targets.
     */
    WBE_META(WBE_REFLECT)
    AttachmentLoadOpVK color_load_op = AttachmentLoadOpVK::CLEAR;
    /**
     * @brief Load operation of the depth render target.
     */
    WBE_META(WBE_REFLECT)
    AttachmentLoadOpVK depth_load_op = AttachmentLoadOpVK::CLEAR;
};

} // namespace WhiteBirdEngine

#endif
