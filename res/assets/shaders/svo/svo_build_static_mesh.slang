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
#ifndef WBE_SVO_BUILD_SLANG
#define WBE_SVO_BUILD_SLANG

#include "utils/utils.slang"
#include "svo/svo_utils.slang"
#ifdef WBE_DYNAMIC_BUILD
#include "svo/svo_dynamic_build.slang"
#endif

/**
 * @brief The input data from the vertex buffer
 */
struct VSInput {
    /**
     * @brief The position of the vertex.
     */
    [WBE_VERTEX_INPUT]
    float3 position;
};

/**
 * @brief Transformed triangle vertices for voxel coverage generation.
 */
struct VSOutput {
    /**
     * @brief Normalized position before the geometry shader selects a projection.
     */
    float4 position       : SV_Position;
    /**
     * @brief Position in SVO space (0, 0 is the corner of the SVO's region, and values inside the
     * SVO are ranged [0, 1)^3)
     */
    float3 svo_position : TEXCOORD0;
    /**
     * @brief Number of cells along each SVO axis.
     */
    nointerpolation uint grid_extent : TEXCOORD1;
};

/**
 * @brief A conservatively enlarged projected triangle carrying the original triangle's plane depth in voxel-grid coordinates.
 */
struct GSOutput {
    /**
     * @brief Clip-space position of the enlarged triangle.
     */
    float4 position : SV_Position;
    /**
     * @brief Depth of the original triangle's plane along the depth axis, in voxel units.
     * @details The projection is orthographic, so linear interpolation across the enlarged triangle is exact.
     */
    noperspective float plane_depth : TEXCOORD0;
    /**
     * @brief Original grid axis used as depth for this triangle's projection.
     */
    nointerpolation uint depth_axis : TEXCOORD1;
    /**
     * @brief Inclusive cell range of the original triangle: (horizontal min, vertical min, horizontal max, vertical max).
     */
    nointerpolation uint4 cell_bounds : TEXCOORD2;
    /**
     * @brief Inclusive cell range of the original triangle along the depth axis: (min, max).
     */
    nointerpolation uint2 depth_bounds : TEXCOORD3;
};

// Closed-cell overlap includes surfaces on cell boundaries. This tolerance is in voxel units.
static const float VOXEL_COVERAGE_EPSILON = 0.0001;

struct SVODescriptorIndices {
    [WBE_DESCRIPTOR("storage_buffer", "svo_info")]
    uint2 svo_info;
    [WBE_DESCRIPTOR("storage_buffer", "svo_node_buffer")]
    uint2 tree_nodes;
    [WBE_DESCRIPTOR("storage_buffer", "svo_leaf_buffer")]
    uint2 tree_leaves;
    [WBE_DESCRIPTOR("storage_buffer", "svo_deferred_list_ping")]
    uint2 deferred_list;
    [WBE_PUSH_CONSTANT("frame_slot")]
    uint shader_debug_frame_slot;

    MeshDescriptorIndices mesh;
};

[vk::push_constant]
ConstantBuffer<SVODescriptorIndices> descriptor_indices;

/**
 * @brief Transform a mesh vertex for the svo.
 * @param p_input Incoming vertex attributes.
 * @param p_instance_id Index into mesh.instances.
 * @return Vertex output in clip space.
 */
[shader("vertex")]
VSOutput vertex_main(VSInput p_input, uint p_instance_id : SV_VulkanInstanceID) {
    RWStructuredBuffer<SVOInfo> svo_info_buffer = ResourceDescriptorHeap[descriptor_indices.svo_info.x];
    SVOInfo svo_info = svo_info_buffer[0];
    StructuredBuffer<InstanceData> instances = ResourceDescriptorHeap[descriptor_indices.mesh.instances.x];
    StructuredBuffer<SubmeshData> submeshes = ResourceDescriptorHeap[descriptor_indices.mesh.submeshes.x];

    InstanceData instance = instances[p_instance_id];
    NodeData node = load_mesh_node(descriptor_indices.mesh, instance.node_index);
    SubmeshData submesh = submeshes[instance.submesh_index];
    float4 world_position = mul(node.transform, float4(p_input.position.xyz, 1.0));
    float3x3 direction_transform = (float3x3)node.transform;

    VSOutput output;
    output.svo_position = (world_position.xyz - svo_info.origin) / svo_info.width;
    output.position = float4(output.svo_position * 2.0 - 1.0, 1.0);
    output.grid_extent = 1u << svo_info.max_depth;
    return output;
}

