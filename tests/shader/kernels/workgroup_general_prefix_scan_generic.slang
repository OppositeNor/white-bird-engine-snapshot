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
 * @file workgroup_general_prefix_scan_generic.slang
 * @brief Test kernel that runs workgroup_general_prefix_sum with a generic element type and operator on one workgroup.
 * @details Required defines: KERNEL_WORKGROUP_SIZE, DATA_START, DATA_COUNT, ELEMENT_TYPE, and OPERATOR.
 */
#include "prefix_sum/prefix_sum_utils.slang"

/**
 * @brief Keeps the later element unless it is zero; associative but not commutative.
 */
struct LastNonZero<T : IArithmetic & IComparable> : IOperator<T> {
    static T identity() {
        return T(0);
    }

    static T apply(T p_val1, T p_val2) {
        return p_val2 == T(0) ? p_val1 : p_val2;
    }
}

RWStructuredBuffer<ELEMENT_TYPE> data;
groupshared ELEMENT_TYPE scratch[KERNEL_WORKGROUP_SIZE];

[shader("compute")]
[numthreads(KERNEL_WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_thread_id : SV_GroupThreadID) {
    workgroup_general_prefix_sum<ELEMENT_TYPE, OPERATOR<ELEMENT_TYPE>, KERNEL_WORKGROUP_SIZE>(data, DATA_START, DATA_COUNT,
        p_group_thread_id.x, scratch);
}
