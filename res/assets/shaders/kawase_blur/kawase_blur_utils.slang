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
#ifndef WBE_FILE_KAWASE_BLUR_UTILS_SLANG
#define WBE_FILE_KAWASE_BLUR_UTILS_SLANG

/**
 * @brief Run one Kawase blur iteration at a texture coordinate.
 * @details Averages four bilinear samples placed diagonally around the coordinate. Chain iterations through ping-pong
 * textures to widen the blur; the classic sequence increases the offset each iteration (for example 0.5, 1.5, 2.5).
 * The sampler should use linear filtering so each tap blends four texels.
 * @param p_source Texture to blur.
 * @param p_sampler Sampler used for every tap.
 * @param p_uv Normalized texture coordinate of the output texel center.
 * @param p_inverse_extent Reciprocal of the source texture extent in texels.
 * @param p_offset Diagonal tap distance from the coordinate, in texels.
 * @return The average of the four taps.
 */
float4 kawase_blur(Texture2D<float4> p_source, SamplerState p_sampler, float2 p_uv, float2 p_inverse_extent, float p_offset) {
    float2 sample_offset = p_offset * p_inverse_extent;
    float4 accumulated_color = p_source.SampleLevel(p_sampler, p_uv + float2(-sample_offset.x, -sample_offset.y), 0);
    accumulated_color += p_source.SampleLevel(p_sampler, p_uv + float2(sample_offset.x, -sample_offset.y), 0);
    accumulated_color += p_source.SampleLevel(p_sampler, p_uv + float2(-sample_offset.x, sample_offset.y), 0);
    accumulated_color += p_source.SampleLevel(p_sampler, p_uv + float2(sample_offset.x, sample_offset.y), 0);
    return accumulated_color * 0.25;
}

#endif
