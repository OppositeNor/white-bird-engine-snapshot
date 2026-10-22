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
#ifndef WBE_FILE_VERTEX_INPUT_REQUIREMENT_HH
#define WBE_FILE_VERTEX_INPUT_REQUIREMENT_HH

#include "function/render/render_resource_unit.hh"
#include "function/render/shader_role.hh"
#include <cstdint>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class VertexInputRequirement
 * @brief Backend-neutral vertex input one shader vertex entry consumes, independent of any mesh. Copyable and movable.
 *
 * Produced by VertexInputContractResolver::resolve_requirement() from the WBE_VERTEX_INPUT fields of the entry; matching it
 * against a mesh's vertex bindings with VertexInputContractResolver::resolve() gives the VertexInputContract of a draw. Roles and
 * locations are unique.
 */
class VertexInputRequirement final {
public:
    /**
     * @brief One attribute the shader reads.
     */
    struct Attribute {
        /** Shader role the attribute is matched to resource attributes by. */
        ShaderRole role;
        /** Format reflected from the shader field type. */
        RUVertexAttributeFormat format = RUVertexAttributeFormat::FLOAT;
        /** Reflected shader varying-input location. */
        uint32_t location = 0;
    };

    /**
     * @brief Construct an empty requirement: a vertex-less entry.
     */
    VertexInputRequirement() = default;
    /**
     * @brief Destroy owned attributes.
     */
    ~VertexInputRequirement() = default;
    /**
     * @brief Copy a requirement.
     */
    VertexInputRequirement(const VertexInputRequirement&) = default;
    /**
     * @brief Move a requirement.
     */
    VertexInputRequirement(VertexInputRequirement&&) noexcept = default;
    /**
     * @brief Copy a requirement.
     */
    VertexInputRequirement& operator=(const VertexInputRequirement&) = default;
    /**
     * @brief Move a requirement.
     */
    VertexInputRequirement& operator=(VertexInputRequirement&&) noexcept = default;

    /** Attributes in shader reflection order; empty when the entry consumes no vertex input. */
    std::vector<Attribute> attributes;
};

} // namespace WhiteBirdEngine

#endif
