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
#include "function/render_vk/vertex_input_description_vk.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render/vertex_input_contract.hh"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

static VkFormat get_vertex_format_vk(RUVertexAttributeFormat p_format) {
    switch (p_format) {
    case RUVertexAttributeFormat::FLOAT:
        return VK_FORMAT_R32_SFLOAT;
    case RUVertexAttributeFormat::FLOAT2:
        return VK_FORMAT_R32G32_SFLOAT;
    case RUVertexAttributeFormat::FLOAT3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case RUVertexAttributeFormat::FLOAT4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RUVertexAttributeFormat::INT:
        return VK_FORMAT_R32_SINT;
    case RUVertexAttributeFormat::INT2:
        return VK_FORMAT_R32G32_SINT;
    case RUVertexAttributeFormat::INT3:
        return VK_FORMAT_R32G32B32_SINT;
    case RUVertexAttributeFormat::INT4:
        return VK_FORMAT_R32G32B32A32_SINT;
    case RUVertexAttributeFormat::UINT:
        return VK_FORMAT_R32_UINT;
    case RUVertexAttributeFormat::UINT2:
        return VK_FORMAT_R32G32_UINT;
    case RUVertexAttributeFormat::UINT3:
        return VK_FORMAT_R32G32B32_UINT;
    case RUVertexAttributeFormat::UINT4:
        return VK_FORMAT_R32G32B32A32_UINT;
    }
    throw std::runtime_error("Failed to translate vertex input contract: unsupported resource format.");
}

VertexInputDescriptionVK VertexInputDescriptionVK::from_contract(const VertexInputContract& p_contract) {
    VertexInputDescriptionVK result;
    result.bindings.reserve(p_contract.bindings.size());
    result.source_bindings.reserve(p_contract.bindings.size());
    for (size_t i = 0; i < p_contract.bindings.size(); ++i) {
        const VertexInputContract::Binding& binding = p_contract.bindings[i];
        if (binding.vertex_stride > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Failed to translate vertex input contract: vertex stride exceeds Vulkan limits.");
        }
        result.bindings.push_back(VkVertexInputBindingDescription2EXT{.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
            .binding = static_cast<uint32_t>(i),
            .stride = static_cast<uint32_t>(binding.vertex_stride),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            .divisor = 1});
        result.source_bindings.push_back(binding.source_binding);
    }
    result.attributes.reserve(p_contract.attributes.size());
    for (const VertexInputContract::Attribute& attribute : p_contract.attributes) {
        if (attribute.offset > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Failed to translate vertex input contract: vertex attribute offset exceeds Vulkan limits.");
        }
        if (attribute.binding >= p_contract.bindings.size()) {
            throw std::runtime_error("Failed to translate vertex input contract: vertex attribute references a missing binding.");
        }
        result.attributes.push_back(VkVertexInputAttributeDescription2EXT{.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
            .location = attribute.location,
            .binding = attribute.binding,
            .format = get_vertex_format_vk(attribute.format),
            .offset = static_cast<uint32_t>(attribute.offset)});
    }
    return result;
}

} // namespace WhiteBirdEngine
