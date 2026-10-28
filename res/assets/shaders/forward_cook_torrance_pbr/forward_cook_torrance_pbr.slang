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

#ifdef WBE_REFLECTION_METADATA
ConstantBuffer<MaterialParamsBuffer> material_params;
ParameterBlock<MaterialTextureContractMetadata> material_textures;
ParameterBlock<MeshContractMetadata> mesh;
#endif

struct ForwardDescriptorIndices {
    [WBE_DESCRIPTOR("uniform_buffer", "camera_info")]
    uint2 camera_info;
    [WBE_DESCRIPTOR("uniform_buffer", "material_params")]
    uint2 material_params;
    ForwardLightParamDescriptorIndices lights[WBE_MAX_SPOT_LIGHT_COUNT];
    [WBE_DESCRIPTOR("uniform_buffer", "spot_light_count")]
    uint2 light_count;
    [WBE_DESCRIPTOR("sampler", "material_sampler")]
    uint2 material_sampler;
    MeshDescriptorIndices mesh;
};

[vk::push_constant]
ConstantBuffer<ForwardDescriptorIndices> descriptor_indices;

/**
 * @brief Transform a mesh vertex and resolve its bindless material identifier.
 * @param p_input Incoming vertex attributes.
 * @param p_instance_id Index into mesh.instances.
 * @return Vertex output in world and clip space.
 */
[shader("vertex")]
VSOutput vertex_main(Vertex3DTexturedInput p_input, uint p_instance_id : SV_VulkanInstanceID) {
    ConstantBuffer<CameraInfo> camera_info = ResourceDescriptorHeap[descriptor_indices.camera_info.x];
    StructuredBuffer<InstanceData> instances = ResourceDescriptorHeap[descriptor_indices.mesh.instances.x];
    StructuredBuffer<SubmeshData> submeshes = ResourceDescriptorHeap[descriptor_indices.mesh.submeshes.x];
    ConstantBuffer<uint> light_count = ResourceDescriptorHeap[descriptor_indices.light_count.x];
    InstanceData instance = instances[p_instance_id];
    NodeData node = load_mesh_node(descriptor_indices.mesh, instance.node_index);
    SubmeshData submesh = submeshes[instance.submesh_index];
    float4 world_position = mul(node.transform, float4(p_input.position.xyz, 1.0));
    float3x3 direction_transform = (float3x3)node.transform;

    VSOutput output;
    float4 view_position =
        software_mul_float4x4_vector(WBE_SOFTWARE_MATRIX_ROWS(camera_info.view), world_position);
    output.position = software_mul_float4x4_vector(WBE_SOFTWARE_MATRIX_ROWS(camera_info.proj), view_position);
    output.world_position = float4(world_position.xyz, float(submesh.material_id));
    output.normal = transform_static_normal(node.normal_transform, p_input.normal);
    output.tangent = normalize(mul(direction_transform, p_input.tangent));
    output.bitangent = normalize(mul(direction_transform, p_input.bitangent));
    output.uv = p_input.uv;
    if (light_count > 0) {
        ConstantBuffer<SpotLightInfo> light_info = ResourceDescriptorHeap[descriptor_indices.lights[0].info.x];
        float4 light_view_position =
            software_mul_float4x4_vector(WBE_SOFTWARE_MATRIX_ROWS(light_info.view), world_position);
        output.light_space_pos =
            software_mul_float4x4_vector(WBE_SOFTWARE_MATRIX_ROWS(light_info.proj), light_view_position);
    } else {
        output.light_space_pos = world_position;
    }
    return output;
}

/** Number of samples used for each test-only PCSS search and filtering pass. */
static const uint WBE_PCSS_SAMPLE_COUNT = 16;

/**
 * @brief Generate a rotated Vogel-disk sample for test-only PCSS filtering.
 * @param p_sample_index The sample index in the disk.
 * @param p_rotation The per-fragment rotation in radians.
 * @return A sample position inside the unit disk.
 */
