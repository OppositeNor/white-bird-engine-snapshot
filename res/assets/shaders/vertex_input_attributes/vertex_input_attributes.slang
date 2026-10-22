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
#include "utils.slang"

/**
 * @struct VertexInputAttributes
 * @brief Vertex fields used to test explicit and implicit reflected input roles.
 */
struct VertexInputAttributes {
    /**
     * @brief Position with an explicit reflected role.
     */
    [WBE_VERTEX_INPUT("position")]
    float3 position;

    /**
     * @brief Normal with an implicit reflected role.
     */
    [WBE_VERTEX_INPUT]
    float3 normal;

    /**
     * @brief Tangent with an implicit reflected role.
     */
    [WBE_VERTEX_INPUT]
    float3 tangent;

    /**
     * @brief Bitangent with an implicit reflected role.
     */
    [WBE_VERTEX_INPUT]
    float3 bitangent;

    /**
     * @brief Texture coordinate with an implicit reflected role.
     */
    [WBE_VERTEX_INPUT]
    float2 uv;
};

/**
 * @brief Passes the test vertex position through to clip space.
 * @param p_input Reflected vertex input fields.
 * @return Clip-space vertex position.
 */
[shader("vertex")]
float4 vertex_main(VertexInputAttributes p_input) : SV_Position {
    return float4(p_input.position, 1.0);
}
