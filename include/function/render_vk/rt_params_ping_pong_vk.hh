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
#ifndef WBE_FILE_RT_PARAMS_PING_PONG_VK_HH
#define WBE_FILE_RT_PARAMS_PING_PONG_VK_HH

#include "core/data/data_value.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include <cstdint>
#include <optional>
#include <string>

namespace WhiteBirdEngine {

/**
 * @class RTParamsPingPongVK
 * @brief Recipe parameters of RTPingPongVK. Copyable and movable.
 *
 * Deserialization is strict: an unknown key, such as a misspelt parameter, throws instead of being ignored.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RTParamsPingPongVK {
    /**
     * @brief Resource key of the compute pipeline selected by a false state.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED, WBE_RECIPE_RESOURCE(database, ROComputePipelineVK))
    std::string ping_pipeline;
    /**
     * @brief Resource key of the compute pipeline selected by a true state.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED, WBE_RECIPE_RESOURCE(database, ROComputePipelineVK))
    std::string pong_pipeline;
    /**
     * @brief Selection used by the first iteration; false selects ping and true selects pong.
     */
    WBE_META(WBE_REFLECT)
    bool initial_state = false;
    /**
     * @brief Number of alternating dispatches recorded by the task.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    uint32_t iterations;
    /**
     * @brief Ping and pong workgroup count along x passed to vkCmdDispatch.
     */
    WBE_META(WBE_REFLECT)
    uint32_t group_count_x = 1;
    /**
     * @brief Ping and pong workgroup count along y passed to vkCmdDispatch.
     */
    WBE_META(WBE_REFLECT)
    uint32_t group_count_y = 1;
    /**
     * @brief Ping and pong workgroup count along z passed to vkCmdDispatch.
     */
    WBE_META(WBE_REFLECT)
    uint32_t group_count_z = 1;
    /**
     * @brief Resource key of the compute pipeline recorded before every ping dispatch; set together with pong_prefix_pipeline.
     */
    WBE_META(WBE_REFLECT, WBE_RECIPE_RESOURCE(database, ROComputePipelineVK))
    std::optional<std::string> ping_prefix_pipeline;
    /**
     * @brief Resource key of the compute pipeline recorded before every pong dispatch; set together with ping_prefix_pipeline.
     */
    WBE_META(WBE_REFLECT, WBE_RECIPE_RESOURCE(database, ROComputePipelineVK))
    std::optional<std::string> pong_prefix_pipeline;
    /**
     * @brief Prefix workgroup count along x passed to vkCmdDispatch.
     */
    WBE_META(WBE_REFLECT)
    uint32_t prefix_group_count_x = 1;
    /**
     * @brief Prefix workgroup count along y passed to vkCmdDispatch.
     */
    WBE_META(WBE_REFLECT)
    uint32_t prefix_group_count_y = 1;
    /**
     * @brief Prefix workgroup count along z passed to vkCmdDispatch.
     */
    WBE_META(WBE_REFLECT)
    uint32_t prefix_group_count_z = 1;
    /**
     * @brief Queue the dispatches are submitted to: a QueueFamilyVK name or alias, such as "compute_1".
     */
    WBE_META(WBE_REFLECT)
    QueueFamilyVK queue_family = QueueFamilyVK::GRAPHICS;
    /**
     * @brief Custom WBE_PUSH_CONSTANT values: a map from identifier to number, typed as the pipeline declares each one.
     */
    WBE_META(WBE_REFLECT)
    DataValue push_constants;
};

} // namespace WhiteBirdEngine

#endif
