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
 * @file register_pixel_sort_keys.slang
 * @brief Compute pass that registers the pixels inside the contrast mask into the radix sort key buffer.
 * @details One invocation per sorted line; dispatch ceil(width / 64) workgroups, or ceil(height / 64) for the horizontal
 * variant. Each invocation walks its line from the start and writes the key of every masked pixel to the next entry of
 * the line's range of the key buffer, and the pixel's rank among the masked pixels of its line to the mask image.
 * Pixels outside the mask get WBE_PIXEL_SORT_UNMASKED. The key buffer receives the total number of masked pixels as its
 * element count, and the number of radix sort local sorts of that count. With the span scope, a span starts at a masked pixel that is the first of its line or follows an
 * unmasked pixel; the invocation measures the span before registering its pixels with span keys, and also writes each
 * pixel's offset in its span and the span length to the mask image.
 */
#include "utils.slang"
#include "sort/radix_sort_utils.slang"
#include "pixel_sort/pixel_sort_utils.slang"

/** Descriptor indices of the resources used by the register pass. */
struct RegisterPixelSortKeysDescriptorIndices {
    /** Image whose pixels are sorted. */
    [WBE_DESCRIPTOR("sampled_image", "pixel_sort_source")] uint2 source;
    /** The contrast range of the sorted pixels and the metric they are sorted by. */
    [WBE_DESCRIPTOR("uniform_buffer", "pixel_sort_contrast")] uint2 contrast;
    /** The end offset of every line in the key buffer, from prefix_sum_pixel_sort_columns. */
    [WBE_DESCRIPTOR("storage_buffer", "pixel_sort_column_offsets", "read")] uint2 column_offsets;
    /** Radix sort key buffer receiving the keys, their count, and the local sort count. */
    [WBE_DESCRIPTOR("storage_buffer", "pixel_sort_keys", "write")] uint2 keys;
    /**
     * Mask image receiving the line rank of every masked pixel, and with the span scope its span offset and span length;
     * must have the extent of source, and be R16G16B16A16_UINT with the span scope or R16_UINT with the line scope.
     */
    [WBE_DESCRIPTOR("storage_image", "pixel_sort_mask", "write")] uint2 mask;
    /** Renderer frame slot receiving shader-debug errors. */
    [WBE_PUSH_CONSTANT("frame_slot")] uint shader_debug_frame_slot;
};

[vk::push_constant]
ConstantBuffer<RegisterPixelSortKeysDescriptorIndices> descriptor_indices;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    Texture2D<float4> source = ResourceDescriptorHeap[descriptor_indices.source.x];
    ConstantBuffer<PixelSortContrast> contrast = ResourceDescriptorHeap[descriptor_indices.contrast.x];
    RWStructuredBuffer<uint> column_offsets = ResourceDescriptorHeap[descriptor_indices.column_offsets.x];
    uint frame_slot = descriptor_indices.shader_debug_frame_slot;
    CountedBuffer<uint> keys = CountedBuffer<uint>(descriptor_indices.keys, frame_slot);
    RWTexture2D<uint4> mask = ResourceDescriptorHeap[descriptor_indices.mask.x];
    uint2 extent;
    source.GetDimensions(extent.x, extent.y);
    uint2 line_extent = pixel_sort_line_coordinates(extent);
    uint line = p_dispatch_thread_id.x;
    if (line >= line_extent.x) {
        return;
    }
    if (line == line_extent.x - 1) {
        uint element_count = column_offsets[WBE_PIXEL_SORT_COLUMN_OFFSETS_START + line];
        keys.set_size(element_count);
        keys.set_header(WBE_RADIX_SORT_LOCAL_SORT_NUM_INDEX, radix_sort_local_sort_num(element_count));
    }
    uint rank = 0;
    if (contrast.sort_scope == WBE_PIXEL_SORT_SCOPE_SPAN) {
        uint position = 0;
        while (position < line_extent.y) {
            uint2 pixel = pixel_sort_line_pixel(uint2(line, position));
            if (!pixel_sort_in_contrast(source.Load(int3(pixel, 0)).rgb, contrast)) {
                mask[pixel] = uint4(WBE_PIXEL_SORT_UNMASKED, 0, 0, 0);
                ++position;
                continue;
            }
            // The pixel starts a span, which ends before the next unmasked pixel or at the end of the line.
            uint span_length = 1;
            while (position + span_length < line_extent.y) {
                uint2 next_pixel = pixel_sort_line_pixel(uint2(line, position + span_length));
                if (!pixel_sort_in_contrast(source.Load(int3(next_pixel, 0)).rgb, contrast)) {
                    break;
                }
                ++span_length;
            }
            uint span_start = pixel_sort_key_index(column_offsets, line, rank);
            for (uint offset = 0; offset < span_length; ++offset) {
                uint2 span_pixel = pixel_sort_line_pixel(uint2(line, position + offset));
                float3 color = source.Load(int3(span_pixel, 0)).rgb;
                keys.set(span_start + offset,
                    pixel_sort_make_span_key(color, contrast.sort_metric, span_start, span_length, offset, extent));
                mask[span_pixel] = uint4(rank + offset, offset, span_length, 0);
            }
            rank += span_length;
            position += span_length;
        }
        return;
    }
    for (uint position = 0; position < line_extent.y; ++position) {
        uint2 pixel = pixel_sort_line_pixel(uint2(line, position));
        float3 color = source.Load(int3(pixel, 0)).rgb;
        if (pixel_sort_in_contrast(color, contrast)) {
            keys.set(pixel_sort_key_index(column_offsets, line, rank),
                pixel_sort_make_key(color, contrast.sort_metric, pixel, extent));
            mask[pixel] = uint4(rank, 0, 0, 0);
            ++rank;
        } else {
            mask[pixel] = uint4(WBE_PIXEL_SORT_UNMASKED, 0, 0, 0);
        }
    }
}
