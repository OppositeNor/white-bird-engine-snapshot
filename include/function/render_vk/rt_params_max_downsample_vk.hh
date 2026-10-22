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
#ifndef WBE_FILE_RT_PARAMS_MAX_DOWNSAMPLE_VK_HH
#define WBE_FILE_RT_PARAMS_MAX_DOWNSAMPLE_VK_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include <string>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class RTParamsMaxDownsampleVK
 * @brief Recipe parameters of RTMaxDownsampleVK. Copyable and movable.
 *
 * Deserialization is strict: an unknown key, such as a misspelt parameter, throws instead of being ignored.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RTParamsMaxDownsampleVK {
    /**
     * @brief Resource key of the compiled max-downsample compute pipeline in the renderer's RenderDatabase.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED, WBE_RECIPE_RESOURCE(database, ROComputePipelineVK))
    std::string pipeline;
    /**
     * @brief Graph resource ids of the mip-level image views, from the full-resolution level down; at least two.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::vector<std::string> mip_views;
    /**
     * @brief Queue the dispatches are submitted to: a QueueFamilyVK name or alias, such as "compute_1".
     */
    WBE_META(WBE_REFLECT)
    QueueFamilyVK queue_family = QueueFamilyVK::GRAPHICS;
};

} // namespace WhiteBirdEngine

#endif
