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
 * @file write_sorted_pixels.slang
 * @brief Compute pass that writes the pixels of the source image in the order of the sorted radix sort keys.
 * @details Dispatch one 8x8 workgroup per 8x8 pixel tile. A pixel inside the contrast mask receives the source pixel of
 * the sorted key at its line rank, which register_pixel_sort_keys stored in the mask image. That key belongs to the
 * same line, so the masked pixels of every line end up sorted by the metric selected in PixelSortContrast, from the
 * smallest value, top to bottom in the vertical variant and left to right in the horizontal variant. With the span scope,
 * the key belongs to the same span, whose pixels are consecutive, so the source pixel is the pixel of the span at the
 * offset the key encodes, and every span ends up sorted on its own. Other pixels keep their source color.
 */
#include "utils.slang"
#include "sort/radix_sort_utils.slang"
#include "pixel_sort/pixel_sort_utils.slang"

/** Descriptor indices of the resources used by the write pass. */
struct WriteSortedPixelsDescriptorIndices {
    /** Image whose pixels are sorted. */
    [WBE_DESCRIPTOR("sampled_image", "pixel_sort_source")] uint2 source;
    /** Radix sort key buffer holding the sorted keys. */
    [WBE_DESCRIPTOR("storage_buffer", "pixel_sort_keys", "read")] uint2 keys;
    /** Mask image holding the line rank, span offset, and span length of every masked pixel, from register_pixel_sort_keys. */
    [WBE_DESCRIPTOR("storage_image", "pixel_sort_mask", "read")] uint2 mask;
    /** The scope of the sort. */
    [WBE_DESCRIPTOR("uniform_buffer", "pixel_sort_contrast")] uint2 contrast;
    /** The end offset of every line in the key buffer, from prefix_sum_pixel_sort_columns. */
    [WBE_DESCRIPTOR("storage_buffer", "pixel_sort_column_offsets", "read")] uint2 column_offsets;
    /** Image receiving the sorted pixels; must have the extent of source. */
    [WBE_DESCRIPTOR("storage_image", "pixel_sort_destination", "write")] uint2 destination;
};

[[vk::push_constant]]
ConstantBuffer<WriteSortedPixelsDescriptorIndices> descriptor_indices;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    Texture2D<float4> source = ResourceDescriptorHeap[descriptor_indices.source.x];
    RWStructuredBuffer<uint> keys = ResourceDescriptorHeap[descriptor_indices.keys.x];
    RWTexture2D<uint4> mask = ResourceDescriptorHeap[descriptor_indices.mask.x];
    ConstantBuffer<PixelSortContrast> contrast = ResourceDescriptorHeap[descriptor_indices.contrast.x];
    RWStructuredBuffer<uint> column_offsets = ResourceDescriptorHeap[descriptor_indices.column_offsets.x];
    RWTexture2D<float4> destination = ResourceDescriptorHeap[descriptor_indices.destination.x];
    uint2 extent;
    source.GetDimensions(extent.x, extent.y);
    uint2 pixel = p_dispatch_thread_id.xy;
    if (any(pixel >= extent)) {
        return;
    }
    uint4 mask_value = mask[pixel];
    uint rank = mask_value.x;
    uint2 source_pixel = pixel;
    if (rank != WBE_PIXEL_SORT_UNMASKED) {
        uint2 line_coordinates = pixel_sort_line_coordinates(pixel);
        uint key_index = pixel_sort_key_index(column_offsets, line_coordinates.x, rank);
        uint key = keys[WBE_RADIX_SORT_DATA_START + key_index];
        if (contrast.sort_scope == WBE_PIXEL_SORT_SCOPE_SPAN) {
            uint offset = mask_value.y;
            uint span_length = mask_value.z;
            uint source_offset = pixel_sort_span_key_offset(key, key_index - offset, span_length, extent);
            source_pixel = pixel_sort_line_pixel(uint2(line_coordinates.x, line_coordinates.y - offset + source_offset));
        } else {
            source_pixel = pixel_sort_key_pixel(key, line_coordinates.x, extent);
        }
    }
    destination[pixel] = float4(source.Load(int3(source_pixel, 0)).rgb, 1.0);
}
