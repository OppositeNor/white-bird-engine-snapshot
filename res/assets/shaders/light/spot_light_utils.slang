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
#ifndef WBE_FILE_SPOT_LIGHT_UTILS_SLANG
#define WBE_FILE_SPOT_LIGHT_UTILS_SLANG

#ifndef WBE_USE_SUMMED_AREA_TABLE_SHADOW_MOMENTS
#define WBE_USE_SUMMED_AREA_TABLE_SHADOW_MOMENTS 0
#endif

#include "utils.slang"

/**
 * @brief Maximum number of spot lights a lighting pass evaluates.
 */
static const uint WBE_MAX_SPOT_LIGHT_COUNT = 4;

/**
 * @brief Spot light parameters shared by the lighting, shadow, and RSM passes. Cone angles are stored as cosines for cheap evaluation.
 */
struct SpotLightInfo {
    /// World-to-light view matrix used to sample the shadow map.
    float4x4 view;
    /// Light-space projection matrix used to sample the shadow map.
    float4x4 proj;
    /// Inverse light-space view-projection used to reconstruct world positions from shadow-map depth.
    float4x4 inv_view_proj;
    /// World-space position of the spot light.
    float3 position;
    /// Scalar intensity multiplier.
    float intensity;
    /// World-space direction the spot is pointing (unit vector).
    float3 direction;
    /// Cosine of the inner cone half-angle (full intensity inside).
    float inner_cone_cos;
    /// Linear-space color of the spot light.
    float3 color;
    /// Cosine of the outer cone half-angle (zero intensity outside).
    float outer_cone_cos;
    /// Maximum effective light distance.
    float range;
    /// Projected emitter width in world units.
    float emitter_width;
};

/**
 * @brief Spot light uniform payload holding every active spot light.
 */
struct SpotLightInfoBuffer {
    /// Active spot lights. Entries beyond spot_light_count are ignored.
    SpotLightInfo spot_light_info[WBE_MAX_SPOT_LIGHT_COUNT];
    /// Number of active spot light entries in spot_light_info.
    uint spot_light_count;
};

struct LightParamDescriptorIndices {
#if WBE_USE_SUMMED_AREA_TABLE_SHADOW_MOMENTS
    [WBE_DESCRIPTOR("sampled_image", "shadow_moments[]")]
#else
    [WBE_DESCRIPTOR("sampled_image", "shadow_moments_sampled[]")]
#endif
    uint2 shadowmap;
#if WBE_USE_SUMMED_AREA_TABLE_SHADOW_MOMENTS
    [WBE_DESCRIPTOR("sampled_image", "shadow_sat[]")]
    uint2 shadow_moments_summed_area_table;
#endif
    [WBE_DESCRIPTOR("sampled_image", "shadowmap_color[]")]
    uint2 rsm_flux;
    [WBE_DESCRIPTOR("sampled_image", "rsm_normal[]")]
    uint2 rsm_octa_normal;
    [WBE_DESCRIPTOR("sampler", "shadow_sampler")]
    uint2 shadowmap_sampler;
};

// Structured-buffer matrices use ordinary mul(). The uniform-buffer scalar-load workaround
// transposes these matrices and must not be applied to this returned value.
SpotLightInfo get_spot_light_info(uint2 p_light_infos, uint p_spot_light_index) {
    StructuredBuffer<SpotLightInfoBuffer> light_infos = ResourceDescriptorHeap[p_light_infos.x];
    return light_infos[0].spot_light_info[p_spot_light_index];
}

/**
 * @brief Read the number of active spot lights stored in a spot light table, clamped to WBE_MAX_SPOT_LIGHT_COUNT.
 * @param p_light_infos Descriptor indices of a storage buffer holding one SpotLightInfoBuffer.
 * @return The number of spot light entries to evaluate.
 */
uint get_spot_light_count(uint2 p_light_infos) {
    StructuredBuffer<SpotLightInfoBuffer> light_infos = ResourceDescriptorHeap[p_light_infos.x];
    return min(light_infos[0].spot_light_count, WBE_MAX_SPOT_LIGHT_COUNT);
}

/**
 * @brief Reconstruct the world position stored in one spot-light shadow-map texel.
 * @details The shadow moments store light distance divided by range, so the position is recovered by walking that distance
 * along the light-space ray through the texel.
 * @param p_spot_light_info Light whose shadow map produced the texel.
 * @param p_texel_uv UV of the texel center.
 * @param p_shadow_depth First shadow moment: light distance divided by range.
 * @return World-space position of the surface seen by the texel.
 */
