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
#ifndef WBE_FILE_MESH_NODE_CONTRACT_HH
#define WBE_FILE_MESH_NODE_CONTRACT_HH

#include <cstddef>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @brief Name of the reflection-only vertex-shader parameter block that declares the mesh storage-buffer contract.
 */
inline constexpr std::string_view MESH_NODE_CONTRACT_PARAMETER_NAME = "mesh";

/**
 * @class MeshNodeContract
 * @brief Reflected byte layout of one shader-side mesh node element.
 */
class MeshNodeContract final {
public:
    /**
     * @brief Construct an empty contract.
     */
    MeshNodeContract() = default;
    /**
     * @brief Destroy a contract.
     */
    ~MeshNodeContract() = default;
    /**
     * @brief Copy a contract.
     */
    MeshNodeContract(const MeshNodeContract&) = default;
    /**
     * @brief Move a contract.
     */
    MeshNodeContract(MeshNodeContract&&) noexcept = default;
    /**
     * @brief Copy a contract.
     */
    MeshNodeContract& operator=(const MeshNodeContract&) = default;
    /**
     * @brief Move a contract.
     */
    MeshNodeContract& operator=(MeshNodeContract&&) noexcept = default;

    /**
     * @brief Test exact byte-layout compatibility with another contract.
     */
    bool matches(const MeshNodeContract& p_other) const;

    /** Byte stride between reflected node elements. */
    size_t element_stride = 0;
    /** Byte offset of NodeData.transform. */
    size_t transform_offset = 0;
    /** Byte offset of NodeData.normal_transform. */
    size_t normal_transform_offset = 0;
};

} // namespace WhiteBirdEngine

#endif