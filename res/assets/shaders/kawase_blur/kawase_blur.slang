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
#include "kawase_blur/kawase_blur_utils.slang"
#include "utils.slang"

/**
 * @file kawase_blur.slang
 * @brief General compute pass that writes one Kawase blur iteration between two ping-pong images.
 * @details The logical resources are placeholders; alias them to graph resources with RendTaskDescVK::alias(). The default
 * variant samples kawase_blur_ping and writes kawase_blur_pong; defining WBE_KAWASE_BLUR_WRITE_PING swaps them, so a
 * multi-iteration blur alternates the two variants in one ping-pong task. The dispatch covers the destination in 8x8
 * workgroups, and the source is sampled with normalized coordinates, so the two images may differ in extent. Define
 * WBE_KAWASE_BLUR_OFFSET to select the tap offset in source texels.
 */

#ifndef WBE_KAWASE_BLUR_OFFSET
#define WBE_KAWASE_BLUR_OFFSET 1.5
#endif

/** Descriptor indices of the resources used by one Kawase blur pass. */
struct KawaseBlurDescriptorIndices {
#ifdef WBE_KAWASE_BLUR_WRITE_PING
    /** Image sampled by the blur. */
    [WBE_DESCRIPTOR("sampled_image", "kawase_blur_pong")] uint2 source;
    /** Image receiving the blurred result. */
    [WBE_DESCRIPTOR("storage_image", "kawase_blur_ping", "write")] uint2 destination;
#else
    /** Image sampled by the blur. */
    [WBE_DESCRIPTOR("sampled_image", "kawase_blur_ping")] uint2 source;
    /** Image receiving the blurred result. */
    [WBE_DESCRIPTOR("storage_image", "kawase_blur_pong", "write")] uint2 destination;
#endif
    /** Linear-filtering sampler used for every tap. */
    [WBE_DESCRIPTOR("sampler", "kawase_blur_sampler")] uint2 blur_sampler;
};

[[vk::push_constant]]
ConstantBuffer<KawaseBlurDescriptorIndices> descriptor_indices;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
    Texture2D<float4> source = ResourceDescriptorHeap[descriptor_indices.source.x];
    RWTexture2D<float4> destination = ResourceDescriptorHeap[descriptor_indices.destination.x];
    SamplerState blur_sampler = SamplerDescriptorHeap[descriptor_indices.blur_sampler.x];
    uint destination_width;
    uint destination_height;
    destination.GetDimensions(destination_width, destination_height);
    uint2 dispatch_thread_id = z_order_swizzle_8x8(p_group_id.xy, p_group_thread_id.xy);
    if (dispatch_thread_id.x >= destination_width || dispatch_thread_id.y >= destination_height) {
        return;
    }
    uint source_width;
    uint source_height;
    source.GetDimensions(source_width, source_height);
    float2 sample_uv = (float2(dispatch_thread_id.xy) + 0.5) / float2(destination_width, destination_height);
    float2 source_inverse_extent = 1.0 / float2(source_width, source_height);
    destination[dispatch_thread_id.xy] =
        kawase_blur(source, blur_sampler, sample_uv, source_inverse_extent, WBE_KAWASE_BLUR_OFFSET);
}
