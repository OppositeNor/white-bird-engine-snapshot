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
#ifndef WBE_FILE_DEFERRED_GBUFFER_UTILS_SLANG
#define WBE_FILE_DEFERRED_GBUFFER_UTILS_SLANG

#include "utils.slang"


/**
 * @brief Vertex shader output / fragment shader input for the deferred G-buffer pass.
 */
struct VSOutput {
    /**
 * @brief Clip-space position.
     */
    float4 position       : SV_Position;
    /**
     * @brief World-space position in xyz. The w component stores the indirect draw's material-table index.
     */
    float4 world_position : TEXCOORD0;
    /**
 * @brief World-space normal transformed from the vertex attribute.
     */
    float3 normal         : TEXCOORD1;
    /**
 * @brief World-space tangent transformed from the vertex attribute.
     */
    float3 tangent        : TEXCOORD2;
    /**
 * @brief World-space bitangent transformed from the vertex attribute.
     */
    float3 bitangent      : TEXCOORD3;
    /**
 * @brief Texture coordinate interpolated from the vertex attribute.
     */
    float2 uv             : TEXCOORD4;
};

/**
 * @brief Compute a world-space surface normal from a tangent-space normal map sample.
 * @param p_sampled_normal The encoded normal-map X and Y components in [0, 1].
 * @param p_normal The interpolated surface normal.
 * @param p_tangent The interpolated surface tangent.
 * @param p_bitangent The interpolated surface bitangent.
 * @return The unit-length world-space normal.
 */
float3 normal_from_texture(float2 p_sampled_normal, float3 p_normal, float3 p_tangent, float3 p_bitangent) {
    float3 normal = normalize(p_normal);
    float3 tangent = p_tangent;
    float3 bitangent = p_bitangent;
    if (dot(tangent, tangent) < 0.0001 || dot(bitangent, bitangent) < 0.0001) {
        float3 helper_axis = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
        tangent = normalize(cross(helper_axis, normal));
        bitangent = normalize(cross(normal, tangent));
    } else {
        tangent = normalize(tangent - normal * dot(normal, tangent));
        float handedness = dot(cross(normal, tangent), bitangent) < 0.0 ? -1.0 : 1.0;
        bitangent = normalize(cross(normal, tangent)) * handedness;
    }
    float3 tangent_space_normal = decode_normal_map(p_sampled_normal);
    return normalize(tangent * tangent_space_normal.x + bitangent * tangent_space_normal.y + normal * tangent_space_normal.z);
}

#endif