float2 pcss_disk_sample(uint p_sample_index, float p_rotation) {
    const float golden_angle = 2.39996323;
    float radius = sqrt((float(p_sample_index) + 0.5) / float(WBE_PCSS_SAMPLE_COUNT));
    float angle = float(p_sample_index) * golden_angle + p_rotation;
    return radius * float2(cos(angle), sin(angle));
}

/**
 * @brief Convert one reversed shadow-map depth sample into light-view distance.
 * @param p_inv_view_proj_row_0 First inverse view-projection row.
 * @param p_inv_view_proj_row_1 Second inverse view-projection row.
 * @param p_inv_view_proj_row_2 Third inverse view-projection row.
 * @param p_inv_view_proj_row_3 Fourth inverse view-projection row.
 * @param p_view_row_0 First view row.
 * @param p_view_row_1 Second view row.
 * @param p_view_row_2 Third view row.
 * @param p_view_row_3 Fourth view row.
 * @param p_uv The shadow-map coordinate of the sample.
 * @param p_depth The reversed projected depth stored in the shadow map.
 * @return The positive distance from the light along its view axis.
 */
float shadow_depth_to_light_distance(float4 p_inv_view_proj_row_0,
    float4 p_inv_view_proj_row_1,
    float4 p_inv_view_proj_row_2,
    float4 p_inv_view_proj_row_3,
    float4 p_view_row_0,
    float4 p_view_row_1,
    float4 p_view_row_2,
    float4 p_view_row_3,
    float2 p_uv,
    float p_depth) {
    float4 world_position = software_mul_float4x4_vector(p_inv_view_proj_row_0,
        p_inv_view_proj_row_1,
        p_inv_view_proj_row_2,
        p_inv_view_proj_row_3,
        float4(p_uv * 2.0 - 1.0, p_depth, 1.0));
    world_position /= abs(world_position.w) > 0.0001 ? world_position.w : 0.0001;
    float4 light_view_position = software_mul_float4x4_vector(
        p_view_row_0, p_view_row_1, p_view_row_2, p_view_row_3, float4(world_position.xyz, 1.0));
    return max(abs(light_view_position.z), 0.0001);
}

/**
 * @brief Load an unfiltered depth texel from the test-only forward shadow map.
 * @param p_spot_light_index The light whose shadow map is sampled.
 * @param p_uv The normalized shadow-map coordinate.
 * @param p_shadow_size The shadow-map dimensions in texels.
 * @param p_sample_uv The texel-center coordinate that was actually loaded.
 * @return The reversed depth stored in the selected texel.
 */
float load_shadow_depth(uint p_spot_light_index, float2 p_uv, uint2 p_shadow_size, out float2 p_sample_uv) {
    Texture2D<float> shadowmap = ResourceDescriptorHeap[descriptor_indices.lights[p_spot_light_index].shadowmap.x];
    int2 maximum_texel = int2(p_shadow_size) - 1;
    int2 sample_texel = clamp(int2(p_uv * float2(p_shadow_size)), int2(0), maximum_texel);
    p_sample_uv = (float2(sample_texel) + 0.5) / float2(p_shadow_size);
    return shadowmap.Load(int3(sample_texel, 0));
}

/**
 * @brief Compute test-only PCSS visibility for a reversed-depth spot-light shadow map.
 * @param p_spot_light_index The light whose shadow map is sampled.
 * @param p_light_space_pos The fragment position transformed by the spot light's view and projection matrices.
 * @return The percentage-closer visibility in [0, 1].
 */
