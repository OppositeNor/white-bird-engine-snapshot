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

struct MaskedShadowVSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float material_index : TEXCOORD1;
};

struct MaskedShadowDescriptorIndices {
    [WBE_DESCRIPTOR("uniform_buffer", "shadow_light_info")]
    uint2 light_camera_info;
    [WBE_DESCRIPTOR("uniform_buffer", "material_params")]
    uint2 material_params;
    [WBE_DESCRIPTOR("sampler", "material_sampler")]
    uint2 material_sampler;
    MeshDescriptorIndices mesh;
};

[[vk::push_constant]]
ConstantBuffer<MaskedShadowDescriptorIndices> descriptor_indices;

/**
 * @brief Transform a masked mesh vertex and resolve its material identifier.
 * @param p_input Incoming vertex attributes.
 * @param p_instance_id Index into mesh.instances.
 * @return Clip-space vertex output with material metadata.
 */
[shader("vertex")]
MaskedShadowVSOutput vertex_main(Vertex3DTexturedInput p_input, uint p_instance_id : SV_VulkanInstanceID) {
    ConstantBuffer<CameraInfo> light_camera_info = ResourceDescriptorHeap[descriptor_indices.light_camera_info.x];
    StructuredBuffer<InstanceData> instances = ResourceDescriptorHeap[descriptor_indices.mesh.instances.x];
    StructuredBuffer<SubmeshData> submeshes = ResourceDescriptorHeap[descriptor_indices.mesh.submeshes.x];
    InstanceData instance = instances[p_instance_id];
    float4 world_position = mul(load_mesh_node(descriptor_indices.mesh, instance.node_index).transform, float4(p_input.position.xyz, 1.0));
    MaskedShadowVSOutput output;
    float4 light_view_position = mul(light_camera_info.view, world_position);
    output.position = mul(light_camera_info.proj, light_view_position);
    output.uv = p_input.uv;
    output.material_index = float(submeshes[instance.submesh_index].material_id);
    return output;
}

[shader("fragment")]
float4 fragment_main(MaskedShadowVSOutput p_input) : SV_Target0 {
    ConstantBuffer<MaterialParamsBuffer> material_params = ResourceDescriptorHeap[descriptor_indices.material_params.x];
    SamplerState material_sampler = SamplerDescriptorHeap[descriptor_indices.material_sampler.x];
    uint material_index = uint(round(p_input.material_index));
    MaterialParams active_material = material_params.materials[material_index];
    uint albedo_index = uint(active_material.albedo_index >= 0 ? active_material.albedo_index : 0);
    uint albedo_descriptor_index = NonUniformResourceIndex(albedo_index);
    Texture2D<float4> albedo_texture = ResourceDescriptorHeap[albedo_descriptor_index];
    float alpha = albedo_texture.Sample(material_sampler, p_input.uv).a;
    if (alpha < 0.5) {
        discard;
    }
    return float4(0.0, 0.0, 0.0, 1.0);
}
