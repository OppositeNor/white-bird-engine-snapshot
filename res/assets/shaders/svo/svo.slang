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
#ifndef WBE_FILE_SVO_SLANG
#define WBE_FILE_SVO_SLANG

#include "utils/utils.slang"
#include "svo/svo_common.slang"

/**
 * @brief Read-only view of an already constructed SVO that resolves positions to the data_index of their nodes and leaves.
 * @details Every stored node and leaf owns one data element through its data_index; the SVO does not own or read the data,
 * so callers index their own data buffer with the traced index. The construction passes assign the static node and leaf
 * data first, followed by the dynamic node data and then the dynamic leaf data. The node buffer holds the static nodes
 * followed by the dynamic nodes, so node indices below info.node_count are static and the dynamic ones follow them; the leaf
 * buffer likewise holds the static leaves followed by the dynamic leaves from info.leaves_count on. The SVO must be fully
 * constructed and published, and its buffers must not change during the calling pass, because node descriptions are read
 * without synchronization. Construction is not performed through this type.
 */
struct SVO {
    /**
     * @brief SVO node storage: the static nodes followed by the dynamic nodes, which traces reach only when
     * traverse_dynamic is true.
     */
    StructuredBuffer<SVONode> nodes;
    /**
     * @brief SVO leaf storage: the static leaves followed by the dynamic leaves, which traces reach only when
     * traverse_dynamic is true.
     */
    StructuredBuffer<SVOLeaf> leaves;
    /**
     * @brief Spatial configuration of the SVO.
     */
    SVOInfo info;
    /**
     * @brief Whether traces follow the dynamic children published by the dynamic construction passes.
     */
    bool traverse_dynamic;

    /**
     * @brief Create a view over the buffers of a constructed SVO.
     */
    __init(StructuredBuffer<SVONode> p_nodes,
        StructuredBuffer<SVOLeaf> p_leaves,
        SVOInfo p_info,
        bool p_traverse_dynamic) {
        nodes = p_nodes;
        leaves = p_leaves;
        info = p_info;
        traverse_dynamic = p_traverse_dynamic;
    }

    /**
     * @brief Convert a world-space position into the SVO's unit cube.
     * @param p_world_position Position in world space.
     * @return The position relative to info.origin, scaled by 1 / info.width.
     */
    float3 get_svo_position(float3 p_world_position) {
        return (p_world_position - info.origin) / info.width;
    }

    /**
     * @brief Trace from the root to the deepest stored node or occupied leaf containing a position, and return its data index.
     * @details The trace stops at an occupied leaf, or at the deepest node whose child containing the position is empty or
     * an unoccupied leaf, so unoccupied regions return the data index of their coarsest stored ancestor. Dynamic children
     * are followed only when traverse_dynamic is true.
     * @param p_world_position Position in world space; every component must lie in [info.origin, info.origin + info.width).
     * @param p_trace_info Receives the final traversal state: the storage and index of the reached node or leaf, and its
     * depth.
     * @return The data_index of the reached node or leaf.
     */
    uint trace_data(float3 p_world_position, out SVONodeTraceInfo p_trace_info) {
        p_trace_info = get_svo_root_trace_info(get_svo_position(p_world_position), info.max_depth);
        traverse_svo(nodes, traverse_dynamic, p_trace_info);
        return get_data_index(p_trace_info);
    }

    /**
     * @brief Trace like trace_data(), but stop at a node no deeper than p_depth.
     * @param p_world_position Position in world space; every component must lie in [info.origin, info.origin + info.width).
     * @param p_depth Maximum depth of the reached node or leaf, in [1, info.max_depth]: one for the root's sectors and
     * info.max_depth for leaves.
     * @param p_trace_info Receives the final traversal state: the storage and index of the reached node or leaf, and its
     * depth.
     * @return The data_index of the reached node or leaf.
     */
    uint trace_data(float3 p_world_position, uint p_depth, out SVONodeTraceInfo p_trace_info) {
        p_trace_info = get_svo_root_trace_info(get_svo_position(p_world_position), info.max_depth);
        p_trace_info.remain_depth = int(p_depth) - 1;
        traverse_svo(nodes, traverse_dynamic, p_trace_info);
        return get_data_index(p_trace_info);
    }

