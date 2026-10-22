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
#ifndef WBE_FILE_DEFERRED_COMPOSITION_UTILS_SLANG
#define WBE_FILE_DEFERRED_COMPOSITION_UTILS_SLANG

#include "light/spot_light_utils.slang"
#include "utils.slang"

static const uint WBE_RSM_SAMPLE_COUNT = 4;
static const float WBE_RSM_SAMPLE_RADIUS_UV = 0.5;
static const float WBE_RSM_MIN_DISTANCE_SQUARED = 0.05;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct CompositionCamera {
    float4x4 inv_view_proj;
    float4 camera_world_pos;
};

VSOutput build_fullscreen_triangle(uint p_vertex_id) {
    VSOutput output;
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(0.0, 2.0),
        float2(2.0, 0.0)
    };
    output.position = float4(positions[p_vertex_id], 0.0, 1.0);
    output.uv = uvs[p_vertex_id];
    return output;
}

float distribution_ggx(float3 p_normal, float3 p_half_vector, float p_roughness) {
    float alpha = p_roughness * p_roughness;
    float alpha2 = alpha * alpha;
    float ndoth = max(dot(p_normal, p_half_vector), 0.0);
    float ndoth2 = ndoth * ndoth;
    float denominator = ndoth2 * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(WBE_PI * denominator * denominator, 0.000000000001);
}

float geometry_schlick_ggx(float p_ndotv, float p_roughness) {
    float k = (p_roughness + 1.0) * (p_roughness + 1.0) / 8.0;
    return p_ndotv / max(p_ndotv * (1.0 - k) + k, 0.0001);
}

float geometry_smith(float3 p_normal, float3 p_view_dir, float3 p_light_dir, float p_roughness) {
    float ndotv = max(dot(p_normal, p_view_dir), 0.0);
    float ndotl = max(dot(p_normal, p_light_dir), 0.0);
    return geometry_schlick_ggx(ndotv, p_roughness) * geometry_schlick_ggx(ndotl, p_roughness);
}

float3 fresnel_schlick(float p_cos_theta, float3 p_f0) {
    return p_f0 + (1.0 - p_f0) * pow(1.0 - clamp(p_cos_theta, 0.0, 1.0), 5.0);
}

float3 world_position_from_depth(float2 p_uv, float p_depth, float4x4 p_inv_view_proj) {
    float2 ndc_xy = p_uv * 2.0 - 1.0;
    float4 ndc = float4(ndc_xy, p_depth, 1.0);
    float4 world = mul(p_inv_view_proj, ndc);
    return world.xyz / world.w;
}

float2 random_rsm_disk_offset(float2 p_receiver_uv, uint p_spot_light_index, uint p_sample_index) {
    float sample_index = float(p_sample_index);
    float light_index = float(p_spot_light_index + 1);
    float2 seed = p_receiver_uv * 4096.0 + float2(sample_index * 17.0 + light_index * 31.0, sample_index * 29.0 + light_index * 13.0);
    float angle = 2.0 * WBE_PI * random(seed + float2(0.37, 0.61));
    float radius = (sample_index + random(seed + float2(0.83, 0.17))) / float(WBE_RSM_SAMPLE_COUNT);
    return float2(cos(angle), sin(angle)) * radius;
}

float3 compute_rsm_indirect(SpotLight p_spot_light, float2 p_receiver_uv, float3 p_world_position, float3 p_normal) {
    float4 light_view_position = mul(p_spot_light.info.view, float4(p_world_position, 1.0));
    float4 light_space_position = mul(p_spot_light.info.proj, light_view_position);
    float3 ndc = light_space_position.xyz / max(light_space_position.w, 0.0001);
    float2 center_uv = ndc.xy * 0.5 + 0.5;
    if (ndc.z < 0.0 || ndc.z > 1.0) {
        return float3(0.0);
    }
    uint2 shadow_size = p_spot_light.get_shadow_size();
    uint shadow_width = shadow_size.x;
    uint shadow_height = shadow_size.y;
    float2 texel_size = float2(1.0 / float(shadow_width), 1.0 / float(shadow_height));
    float3 indirect_irradiance = float3(0.0);
    float maximum_sample_distance_squared = 0.0;
    for (uint sample_index = 0; sample_index < WBE_RSM_SAMPLE_COUNT; ++sample_index) {
        float2 sample_disk_offset = random_rsm_disk_offset(p_receiver_uv, p_spot_light.index, sample_index);
        float sample_radius_squared = dot(sample_disk_offset, sample_disk_offset);
        float2 sample_uv = center_uv + sample_disk_offset * WBE_RSM_SAMPLE_RADIUS_UV;
        if (sample_uv.x < 0.0 || sample_uv.x >= 1.0 || sample_uv.y < 0.0 || sample_uv.y >= 1.0) {
            continue;
        }
        int2 sample_texel = int2(sample_uv * float2(shadow_width, shadow_height));
        float2 sample_texel_uv = (float2(sample_texel) + 0.5) * texel_size;
        float sample_depth = p_spot_light.shadow_moments.Load(int3(sample_texel, 0)).x;
        if (sample_depth <= 0.0) {
            continue;
        }
        float3 sample_flux = p_spot_light.rsm_flux.Load(int3(sample_texel, 0)).rgb;
        if (dot(sample_flux, sample_flux) < 0.000001) {
            continue;
        }
        float2 sample_octa_normal = p_spot_light.rsm_octa_normal.Load(int3(sample_texel, 0));
        float3 sample_world_position = p_spot_light.shadow_texel_world_position(sample_texel_uv, sample_depth);
        float3 sample_world_normal = octahedron_decode(sample_octa_normal);
        float3 receiver_to_sample = sample_world_position - p_world_position;
        float distance_squared = dot(receiver_to_sample, receiver_to_sample);
        if (distance_squared <= WBE_RSM_MIN_DISTANCE_SQUARED) {
            continue;
        }
        maximum_sample_distance_squared = max(maximum_sample_distance_squared, distance_squared);
        float receiver_numerator = max(dot(p_normal, receiver_to_sample), 0.0);
        float emitter_numerator = max(dot(sample_world_normal, -receiver_to_sample), 0.0);
        if (receiver_numerator <= 0.0 || emitter_numerator <= 0.0) {
            continue;
        }
        indirect_irradiance +=
            sample_flux * receiver_numerator * emitter_numerator * sample_radius_squared / (distance_squared * distance_squared);
    }
    return indirect_irradiance * (2.0 * maximum_sample_distance_squared / float(WBE_RSM_SAMPLE_COUNT));
}

#endif
