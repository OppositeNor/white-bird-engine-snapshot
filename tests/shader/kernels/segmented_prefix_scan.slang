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
/**
 * @file segmented_prefix_scan.slang
 * @brief Test kernel that runs an inclusive prefix scan of SegmentedWrapper<uint> elements, with OPERATOR<uint, Add<uint>>,
 * on one workgroup.
 * @details Required defines: OPERATOR, SegmentedPrefix or SegmentedBreakPrefix, and KERNEL_SCAN, 0 for
 * workgroup_general_prefix_sum and 1 for workgroup_inclusive_prefix_sum_64_256 over a 256-element buffer on a
 * 64-wide workgroup. workgroup_general_prefix_sum also requires KERNEL_WORKGROUP_SIZE, DATA_START, and DATA_COUNT, and
 * writes the total it returns to total[0].
 */
#include "prefix_sum/prefix_sum_utils.slang"

RWStructuredBuffer<SegmentedWrapper<uint> > data;
RWStructuredBuffer<SegmentedWrapper<uint> > total;

#if KERNEL_SCAN == 0
groupshared SegmentedWrapper<uint> scratch[KERNEL_WORKGROUP_SIZE];

[shader("compute")]
[numthreads(KERNEL_WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_thread_id : SV_GroupThreadID) {
    SegmentedWrapper<uint> result = workgroup_general_prefix_sum<SegmentedWrapper<uint>, OPERATOR<uint, Add<uint> >,
        KERNEL_WORKGROUP_SIZE>(data, DATA_START, DATA_COUNT, p_group_thread_id.x, scratch);
    if (p_group_thread_id.x == 0) {
        total[0] = result;
    }
}
#else
groupshared SegmentedWrapper<uint> shared_data[256];

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 p_group_thread_id : SV_GroupThreadID) {
    for (uint i = p_group_thread_id.x; i < 256; i += 64) {
        shared_data[i] = data[i];
    }
    GroupMemoryBarrierWithGroupSync();
    workgroup_inclusive_prefix_sum_64_256<SegmentedWrapper<uint>, OPERATOR<uint, Add<uint> > >(shared_data,
        p_group_thread_id.x * 4);
    for (uint i = p_group_thread_id.x; i < 256; i += 64) {
        data[i] = shared_data[i];
    }
}
#endif
