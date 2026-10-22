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
#include "svo/svo_dynamic_types.slang"

/**
 * @brief Read-only view of an already constructed SVO that resolves positions to the data_index of their nodes and leaves.
 * @details Every stored node and leaf owns one data element through its data_index; the SVO does not own or read the data,
 * so callers index their own data buffer with the traced index. The construction passes assign the static node and leaf
 * data first, followed by the dynamic node data and then the dynamic leaf data. The SVO must be fully constructed and
 * published, and its buffers must not change during the calling pass, because node descriptions are read without
 * synchronization. Construction is not performed through this type.
 */
struct SVO {
    /**
     * @brief Static SVO node storage.
     */
    RWStructuredBuffer<SVONode> nodes;
    /**
     * @brief Dynamic SVO node storage; read only when traverse_dynamic is true.
     */
    RWStructuredBuffer<SVONode> dynamic_nodes;
    /**
     * @brief Static SVO leaf storage.
     */
    StructuredBuffer<SVOLeaf> leaves;
    /**
     * @brief Dynamic SVO leaf blocks; read only when traverse_dynamic is true.
     */
    StructuredBuffer<SVODynamicLeafBlock> dynamic_leaves;
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
    __init(RWStructuredBuffer<SVONode> p_nodes,
        RWStructuredBuffer<SVONode> p_dynamic_nodes,
        StructuredBuffer<SVOLeaf> p_leaves,
        StructuredBuffer<SVODynamicLeafBlock> p_dynamic_leaves,
        SVOInfo p_info,
        bool p_traverse_dynamic) {
        nodes = p_nodes;
        dynamic_nodes = p_dynamic_nodes;
        leaves = p_leaves;
        dynamic_leaves = p_dynamic_leaves;
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
        traverse_svo(nodes, dynamic_nodes, info, traverse_dynamic, p_trace_info);
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
        traverse_svo(nodes, dynamic_nodes, info, traverse_dynamic, p_trace_info);
        return get_data_index(p_trace_info);
    }

