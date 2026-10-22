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
 * @file prefix_sum_pixel_sort_columns.slang
 * @brief Compute pass that turns the per-line mask counts into the end offset of every sorted line in the key buffer.
 * @details Dispatch exactly one workgroup. After the pass, the keys of line l occupy [offset(l - 1), offset(l)) of the
 * key data, with offset(-1) = 0. The pass does not depend on the sort direction, so both variants share it.
 */
#include "utils.slang"
#include "prefix_sum/prefix_sum_utils.slang"
#include "pixel_sort/pixel_sort_utils.slang"

/** Descriptor indices of the resources used by the column prefix sum pass. */
struct PrefixSumPixelSortColumnsDescriptorIndices {
    /** Buffer holding the mask count of every line, summed in place. */
    [WBE_DESCRIPTOR("storage_buffer", "pixel_sort_column_offsets", "read_write")] uint2 column_offsets;
};

[[vk::push_constant]]
ConstantBuffer<PrefixSumPixelSortColumnsDescriptorIndices> descriptor_indices;

groupshared uint prefix_sum_scratch[64];

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 p_group_thread_id : SV_GroupThreadID) {
    RWStructuredBuffer<uint> column_offsets = ResourceDescriptorHeap[descriptor_indices.column_offsets.x];
    uint column_count = column_offsets[0];
    workgroup_general_prefix_sum<64>(column_offsets, WBE_PIXEL_SORT_COLUMN_OFFSETS_START, column_count,
        p_group_thread_id.x, prefix_sum_scratch);
}