/**
 * @brief Returns the homogeneous 2D line of one projected edge, pushed outward to cover every touched pixel.
 * @details The line is (n.x, n.y, d) with n pointing into the triangle, so inside points satisfy dot(n, p) + d >= 0.
 * A unit pixel square centered at p touches the edge's inner half-plane exactly when
 * dot(n, p) + d >= -0.5 * (|n.x| + |n.y|); adding that support distance to d turns "the square touches" into
 * "the pixel center is inside". p_orientation is the sign of the projected triangle's signed area.
 */
float3 get_enlarged_edge_line(float2 p_start, float2 p_end, float p_orientation) {
    float2 edge = p_end - p_start;
    float2 inward_normal = float2(-edge.y, edge.x) * p_orientation;
    float offset = dot(inward_normal, -p_start) + (0.5 + VOXEL_COVERAGE_EPSILON) * (abs(inward_normal.x) + abs(inward_normal.y));
    return float3(inward_normal, offset);
}

/**
 * @brief Emits the triangle's dominant projection enlarged by half a pixel along each edge normal.
 * @details Every pixel whose square touches the projected triangle has its center inside the enlarged triangle, including
 * pixels of subpixel triangles. The enlarged triangle's corners overshoot at acute angles, so the triangle's inclusive cell
 * bounds are passed for the fragment stage to clip against. The original triangle's plane depth is evaluated at each
 * enlarged corner so the fragment stage can interpolate it.
 */
[shader("geometry")]
[maxvertexcount(3)]
void geometry_main(triangle VSOutput p_input[3], inout TriangleStream<GSOutput> p_stream) {
    float extent = float(p_input[0].grid_extent);
    float3 a = p_input[0].svo_position * extent;
    float3 b = p_input[1].svo_position * extent;
    float3 c = p_input[2].svo_position * extent;
    float3 normal = cross(b - a, c - a);
    float3 normal_magnitude = abs(normal);
    uint dominant_axis = normal_magnitude.x >= normal_magnitude.y && normal_magnitude.x >= normal_magnitude.z ? 0 :
        (normal_magnitude.y >= normal_magnitude.z ? 1 : 2);
    if (normal[dominant_axis] == 0.0) {
        return;
    }
    uint horizontal_axis = dominant_axis == 0 ? 1 : 0;
    uint vertical_axis = dominant_axis == 2 ? 1 : 2;
    float3 lower = max(floor(min(a, min(b, c)) - VOXEL_COVERAGE_EPSILON), float3(0.0));
    float3 upper = min(floor(max(a, max(b, c)) + VOXEL_COVERAGE_EPSILON), float3(extent - 1.0));
    if (any(lower > upper)) {
        return;
    }
    float2 projected[3] = {
        float2(a[horizontal_axis], a[vertical_axis]),
        float2(b[horizontal_axis], b[vertical_axis]),
        float2(c[horizontal_axis], c[vertical_axis]),
    };
    float2 first_edge = projected[1] - projected[0];
    float2 second_edge = projected[2] - projected[0];
    // Nonzero: it equals the dominant normal component checked above, up to sign.
    float orientation = sign(first_edge.x * second_edge.y - first_edge.y * second_edge.x);
    float3 edge_lines[3];
    for (uint i = 0; i < 3; ++i) {
        edge_lines[i] = get_enlarged_edge_line(projected[i], projected[(i + 1) % 3], orientation);
    }
    uint4 cell_bounds = uint4(uint(lower[horizontal_axis]), uint(lower[vertical_axis]),
        uint(upper[horizontal_axis]), uint(upper[vertical_axis]));
    uint2 depth_bounds = uint2(uint(lower[dominant_axis]), uint(upper[dominant_axis]));
    for (uint i = 0; i < 3; ++i) {
        // Vertex i moves to the intersection of its two adjacent enlarged edges. Adjacent edges of a nondegenerate
        // triangle are never parallel, so the homogeneous weight is nonzero.
        float3 corner = cross(edge_lines[(i + 2) % 3], edge_lines[i]);
        float2 corner_position = corner.xy / corner.z;
        GSOutput output;
        output.position = float4(corner_position / extent * 2.0 - 1.0, 0.5, 1.0);
        output.plane_depth = a[dominant_axis] -
            (normal[horizontal_axis] * (corner_position.x - a[horizontal_axis]) +
                normal[vertical_axis] * (corner_position.y - a[vertical_axis])) / normal[dominant_axis];
        output.depth_axis = dominant_axis;
        output.cell_bounds = cell_bounds;
        output.depth_bounds = depth_bounds;
        p_stream.Append(output);
    }
    p_stream.RestartStrip();
}

