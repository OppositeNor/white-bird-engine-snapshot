/* Copyright 2026 OppositeNor

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
#include "svo_2/svo_utils.slang"

/**
 * @brief Reflected descriptor-heap bindings for this construction stage.
 */
struct SVOUpdateStatesDescriptorIndices {
    /**
     * @brief Descriptor-heap reference to svo_info.
     */
    [WBE_DESCRIPTOR("storage_buffer", "svo_info", "read")]
    uint2 svo_info;
    /**
     * @brief Descriptor-heap reference to svo_node_buffer.
     */
    [WBE_DESCRIPTOR("storage_buffer", "svo_node_buffer")]
    uint2 tree_nodes;
};

[vk::push_constant]
ConstantBuffer<SVOUpdateStatesDescriptorIndices> descriptor_indices;

/**
 * @brief Publishes completed allocations between construction passes.
 * @details The producer must finish and make all child initialization visible before this dispatch.
 * Each invocation exclusively updates one node. A memory dependency must precede the next consumer.
 */
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    StructuredBuffer<SVOInfo> tree_info = ResourceDescriptorHeap[descriptor_indices.svo_info.x];
    uint node_index = p_dispatch_thread_id.x;
    if (node_index >= tree_info[0].node_count) {
        return;
    }
    RWStructuredBuffer<SVONode> nodes = ResourceDescriptorHeap[descriptor_indices.tree_nodes.x];
    uint children_data = nodes[node_index].children_data;
    if ((children_data >> SVO_CHILDREN_STATE_SHIFT) != uint(SVONode::ChildrenState::ALLOCATING)) {
        return;
    }
    uint state = (children_data & SVO_CHILDREN_OCCUPANCY_MASK) != 0 ? uint(SVONode::ChildrenState::LEAF)
                                                               : uint(SVONode::ChildrenState::NODE);
    nodes[node_index].children_data = (children_data & SVO_CHILDREN_PAYLOAD_MASK) | (state << SVO_CHILDREN_STATE_SHIFT);
}
