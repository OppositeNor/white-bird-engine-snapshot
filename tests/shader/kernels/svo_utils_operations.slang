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
 * @file svo_utils_operations.slang
 * @brief Test kernel that runs one SVO construction helper per entry of results.
 * @details Required defines: SVO_UTILS_HEADER, the quoted SVO utils header to test; SVO_UTILS_VERSION, 1 for
 * svo/svo_utils.slang and 2 for svo_2/svo_utils.slang; OPERATION, one of the OPERATION_* values below; and
 * IS_LEAF, the p_is_leaf argument of try_create_svo_entry(). Per-invocation inputs are read from arguments,
 * positions, and traces, and outputs are written to results and traces. The frame slot is always zero, and shader
 * debugging is disabled.
 */
#include "utils/utils.slang"
#include SVO_UTILS_HEADER

#define OPERATION_RESERVE_NODES 0
#define OPERATION_RESERVE_LEAVES 1
#define OPERATION_CREATE_CHILDREN_NODE 2
#define OPERATION_CREATE_CHILDREN_LEAF 3
#define OPERATION_CREATE_ENTRY 4
#define OPERATION_OCCUPY_LEAF 5
#define OPERATION_LOAD_NODE 6
#define OPERATION_PUBLISH_CHILDREN_DATA 7
#define OPERATION_VOXEL_ALLOCATE 8

RWStructuredBuffer<SVONode> nodes;
RWStructuredBuffer<SVOLeaf> leaves;
RWStructuredBuffer<SVOInfo> tree_info;
// Per-invocation node index, or (node index, value) pairs for OPERATION_PUBLISH_CHILDREN_DATA.
RWStructuredBuffer<uint2> arguments;
RWStructuredBuffer<float4> positions;
RWStructuredBuffer<SVONodeTraceInfo> traces;
RWStructuredBuffer<uint4> results;

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    uint invocation_count;
    uint result_stride;
    results.GetDimensions(invocation_count, result_stride);
    uint index = p_dispatch_thread_id.x;
    if (index >= invocation_count) {
        return;
    }
#if OPERATION == OPERATION_RESERVE_NODES
    uint children_index;
    bool succeeded = try_reserve_svo_nodes(nodes, tree_info, 0, children_index);
    results[index] = uint4(succeeded ? 1 : 0, children_index, 0, 0);
#elif OPERATION == OPERATION_RESERVE_LEAVES
    uint children_index;
    bool succeeded = try_reserve_svo_leaves(leaves, tree_info, 0, children_index);
    results[index] = uint4(succeeded ? 1 : 0, children_index, 0, 0);
#elif OPERATION == OPERATION_CREATE_CHILDREN_NODE
    uint children_index;
    bool succeeded = create_svo_children_node(nodes, tree_info, arguments[index].x, 0, children_index);
    results[index] = uint4(succeeded ? 1 : 0, children_index, 0, 0);
#elif OPERATION == OPERATION_CREATE_CHILDREN_LEAF
    uint children_index;
    bool succeeded = create_svo_children_leaf(leaves, tree_info, arguments[index].x, 0, children_index);
    results[index] = uint4(succeeded ? 1 : 0, children_index, 0, 0);
#elif OPERATION == OPERATION_CREATE_ENTRY
#if SVO_UTILS_VERSION == 1
    bool succeeded = try_create_svo_entry(nodes, leaves, tree_info, arguments[index].x, IS_LEAF != 0, 0);
#else
    bool succeeded = try_create_svo_entry(nodes, leaves, tree_info, arguments[index].x, IS_LEAF != 0,
        positions[index].xyz, 0);
#endif
    results[index] = uint4(succeeded ? 1 : 0, 0, 0, 0);
#elif OPERATION == OPERATION_OCCUPY_LEAF
    occupy_svo_leaf(nodes, arguments[index].x, positions[index].xyz);
    results[index] = uint4(1, 0, 0, 0);
#elif OPERATION == OPERATION_LOAD_NODE
    SVONode node = load_svo_node(nodes, arguments[index].x);
    results[index] = uint4(node.children_data, node.data_index, uint(node.parent_index), 0);
#elif OPERATION == OPERATION_PUBLISH_CHILDREN_DATA
    publish_svo_children_data(nodes, arguments[index].x, arguments[index].y);
    results[index] = uint4(1, 0, 0, 0);
#elif OPERATION == OPERATION_VOXEL_ALLOCATE
    SVONodeTraceInfo trace_info = traces[index];
    bool succeeded = try_voxel_allocate(nodes, leaves, tree_info, trace_info, 0);
    traces[index] = trace_info;
    results[index] = uint4(succeeded ? 1 : 0, 0, 0, 0);
#endif
}
