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
#ifndef WBE_FILE_VERTEX_INPUT_DESCRIPTION_VK_HH
#define WBE_FILE_VERTEX_INPUT_DESCRIPTION_VK_HH

#include "function/render/vertex_input_contract.hh"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {
/**
 * @struct VertexBufferBindingVK
 * @brief The vertex buffer and byte offset bound for one dynamic vertex-input binding. Copyable and movable.
 */
struct VertexBufferBindingVK {
    /** Index of the buffer in the drawing task's vertex buffer list. */
    uint32_t buffer_index = 0;
    /** Byte offset bound for the buffer. */
    VkDeviceSize offset = 0;
};


/**
 * @class VertexInputDescriptionVK
 * @brief Owned Vulkan dynamic state translated from a backend-neutral vertex contract.
 */
struct VertexInputDescriptionVK {
    /**
     * @brief Construct an empty vertex-less state.
     */
    VertexInputDescriptionVK() = default;
    /**
     * @brief Destroy owned descriptions.
     */
    ~VertexInputDescriptionVK() = default;
    /**
     * @brief Copy owned descriptions.
     */
    VertexInputDescriptionVK(const VertexInputDescriptionVK&) = default;
    /**
     * @brief Move owned descriptions.
     */
    VertexInputDescriptionVK(VertexInputDescriptionVK&&) noexcept = default;
    /**
     * @brief Copy owned descriptions.
     */
    VertexInputDescriptionVK& operator=(const VertexInputDescriptionVK&) = default;
    /**
     * @brief Move owned descriptions.
     */
    VertexInputDescriptionVK& operator=(VertexInputDescriptionVK&&) noexcept = default;

    /**
     * @brief Translate a validated contract to Vulkan binding and attribute descriptions.
     * @throws std::runtime_error When a stride or offset exceeds Vulkan's 32-bit dynamic-state fields.
     */
    static VertexInputDescriptionVK from_contract(const VertexInputContract& p_contract);

    /** Dynamic binding descriptions numbered from zero; empty for a vertex-less draw. */
    std::vector<VkVertexInputBindingDescription2EXT> bindings;
    /** Index of the resource vertex binding each Vulkan binding reads, parallel to bindings. */
    std::vector<size_t> source_bindings;
    /** Dynamic shader attribute descriptions in reflected-location order. */
    std::vector<VkVertexInputAttributeDescription2EXT> attributes;
};

} // namespace WhiteBirdEngine

#endif
