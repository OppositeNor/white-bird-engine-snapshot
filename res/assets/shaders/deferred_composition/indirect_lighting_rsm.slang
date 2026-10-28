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

struct IndirectLightingDescriptorIndices {
    [WBE_DESCRIPTOR("uniform_buffer", "composition_camera_info")]
    uint2 camera_info;
    [WBE_DESCRIPTOR("storage_buffer", "spot_light_infos", "read")]
    uint2 light_infos;
    LightParamDescriptorIndices lights[WBE_MAX_SPOT_LIGHT_COUNT];
    [WBE_DESCRIPTOR("sampled_image", "gbuffer_albedo")]
    uint2 gbuffer_albedo;
    [WBE_DESCRIPTOR("sampled_image", "gbuffer_normal")]
    uint2 gbuffer_octa_normal;
    [WBE_DESCRIPTOR("sampled_image", "gbuffer_rma")]
    uint2 gbuffer_rma;
    [WBE_DESCRIPTOR("sampled_image", "gbuffer_depth")]
    uint2 gbuffer_depth;
    [WBE_DESCRIPTOR("sampler", "gbuffer_sampler")]
    uint2 gbuffer_sampler;
    [WBE_DESCRIPTOR("storage_image", "indirect_lighting")]
    uint2 indirect_lighting_output;
};

[vk::push_constant]
ConstantBuffer<IndirectLightingDescriptorIndices> descriptor_indices;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
    ConstantBuffer<CompositionCamera> camera_info = ResourceDescriptorHeap[descriptor_indices.camera_info.x];
    Texture2D<float4> gbuffer_albedo = ResourceDescriptorHeap[descriptor_indices.gbuffer_albedo.x];
    Texture2D<float2> gbuffer_octa_normal = ResourceDescriptorHeap[descriptor_indices.gbuffer_octa_normal.x];
    Texture2D<float4> gbuffer_rma = ResourceDescriptorHeap[descriptor_indices.gbuffer_rma.x];
    Texture2D<float> gbuffer_depth = ResourceDescriptorHeap[descriptor_indices.gbuffer_depth.x];
    SamplerState gbuffer_sampler = SamplerDescriptorHeap[descriptor_indices.gbuffer_sampler.x];
    RWTexture2D<float4> indirect_lighting_output = ResourceDescriptorHeap[descriptor_indices.indirect_lighting_output.x];
    uint width;
    uint height;
    uint2 dispatch_thread_id = z_order_swizzle_16x16(p_group_id.xy, p_group_thread_id.xy);
    indirect_lighting_output.GetDimensions(width, height);
    if (dispatch_thread_id.x >= width || dispatch_thread_id.y >= height) {
        return;
    }
    float2 uv = (float2(dispatch_thread_id) + 0.5) / float2(width, height);
    float depth = gbuffer_depth.SampleLevel(gbuffer_sampler, uv, 0.0);
    if (depth <= 0.0) {
        indirect_lighting_output[dispatch_thread_id] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    float4 albedo_matid = gbuffer_albedo.SampleLevel(gbuffer_sampler, uv, 0.0);
    float2 octa = gbuffer_octa_normal.SampleLevel(gbuffer_sampler, uv, 0.0);
    float4 rma_flags = gbuffer_rma.SampleLevel(gbuffer_sampler, uv, 0.0);
    float3 base_color = albedo_matid.rgb;
    float3 normal = octahedron_decode(octa);
    float metallic = clamp(rma_flags.g, 0.0, 1.0);
    float3 world_pos = world_position_from_depth(uv, depth, WBE_SOFTWARE_MATRIX_ROWS(camera_info.inv_view_proj));
    float3 indirect_irradiance = float3(0.0);
    uint spot_light_count = get_spot_light_count(descriptor_indices.light_infos);
    for (uint spot_light_index = 0; spot_light_index < spot_light_count; ++spot_light_index) {
        SpotLight spot_light = SpotLight(descriptor_indices.light_infos, descriptor_indices.lights[spot_light_index], spot_light_index);
        indirect_irradiance += compute_rsm_indirect(spot_light, uv, world_pos, normal);
    }
    float3 indirect_lighting = ((1.0 - metallic) * base_color / WBE_PI) * indirect_irradiance;
#if WBE_VOXEL_DEBUG_VIEW
    indirect_lighting += base_color * 0.24;
#endif
    indirect_lighting_output[dispatch_thread_id] = float4(indirect_lighting, 1.0);
}