float3 spot_shadow_texel_world_position(SpotLightInfo p_spot_light_info, float2 p_texel_uv, float p_shadow_depth) {
    // Any point on the texel's light-space ray works; use the far plane.
    float4 ray_point = mul(p_spot_light_info.inv_view_proj, float4(p_texel_uv * 2.0 - 1.0, 1.0, 1.0));
    float3 ray_direction = normalize(ray_point.xyz / ray_point.w - p_spot_light_info.position);
    return p_spot_light_info.position + ray_direction * p_shadow_depth * p_spot_light_info.range;
}

/**
 * @brief One spot light from a spot light table, packed with its shadow-map and RSM resources for easy access.
 * @details Resolves the light parameters and every resource referenced by its LightParamDescriptorIndices once, so passes
 * read them through one value. The light table and shadow resources must not be written during the calling pass.
 */
struct SpotLight {
    /**
     * @brief Light parameters read from the spot light table.
     */
    SpotLightInfo info;
    /**
     * @brief Index of the light in the spot light table.
     */
    uint index;
    /**
     * @brief Shadow moments: light distance divided by range, and its square.
     */
    Texture2D<float2> shadow_moments;
#if WBE_USE_SUMMED_AREA_TABLE_SHADOW_MOMENTS
    /**
     * @brief Inclusive summed-area table of shadow_moments.
     */
    Texture2D<float2> shadow_moments_summed_area_table;
#endif
    /**
     * @brief Reflective shadow-map flux.
     */
    Texture2D<float4> rsm_flux;
    /**
     * @brief Reflective shadow-map octahedron-encoded world normals.
     */
    Texture2D<float2> rsm_octa_normal;
    /**
     * @brief Sampler used for filtered shadow-moment lookups.
     */
    SamplerState shadowmap_sampler;

    /**
     * @brief Resolve one spot light and its shadow resources.
     * @param p_light_infos Descriptor indices of a storage buffer holding one SpotLightInfoBuffer.
     * @param p_light Shadow-resource descriptor indices of the light.
     * @param p_index Index of the light in the spot light table; must be below get_spot_light_count().
     */
    __init(uint2 p_light_infos, LightParamDescriptorIndices p_light, uint p_index) {
        info = get_spot_light_info(p_light_infos, p_index);
        index = p_index;
        shadow_moments = ResourceDescriptorHeap[p_light.shadowmap.x];
#if WBE_USE_SUMMED_AREA_TABLE_SHADOW_MOMENTS
        shadow_moments_summed_area_table = ResourceDescriptorHeap[p_light.shadow_moments_summed_area_table.x];
#endif
        rsm_flux = ResourceDescriptorHeap[p_light.rsm_flux.x];
        rsm_octa_normal = ResourceDescriptorHeap[p_light.rsm_octa_normal.x];
        shadowmap_sampler = SamplerDescriptorHeap[p_light.shadowmap_sampler.x];
    }

    /**
     * @brief Get the extent of the shadow map in texels.
     */
    uint2 get_shadow_size() {
        uint width;
        uint height;
        shadow_moments.GetDimensions(width, height);
        return uint2(width, height);
    }

    /**
     * @brief Reconstruct the world position stored in one shadow-map texel.
     * @param p_texel_uv UV of the texel center.
     * @param p_shadow_depth First shadow moment of the texel.
     * @return World-space position of the surface seen by the texel.
     */
    float3 shadow_texel_world_position(float2 p_texel_uv, float p_shadow_depth) {
        return spot_shadow_texel_world_position(info, p_texel_uv, p_shadow_depth);
    }

    /**
     * @brief Reconstruct the world position stored in the shadow-map texel containing a UV.
     * @details Loads the first shadow moment of the texel from mip level zero and reconstructs the position at the texel
     * center, as the two-argument overload does.
     * @param p_uv Shadow-map UV in [0, 1]; a UV of 1 selects the last texel.
     * @return World-space position of the surface seen by the texel.
     */
    float3 shadow_texel_world_position(float2 p_uv) {
        uint2 shadow_size = get_shadow_size();
        uint2 texel = min(uint2(p_uv * float2(shadow_size)), shadow_size - 1);
        float2 texel_uv = (float2(texel) + 0.5) / float2(shadow_size);
        float shadow_depth = shadow_moments.Load(int3(int2(texel), 0)).x;
        return shadow_texel_world_position(texel_uv, shadow_depth);
    }

