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
#include "svo_2/svo_utils.slang"
#include "svo/svo_dynamic_types.slang"
/**
 * @brief Descriptor-heap resources used by incremental maintenance.
 */
struct DynamicDescriptorIndices {
    /**
     * @brief Descriptor-heap reference to svo_info.
     */
    [WBE_DESCRIPTOR("storage_buffer", "svo_info", "read")]
    uint2 info;
    /**
     * @brief Descriptor-heap reference to svo_node_buffer.
     */
    [WBE_DESCRIPTOR("storage_buffer", "svo_node_buffer")]
    uint2 nodes;
    /**
     * @brief Descriptor-heap reference to svo_dynamic_nodes.
     */
    [WBE_DESCRIPTOR("storage_buffer", "svo_dynamic_nodes")]
    uint2 dynamic_nodes;
    /**
     * @brief Descriptor-heap reference to svo_dynamic_leaves.
     */
    [WBE_DESCRIPTOR("storage_buffer", "svo_dynamic_leaves", "read")]
    uint2 dynamic_leaves;
};
[[vk::push_constant]]
ConstantBuffer<DynamicDescriptorIndices> descriptor_indices;

/**
 * @brief Publishes initialized dynamic child blocks at a pass boundary.
 */
[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint3 p_id : SV_DispatchThreadID) {
    StructuredBuffer<SVOInfo> info = ResourceDescriptorHeap[descriptor_indices.info.x];
    RWStructuredBuffer<SVONode> nodes = ResourceDescriptorHeap[descriptor_indices.nodes.x];
    RWStructuredBuffer<SVONode> dynamic_nodes = ResourceDescriptorHeap[descriptor_indices.dynamic_nodes.x];
    uint index = p_id.x;
    bool is_static = index < info[0].max_node_count;
    uint local = is_static ? index : index - info[0].max_node_count;
    if (is_static ? local >= info[0].node_count : local >= info[0].dynamic_node_count) {
        return;
    }
    uint description = is_static ? nodes[local].children_data : dynamic_nodes[local].children_data;
    if ((description >> SVO_CHILDREN_STATE_SHIFT) != uint(SVONode::ChildrenState::ALLOCATING_DYNAMIC)) {
        return;
    }
    uint state = (description & SVO_CHILDREN_OCCUPANCY_MASK) != 0 ? uint(SVONode::ChildrenState::LEAF_DYNAMIC)
                                                  : uint(SVONode::ChildrenState::NODE_DYNAMIC);
    description = (description & SVO_CHILDREN_PAYLOAD_MASK) | (state << SVO_CHILDREN_STATE_SHIFT);
    if (is_static) {
        nodes[local].children_data = description;
    } else {
        dynamic_nodes[local].children_data = description;
    }
}
