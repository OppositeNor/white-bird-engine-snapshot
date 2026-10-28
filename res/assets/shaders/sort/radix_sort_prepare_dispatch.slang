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
#include "sort/radix_sort_utils.slang"

struct DescriptorIndices {
    [WBE_DESCRIPTOR("storage_buffer", "input_buffer", "read")] uint2 input_buffer;
    [WBE_DESCRIPTOR("storage_buffer", "dispatch_buffer", "write")] uint2 dispatch_buffer;
    [WBE_PUSH_CONSTANT("frame_slot")] uint shader_debug_frame_slot;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

// Writes the indirect dispatches of a radix sort from the local sort count in the header of the key buffer: the local
// sort dispatch, with one workgroup per local sort, at WBE_RADIX_SORT_LOCAL_SORT_DISPATCH_START, and the histogram prefix
// scan dispatch, with one workgroup per WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM local sorts, at
// WBE_RADIX_SORT_HISTOGRAM_SCAN_DISPATCH_START. The element count does not change between the passes, so every pass uses the
// same dispatches. Must be dispatched with exactly one workgroup.
[shader("compute")]
[numthreads(1, 1, 1)]
void main() {
    CountedBuffer<uint> keys =
        CountedBuffer<uint>(descriptor_indices.input_buffer, descriptor_indices.shader_debug_frame_slot);
    RWStructuredBuffer<uint> dispatch_buffer = ResourceDescriptorHeap[descriptor_indices.dispatch_buffer.x];
    uint local_sort_num = keys.get_header(WBE_RADIX_SORT_LOCAL_SORT_NUM_INDEX);
    dispatch_buffer[WBE_RADIX_SORT_LOCAL_SORT_DISPATCH_START + 0] = local_sort_num;
    dispatch_buffer[WBE_RADIX_SORT_LOCAL_SORT_DISPATCH_START + 1] = 1;
    dispatch_buffer[WBE_RADIX_SORT_LOCAL_SORT_DISPATCH_START + 2] = 1;
    dispatch_buffer[WBE_RADIX_SORT_HISTOGRAM_SCAN_DISPATCH_START + 0] =
        (local_sort_num + WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM - 1) / WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM;
    dispatch_buffer[WBE_RADIX_SORT_HISTOGRAM_SCAN_DISPATCH_START + 1] = 1;
    dispatch_buffer[WBE_RADIX_SORT_HISTOGRAM_SCAN_DISPATCH_START + 2] = 1;
}
