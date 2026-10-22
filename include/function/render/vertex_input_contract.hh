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
#ifndef WBE_FILE_VERTEX_INPUT_CONTRACT_HH
#define WBE_FILE_VERTEX_INPUT_CONTRACT_HH

#include "function/render/render_resource_unit.hh"
#include "function/render/shader_role.hh"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class VertexInputContract
 * @brief Backend-neutral match between one shader vertex entry and one resource layout.
 */
class VertexInputContract final {
public:
    /**
     * @brief One resource vertex binding read by the shader.
     */
    struct Binding {
        /** Index of the binding in the resource's vertex bindings. */
        size_t source_binding = 0;
        /** Resource byte stride between vertices of the binding. */
        size_t vertex_stride = 0;
    };
    /**
     * @brief One shader-consumed resource attribute.
     */
    struct Attribute {
        /** Exact matched shader/resource role. */
        ShaderRole role;
        /** Matched resource format. */
        RUVertexAttributeFormat format = RUVertexAttributeFormat::FLOAT;
        /** Reflected shader varying-input location. */
        uint32_t location = 0;
        /** Index of the attribute's binding in bindings. */
        uint32_t binding = 0;
        /** Byte offset inside one vertex of the attribute's binding. */
        size_t offset = 0;
    };

    /**
     * @brief Construct an empty vertex-less contract.
     */
    VertexInputContract() = default;
    /**
     * @brief Destroy owned attributes.
     */
    ~VertexInputContract() = default;
    /**
     * @brief Copy a contract.
     */
    VertexInputContract(const VertexInputContract&) = default;
    /**
     * @brief Move a contract.
     */
    VertexInputContract(VertexInputContract&&) noexcept = default;
    /**
     * @brief Copy a contract.
     */
    VertexInputContract& operator=(const VertexInputContract&) = default;
    /**
     * @brief Move a contract.
     */
    VertexInputContract& operator=(VertexInputContract&&) noexcept = default;

    /** Resource bindings read by the shader, in resource binding order; empty when the shader consumes no attributes. */
    std::vector<Binding> bindings;
    /** Matched attributes sorted by shader location. */
    std::vector<Attribute> attributes;
};

} // namespace WhiteBirdEngine

#endif
