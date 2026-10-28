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
#include "utils/utils.slang"
#include "sort/radix_sort_uint2_utils.slang"

struct DescriptorIndices {
    [WBE_DESCRIPTOR("storage_buffer", "input_buffer", "read")] uint2 input_buffer;
    [WBE_DESCRIPTOR("storage_buffer", "histogram_buffer", "read")] uint2 histogram_buffer;
    [WBE_DESCRIPTOR("storage_buffer", "output_buffer", "write")] uint2 output_buffer;
    [WBE_PUSH_CONSTANT("frame_slot")] uint shader_debug_frame_slot;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

groupshared uint sort_entries[WBE_RADIX_SORT_UINT2_BATCH_SIZE];
groupshared uint sort_scratch[WBE_RADIX_SORT_UINT2_BATCH_SIZE];
groupshared uint2 batch_keys[WBE_RADIX_SORT_UINT2_BATCH_SIZE];
groupshared uint digit_offsets[WBE_RADIX_SORT_BUCKET_COUNT];
groupshared uint batch_digit_starts[WBE_RADIX_SORT_BUCKET_COUNT];

// Scatters the uint2 keys of one sub-histogram into their sorted positions for the digit at WBE_RADIX_HISTOGRAM_BIT_REGION;
// see radix_sort_uint2_scatter(). Must be dispatched with the local sort dispatch.
[shader("compute")]
[numthreads(WBE_RADIX_SORT_UINT2_WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
    uint frame_slot = descriptor_indices.shader_debug_frame_slot;
    CountedBuffer<uint2> input_buffer = CountedBuffer<uint2>(descriptor_indices.input_buffer, frame_slot);
    RWStructuredBuffer<uint> histogram_buffer = ResourceDescriptorHeap[descriptor_indices.histogram_buffer.x];
    CountedBuffer<uint2> output_buffer = CountedBuffer<uint2>(descriptor_indices.output_buffer, frame_slot);
    radix_sort_uint2_scatter(input_buffer, histogram_buffer, output_buffer, sort_entries, sort_scratch, batch_keys,
        digit_offsets, batch_digit_starts, p_group_id.x, p_group_thread_id.x, WBE_RADIX_HISTOGRAM_BIT_REGION);
}