    /**
     * @brief Get the data index of the node or leaf reached by a trace.
     * @param p_trace_info Traversal state produced by trace_data() or traverse_svo().
     * @return The data_index of the referenced node or leaf.
     */
    uint get_data_index(SVONodeTraceInfo p_trace_info) {
        switch (p_trace_info.index_type) {
        case SVONodeTraceInfo::IndexType::DYNAMIC_NODE:
            return dynamic_nodes[p_trace_info.node_index].data_index;
        case SVONodeTraceInfo::IndexType::STATIC_LEAF:
            return leaves[p_trace_info.node_index].data_index;
        case SVONodeTraceInfo::IndexType::DYNAMIC_LEAF:
            return dynamic_leaves[p_trace_info.node_index / 8].leaves[p_trace_info.node_index % 8].data_index;
        default:
            return nodes[p_trace_info.node_index].data_index;
        }
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
 * @brief An SVO paired with a read-only voxel data image.
 * @details The image holds the data of the SVO's nodes and leaves; its layout and indexing are defined by the caller. The
 * same synchronization requirements as SVO apply, and the image must not be written during the calling pass.
 * @tparam T Texel type of the voxel data image.
 */
struct SVOReadImage<T : ITexelElement> {
    /**
     * @brief The SVO the voxel data belongs to.
     */
    SVO svo;
    /**
     * @brief Read-only voxel data image.
     */
    Texture3D<T> voxel_data;

    /**
     * @brief Pair an SVO with its voxel data image.
     */
    __init(SVO p_svo, Texture3D<T> p_voxel_data) {
        svo = p_svo;
        voxel_data = p_voxel_data;
    }

    /**
     * @brief Trace the SVO to a depth and read the voxel data of the reached node or leaf.
     * @details The traced data index is expanded into the image's dimensions row-major, x fastest and z slowest. In
     * shader-debug builds, a data index past the image's texel count reports SVO_VOXEL_DATA_INDEX_OVERFLOW; the read is
     * not guarded otherwise.
     * @param p_global_position Position in world space; every component must lie in [svo.info.origin, svo.info.origin +
     * svo.info.width).
     * @param p_depth Maximum depth of the reached node or leaf, as in SVO::trace_data().
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     * @return The stored texel holding the reached node's or leaf's data.
     */
    T trace_data(float3 p_global_position, uint p_depth, uint p_frame_slot) {
        SVONodeTraceInfo trace_info;
        uint data_index = svo.trace_data(p_global_position, p_depth, trace_info);
        uint3 dimensions;
        voxel_data.GetDimensions(dimensions.x, dimensions.y, dimensions.z);
        uint3 texel = unflatten_index_3d_checked(
            data_index, dimensions, ShaderDebugError::SVO_VOXEL_DATA_INDEX_OVERFLOW, p_frame_slot);
        return voxel_data.Load(int4(int3(texel), 0));
    }
};

/**
 * @brief An SVO paired with a writable voxel data image.
 * @details The image holds the data of the SVO's nodes and leaves; its layout and indexing are defined by the caller, who is
 * also responsible for synchronizing writes to it. The same synchronization requirements as SVO apply to the SVO itself.
 * @tparam T Texel type of the voxel data image.
 */
struct SVOWriteImage<T : ITexelElement> {
    /**
     * @brief The SVO the voxel data belongs to.
     */
    SVO svo;
    /**
     * @brief Writable voxel data image.
     */
    RWTexture3D<T> voxel_data;

    /**
     * @brief Pair an SVO with its voxel data image.
     */
    __init(SVO p_svo, RWTexture3D<T> p_voxel_data) {
        svo = p_svo;
        voxel_data = p_voxel_data;
    }

    /**
     * @brief Trace the SVO to a depth and read the voxel data of the reached node or leaf.
     * @details The traced data index is expanded into the image's dimensions row-major, x fastest and z slowest. In
     * shader-debug builds, a data index past the image's texel count reports SVO_VOXEL_DATA_INDEX_OVERFLOW; the read is
     * not guarded otherwise.
     * @param p_global_position Position in world space; every component must lie in [svo.info.origin, svo.info.origin +
     * svo.info.width).
     * @param p_depth Maximum depth of the reached node or leaf, as in SVO::trace_data().
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     * @return The stored texel holding the reached node's or leaf's data.
     */
    T trace_data(float3 p_global_position, uint p_depth, uint p_frame_slot) {
        return voxel_data[trace_texel(p_global_position, p_depth, p_frame_slot)];
    }

    /**
     * @brief Trace the SVO to a depth and overwrite the voxel data of the reached node or leaf.
     * @details The texel is located as in trace_data(), with the same shader-debug overflow check. The store is not atomic;
     * callers must ensure no other invocation accesses the same texel during the calling pass.
     * @param p_global_position Position in world space; every component must lie in [svo.info.origin, svo.info.origin +
     * svo.info.width).
     * @param p_depth Maximum depth of the reached node or leaf, as in SVO::trace_data().
     * @param p_value Value stored into the reached node's or leaf's texel.
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     */
    void write_data(float3 p_global_position, uint p_depth, T p_value, uint p_frame_slot) {
        voxel_data[trace_texel(p_global_position, p_depth, p_frame_slot)] = p_value;
    }

    /**
     * @brief Overwrite the voxel data of a static leaf.
     * @details The texel is located from the leaf's data_index as in trace_data(), with the same shader-debug overflow
     * check. The store is not atomic; callers must ensure no other invocation accesses the same texel during the calling
     * pass.
     * @param p_leaf_index Index into svo.leaves; must be below the static leaf count.
     * @param p_value Value stored into the leaf's texel.
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     */
    void write_leaf_data(uint p_leaf_index, T p_value, uint p_frame_slot) {
        voxel_data[get_data_texel(svo.leaves[p_leaf_index].data_index, p_frame_slot)] = p_value;
    }

    uint3 trace_texel(float3 p_global_position, uint p_depth, uint p_frame_slot) {
        SVONodeTraceInfo trace_info;
        return get_data_texel(svo.trace_data(p_global_position, p_depth, trace_info), p_frame_slot);
    }

    uint3 get_data_texel(uint p_data_index, uint p_frame_slot) {
        uint3 dimensions;
        voxel_data.GetDimensions(dimensions.x, dimensions.y, dimensions.z);
        return unflatten_index_3d_checked(
            p_data_index, dimensions, ShaderDebugError::SVO_VOXEL_DATA_INDEX_OVERFLOW, p_frame_slot);
    }
};

/**
 * @brief Number of texels along each axis of an SVO voxel data brick.
 */
static const uint SVO_BRICK_SIZE = 3;

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
    Texture3D<T> voxel_data;
    /**
     * @brief Sampler used to read the bricks; it must filter linearly within a mip level.
     */
    SamplerState brick_sampler;

    /**
     * @brief Pair an SVO with its brick image and the sampler that reads it.
     */
    __init(SVO p_svo, Texture3D<T> p_voxel_data, SamplerState p_brick_sampler) {
        svo = p_svo;
        voxel_data = p_voxel_data;
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
        uint3 dimensions;
        voxel_data.GetDimensions(dimensions.x, dimensions.y, dimensions.z);
        uint3 brick_origin = unflatten_index_3d_checked(data_index, dimensions / SVO_BRICK_SIZE,
                                 ShaderDebugError::SVO_VOXEL_DATA_INDEX_OVERFLOW, p_frame_slot) *
            SVO_BRICK_SIZE;
        // Sub-voxel centers sit at local 0, 0.5, and 1, the centers of the brick's texels 0, 1, and 2.
        float3 texel_position = float3(brick_origin) + 0.5 + trace_info.svo_position * float(SVO_BRICK_SIZE - 1);
        return voxel_data.SampleLevel(brick_sampler, texel_position / float3(dimensions), 0.0);
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
    RWTexture3D<T> voxel_data;

    /**
     * @brief Pair an SVO with its brick image.
     */
    __init(SVO p_svo, RWTexture3D<T> p_voxel_data) {
        svo = p_svo;
        voxel_data = p_voxel_data;
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
    void write_data(float3 p_global_position, uint p_depth, T p_value, uint p_frame_slot) {
        SVONodeTraceInfo trace_info;
        uint data_index = svo.trace_data(p_global_position, p_depth, trace_info);
        uint3 dimensions;
        voxel_data.GetDimensions(dimensions.x, dimensions.y, dimensions.z);
        uint3 brick_origin = unflatten_index_3d_checked(data_index, dimensions / SVO_BRICK_SIZE,
                                 ShaderDebugError::SVO_VOXEL_DATA_INDEX_OVERFLOW, p_frame_slot) *
            SVO_BRICK_SIZE;
        // Round to the nearest sub-voxel center at local 0, 0.5, or 1.
        uint3 sub_voxel = min(uint3(trace_info.svo_position * float(SVO_BRICK_SIZE - 1) + 0.5), uint3(SVO_BRICK_SIZE - 1));
        voxel_data[brick_origin + sub_voxel] = p_value;
    }
};

#endif
