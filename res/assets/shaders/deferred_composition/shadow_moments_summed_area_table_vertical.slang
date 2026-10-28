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

struct ShadowMomentsVerticalDescriptorIndices {
    [WBE_DESCRIPTOR("sampled_image", "shadow_sat_horizontal")]
    uint2 shadow_moments_horizontal_input;
    [WBE_DESCRIPTOR("storage_image", "shadow_sat")]
    uint2 shadow_moments_summed_area_table_output;
};

[vk::push_constant]
ConstantBuffer<ShadowMomentsVerticalDescriptorIndices> descriptor_indices;

/**
 * @brief Complete a shadow-moment summed-area table from horizontal prefix sums.
 * @param p_dispatch_thread_id The dispatched column index.
 */
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    Texture2D<float2> shadow_moments_horizontal_input =
        ResourceDescriptorHeap[descriptor_indices.shadow_moments_horizontal_input.x];
    RWTexture2D<float2> shadow_moments_summed_area_table_output =
        ResourceDescriptorHeap[descriptor_indices.shadow_moments_summed_area_table_output.x];
    uint width;
    uint height;
    shadow_moments_horizontal_input.GetDimensions(width, height);
    uint column = p_dispatch_thread_id.x;
    if (column >= width) {
        return;
    }
    float2 moment_sum = float2(0.0);
    for (uint row = 0; row < height; ++row) {
        // SAT(x, y) = sum(H(x, j), j = 0..y).
        moment_sum += shadow_moments_horizontal_input.Load(int3(column, row, 0));
        shadow_moments_summed_area_table_output[uint2(column, row)] = moment_sum;
    }
}
