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
#include "max_downsample/max_downsample_utils.slang"
#include "utils.slang"

/**
 * @file max_downsample.slang
 * @brief Compute pass that writes one mip level as the per-channel maximum of each 2x2 region of the previous level.
 * @details RTMaxDownsampleVK binds the logical resources itself for every generated mip level: max_downsample_source is a
 * single-level view of the previous mip, and max_downsample_destination is a single-level view of the generated mip. The
 * dispatch covers the destination in 16x16 workgroups. See max_downsample() for the footprint of odd source extents.
 */

/** Descriptor indices of the two mip levels used by one max-downsample pass. */
struct MaxDownsampleDescriptorIndices {
    /** Previous mip level, read with texel loads. */
    [WBE_DESCRIPTOR("sampled_image", "max_downsample_source")] uint2 source;
    /** Generated mip level. */
    [WBE_DESCRIPTOR("storage_image", "max_downsample_destination", "write")] uint2 destination;
};

[vk::push_constant]
ConstantBuffer<MaxDownsampleDescriptorIndices> descriptor_indices;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
    Texture2D<float4> source = ResourceDescriptorHeap[descriptor_indices.source.x];
    RWTexture2D<float4> destination = ResourceDescriptorHeap[descriptor_indices.destination.x];
    uint2 destination_extent;
    destination.GetDimensions(destination_extent.x, destination_extent.y);
    uint2 dispatch_thread_id = z_order_swizzle_16x16(p_group_id.xy, p_group_thread_id.xy);
    if (dispatch_thread_id.x >= destination_extent.x || dispatch_thread_id.y >= destination_extent.y) {
        return;
    }
    uint2 source_extent;
    source.GetDimensions(source_extent.x, source_extent.y);
    destination[dispatch_thread_id] = max_downsample(source, source_extent, destination_extent, dispatch_thread_id);
}
