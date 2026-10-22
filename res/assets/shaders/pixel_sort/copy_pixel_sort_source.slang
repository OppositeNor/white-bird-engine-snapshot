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
 * @file copy_pixel_sort_source.slang
 * @brief Compute pass that copies an image into the pixel sort source, so that the sorted pixels can be written back to
 * the copied image in place.
 * @details Dispatch one 8x8 workgroup per 8x8 pixel tile of the input image.
 */
#include "utils.slang"

/** Descriptor indices of the resources used by the copy pass. */
struct CopyPixelSortSourceDescriptorIndices {
    /** Image whose pixels are sorted. */
    [WBE_DESCRIPTOR("sampled_image", "pixel_sort_input")] uint2 input;
    /** Image receiving the copy; must have the extent of input. */
    [WBE_DESCRIPTOR("storage_image", "pixel_sort_source", "write")] uint2 source;
};

[[vk::push_constant]]
ConstantBuffer<CopyPixelSortSourceDescriptorIndices> descriptor_indices;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    Texture2D<float4> input = ResourceDescriptorHeap[descriptor_indices.input.x];
    RWTexture2D<float4> source = ResourceDescriptorHeap[descriptor_indices.source.x];
    uint2 extent;
    input.GetDimensions(extent.x, extent.y);
    uint2 pixel = p_dispatch_thread_id.xy;
    if (any(pixel >= extent)) {
        return;
    }
    source[pixel] = input.Load(int3(pixel, 0));
}
