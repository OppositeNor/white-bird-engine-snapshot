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
#ifndef WBE_FILE_MESH_NODE_CONTRACT_RESOLVER_HH
#define WBE_FILE_MESH_NODE_CONTRACT_RESOLVER_HH

#include "function/render/shader_metadata.hh"
#include "function/render/mesh_node_contract.hh"
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class MeshNodeContractResolver
 * @brief Resolves the reflected byte layout of the shader-side mesh node buffer.
 */
class MeshNodeContractResolver final {
public:
    /**
     * @brief Construct a stateless resolver.
     */
    MeshNodeContractResolver() = default;
    /**
     * @brief Destroy a resolver.
     */
    ~MeshNodeContractResolver() = default;
    /**
     * @brief Copy a resolver.
     */
    MeshNodeContractResolver(const MeshNodeContractResolver&) = default;
    /**
     * @brief Move a resolver.
     */
    MeshNodeContractResolver(MeshNodeContractResolver&&) noexcept = default;
    /**
     * @brief Copy a resolver.
     */
    MeshNodeContractResolver& operator=(const MeshNodeContractResolver&) = default;
    /**
     * @brief Move a resolver.
     */
    MeshNodeContractResolver& operator=(MeshNodeContractResolver&&) noexcept = default;

    /**
     * @brief Resolve the shader-side byte layout of one `NodeData` element.
     * @throws std::runtime_error When the reflection metadata is missing or malformed.
     */
    MeshNodeContract resolve(const ShaderMetadata& p_metadata, std::string_view p_mesh_parameter_name) const;
};

} // namespace WhiteBirdEngine

#endif