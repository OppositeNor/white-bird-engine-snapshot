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
/**
 * @file kawase_blur.slang
 * @brief Test kernel that evaluates kawase_blur at every texel center of source and writes the results row by row.
 * @details Required defines: SOURCE_WIDTH, SOURCE_HEIGHT, and BLUR_OFFSET, the diagonal tap distance in texels.
 */
#include "kawase_blur/kawase_blur_utils.slang"

Texture2D<float4> source;
SamplerState source_sampler;
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 p_thread_id : SV_DispatchThreadID) {
    if (p_thread_id.x >= SOURCE_WIDTH || p_thread_id.y >= SOURCE_HEIGHT) {
        return;
    }
    float2 inverse_extent = 1.0 / float2(SOURCE_WIDTH, SOURCE_HEIGHT);
    float2 uv = (float2(p_thread_id.xy) + 0.5) * inverse_extent;
    output[p_thread_id.y * SOURCE_WIDTH + p_thread_id.x] = kawase_blur(source, source_sampler, uv, inverse_extent,
        BLUR_OFFSET);
}
