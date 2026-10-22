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
#include "svo/svo_utils.slang"
#include "svo/svo_dynamic_types.slang"
/**
 * @brief Descriptor-heap resources used by incremental maintenance.
 */
struct DynamicDescriptorIndices {
    /**
     * @brief Descriptor-heap reference to svo_info.
     */
    [WBE_DESCRIPTOR("storage_buffer", "svo_info")]
    uint2 info;
    /**
     * @brief Descriptor-heap reference to svo_node_buffer.
     */
    [WBE_DESCRIPTOR("storage_buffer", "svo_node_buffer")]
    uint2 nodes;
    /**
     * @brief Descriptor-heap reference to svo_dynamic_nodes.
     */
    [WBE_DESCRIPTOR("storage_buffer", "svo_dynamic_nodes", "read")]
    uint2 dynamic_nodes;
    /**
     * @brief Descriptor-heap reference to svo_dynamic_leaves.
     */
    [WBE_DESCRIPTOR("storage_buffer", "svo_dynamic_leaves", "read")]
    uint2 dynamic_leaves;
    /**
     * @brief Descriptor-heap reference to svo_deferred_list_ping.
     */
    [WBE_DESCRIPTOR("storage_buffer", "svo_deferred_list_ping", "write")]
    uint2 deferred_list_ping;
    /**
     * @brief Descriptor-heap reference to svo_deferred_list_pong.
     */
    [WBE_DESCRIPTOR("storage_buffer", "svo_deferred_list_pong", "write")]
    uint2 deferred_list_pong;
};
[[vk::push_constant]]
ConstantBuffer<DynamicDescriptorIndices> descriptor_indices;

/**
 * @brief Restores static parents and discards the previous frame's dynamic allocation ranges.
 * @details Leaf restoration uses its saved block directly, never the concurrently reset dynamic count.
 * A graph dependency must complete this entire dispatch before any dynamic allocation begins.
 */
[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint3 p_id : SV_DispatchThreadID) {
    RWStructuredBuffer<SVOInfo> info = ResourceDescriptorHeap[descriptor_indices.info.x];
    RWStructuredBuffer<SVONode> nodes = ResourceDescriptorHeap[descriptor_indices.nodes.x];
    StructuredBuffer<SVODynamicLeafBlock> leaves = ResourceDescriptorHeap[descriptor_indices.dynamic_leaves.x];
    uint index = p_id.x;
    if (index < info[0].node_count) {
        uint description = nodes[index].children_data;
        uint state = description >> SVO_CHILDREN_STATE_SHIFT;
        if (state == uint(SVONode::ChildrenState::NODE_DYNAMIC)) {
            nodes[index].children_data = uint(SVONode::ChildrenState::EMPTY) << SVO_CHILDREN_STATE_SHIFT;
        } else if (state == uint(SVONode::ChildrenState::LEAF_DYNAMIC)) {
            nodes[index].children_data = leaves[description & SVO_CHILDREN_BLOCK_INDEX_MASK].original_children_data;
        }
    }
    if (index == 0) {
        info[0].dynamic_node_count = 0;
        info[0].dynamic_leaves_count = 0;
        RWStructuredBuffer<uint> deferred_list_ping = ResourceDescriptorHeap[descriptor_indices.deferred_list_ping.x];
        RWStructuredBuffer<uint> deferred_list_pong = ResourceDescriptorHeap[descriptor_indices.deferred_list_pong.x];
        deferred_list_ping[SVO_DEFERRED_LIST_COUNT_INDEX] = 0;
        deferred_list_pong[SVO_DEFERRED_LIST_COUNT_INDEX] = 0;
    }
}