/**
 * @brief Allocates a covered cell, queueing the voxel for a retry from the root on contention.
 */
void allocate_covered_voxel(float3 p_svo_position) {
    RWStructuredBuffer<SVOInfo> svo_info = ResourceDescriptorHeap[descriptor_indices.svo_info.x];
    RWStructuredBuffer<SVONode> tree_nodes = ResourceDescriptorHeap[descriptor_indices.tree_nodes.x];
    RWStructuredBuffer<SVOLeaf> tree_leaves = ResourceDescriptorHeap[descriptor_indices.tree_leaves.x];
    RWStructuredBuffer<uint> deferred_list = ResourceDescriptorHeap[descriptor_indices.deferred_list.x];
    // Trace to the bottom-most node
    SVONodeTraceInfo trace_info = get_svo_root_trace_info(p_svo_position, svo_info[0].max_depth);
    // Try to allocate the node.
#ifdef WBE_DYNAMIC_BUILD
    if (!try_dynamic_voxel_allocate_atomic(tree_nodes, tree_leaves, svo_info, trace_info, descriptor_indices.shader_debug_frame_slot)) {
#else
    if (!try_voxel_allocate(tree_nodes, tree_leaves, svo_info, trace_info, descriptor_indices.shader_debug_frame_slot)) {
#endif
        // If allocation failed, push this to the deferred list.
        uint new_task_index;
        InterlockedAdd(deferred_list[SVO_DEFERRED_LIST_COUNT_INDEX], 1, new_task_index);
        WBE_SHADER_DEBUG_ASSERT(new_task_index < svo_info[0].max_deferred_task_count,
            ShaderDebugError::SVO_DEFERRED_TASK_INDEX_OVERFLOW, descriptor_indices.shader_debug_frame_slot);
        deferred_list[SVO_DEFERRED_LIST_ENTRY_OFFSET + new_task_index] =
            to_deferred_list_entry(p_svo_position, svo_info[0].max_depth);
    }
}

/**
 * @brief Allocates the cell at the triangle's plane depth under each rasterized pixel.
 * @details Pixels outside the original triangle's cell bounds come from the enlarged triangle's overshooting corners and
 * are discarded. The plane depth is clamped to the triangle's depth bounds, which it overshoots near the triangle's edges.
 */
[shader("fragment")]
void fragment_main(GSOutput p_input) {
    // Helper invocations cannot publish storage writes and receive undefined atomic results.
    // Keep them out of allocation retry loops; this shader does not need derivatives.
    if (IsHelperLane()) {
        return;
    }
    // Pixel centers lie at cell + 0.5, so integer cell comparison has no boundary ambiguity.
    uint2 cell = uint2(p_input.position.xy);
    if (any(cell < p_input.cell_bounds.xy) || any(cell > p_input.cell_bounds.zw)) {
        return;
    }
    StructuredBuffer<SVOInfo> tree_info = ResourceDescriptorHeap[descriptor_indices.svo_info.x];
    float extent = float(1u << tree_info[0].max_depth);
    uint depth_axis = p_input.depth_axis;
    uint horizontal_axis = depth_axis == 0 ? 1 : 0;
    uint vertical_axis = depth_axis == 2 ? 1 : 2;
    float3 center;
    center[horizontal_axis] = float(cell.x) + 0.5;
    center[vertical_axis] = float(cell.y) + 0.5;
    center[depth_axis] = clamp(floor(p_input.plane_depth), float(p_input.depth_bounds.x), float(p_input.depth_bounds.y)) + 0.5;
    allocate_covered_voxel(center / extent);
}

#endif
