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
#include "octree/octree_debug.slang"

/**
 * @file octree_debug_draw.slang
 * @brief Wireframes of the octree's leaves and bounds, or of its occupied nodes.
 * @details Without WBE_OCTREE_DEBUG_NODES this is the leaf view, drawn with one indirect command per subgrid from
 * octree_debug_gather_leaves.slang: instance i draws the occupied leaf of rank i % OCTREE_DEBUG_SUBGRID_LEAF_COUNT in the
 * occupancy mask of subgrid i / OCTREE_DEBUG_SUBGRID_LEAF_COUNT, and the instance after the last subgrid draws the bounds.
 * With WBE_OCTREE_DEBUG_NODES this is the node view: instance i draws the i-th gathered node, colored by depth.
 */

/**
 * @brief Reflected descriptor-heap bindings of the debug draws.
 */
struct OctreeDebugDrawDescriptorIndices {
    /**
     * @brief Descriptor-heap reference to octree_info.
     */
    [WBE_DESCRIPTOR("storage_buffer", "octree_info", "read")]
    uint2 octree_info;
#ifdef WBE_OCTREE_DEBUG_NODES
    /**
     * @brief Descriptor-heap reference to octree_debug_node_list.
     */
    [WBE_DESCRIPTOR("storage_buffer", "octree_debug_node_list", "read")]
    uint2 node_list;
#else
    /**
     * @brief Descriptor-heap reference to octree_debug_leaf_masks.
     */
    [WBE_DESCRIPTOR("storage_buffer", "octree_debug_leaf_masks", "read")]
    uint2 leaf_masks;
#endif
    /**
     * @brief Descriptor-heap reference to the G-buffer camera.
     */
    [WBE_DESCRIPTOR("uniform_buffer", "gbuffer_camera_info")]
    uint2 camera_info;
};

/**
 * @brief Vertex output for octree debug wireframes.
 */
struct OctreeDebugDrawVertexOutput {
    /**
     * @brief Clip-space vertex position.
     */
    float4 position : SV_Position;
    /**
     * @brief Wireframe color.
     */
    float4 color : COLOR0;
};

[vk::push_constant]
ConstantBuffer<OctreeDebugDrawDescriptorIndices> descriptor_indices;

/**
 * @brief Produces one of the eight corners of a leaf, a node, or the octree bounds without a vertex buffer.
 * @param p_instance_id Instance index including the draw command's first instance.
 */
[shader("vertex")]
OctreeDebugDrawVertexOutput vertex_main(uint p_vertex_id : SV_VulkanVertexID, uint p_instance_id : SV_VulkanInstanceID) {
    StructuredBuffer<OctreeInfo> octree_info_buffer = ResourceDescriptorHeap[descriptor_indices.octree_info.x];
    ConstantBuffer<CameraInfo> camera_info = ResourceDescriptorHeap[descriptor_indices.camera_info.x];
    OctreeInfo info = octree_info_buffer[0];

    uint3 corner = get_octree_debug_cube_corner(p_vertex_id);
    float3 world_position;
    float4 color = float4(1.0);
#ifdef WBE_OCTREE_DEBUG_NODES
    StructuredBuffer<uint> node_list = ResourceDescriptorHeap[descriptor_indices.node_list.x];
    uint level;
    uint3 node = get_octree_debug_node_entry(node_list[OCTREE_DEBUG_NODE_LIST_ENTRY_OFFSET + p_instance_id], level);
    world_position = get_octree_texel_corner(info, level, node, corner);
    // Fine nodes are green and coarse nodes blue.
    float coarseness = info.max_depth > 2 ? float(level - 1) / float(info.max_depth - 2) : 0.0;
    color = float4(0.0, 1.0 - 0.7 * coarseness, coarseness, 1.0);
#else
    uint3 grid_extent = get_octree_debug_subgrid_grid_extent(info.max_depth);
    uint subgrid_index = p_instance_id / OCTREE_DEBUG_SUBGRID_LEAF_COUNT;
    if (subgrid_index < grid_extent.x * grid_extent.y * grid_extent.z) {
        StructuredBuffer<OctreeDebugSubgridMask> leaf_masks = ResourceDescriptorHeap[descriptor_indices.leaf_masks.x];
        uint local_index = get_octree_debug_set_bit(leaf_masks[subgrid_index], p_instance_id % OCTREE_DEBUG_SUBGRID_LEAF_COUNT);
        uint3 voxel = get_octree_debug_subgrid(subgrid_index, grid_extent) * OCTREE_DEBUG_SUBGRID_EXTENT +
            get_octree_debug_subgrid_leaf(local_index);
        world_position = get_octree_texel_corner(info, 0, voxel, corner);
    } else {
        world_position = get_octree_texel_corner(info, info.max_depth, uint3(0), corner);
        color = float4(1.0, 0.0, 0.0, 1.0);
    }
#endif
    float4 view_position =
        software_mul_float4x4_vector(WBE_SOFTWARE_MATRIX_ROWS(camera_info.view), float4(world_position, 1.0));
    OctreeDebugDrawVertexOutput output;
    output.position = software_mul_float4x4_vector(WBE_SOFTWARE_MATRIX_ROWS(camera_info.proj), view_position);
    output.color = color;
    return output;
}

/**
 * @brief Colors each debug wireframe.
 */
[shader("fragment")]
float4 fragment_main(OctreeDebugDrawVertexOutput p_input) : SV_Target0 {
    return p_input.color;
}