    /**
     * @brief Sample the shadow moments with shadowmap_sampler.
     * @details Mip levels above zero exist only while the light binds the moments' mip chain, that is, without
     * WBE_USE_SUMMED_AREA_TABLE_SHADOW_MOMENTS.
     * @param p_uv Shadow-map UV.
     * @param p_mip Mip level to sample; fractional levels blend between neighboring levels.
     * @return The sampled first and second moments.
     */
    float2 sample_shadow_moments(float2 p_uv, float p_mip = 0.0) {
        return shadow_moments.SampleLevel(shadowmap_sampler, p_uv, p_mip);
    }

#if WBE_USE_SUMMED_AREA_TABLE_SHADOW_MOMENTS
    /**
     * @brief Sample the summed-area table of the shadow moments with shadowmap_sampler.
     * @param p_uv Shadow-map UV.
     * @return The sampled inclusive prefix sums of the first and second moments.
     */
    float2 sample_shadow_moments_summed_area_table(float2 p_uv) {
        return shadow_moments_summed_area_table.SampleLevel(shadowmap_sampler, p_uv, 0.0);
    }
#endif

    /**
     * @brief Sample the reflective shadow-map flux with shadowmap_sampler.
     * @param p_uv Shadow-map UV.
     * @return The sampled flux.
     */
    float4 sample_rsm_flux(float2 p_uv) {
        return rsm_flux.SampleLevel(shadowmap_sampler, p_uv, 0.0);
    }

    /**
     * @brief Sample the reflective shadow-map octahedron-encoded world normals with shadowmap_sampler.
     * @details The encoded value is filtered as is; filtering across an octahedron fold blends unrelated normals.
     * @param p_uv Shadow-map UV.
     * @return The sampled octahedron-encoded normal.
     */
    float2 sample_rsm_octa_normal(float2 p_uv) {
        return rsm_octa_normal.SampleLevel(shadowmap_sampler, p_uv, 0.0);
    }

#if WBE_USE_SUMMED_AREA_TABLE_SHADOW_MOMENTS
    /**
     * @brief Load one summed-area-table entry, treating coordinates outside its negative boundaries as zero.
     * @param p_texel The inclusive prefix-sum coordinate.
     * @return The summed first and second moments through p_texel, or zero outside the negative boundaries.
     */
    float2 load_shadow_moments_summed_area_table(int2 p_texel) {
        if (any(p_texel < int2(0))) {
            return float2(0.0);
        }
        return shadow_moments_summed_area_table.Load(int3(p_texel, 0));
    }
#endif

    /**
     * @brief Box-filter the shadow moments: exactly with the summed-area table when enabled, otherwise approximately with a
     * trilinearly sampled mip level.
     * @param p_center_uv The filter center in shadow-map UV coordinates.
     * @param p_radius_uv The square filter half-width in UV coordinates.
     * @param p_shadow_size The shadow-map extent in texels.
     * @return The filtered first and second moments.
     */
    float2 sample_shadow_moments(float2 p_center_uv, float p_radius_uv, uint2 p_shadow_size) {
#if WBE_USE_SUMMED_AREA_TABLE_SHADOW_MOMENTS
        int2 maximum_texel = int2(p_shadow_size) - int2(1);
        float2 minimum_uv = clamp(p_center_uv - p_radius_uv, 0.0, 1.0);
        float2 maximum_uv = clamp(p_center_uv + p_radius_uv, 0.0, 1.0);
        int2 minimum_texel = clamp(int2(floor(minimum_uv * float2(p_shadow_size))), int2(0), maximum_texel);
        int2 maximum_filter_texel =
            clamp(int2(ceil(maximum_uv * float2(p_shadow_size))) - int2(1), minimum_texel, maximum_texel);

        // sum([x0, x1] x [y0, y1]) = SAT(x1, y1) - SAT(x0 - 1, y1) - SAT(x1, y0 - 1) + SAT(x0 - 1, y0 - 1).
        float2 moment_sum = load_shadow_moments_summed_area_table(maximum_filter_texel) -
                            load_shadow_moments_summed_area_table(int2(minimum_texel.x - 1, maximum_filter_texel.y)) -
                            load_shadow_moments_summed_area_table(int2(maximum_filter_texel.x, minimum_texel.y - 1)) +
                            load_shadow_moments_summed_area_table(minimum_texel - int2(1));
        int2 filter_extent = maximum_filter_texel - minimum_texel + int2(1);
        float filter_area = float(filter_extent.x * filter_extent.y);
        // E[z^n] = sum(z_i^n) / sample_count.
        return clamp(moment_sum / filter_area, 0.0, 1.0);
#else
        float filter_width_texels = max(2.0 * p_radius_uv * float(p_shadow_size.x), 1.0);
        // L = log2(filter_width_texels).
        float maximum_mip_level = floor(log2(float(max(p_shadow_size.x, p_shadow_size.y))));
        float mip_level = clamp(log2(filter_width_texels), 0.0, maximum_mip_level);
        return shadow_moments.SampleLevel(shadowmap_sampler, p_center_uv, mip_level);
#endif
    }