    /**
     * @brief Get the data index of the node or leaf reached by a trace.
     * @param p_trace_info Traversal state produced by trace_data() or traverse_svo().
     * @return The data_index of the referenced node or leaf.
     */
    uint get_data_index(SVONodeTraceInfo p_trace_info) {
        if (p_trace_info.is_leaf()) {
            return leaves[p_trace_info.node_index].data_index;
        }
        return nodes[p_trace_info.node_index].data_index;
    }

    /**
     * @brief Count the allocated leaves in static and dynamic storage.
     * @return The upper bound of the leaf indices taken by get_leaf(), is_leaf_occupied(), and get_leaf_grid_position().
     */
    uint get_total_leaves() {
        return info.leaves_count + info.dynamic_leaves_count;
    }

    /**
     * @brief Count the stored nodes in static and dynamic storage, excluding the implicit root.
     * @return The upper bound of the node indices taken by get_node().
     */
    uint get_total_nodes() {
        return info.node_count + info.dynamic_node_count;
    }

    /**
     * @brief Read a node from static or dynamic storage.
     * @param p_index Node index below get_total_nodes(); static below info.node_count, dynamic from it on.
     */
    SVONode get_node(uint p_index) {
        return nodes[p_index];
    }

    /**
     * @brief Read a leaf from static or dynamic storage.
     * @param p_index Leaf index below get_total_leaves(); static below info.leaves_count, dynamic from it on.
     */
    SVOLeaf get_leaf(uint p_index) {
        return leaves[p_index];
    }

    /**
     * @brief Test whether an allocated leaf is occupied and still referenced by its parent.
     * @details When the dynamic construction replaces a static node's leaf block, the superseded static leaves still name
     * that node as their parent; they are reported unoccupied because the parent no longer references their block. In
     * shader-debug builds, an invalid parent index reports SVO_NODE_INDEX_OVERFLOW.
     * @param p_index Leaf index below get_total_leaves(), as in get_leaf().
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     */
    bool is_leaf_occupied(uint p_index, uint p_frame_slot) {
        SVOLeaf leaf = get_leaf(p_index);
        WBE_SHADER_DEBUG_ASSERT(leaf.parent_index >= 0 && uint(leaf.parent_index) < get_total_nodes(),
            ShaderDebugError::SVO_NODE_INDEX_OVERFLOW,
            p_frame_slot);
        SVONode parent = get_node(uint(leaf.parent_index));
        bool dynamic = p_index >= info.leaves_count;
        uint expected = uint(dynamic ? SVONode::ChildrenState::LEAF_DYNAMIC : SVONode::ChildrenState::LEAF);
        if ((parent.children_data >> SVO_CHILDREN_STATE_SHIFT) != expected ||
            get_svo_children_index(parent) / 8 != p_index / 8) {
            return false;
        }
        return (parent.children_data & (1u << (SVO_CHILDREN_OCCUPANCY_SHIFT + p_index % 8))) != 0;
    }