float compute_spot_shadow(uint p_spot_light_index, float4 p_light_space_pos) {
    Texture2D<float> shadowmap = ResourceDescriptorHeap[descriptor_indices.lights[p_spot_light_index].shadowmap.x];
    float3 ndc = p_light_space_pos.xyz / max(p_light_space_pos.w, 0.0001);
    float2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }
    const float bias = 0.0015;
    float current_depth = clamp(ndc.z + bias, 0.0, 1.0);
    uint shadow_width;
    uint shadow_height;
    shadowmap.GetDimensions(shadow_width, shadow_height);
    uint2 shadow_size = uint2(shadow_width, shadow_height);
    ConstantBuffer<SpotLightInfo> spot_light_info =
        ResourceDescriptorHeap[descriptor_indices.lights[p_spot_light_index].info.x];
    float receiver_distance = max(abs(p_light_space_pos.w), 0.0001);
    float light_near_distance = shadow_depth_to_light_distance(WBE_SOFTWARE_MATRIX_ROWS(spot_light_info.inv_view_proj),
        WBE_SOFTWARE_MATRIX_ROWS(spot_light_info.view),
        float2(0.5),
        1.0);
    // r_light_uv = emitter_width * |projection_x_scale| / (4 * near_distance).
    float emitter_radius_uv =
        max(spot_light_info.emitter_width, 0.0) * abs(spot_light_info.proj[0][0]) / (4.0 * light_near_distance);
    // r_search_uv = r_light_uv * (receiver_distance - near_distance) / receiver_distance.
    float search_radius_uv = emitter_radius_uv * max(receiver_distance - light_near_distance, 0.0) / receiver_distance;
    float sample_rotation = random(uv) * 2.0 * WBE_PI;

    // Search for shadow texels that are closer to the light than the receiver.
    float blocker_distance_sum = 0.0;
    uint blocker_count = 0;
    for (uint sample_index = 0; sample_index < WBE_PCSS_SAMPLE_COUNT; ++sample_index) {
        float2 sample_uv;
        float2 search_uv = uv + pcss_disk_sample(sample_index, sample_rotation) * search_radius_uv;
        float sampled_depth = load_shadow_depth(p_spot_light_index, search_uv, shadow_size, sample_uv);
        if (sampled_depth > current_depth) {
            blocker_distance_sum += shadow_depth_to_light_distance(WBE_SOFTWARE_MATRIX_ROWS(spot_light_info.inv_view_proj),
                WBE_SOFTWARE_MATRIX_ROWS(spot_light_info.view),
                sample_uv,
                sampled_depth);
            blocker_count += 1;
        }
    }
    if (blocker_count == 0) {
        return 1.0;
    }

    // blocker_distance = sum(blocker_distances) / blocker_count.
    float average_blocker_distance = blocker_distance_sum / float(blocker_count);
    // penumbra_ratio = (receiver_distance - blocker_distance) / blocker_distance.
    float penumbra_ratio = max(receiver_distance - average_blocker_distance, 0.0) / average_blocker_distance;
    // r_filter_uv = penumbra_ratio * r_light_uv * near_distance / receiver_distance.
    float filter_radius_uv = penumbra_ratio * emitter_radius_uv * light_near_distance / receiver_distance;

    // Percentage-filter the reversed-depth comparisons across the estimated penumbra.
    float visibility = 0.0;
    for (uint sample_index = 0; sample_index < WBE_PCSS_SAMPLE_COUNT; ++sample_index) {
        float2 sample_uv;
        float2 filter_uv = uv + pcss_disk_sample(sample_index, sample_rotation) * filter_radius_uv;
        float sampled_depth = load_shadow_depth(p_spot_light_index, filter_uv, shadow_size, sample_uv);
        visibility += current_depth >= sampled_depth ? 1.0 : 0.0;
    }
    return visibility / float(WBE_PCSS_SAMPLE_COUNT);
}

/**
 * @brief Evaluate the Cook-Torrance BRDF for one forward-shaded fragment lit by a single shadow-mapped spot light.
 * @param p_input The interpolated vertex output.
 * @param p_masked Whether fragments below the alpha-mask cutoff should be discarded.
 * @param p_front_face Whether to retain the front-face lighting normal.
 * @return The linear shaded color with alpha = 1.0.
 */
