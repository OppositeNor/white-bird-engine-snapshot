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
#include "deferred_gbuffer/deferred_gbuffer_utils.slang"

#ifdef WBE_REFLECTION_METADATA
ConstantBuffer<MaterialParamsBuffer> material_params;
ParameterBlock<MaterialTextureContractMetadata> material_textures;
ParameterBlock<MeshContractMetadata> mesh;
#endif

struct DeferredGBufferDescriptorIndices {
    [WBE_DESCRIPTOR("uniform_buffer", "gbuffer_camera_info")]
    uint2 camera_info;
    [WBE_DESCRIPTOR("uniform_buffer", "material_params")]
    uint2 material_params;
    [WBE_DESCRIPTOR("sampler", "material_sampler")]
    uint2 material_sampler;
    MeshDescriptorIndices mesh;
};

[[vk::push_constant]]
ConstantBuffer<DeferredGBufferDescriptorIndices> descriptor_indices;

/**
 * @brief Transform a mesh vertex for the deferred G-buffer pass.
 * @param p_input Incoming vertex attributes.
 * @param p_instance_id Index into mesh.instances.
 * @return Vertex output in world and clip space.
 */
[shader("vertex")]
VSOutput vertex_main(Vertex3DTexturedInput p_input, uint p_instance_id : SV_VulkanInstanceID) {
    ConstantBuffer<CameraInfo> camera_info = ResourceDescriptorHeap[descriptor_indices.camera_info.x];
    StructuredBuffer<InstanceData> instances = ResourceDescriptorHeap[descriptor_indices.mesh.instances.x];
    StructuredBuffer<SubmeshData> submeshes = ResourceDescriptorHeap[descriptor_indices.mesh.submeshes.x];
    InstanceData instance = instances[p_instance_id];
    NodeData node = load_mesh_node(descriptor_indices.mesh, instance.node_index);
    SubmeshData submesh = submeshes[instance.submesh_index];
    float4 world_position = mul(node.transform, float4(p_input.position.xyz, 1.0));
    float3x3 direction_transform = (float3x3)node.transform;

    VSOutput output;
    float4 view_position = mul(camera_info.view, world_position);
    output.position = mul(camera_info.proj, view_position);
    output.world_position = float4(world_position.xyz, float(submesh.material_id));
    output.normal = transform_static_normal(node.normal_transform, p_input.normal);
    output.tangent = normalize(mul(direction_transform, p_input.tangent));
    output.bitangent = normalize(mul(direction_transform, p_input.bitangent));
    output.uv = p_input.uv;
    return output;
}

/**
 * @brief Multi-render-target output written by the deferred G-buffer pass.
 */
struct GBufferOutput {
    /**
     * @brief Albedo (rgb) + material id (a). RGBA8 UNORM.
     */
    float4 albedo_matid : SV_Target0;
    /**
     * @brief Octahedron-encoded surface normal stored into R16G16_SNORM ([-1, 1]).
     */
    float2 octahedron_normal : SV_Target1;
    /**
     * @brief Roughness (r), metallic (g), ambient occlusion (b), flags (a). RGBA8 UNORM.
     */
    float4 rma_flags : SV_Target2;
};

/**
 * @brief Sample the active material and pack its surface data into the G-buffer outputs.
 * @param p_input The interpolated vertex output.
 * @param p_masked Whether alpha-cutout discard should be applied.
 * @param p_front_face Whether to retain the front-face lighting normal.
 * @return The packed G-buffer attachments for this fragment.
 */
static GBufferOutput shade_fragment(VSOutput p_input, bool p_masked, bool p_front_face) {
    GBufferOutput output;
    ConstantBuffer<MaterialParamsBuffer> material_params = ResourceDescriptorHeap[descriptor_indices.material_params.x];
    SamplerState material_sampler = SamplerDescriptorHeap[descriptor_indices.material_sampler.x];

    uint material_index = uint(round(p_input.world_position.w));
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
    if (p_masked && albedo_sample.a < 0.5) {
        discard;
    }
    float material_id_unorm = float(material_index) / 255.0;
    output.albedo_matid = float4(albedo_sample.rgb, material_id_unorm);

    float2 sampled_normal = normal_texture.Sample(material_sampler, p_input.uv).rg;
    float3 normal = normal_from_texture(sampled_normal, p_input.normal, p_input.tangent, p_input.bitangent);
    normal = p_front_face ? normal : -normal;
    output.octahedron_normal = octahedron_encode(normal);

    float3 rma = rma_texture.Sample(material_sampler, p_input.uv).rgb;
    float flags = 0.0;
    output.rma_flags = float4(rma, flags);

    return output;
}

[shader("fragment")]
GBufferOutput fragment_main(VSOutput p_input) {
    return shade_fragment(p_input, false, true);
}

[shader("fragment")]
GBufferOutput fragment_masked_main(VSOutput p_input) {
    return shade_fragment(p_input, true, true);
}

/**
 * @brief Shades opaque back faces with reversed lighting normals.
 */
[shader("fragment")]
GBufferOutput fragment_double_sided_main(VSOutput p_input, bool p_front_face : SV_IsFrontFace) {
    return shade_fragment(p_input, false, p_front_face);
}