    /**
     * @brief Compute the soft-shadow visibility of a world position with variance-based PCSS.
     * @param p_world_position Receiver position in world space.
     * @return Visibility in [0, 1]; positions outside the light frustum are fully visible.
     */
    float compute_shadow(float3 p_world_position) {
        float4 light_view_position = mul(info.view, float4(p_world_position, 1.0));
        float4 light_space_position = mul(info.proj, light_view_position);
        float3 ndc = light_space_position.xyz / max(light_space_position.w, 0.0001);
        float2 uv = ndc.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) {
            return 1.0;
        }
        const float bias = 0.0015;
        const float minimum_variance = 0.00002;
        const float light_bleeding_reduction = 0.2;
        float receiver_distance = length(info.position - p_world_position);
        // t = distance(light, receiver) / light_range - bias.
        float current_depth = clamp(receiver_distance / max(info.range, 0.0001) - bias, 0.0, 1.0);
        uint2 shadow_size = get_shadow_size();
        float emitter_width = max(info.emitter_width, 0.0);
        float4 light_near_homogeneous = mul(info.inv_view_proj, float4(0.0, 0.0, 1.0, 1.0));
        float3 light_near_position = light_near_homogeneous.xyz / light_near_homogeneous.w;
        float light_near_distance = max(length(light_near_position - info.position), 0.0001);
        // r_light_uv = emitter_width * |projection_x_scale| / (4 * near_distance).
        float emitter_radius_near_uv = emitter_width * abs(info.proj[0][0]) / (4.0 * light_near_distance);
        // r_search_uv = r_light_uv * (receiver_distance - near_distance) / receiver_distance.
        float search_radius_uv = emitter_radius_near_uv * max(receiver_distance - light_near_distance, 0.0) /
                                 max(receiver_distance, 0.0001);
        float2 search_moments = sample_shadow_moments(uv, search_radius_uv, shadow_size);
        if (current_depth <= search_moments.x) {
            return 1.0;
        }
        float non_blocker_probability = chebyshev_upper_bound(search_moments, current_depth, minimum_variance);
        if (non_blocker_probability >= 0.9999) {
            return 1.0;
        }
        // blocker_depth = (E[z] - p_non_blocker * receiver_depth) / (1 - p_non_blocker).
        float average_blocker_depth =
            (search_moments.x - non_blocker_probability * current_depth) / max(1.0 - non_blocker_probability, 0.0001);
        float light_near_depth = light_near_distance / max(info.range, 0.0001);
        average_blocker_depth = clamp(average_blocker_depth, light_near_depth, max(current_depth, light_near_depth));
        // penumbra_ratio = (receiver_depth - blocker_depth) / blocker_depth.
        float penumbra_ratio = max(current_depth - average_blocker_depth, 0.0) / max(average_blocker_depth, 0.0001);
        // filter_width_uv = emitter_width * |projection_x_scale| * penumbra_ratio / (2 * receiver_distance).
        float projected_filter_width_uv =
            emitter_width * abs(info.proj[0][0]) * penumbra_ratio / (2.0 * max(receiver_distance, 0.0001));
        float filter_radius_uv = 0.5 * projected_filter_width_uv;
        float2 filtered_moments = sample_shadow_moments(uv, filter_radius_uv, shadow_size);
        if (current_depth <= filtered_moments.x) {
            return 1.0;
        }
        float visibility = chebyshev_upper_bound(filtered_moments, current_depth, minimum_variance);
        // visibility_reduced = saturate((visibility - reduction) / (1 - reduction)).
        return saturate((visibility - light_bleeding_reduction) / (1.0 - light_bleeding_reduction));
    }
};

#endif
