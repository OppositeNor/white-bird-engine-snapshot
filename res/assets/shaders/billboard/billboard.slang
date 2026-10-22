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

struct BillboardInfo {
    float3 position;
    float2 size;
    float3 rotation_axis;
};

struct BillboardDescriptorIndices {
    [WBE_DESCRIPTOR("uniform_buffer", "gbuffer_camera_info")]
    uint2 camera_info;
    [WBE_DESCRIPTOR("uniform_buffer", "light_billboard_info")]
    uint2 billboard_info;
    [WBE_DESCRIPTOR("sampled_image", "light_billboard_texture")]
    uint2 texture_image;
    [WBE_DESCRIPTOR("sampler", "light_billboard_sampler")]
    uint2 texture_sampler;
};

[[vk::push_constant]]
ConstantBuffer<BillboardDescriptorIndices> descriptor_indices;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float2 quad_corner_from_vertex_id(uint p_vertex_id) {
    switch (p_vertex_id) {
    case 0:
        return float2(-0.5, -0.5);
    case 1:
        return float2(0.5, -0.5);
    case 2:
        return float2(0.5, 0.5);
    case 3:
        return float2(-0.5, -0.5);
    case 4:
        return float2(0.5, 0.5);
    default:
        return float2(-0.5, 0.5);
    }
}

float2 quad_uv_from_vertex_id(uint p_vertex_id) {
    switch (p_vertex_id) {
    case 0:
        return float2(0.0, 1.0);
    case 1:
        return float2(1.0, 1.0);
    case 2:
        return float2(1.0, 0.0);
    case 3:
        return float2(0.0, 1.0);
    case 4:
        return float2(1.0, 0.0);
    default:
        return float2(0.0, 0.0);
    }
}

float3 safe_normalize(float3 p_vector, float3 p_fallback) {
    float length_squared = dot(p_vector, p_vector);
    if (length_squared <= 0.000001) {
        return p_fallback;
    }
    return p_vector * rsqrt(length_squared);
}

float3 build_billboard_right(float3 p_up, float3 p_forward) {
    return safe_normalize(cross(p_up, p_forward), float3(1.0, 0.0, 0.0));
}

VSOutput build_billboard_vertex(uint p_vertex_id,
    bool p_limit_axis,
    ConstantBuffer<CameraInfo> p_camera_info,
    ConstantBuffer<BillboardInfo> p_billboard_info) {
    float3 billboard_position = p_billboard_info.position;
    float3 to_camera = safe_normalize(p_camera_info.world_position.xyz - billboard_position, float3(0.0, 0.0, 1.0));
    float3 world_up = abs(to_camera.y) > 0.99 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    float3 right = build_billboard_right(world_up, to_camera);
    float3 up = safe_normalize(cross(to_camera, right), world_up);

    if (p_limit_axis) {
        float3 axis = safe_normalize(p_billboard_info.rotation_axis, float3(0.0, 1.0, 0.0));
        float3 planar_to_camera = to_camera - axis * dot(to_camera, axis);
        float3 forward = safe_normalize(planar_to_camera, float3(0.0, 0.0, 1.0));
        right = safe_normalize(cross(axis, forward), right);
        up = safe_normalize(cross(forward, right), axis);
    }

    float2 quad_corner = quad_corner_from_vertex_id(p_vertex_id);
    float3 world_position = billboard_position + right * quad_corner.x * p_billboard_info.size.x + up * quad_corner.y * p_billboard_info.size.y;
    float4 view_position = mul(p_camera_info.view, float4(world_position, 1.0));

    VSOutput output;
    output.position = mul(p_camera_info.proj, view_position);
    output.uv = quad_uv_from_vertex_id(p_vertex_id);
    return output;
}

[shader("vertex")]
VSOutput vertex_face_camera(uint p_vertex_id : SV_VulkanVertexID) {
    ConstantBuffer<CameraInfo> camera_info = ResourceDescriptorHeap[descriptor_indices.camera_info.x];
    ConstantBuffer<BillboardInfo> billboard_info = ResourceDescriptorHeap[descriptor_indices.billboard_info.x];
    return build_billboard_vertex(p_vertex_id, false, camera_info, billboard_info);
}

[shader("vertex")]
VSOutput vertex_axis_limited(uint p_vertex_id : SV_VulkanVertexID) {
    ConstantBuffer<CameraInfo> camera_info = ResourceDescriptorHeap[descriptor_indices.camera_info.x];
    ConstantBuffer<BillboardInfo> billboard_info = ResourceDescriptorHeap[descriptor_indices.billboard_info.x];
    return build_billboard_vertex(p_vertex_id, true, camera_info, billboard_info);
}

[shader("fragment")]
float4 fragment_main(VSOutput p_input) : SV_Target0 {
    Texture2D<float4> texture_image = ResourceDescriptorHeap[descriptor_indices.texture_image.x];
    SamplerState texture_sampler = SamplerDescriptorHeap[descriptor_indices.texture_sampler.x];
    float4 sampled = texture_image.Sample(texture_sampler, p_input.uv);
    if (sampled.a <= 0.01) {
        discard;
    }
    return sampled;
}