float4 shade_fragment(VSOutput p_input, bool p_masked, bool p_front_face) {
    ConstantBuffer<CameraInfo> camera_info = ResourceDescriptorHeap[descriptor_indices.camera_info.x];
    ConstantBuffer<MaterialParamsBuffer> material_params = ResourceDescriptorHeap[descriptor_indices.material_params.x];
    ConstantBuffer<uint> light_count = ResourceDescriptorHeap[descriptor_indices.light_count.x];
    SamplerState material_sampler = SamplerDescriptorHeap[descriptor_indices.material_sampler.x];
    // The submesh material id crosses the rasterizer in world_position.w. Round before using it as a table index.
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
    float3 base_color = albedo_sample.rgb;
    float2 sampled_normal = normal_texture.Sample(material_sampler, p_input.uv).rg;
    float3 rma = rma_texture.Sample(material_sampler, p_input.uv).rgb;
    float roughness = clamp(rma.r, 0.04, 1.0);
    float metallic = clamp(rma.g, 0.0, 1.0);

    float3 n = normal_from_texture(sampled_normal, p_input.normal, p_input.tangent, p_input.bitangent);
    n = p_front_face ? n : -n;
    float3 v = normalize(camera_info.world_position.xyz - p_input.world_position.xyz);

    float3 f0 = lerp(float3(0.04), base_color, metallic);

    float3 color = float3(0.0);
    uint spot_light_count = min(light_count, WBE_MAX_SPOT_LIGHT_COUNT);
    for (uint spot_light_index = 0; spot_light_index < spot_light_count; ++spot_light_index) {
        ConstantBuffer<SpotLightInfo> spot_light_info =
            ResourceDescriptorHeap[descriptor_indices.lights[spot_light_index].info.x];
        float3 to_light = spot_light_info.position - p_input.world_position.xyz;
        float light_distance = length(to_light);
        float3 l = to_light / max(light_distance, 0.0001);
        float3 spot_axis = normalize(spot_light_info.direction);

        float spot_cos = dot(-l, spot_axis);
        float cone_falloff = saturate(
            (spot_cos - spot_light_info.outer_cone_cos) /
            max(spot_light_info.inner_cone_cos - spot_light_info.outer_cone_cos, 0.0001));
        float distance_falloff = saturate(1.0 - light_distance / max(spot_light_info.range, 0.0001));
        distance_falloff *= distance_falloff;

        float3 h = normalize(v + l);
        float ndotl = max(dot(n, l), 0.0);
        float ndf = distribution_ggx(n, h, roughness);
        float geometry = geometry_smith(n, v, l, roughness);
        float3 fresnel = fresnel_schlick(max(dot(h, v), 0.0), f0);
        float3 specular = ndf * geometry * fresnel / max(4.0 * max(dot(n, v), 0.0) * ndotl, 0.0001);
        float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base_color / WBE_PI;

        float3 radiance = spot_light_info.color * spot_light_info.intensity * cone_falloff * distance_falloff;
        float shadow = spot_light_index == 0 ? compute_spot_shadow(spot_light_index, p_input.light_space_pos) : 1.0;
        color += (diffuse + specular) * radiance * ndotl * shadow;
    }
    color = color / (color + float3(1.0));
    return float4(color, 1.0);
}

[shader("fragment")]
float4 fragment_main(VSOutput p_input) : SV_Target0 {
    return shade_fragment(p_input, false, true);
}

[shader("fragment")]
float4 fragment_masked_main(VSOutput p_input) : SV_Target0 {
    return shade_fragment(p_input, true, true);
}

/**
 * @brief Shades opaque back faces with reversed lighting normals.
 */
[shader("fragment")]
float4 fragment_double_sided_main(VSOutput p_input, bool p_front_face : SV_IsFrontFace) : SV_Target0 {
    return shade_fragment(p_input, false, p_front_face);
}
