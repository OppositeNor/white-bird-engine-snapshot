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
 * @file counted_buffer_operations.slang
 * @brief Test kernel for CountedBuffer from utils/counted_buffer.slang.
 * @details Required defines: ELEMENT_TYPE and KERNEL_PASS. Pass 0 has every invocation reserve id % 3 + 1 elements with
 * atomic_expand() and fill them with its id. Pass 1 has every invocation reserve one element with try_atomic_expand(),
 * fill it with its id when it fits, and write whether it fit to results[id]. Pass 2 runs the non-atomic operations on
 * invocation 0 and writes their results to results. Pass 3 has every invocation add its id onto header word 3 with
 * atomic_add_header(), and invocation 0 read the buffer through a uint view and write its size and second element to
 * results.
 */
#include "utils/counted_buffer.slang"

RWByteAddressBuffer data;
RWStructuredBuffer<uint> results;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    CountedBuffer<ELEMENT_TYPE> buffer = CountedBuffer<ELEMENT_TYPE>(data, 0);
    uint id = p_dispatch_thread_id.x;
#if KERNEL_PASS == 0
    uint count = id % 3 + 1;
    uint first_index = buffer.atomic_expand(count);
    for (uint i = 0; i < count; ++i) {
        buffer.set(first_index + i, ELEMENT_TYPE(id));
    }
#elif KERNEL_PASS == 1
    uint first_index;
    bool fits = buffer.try_atomic_expand(1, first_index);
    if (fits) {
        buffer.set(first_index, ELEMENT_TYPE(id));
    }
    results[id] = fits ? 1 : 0;
#elif KERNEL_PASS == 2
    if (id != 0) {
        return;
    }
    results[0] = buffer.capacity();
    results[1] = buffer.size();
    results[2] = buffer.get_header(1);
    results[3] = buffer.get_header(2);
    results[4] = buffer.get_header(3);
    results[5] = buffer.get_byte_offset(1);
    uint first_index = buffer.expand(2);
    uint second_index = buffer.expand(3);
    results[6] = first_index;
    results[7] = second_index;
    results[8] = buffer.size();
    buffer.set(second_index, ELEMENT_TYPE(77));
    results[9] = all(buffer.get(second_index) == ELEMENT_TYPE(77)) ? 77 : 0;
    results[10] = buffer.byte_size();
    buffer.set_header(2, 0x1234);
#else
    buffer.atomic_add_header(3, id);
    if (id != 0) {
        return;
    }
    CountedBuffer<uint> words = CountedBuffer<uint>(data, 0);
    results[0] = words.size();
    results[1] = words.get(1);
#endif
}
