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
    [WBE_DESCRIPTOR("storage_buffer", "data_buffer", "read_write")] uint2 data_buffer;
    [WBE_DESCRIPTOR("storage_buffer", "sections_buffer", "write")] uint2 sections_buffer;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

groupshared uint block[WBE_LARGE_PREFIX_SCAN_BLOCK_SIZE];

// First pass of the large prefix scan; see large_prefix_scan_blocks(). Must be dispatched with one workgroup per block.
[shader("compute")]
[numthreads(WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
    RWStructuredBuffer<uint> data_buffer = ResourceDescriptorHeap[descriptor_indices.data_buffer.x];
    RWStructuredBuffer<uint> sections_buffer = ResourceDescriptorHeap[descriptor_indices.sections_buffer.x];
    large_prefix_scan_blocks(data_buffer, sections_buffer, p_group_id.x, p_group_thread_id.x, block);
}
