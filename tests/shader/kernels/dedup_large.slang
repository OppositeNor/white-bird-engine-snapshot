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
 * @file dedup_large.slang
 * @brief Test kernel that runs dedup_large_mark() or dedup_large_scatter() from dedup/dedup_large_utils.slang for every key
 * of its dispatch.
 * @details Required define: KERNEL_PASS, 0 to mark and 1 to scatter.
 */
#include "dedup/dedup_large_utils.slang"

RWByteAddressBuffer keys;
RWStructuredBuffer<uint> marks;
RWByteAddressBuffer unique_keys;

[shader("compute")]
[numthreads(WBE_DEDUP_LARGE_WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
#if KERNEL_PASS == 0
    dedup_large_mark(CountedBuffer<uint>(keys, 0), marks, p_dispatch_thread_id.x);
#else
    dedup_large_scatter(CountedBuffer<uint>(keys, 0), marks, CountedBuffer<uint>(unique_keys, 0), p_dispatch_thread_id.x);
#endif
}
