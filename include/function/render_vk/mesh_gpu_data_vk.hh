/* Copyright 2026 OppositeNor

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
#ifndef WBE_FILE_MESH_GPU_DATA_VK_HH
#define WBE_FILE_MESH_GPU_DATA_VK_HH

#include <cstdint>
#include <glm/mat4x4.hpp>

namespace WhiteBirdEngine {

/**
 * @struct GeometrySubmeshGPU
 * @brief Material-table row addressed by an instance link.
 */
struct GeometrySubmeshGPU {
    /** Index into the reflected material parameter table. */
    uint32_t material_id = 0;
    /** GPU row field; padding must remain zero. */
    uint32_t padding_0 = 0;
    /** GPU row field; padding must remain zero. */
    uint32_t padding_1 = 0;
    /** GPU row field; padding must remain zero. */
    uint32_t padding_2 = 0;
};

/**
 * @struct GeometryNodeGPU
 * @brief Model and inverse-transpose normal matrices shared by mesh passes.
 */
struct GeometryNodeGPU {
    /** Object-to-world transform. */
    glm::mat4 transform{1.0F};
    /** Inverse transpose of the model matrix linear part. */
    glm::mat4 normal_transform{1.0F};
};

/**
 * @struct GeometryInstanceGPU
 * @brief Immutable link from firstInstance to a submesh and transform slot.
 */
struct GeometryInstanceGPU {
    /** Index of the material-bearing submesh row. */
    uint32_t submesh_index = 0;
    /** Index of the transform slot. */
    uint32_t node_index = 0;
    /** GPU row field; padding must remain zero. */
    uint32_t padding_0 = 0;
    /** GPU row field; padding must remain zero. */
    uint32_t padding_1 = 0;
};

} // namespace WhiteBirdEngine

#endif
