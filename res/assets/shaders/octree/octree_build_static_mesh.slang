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
#include "octree/octree_common.slang"
#include "octree/octree_level_storage.slang"

/**
 * @file octree_build_static_mesh.slang
 * @brief Raster pass that occupies the leaves covered by a mesh and their ancestors in every mip level of an octree texture.
 * @details WBE_DYNAMIC_BUILD selects the dynamic texture instead of the static one. The ancestors of every leaf occupied
 * before the pass must already be occupied, which holds for a texture cleared to OCTREE_EMPTY and built only by this pass.
 */

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
    float4 position : SV_Position;
    /**
     * @brief Position in the octree's unit cube; positions inside the octree range in [0, 1)^3.
     */
    float3 octree_position : TEXCOORD0;
    /**
     * @brief Number of leaves along each octree axis.
     */
    nointerpolation uint grid_extent : TEXCOORD1;
};

/**
 * @brief A conservatively enlarged projected triangle carrying the original triangle's plane depth in leaf-grid coordinates.
 */
struct GSOutput {
    /**
     * @brief Clip-space position of the enlarged triangle.
     */
    float4 position : SV_Position;
    /**
     * @brief Depth of the original triangle's plane along the depth axis, in leaf units.
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

// Closed-cell overlap includes surfaces on cell boundaries. This tolerance is in leaf units.
static const float VOXEL_COVERAGE_EPSILON = 0.0001;

/**
 * @brief Reflected descriptor-heap bindings for this construction stage.
 */
struct OctreeBuildDescriptorIndices {
    /**
     * @brief Descriptor-heap reference to octree_info.
     */
    [WBE_DESCRIPTOR("storage_buffer", "octree_info", "read")]
    uint2 octree_info;
    /**
     * @brief Storage views of the texture's mip levels.
     */
    OctreeHeapLevelStorage storage;
    MeshDescriptorIndices mesh;
};

[[vk::push_constant]]
ConstantBuffer<OctreeBuildDescriptorIndices> descriptor_indices;

/**
 * @brief Transform a mesh vertex into the octree's unit cube.
 * @param p_input Incoming vertex attributes.
 * @param p_instance_id Index into mesh.instances.
 * @return Vertex output in clip space.
 */
[shader("vertex")]
VSOutput vertex_main(VSInput p_input, uint p_instance_id : SV_VulkanInstanceID) {
    StructuredBuffer<OctreeInfo> octree_info_buffer = ResourceDescriptorHeap[descriptor_indices.octree_info.x];
    OctreeInfo octree_info = octree_info_buffer[0];
    StructuredBuffer<InstanceData> instances = ResourceDescriptorHeap[descriptor_indices.mesh.instances.x];

    InstanceData instance = instances[p_instance_id];
    NodeData node = load_mesh_node(descriptor_indices.mesh, instance.node_index);
    float4 world_position = mul(node.transform, float4(p_input.position.xyz, 1.0));

    VSOutput output;
    output.octree_position = (world_position.xyz - octree_info.origin) / octree_info.width;
    output.position = float4(output.octree_position * 2.0 - 1.0, 1.0);
    output.grid_extent = 1u << octree_info.max_depth;
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
    float3 a = p_input[0].octree_position * extent;
    float3 b = p_input[1].octree_position * extent;
    float3 c = p_input[2].octree_position * extent;
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
 * @brief Occupies the leaf at the triangle's plane depth under each rasterized pixel.
 * @details Pixels outside the original triangle's cell bounds come from the enlarged triangle's overshooting corners and
 * are discarded. The plane depth is clamped to the triangle's depth bounds, which it overshoots near the triangle's edges.
 */
[shader("fragment")]
void fragment_main(GSOutput p_input) {
    // Helper invocations cannot publish storage writes and receive undefined atomic results.
    // This shader does not need derivatives.
    if (IsHelperLane()) {
        return;
    }
    // Pixel centers lie at cell + 0.5, so integer cell comparison has no boundary ambiguity.
    uint2 cell = uint2(p_input.position.xy);
    if (any(cell < p_input.cell_bounds.xy) || any(cell > p_input.cell_bounds.zw)) {
        return;
    }
    StructuredBuffer<OctreeInfo> octree_info = ResourceDescriptorHeap[descriptor_indices.octree_info.x];
    uint depth_axis = p_input.depth_axis;
    uint horizontal_axis = depth_axis == 0 ? 1 : 0;
    uint vertical_axis = depth_axis == 2 ? 1 : 2;
    uint3 voxel;
    voxel[horizontal_axis] = cell.x;
    voxel[vertical_axis] = cell.y;
    voxel[depth_axis] = uint(clamp(floor(p_input.plane_depth), float(p_input.depth_bounds.x), float(p_input.depth_bounds.y)));
    write_octree_ancestors(descriptor_indices.storage, voxel, 0, octree_info[0].max_depth, OCTREE_OCCUPIED);
}
