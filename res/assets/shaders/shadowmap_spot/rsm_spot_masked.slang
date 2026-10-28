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
#include "forward_cook_torrance_pbr/forward_cook_torrance_pbr_utils.slang"
#include "utils.slang"

#ifdef WBE_REFLECTION_METADATA
ConstantBuffer<MaterialParamsBuffer> material_params;
ParameterBlock<MaterialTextureContractMetadata> material_textures;
ParameterBlock<MeshContractMetadata> mesh;
#endif

struct RSMVSOutput {
    float4 position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 tangent : TEXCOORD2;
    float3 bitangent : TEXCOORD3;
    float2 uv : TEXCOORD4;
    float material_index : TEXCOORD5;
};

struct RSMDescriptorIndices {
    [WBE_DESCRIPTOR("uniform_buffer", "shadow_light_info")]
    uint2 light_camera_info;
    [WBE_DESCRIPTOR("uniform_buffer", "material_params")]
    uint2 material_params;
    [WBE_DESCRIPTOR("sampler", "material_sampler")]
    uint2 material_sampler;
    MeshDescriptorIndices mesh;
};

[vk::push_constant]
ConstantBuffer<RSMDescriptorIndices> descriptor_indices;

struct RSMFragmentOutput {
    float4 flux : SV_Target0;
    float2 octahedron_normal : SV_Target1;
    float2 shadow_moments : SV_Target2;
};

float3 compute_rsm_flux(float3 p_world_position, float3 p_world_normal, float3 p_base_color, float p_metallic) {
    ConstantBuffer<SpotLightInfo> light_camera_info = ResourceDescriptorHeap[descriptor_indices.light_camera_info.x];
    float3 to_light = light_camera_info.position - p_world_position;
    float light_distance = length(to_light);
    float3 light_direction = to_light / max(light_distance, 0.0001);
    float3 spot_axis = normalize(light_camera_info.direction);
    float spot_cos = dot(-light_direction, spot_axis);
    float cone_falloff = saturate(
        (spot_cos - light_camera_info.outer_cone_cos) /
        max(light_camera_info.inner_cone_cos - light_camera_info.outer_cone_cos, 0.0001));
    float distance_falloff = saturate(1.0 - light_distance / max(light_camera_info.range, 0.0001));
    distance_falloff *= distance_falloff;
    float ndotl = max(dot(normalize(p_world_normal), light_direction), 0.0);
    float3 incident_radiance = light_camera_info.color * light_camera_info.intensity * cone_falloff * distance_falloff * ndotl;
    return incident_radiance * p_base_color * (1.0 - p_metallic);
}

/**
 * @brief Transform a mesh vertex for reflective shadow-map rendering.
 * @param p_input Incoming vertex attributes.
 * @param p_instance_id Index into mesh.instances.
 * @return Reflective shadow-map vertex output.
 */
[shader("vertex")]
RSMVSOutput vertex_main(Vertex3DTexturedInput p_input, uint p_instance_id : SV_VulkanInstanceID) {
    ConstantBuffer<SpotLightInfo> light_camera_info = ResourceDescriptorHeap[descriptor_indices.light_camera_info.x];
    StructuredBuffer<InstanceData> instances = ResourceDescriptorHeap[descriptor_indices.mesh.instances.x];
    StructuredBuffer<SubmeshData> submeshes = ResourceDescriptorHeap[descriptor_indices.mesh.submeshes.x];
    InstanceData instance = instances[p_instance_id];
    NodeData node = load_mesh_node(descriptor_indices.mesh, instance.node_index);
    float4 world_position = mul(node.transform, float4(p_input.position.xyz, 1.0));
    float3x3 direction_transform = (float3x3)node.transform;

    RSMVSOutput output;
    float4 light_view_position =
        software_mul_float4x4_vector(WBE_SOFTWARE_MATRIX_ROWS(light_camera_info.view), world_position);
    output.position =
        software_mul_float4x4_vector(WBE_SOFTWARE_MATRIX_ROWS(light_camera_info.proj), light_view_position);
    output.world_position = world_position.xyz;
    output.normal = transform_static_normal(node.normal_transform, p_input.normal);
    output.tangent = normalize(mul(direction_transform, p_input.tangent));
    output.bitangent = normalize(mul(direction_transform, p_input.bitangent));
    output.uv = p_input.uv;
    output.material_index = float(submeshes[instance.submesh_index].material_id);
    return output;
}

[shader("fragment")]
RSMFragmentOutput fragment_main(RSMVSOutput p_input) {
    ConstantBuffer<SpotLightInfo> light_camera_info = ResourceDescriptorHeap[descriptor_indices.light_camera_info.x];
    ConstantBuffer<MaterialParamsBuffer> material_params = ResourceDescriptorHeap[descriptor_indices.material_params.x];
    SamplerState material_sampler = SamplerDescriptorHeap[descriptor_indices.material_sampler.x];
    uint material_index = uint(round(p_input.material_index));
    MaterialParams active_material = material_params.materials[material_index];
    uint albedo_index = uint(active_material.albedo_index >= 0 ? active_material.albedo_index : 0);
    uint normal_index = uint(active_material.normal_index >= 0 ? active_material.normal_index : 0);
    uint rma_index = uint(active_material.rma_index >= 0 ? active_material.rma_index : 0);
    uint albedo_descriptor_index = NonUniformResourceIndex(albedo_index);
    uint normal_descriptor_index = NonUniformResourceIndex(normal_index);
    uint rma_descriptor_index = NonUniformResourceIndex(rma_index);

    Texture2D<float4> albedo_texture = ResourceDescriptorHeap[albedo_descriptor_index];
    Texture2D<float4> normal_texture = ResourceDescriptorHeap[normal_descriptor_index];
    Texture2D<float4> rma_texture = ResourceDescriptorHeap[rma_descriptor_index];
    float4 albedo_sample = albedo_texture.Sample(material_sampler, p_input.uv);
    if (albedo_sample.a < 0.5) {
        discard;
    }
    float2 sampled_normal = normal_texture.Sample(material_sampler, p_input.uv).rg;
    float3 rma = rma_texture.Sample(material_sampler, p_input.uv).rgb;
    float3 world_normal = normal_from_texture(sampled_normal, p_input.normal, p_input.tangent, p_input.bitangent);
    float3 rsm_flux = compute_rsm_flux(p_input.world_position, world_normal, albedo_sample.rgb, clamp(rma.g, 0.0, 1.0));
    RSMFragmentOutput output;
    output.flux = float4(rsm_flux, 1.0);
    output.octahedron_normal = octahedron_encode(normalize(world_normal));
    // Store (z, z^2); linear mip filtering produces (E[z], E[z^2]).
    float shadow_depth = saturate(length(light_camera_info.position - p_input.world_position) / max(light_camera_info.range, 0.0001));
    output.shadow_moments = float2(shadow_depth, shadow_depth * shadow_depth);
    return output;
}
