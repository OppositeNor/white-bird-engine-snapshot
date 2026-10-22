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
#ifndef WBE_FILE_REND_BUFFER_PARAMS_VK_HH
#define WBE_FILE_REND_BUFFER_PARAMS_VK_HH

#include "core/reflection/reflection_defs.hh"
#include "function/function_utils.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <cstdint>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class RendBufferParamsVK
 * @brief Recipe parameters of a graph-owned buffer, converted to RendBufferDescVK. Copyable and movable.
 *
 * Recipes cannot set initial contents, so size must be non-zero. Deserialization is strict: an unknown key, such as a misspelt parameter, throws instead of being ignored.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RendBufferParamsVK {
    /**
     * @brief Primary usage; one of the BUFFER_* usages.
     */
    WBE_META(WBE_REFLECT)
    RendResPrimaryUsageVK usage = RendResPrimaryUsageVK::BUFFER_STORAGE;
    /**
     * @brief Vulkan usage flags added to the flags selected by the primary usage.
     */
    WBE_META(WBE_REFLECT)
    std::vector<BufferUsageFlagVK> additional_usage;
    /**
     * @brief Buffer size in bytes.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    uint64_t size = 0;
    /**
     * @brief Memory domain where the buffer is allocated.
     */
    WBE_META(WBE_REFLECT)
    DeviceDomain domain = DeviceDomain::DEVICE;
    /**
     * @brief Whether one buffer is allocated per frame in flight instead of one for the whole graph.
     */
    WBE_META(WBE_REFLECT)
    bool per_frame = false;

    /**
     * @brief Convert to the buffer configuration.
     * @throws std::runtime_error If size is zero.
     */
    RendBufferDescVK to_desc() const;
};

} // namespace WhiteBirdEngine

#endif