    /**
     * @brief Get a node's depth by walking from the node to the root.
     * @details In shader-debug builds, an invalid parent index reports SVO_NODE_INDEX_OVERFLOW and a chain longer than
     * info.max_depth - 1 nodes reports SVO_DEEPER_THAN_EXPECTED.
     * @param p_index Node index below get_total_nodes(), as in get_node().
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     * @return The depth of the node: one for the root's sectors and info.max_depth - 1 for the parents of leaves.
     */
    uint get_node_depth(uint p_index, uint p_frame_slot) {
        WBE_SHADER_DEBUG_ASSERT(p_index < get_total_nodes(), ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
        uint depth = 1;
        int parent_index = get_node(p_index).parent_index;
        for (; parent_index >= 0 && depth < info.max_depth; ++depth) {
            WBE_SHADER_DEBUG_ASSERT(uint(parent_index) < get_total_nodes(),
                ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
            parent_index = get_node(uint(parent_index)).parent_index;
        }
        WBE_SHADER_DEBUG_ASSERT(parent_index < 0 && depth < info.max_depth,
            ShaderDebugError::SVO_DEEPER_THAN_EXPECTED, p_frame_slot);
        return depth;
    }

    /**
     * @brief Reconstruct a leaf's grid position by walking from the leaf to the root.
     * @param p_index Leaf index below get_total_leaves(), as in get_leaf().
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     * @return Integer position of the leaf in the finest SVO grid.
     */
    uint3 get_leaf_grid_position(uint p_index, uint p_frame_slot) {
        uint3 result = uint3(0);
        WBE_SHADER_DEBUG_ASSERT(p_index < get_total_leaves(), ShaderDebugError::SVO_LEAF_INDEX_OVERFLOW, p_frame_slot);
        uint child_index = p_index;
        int parent_index = get_leaf(p_index).parent_index;
        uint traversed_depth = 0;
        for (; traversed_depth < info.max_depth && parent_index >= 0; ++traversed_depth) {
            WBE_SHADER_DEBUG_ASSERT(uint(parent_index) < get_total_nodes(),
                ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
            SVONode parent = get_node(uint(parent_index));
            WBE_SHADER_DEBUG_ASSERT(child_index >= get_svo_children_index(parent) &&
                child_index - get_svo_children_index(parent) < 8,
                ShaderDebugError::SVO_SHALLOWER_THAN_EXPECTED, p_frame_slot);
            uint subregion = child_index % 8;
            result.x |= ((subregion >> 2) & 1) << traversed_depth;
            result.y |= ((subregion >> 1) & 1) << traversed_depth;
            result.z |= (subregion & 1) << traversed_depth;
            child_index = uint(parent_index);
            parent_index = parent.parent_index;
        }
        if (parent_index < 0) {
            WBE_SHADER_DEBUG_ASSERT(child_index < 8 && traversed_depth < 32,
                ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
            // The remaining node is one of the eight sectors of the implicit root.
            result.x |= ((child_index >> 2) & 1) << traversed_depth;
            result.y |= ((child_index >> 1) & 1) << traversed_depth;
            result.z |= (child_index & 1) << traversed_depth;
            ++traversed_depth;
        }
        WBE_SHADER_DEBUG_ASSERT(parent_index < 0 && traversed_depth <= info.max_depth,
            ShaderDebugError::SVO_DEEPER_THAN_EXPECTED, p_frame_slot);
        WBE_SHADER_DEBUG_ASSERT(parent_index >= 0 || traversed_depth >= info.max_depth,
            ShaderDebugError::SVO_SHALLOWER_THAN_EXPECTED, p_frame_slot);
        return result;
    }

    /**
     * @brief Get the world-space minimum corner of the node or leaf containing a voxel at a depth.
     * @details Only the top p_depth bits of each axis are read, so a node may be located by any voxel below it, such as
     * SVONodeTraceInfo::voxel_index. The cell's width is info.width / 2^p_depth.
     * @param p_voxel_index Flattened voxel coordinate in the grid of extent 2^info.max_depth, packed as by
     * to_deferred_list_entry().
     * @param p_depth Depth of the node, at most info.max_depth: one for the root's sectors, info.max_depth for leaves, and
     * zero for the implicit root.
     * @return The world-space position of the cell's minimum corner.
     */
    float3 voxel_index_to_global(uint p_voxel_index, uint p_depth) {
        uint3 cell = get_deferred_list_entry_voxel(p_voxel_index, info.max_depth) >> (info.max_depth - p_depth);
        return info.origin + float3(cell) * (info.width / float(1u << p_depth));
    }
};

/**
 * @brief An SVO paired with a read-only voxel data buffer.
 * @details The buffer holds one element per data index of the SVO's nodes and leaves; its contents are defined by the
 * caller. The same synchronization requirements as SVO apply, and the buffer must not be written during the calling pass.
 * @tparam T Element type of the voxel data buffer.
 */
struct SVOReadBuffer<T> {
    /**
     * @brief The SVO the voxel data belongs to.
     */
    SVO svo;
    /**
     * @brief Read-only atlas holding the voxel data, indexed by data index.
     */
    StructuredBuffer<T> atlas;
    /**
     * @brief Number of elements of the atlas, queried once at construction.
     */
    uint atlas_element_count;

    /**
     * @brief Pair an SVO with its voxel data buffer.
     */
    __init(SVO p_svo, StructuredBuffer<T> p_atlas) {
        svo = p_svo;
        atlas = p_atlas;
        uint stride;
        atlas.GetDimensions(atlas_element_count, stride);
    }

    /**
     * @brief Trace the SVO to a depth and read the voxel data of the reached node or leaf.
     * @details In shader-debug builds, a data index past the buffer's element count reports SVO_VOXEL_DATA_INDEX_OVERFLOW;
     * the read is not guarded otherwise.
     * @param p_global_position Position in world space; every component must lie in [svo.info.origin, svo.info.origin +
     * svo.info.width).
     * @param p_depth Maximum depth of the reached node or leaf, as in SVO::trace_data().
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     * @return The stored element holding the reached node's or leaf's data.
     */
    T trace_data(float3 p_global_position, uint p_depth, uint p_frame_slot) {
        SVONodeTraceInfo trace_info;
        uint data_index = svo.trace_data(p_global_position, p_depth, trace_info);
        WBE_SHADER_DEBUG_ASSERT(
            data_index < atlas_element_count, ShaderDebugError::SVO_VOXEL_DATA_INDEX_OVERFLOW, p_frame_slot);
        return atlas[data_index];
    }
};

/**
 * @brief An SVO paired with a writable voxel data buffer.
 * @details The buffer holds one element per data index of the SVO's nodes and leaves; its contents are defined by the
 * caller, who is also responsible for synchronizing writes to it. The same synchronization requirements as SVO apply to
 * the SVO itself.
 * @tparam T Element type of the voxel data buffer.
 */
struct SVOWriteBuffer<T> {
    /**
     * @brief The SVO the voxel data belongs to.
     */
    SVO svo;
    /**
     * @brief Writable atlas holding the voxel data, indexed by data index.
     */
    RWStructuredBuffer<T> atlas;
    /**
     * @brief Number of elements of the atlas, queried once at construction.
     */
    uint atlas_element_count;

    /**
     * @brief Pair an SVO with its voxel data buffer.
     */
    __init(SVO p_svo, RWStructuredBuffer<T> p_atlas) {
        svo = p_svo;
        atlas = p_atlas;
        uint stride;
        atlas.GetDimensions(atlas_element_count, stride);
    }

    /**
     * @brief Trace the SVO to a depth and read the voxel data of the reached node or leaf.
     * @details In shader-debug builds, a data index past the buffer's element count reports SVO_VOXEL_DATA_INDEX_OVERFLOW;
     * the read is not guarded otherwise.
     * @param p_global_position Position in world space; every component must lie in [svo.info.origin, svo.info.origin +
     * svo.info.width).
     * @param p_depth Maximum depth of the reached node or leaf, as in SVO::trace_data().
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     * @return The stored element holding the reached node's or leaf's data.
     */
    T trace_data(float3 p_global_position, uint p_depth, uint p_frame_slot) {
        return atlas[trace_index(p_global_position, p_depth, p_frame_slot)];
    }

    /**
     * @brief Trace the SVO to a depth and overwrite the voxel data of the reached node or leaf.
     * @details The element is located as in trace_data(), with the same shader-debug overflow check. The store is not
     * atomic; callers must ensure no other invocation accesses the same element during the calling pass.
     * @param p_global_position Position in world space; every component must lie in [svo.info.origin, svo.info.origin +
     * svo.info.width).
     * @param p_depth Maximum depth of the reached node or leaf, as in SVO::trace_data().
     * @param p_value Value stored into the reached node's or leaf's element.
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     */
    void write_data_at_position(float3 p_global_position, uint p_depth, T p_value, uint p_frame_slot) {
        atlas[trace_index(p_global_position, p_depth, p_frame_slot)] = p_value;
    }

    /**
     * @brief Read the voxel data at a data index.
     * @details In shader-debug builds, a data index past the buffer's element count reports SVO_VOXEL_DATA_INDEX_OVERFLOW.
     * @param p_data_index Data index of a node or leaf, such as SVONode::data_index or SVOLeaf::data_index.
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     * @return The stored element.
     */
    T read_data_at_index(uint p_data_index, uint p_frame_slot) {
        return atlas[checked_data_index(p_data_index, p_frame_slot)];
    }

    /**
     * @brief Overwrite the voxel data at a data index.
     * @details In shader-debug builds, a data index past the buffer's element count reports SVO_VOXEL_DATA_INDEX_OVERFLOW.
     * The store is not atomic; callers must ensure no other invocation accesses the same element during the calling pass.
     * @param p_data_index Data index of a node or leaf, such as SVONode::data_index or SVOLeaf::data_index.
     * @param p_value Value stored into the element.
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     */
    void write_data_at_index(uint p_data_index, T p_value, uint p_frame_slot) {
        atlas[checked_data_index(p_data_index, p_frame_slot)] = p_value;
    }

    uint trace_index(float3 p_global_position, uint p_depth, uint p_frame_slot) {
        SVONodeTraceInfo trace_info;
        return checked_data_index(svo.trace_data(p_global_position, p_depth, trace_info), p_frame_slot);
    }

    uint checked_data_index(uint p_data_index, uint p_frame_slot) {
        WBE_SHADER_DEBUG_ASSERT(
            p_data_index < atlas_element_count, ShaderDebugError::SVO_VOXEL_DATA_INDEX_OVERFLOW, p_frame_slot);
        return p_data_index;
    }
};

/**
 * @brief Number of texels along each axis of an SVO voxel data brick.
 */
static const uint SVO_BRICK_SIZE = 3;

/**
 * @brief Convert a position local to a node or leaf into its sub-voxel and the position inside that sub-voxel.
 * @details The sub-voxel is selected as in SVOWriteBrick::write_data_at_position(). Each sub-voxel is
 * 1 / (SVO_BRICK_SIZE - 1) the width of its node or leaf and centered at its texel's local coordinate, so the returned
 * position is 0.5 at the sub-voxel's center. The low and high sub-voxels extend past the node or leaf, so on those axes
 * the position lies in [0.5, 1) and [0, 0.5) respectively.
 * @param p_svo_position Position in the node's or leaf's local unit cube, such as SVONodeTraceInfo::svo_position after
 * SVO::trace_data(); every component must lie in [0, 1).
 * @param p_subvoxel_index Receives the sub-voxel's texel in the brick, flattened as by uint3::flatten() with x fastest and
 * z slowest.
 * @return The position in the sub-voxel's local unit cube.
 */
float3 svo_pos_to_subvoxel_pos(float3 p_svo_position, out uint p_subvoxel_index) {
    float3 brick_position = p_svo_position * float(SVO_BRICK_SIZE - 1);
    // Round to the nearest sub-voxel center at local 0, 0.5, or 1.
    uint3 subvoxel = min(uint3(brick_position + 0.5), uint3(SVO_BRICK_SIZE - 1));
    p_subvoxel_index = subvoxel.flatten(SVO_BRICK_SIZE, SVO_BRICK_SIZE);
    return brick_position - float3(subvoxel) + 0.5;
}

/**
 * @brief An SVO paired with a read-only image of 3x3x3 voxel data bricks.
 * @details Each node or leaf owns the brick at its data index, expanded row-major, x fastest and z slowest, into the
 * image's dimensions divided by SVO_BRICK_SIZE. A brick's texels are sub-voxels of its node or leaf centered at its local
 * coordinates 0, 0.5, and 1 on each axis: its corners, edge centers, face centers, and center. The same synchronization
 * requirements as SVO apply, and the image must not be written during the calling pass.
 * @tparam T Texel type of the brick image; it must support filtered sampling.
 */
struct SVOReadBrick<T : ITexelElement> {
    /**
     * @brief The SVO the voxel data belongs to.
     */
    SVO svo;
    /**
     * @brief Read-only brick image.
     */
    Texture3D<T> atlas;
    /**
     * @brief Extent of the atlas in texels, queried once at construction.
     */
    uint3 atlas_dimensions;
    /**
     * @brief Sampler used to read the bricks; it must filter linearly within a mip level.
     */
    SamplerState brick_sampler;

    /**
     * @brief Pair an SVO with its brick image and the sampler that reads it.
     */
    __init(SVO p_svo, Texture3D<T> p_atlas, SamplerState p_brick_sampler) {
        svo = p_svo;
        atlas = p_atlas;
        atlas.GetDimensions(atlas_dimensions.x, atlas_dimensions.y, atlas_dimensions.z);
        brick_sampler = p_brick_sampler;
    }

    /**
     * @brief Trace the SVO to a depth and interpolate the brick of the reached node or leaf at the traced position.
     * @details The traced position, local to the reached node or leaf, is interpolated trilinearly between the brick's
     * sub-voxel centers, so the sample never reads a neighboring brick. In shader-debug builds, a data index past the
     * image's brick count reports SVO_VOXEL_DATA_INDEX_OVERFLOW; the read is not guarded otherwise.
     * @param p_global_position Position in world space; every component must lie in [svo.info.origin, svo.info.origin +
     * svo.info.width).
     * @param p_depth Maximum depth of the reached node or leaf, as in SVO::trace_data().
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     * @return The interpolated voxel data.
     */
    T trace_data(float3 p_global_position, uint p_depth, uint p_frame_slot) {
        SVONodeTraceInfo trace_info;
        uint data_index = svo.trace_data(p_global_position, p_depth, trace_info);
        uint3 brick_origin = unflatten_index_checked(data_index, atlas_dimensions / SVO_BRICK_SIZE,
                                 ShaderDebugError::SVO_VOXEL_DATA_INDEX_OVERFLOW, p_frame_slot) *
            SVO_BRICK_SIZE;
        // Sub-voxel centers sit at local 0, 0.5, and 1, the centers of the brick's texels 0, 1, and 2.
        float3 texel_position = float3(brick_origin) + 0.5 + trace_info.svo_position * float(SVO_BRICK_SIZE - 1);
        return atlas.SampleLevel(brick_sampler, texel_position / float3(atlas_dimensions), 0.0);
    }
};

/**
 * @brief An SVO paired with a writable image of 3x3x3 voxel data bricks.
 * @details The bricks are laid out as in SVOReadBrick. Each sub-voxel spans half its node's or leaf's width around its
 * center, so every sub-voxel except the center one extends past the node or leaf; only the part inside it receives data,
 * and the outside part is discarded. Callers are responsible for synchronizing writes to the image. The same
 * synchronization requirements as SVO apply to the SVO itself.
 * @tparam T Texel type of the brick image.
 */
struct SVOWriteBrick<T : ITexelElement> {
    /**
     * @brief The SVO the voxel data belongs to.
     */
    SVO svo;
    /**
     * @brief Writable brick image.
     */
    RWTexture3D<T> atlas;
    /**
     * @brief Extent of the atlas in texels, queried once at construction.
     */
    uint3 atlas_dimensions;

    /**
     * @brief Pair an SVO with its brick image.
     */
    __init(SVO p_svo, RWTexture3D<T> p_atlas) {
        svo = p_svo;
        atlas = p_atlas;
        atlas.GetDimensions(atlas_dimensions.x, atlas_dimensions.y, atlas_dimensions.z);
    }

    /**
     * @brief Trace the SVO to a depth and overwrite the sub-voxel of the reached node or leaf containing a position.
     * @details The traced position, local to the reached node or leaf, selects the sub-voxel whose in-voxel part contains
     * it: per axis, [0, 0.25) selects the low sub-voxel, [0.25, 0.75) the middle one, and [0.75, 1) the high one. The
     * store is not atomic; callers must ensure no other invocation accesses the same texel during the calling pass. In
     * shader-debug builds, a data index past the image's brick count reports SVO_VOXEL_DATA_INDEX_OVERFLOW; the store is
     * not guarded otherwise.
     * @param p_global_position Position in world space; every component must lie in [svo.info.origin, svo.info.origin +
     * svo.info.width).
     * @param p_depth Maximum depth of the reached node or leaf, as in SVO::trace_data().
     * @param p_value Value stored into the selected sub-voxel.
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     */
    void write_data_at_position(float3 p_global_position, uint p_depth, T p_value, uint p_frame_slot) {
        SVONodeTraceInfo trace_info;
        uint data_index = svo.trace_data(p_global_position, p_depth, trace_info);
        write_subvoxel(data_index, trace_info, p_value, p_frame_slot);
    }

    /**
     * @brief Trace the SVO to the deepest stored node or occupied leaf and overwrite its sub-voxel containing a position.
     * @details Behaves like the depth-limited overload, but the trace runs as in SVO::trace_data() without a depth limit.
     * @param p_global_position Position in world space; every component must lie in [svo.info.origin, svo.info.origin +
     * svo.info.width).
     * @param p_value Value stored into the selected sub-voxel.
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     */
    void write_data_at_position(float3 p_global_position, T p_value, uint p_frame_slot) {
        SVONodeTraceInfo trace_info;
        uint data_index = svo.trace_data(p_global_position, trace_info);
        write_subvoxel(data_index, trace_info, p_value, p_frame_slot);
    }

    /**
     * @brief Overwrite the sub-voxel of a traced node or leaf containing the traced position.
     * @details The sub-voxel is selected as in write_data_at_position(). The store is not atomic; callers must ensure no
     * other invocation accesses the same texel during the calling pass. In shader-debug builds, a data index past the
     * image's brick count reports SVO_VOXEL_DATA_INDEX_OVERFLOW.
     * @param p_data_index Data index of the traced node or leaf, as returned by SVO::trace_data().
     * @param p_trace_info Traversal state produced by the same SVO::trace_data() call.
     * @param p_value Value stored into the selected sub-voxel.
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     */
    void write_subvoxel(uint p_data_index, SVONodeTraceInfo p_trace_info, T p_value, uint p_frame_slot) {
        atlas[get_subvoxel_texel(p_data_index, p_trace_info, p_frame_slot)] = p_value;
    }

    uint3 get_subvoxel_texel(uint p_data_index, SVONodeTraceInfo p_trace_info, uint p_frame_slot) {
        uint3 brick_origin = unflatten_index_checked(p_data_index, atlas_dimensions / SVO_BRICK_SIZE,
                                 ShaderDebugError::SVO_VOXEL_DATA_INDEX_OVERFLOW, p_frame_slot) * SVO_BRICK_SIZE;
        // Round to the nearest sub-voxel center at local 0, 0.5, or 1.
        uint3 subvoxel = min(uint3(p_trace_info.svo_position * float(SVO_BRICK_SIZE - 1) + 0.5), uint3(SVO_BRICK_SIZE - 1));
        return brick_origin + subvoxel;
    }
};

/**
 * @brief Atomic operations of SVOWriteBrick, available for texel types that support atomic arithmetic.
 * @tparam T Texel type of the brick image.
 */
extension<T : ITexelElement & IArithmeticAtomicable> SVOWriteBrick<T> {
    /**
     * @brief Atomically add a value onto the sub-voxel of a traced node or leaf containing the traced position.
     * @details The sub-voxel is selected as in write_subvoxel(). The addition is atomic with respect to other atomic
     * operations on the same texel. In shader-debug builds, a data index past the image's brick count reports
     * SVO_VOXEL_DATA_INDEX_OVERFLOW.
     * @param p_data_index Data index of the traced node or leaf, as returned by SVO::trace_data().
     * @param p_trace_info Traversal state produced by the same SVO::trace_data() call.
     * @param p_value Value added onto the selected sub-voxel.
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     */
    void atomic_add_subvoxel(uint p_data_index, SVONodeTraceInfo p_trace_info, T p_value, uint p_frame_slot) {
        InterlockedAdd(atlas[get_subvoxel_texel(p_data_index, p_trace_info, p_frame_slot)], p_value);
    }
}

#endif
