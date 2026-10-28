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
#include "deferred_composition/deferred_composition_utils.slang"

struct CombineLightingDescriptorIndices {
    [WBE_DESCRIPTOR("sampled_image", "gbuffer_rma")]
    uint2 gbuffer_rma;
    [WBE_DESCRIPTOR("sampled_image", "direct_lighting")]
    uint2 direct_lighting;
    [WBE_DESCRIPTOR("sampled_image", "indirect_lighting")]
    uint2 indirect_lighting;
    [WBE_DESCRIPTOR("storage_image", "result_image")]
    uint2 result_image;
};

[vk::push_constant]
ConstantBuffer<CombineLightingDescriptorIndices> descriptor_indices;

/**
 * @brief Combine direct lighting with indirect lighting attenuated by the G-buffer ambient occlusion.
 * @param p_group_id Dispatched workgroup index.
 * @param p_group_thread_id Thread index within the workgroup.
 */
[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
    Texture2D<float4> gbuffer_rma = ResourceDescriptorHeap[descriptor_indices.gbuffer_rma.x];
    Texture2D<float4> direct_lighting = ResourceDescriptorHeap[descriptor_indices.direct_lighting.x];
    Texture2D<float4> indirect_lighting = ResourceDescriptorHeap[descriptor_indices.indirect_lighting.x];
    RWTexture2D<float4> result_image = ResourceDescriptorHeap[descriptor_indices.result_image.x];
    uint width;
    uint height;
    uint2 dispatch_thread_id = z_order_swizzle_16x16(p_group_id.xy, p_group_thread_id.xy);
    result_image.GetDimensions(width, height);
    if (dispatch_thread_id.x >= width || dispatch_thread_id.y >= height) {
        return;
    }
    float ambient_occlusion = clamp(gbuffer_rma.Load(int3(dispatch_thread_id, 0)).b, 0.0, 1.0);
    float3 direct = direct_lighting.Load(int3(dispatch_thread_id, 0)).rgb;
    float3 indirect = indirect_lighting.Load(int3(dispatch_thread_id, 0)).rgb;
    result_image[dispatch_thread_id] = float4(direct + indirect * ambient_occlusion, 1.0);
}
