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
#include "prefix_sum/large_prefix_scan_utils.slang"

struct DescriptorIndices {
    [WBE_DESCRIPTOR("storage_buffer", "data_buffer", "read")] uint2 data_buffer;
    [WBE_DESCRIPTOR("storage_buffer", "dispatch_buffer", "write")] uint2 dispatch_buffer;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

// Writes the indirect dispatch of the block passes of a large prefix scan, with one workgroup per block of the element
// count in the metadata of the scanned buffer. Must be dispatched with exactly one workgroup.
[shader("compute")]
[numthreads(1, 1, 1)]
void main() {
    RWStructuredBuffer<uint> data_buffer = ResourceDescriptorHeap[descriptor_indices.data_buffer.x];
    RWStructuredBuffer<uint> dispatch_buffer = ResourceDescriptorHeap[descriptor_indices.dispatch_buffer.x];
    dispatch_buffer[0] = large_prefix_scan_block_num(data_buffer[WBE_LARGE_PREFIX_SCAN_ELEMENT_COUNT_INDEX]);
    dispatch_buffer[1] = 1;
    dispatch_buffer[2] = 1;
}
