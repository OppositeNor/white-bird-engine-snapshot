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
#ifndef WBE_FILE_I_SVO_RENDER_FEATURE_HH
#define WBE_FILE_I_SVO_RENDER_FEATURE_HH

#include "core/allocator/i_allocator.hh"
#include "core/data/data_value.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/rend_graph_recipe_builder_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "glm/ext/vector_float3.hpp"
#include "i_voxel_render_feature.hh"
#include "svo_recipe_params.hh"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <glm/vec3.hpp>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

class DynamicMeshRenderFeature;

/**
 * @class ISVORenderFeature
 * @brief Interface of the deferred test's SVO construction and debug rendering.
 * @details Owns the buffers, raster construction, deferred-list debugging, and debug draws shared by every construction
 * algorithm; implementations supply their publication strategy, deferred retries, and implementation-specific resource
 * ordering. Neither copyable nor movable. All methods and destruction are confined to the rendering thread. Registered
 * graphs retain their resource handles and task factories; borrowed renderer and debug state must remain alive.
 */
class ISVORenderFeature : public IVoxelRenderFeature {
public:
    ISVORenderFeature() = delete;
    virtual ~ISVORenderFeature() override = default;
    ISVORenderFeature(const ISVORenderFeature&) = delete;
    ISVORenderFeature(ISVORenderFeature&&) = delete;
    ISVORenderFeature& operator=(const ISVORenderFeature&) = delete;
    ISVORenderFeature& operator=(ISVORenderFeature&&) = delete;

    /**
     * @brief Names the completed-tree view and the pre-deferred-propagation view.
     */
    virtual const char* get_debug_view_name(VoxelDebugViewMode p_mode) const override;

    /**
     * @brief Describe the SVO to the render_graph_utils.features.voxel_svo module, and supply its buffers.
     */
    virtual void configure_recipe(
        DataValue& p_parameters, RendGraphRecipeImportsVK& p_imports, const VoxelRecipeInputs& p_inputs) const override;

    static constexpr std::string_view RT_RUN_BUILD_SVO_DOUBLE_SIDED = "run_build_svo_double_sided";
    static constexpr std::string_view RT_RESET_SVO = "reset_svo";
    static constexpr std::string_view RT_RUN_BUILD_SVO = "run_build_svo";
    static constexpr std::string_view RT_RUN_BUILD_SVO_DEFERRED = "run_build_svo_deferred";
    static constexpr std::string_view RT_CAPTURE_SVO_PRE_DEFERRED_DEBUG = "capture_svo_pre_deferred_debug";
    static constexpr std::string_view RT_SETUP_SVO_DEBUG_DRAW = "setup_svo_debug_draw";
    static constexpr std::string_view RT_RUN_SVO_DEBUG_DRAW = "run_svo_debug_draw";
    static constexpr std::string_view RT_RUN_SVO_PRE_DEFERRED_DEBUG_DRAW = "run_svo_pre_deferred_debug_draw";
    static constexpr std::string_view RT_UPDATE_SVO_STATES = "update_svo_states";
    static constexpr std::string_view RT_STATIC_SVO_DEFERRED = "static_svo_deferred";
    static constexpr std::string_view RT_CLEAR_DYNAMIC_SVO = "clear_dynamic_svo";
    static constexpr std::string_view RT_RUN_BUILD_DYNAMIC_SVO = "dynamic_svo";
    static constexpr std::string_view RT_RUN_BUILD_DYNAMIC_SVO_SECOND = "dynamic_svo_second";
    static constexpr std::string_view RT_PUBLISH_DYNAMIC_SVO = "publish_dynamic_svo";

    static constexpr std::string_view SVO_INFO = "svo_info";
    static constexpr std::string_view SVO_NODE_BUFFER = "svo_node_buffer";
    static constexpr std::string_view SVO_DYNAMIC_LEAVES = "svo_dynamic_leaves";
    static constexpr std::string_view SVO_LEAF_BUFFER = "svo_leaf_buffer";
    static constexpr std::string_view SVO_DEBUG_DRAW_INDEX_BUFFER = "svo_debug_draw_index_buffer";
    static constexpr std::string_view SVO_DEBUG_DRAW_INDIRECT_BUFFER = "svo_debug_draw_indirect_buffer";
    static constexpr std::string_view SVO_PRE_DEFERRED_DEBUG_DRAW_INDIRECT_BUFFER = "svo_pre_deferred_debug_draw_indirect_buffer";

protected:
    /**
     * @brief Creates the shared pipelines and buffers on the rendering thread.
     * @details The renderer must outlive this object and its registered graphs. The feature retains
     * shared ownership of the static scene. Invalid dependencies and empty scene bounds throw.
     * @param p_scene_bounds World-space bounds of the static scene, which the SVO encloses (see VoxelVolume::enclose()).
     * @param p_shader_prefix Resource-key prefix of the static construction shaders.
     * @param p_dynamic_shader_prefix Resource-key prefix of the implementation's own dynamic update shaders: clear, raster
     * construction, and deferred retries.
     * @param p_rebuild_static_every_frame When true, the full static-scene construction runs every frame instead of once,
     * for observing its per-frame cost. Dynamic construction is unaffected either way.
     */
    ISVORenderFeature(RendererVK* p_renderer,
        Ref<MeshResourcesVK> p_static_mesh_resources,
        const RUAABB3D& p_scene_bounds,
        std::string_view p_shader_prefix,
        std::string_view p_dynamic_shader_prefix,
        bool p_rebuild_static_every_frame);

    /**
     * @brief Workgroup count of the per-frame reset dispatch.
     */
    virtual uint32_t get_reset_group_count() const = 0;
    // The construction the voxel_svo module builds.
    virtual SVORecipeVariant get_recipe_variant() const = 0;

    /**
     * @brief Compiles a compute pipeline from its shader resource key.
     */
    Ref<ROComputePipelineVK> compile_compute_pipeline(std::string_view p_resource_id) const;
    static constexpr size_t SVO_MAX_DEPTH = 9;
    static constexpr size_t SVO_VOXEL_COUNT = (size_t{1} << SVO_MAX_DEPTH) * (size_t{1} << SVO_MAX_DEPTH) * (size_t{1} << SVO_MAX_DEPTH);
    static constexpr size_t SVO_MAX_NODE_COUNT = SVO_VOXEL_COUNT / 64;
    static constexpr size_t SVO_MAX_DYNAMIC_NODE_COUNT = SVO_MAX_NODE_COUNT;
    static constexpr size_t SVO_MAX_DEFERRED_TASK_COUNT = SVO_VOXEL_COUNT / 8;
    // Each deferred list stores its entry count in element zero, followed by up to SVO_MAX_DEFERRED_TASK_COUNT entries.
    static constexpr size_t SVO_DEFERRED_LIST_ELEMENT_COUNT = SVO_MAX_DEFERRED_TASK_COUNT + 1;
    static constexpr uint32_t SVO_DEFERRED_THREADS_PER_GROUP = 128;
    // Deferred-list shaders stride over the whole dispatch, so lists larger than one dispatch remain covered.
    static constexpr uint32_t SVO_DEFERRED_DISPATCH_GROUP_COUNT = static_cast<uint32_t>(std::min<size_t>(
        (SVO_MAX_DEFERRED_TASK_COUNT + SVO_DEFERRED_THREADS_PER_GROUP - 1) / SVO_DEFERRED_THREADS_PER_GROUP, 65535));

    /**
     * @brief Borrowed renderer that owns the pipelines and graphs.
     */
    RendererVK* renderer_vk;
    /**
     * @brief Borrowed renderer data used to create buffers and tasks.
     */
    const RenderContextVK* render_context;
    /**
     * @brief Borrowed allocator of render objects and tasks.
     */
    RenderAllocator* render_allocator;
    /**
     * @brief Resets the ping deferred count before an iteration that writes the ping list.
     */
    Ref<ROComputePipelineVK> svo_deferred_count_reset_ping_compute_pipeline = MEM_NULL;
    /**
     * @brief Resets the pong deferred count before an iteration that writes the pong list.
     */
    Ref<ROComputePipelineVK> svo_deferred_count_reset_pong_compute_pipeline = MEM_NULL;
    /**
     * @brief Static deferred retries that read pong and write ping.
     */
    Ref<ROComputePipelineVK> svo_deferred_propagation_ping_write_compute_pipeline = MEM_NULL;
    /**
     * @brief Static deferred retries that read ping and write pong.
     */
    Ref<ROComputePipelineVK> svo_deferred_propagation_pong_write_compute_pipeline = MEM_NULL;
    /**
     * @brief Dynamic deferred retries that read ping and write pong.
     */
    Ref<ROComputePipelineVK> dynamic_ping_pipeline;
    /**
     * @brief Dynamic deferred retries that read pong and write ping.
     */
    Ref<ROComputePipelineVK> dynamic_pong_pipeline;

private:
    static constexpr std::string_view RT_SVO_FRAME_BEGIN = "svo_frame_begin";
    static constexpr std::string_view GBUFFER_DEPTH_IMAGE_VIEW = "gbuffer_depth";
    static constexpr VkFormat GBUFFER_DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
    static constexpr size_t SVO_EXTENT = size_t{1} << SVO_MAX_DEPTH;
    static constexpr size_t SVO_MAX_LEAVES_COUNT = SVO_VOXEL_COUNT / 8;
    static constexpr size_t SVO_MAX_DYNAMIC_LEAVES_COUNT = SVO_MAX_LEAVES_COUNT / 1024;
    // A voxel can hold at most one unreturned eight-slot ticket; include transient overflow reservations.
    static_assert(SVO_VOXEL_COUNT <= (0xffffffffU - SVO_MAX_NODE_COUNT) / 8U);
    static_assert(SVO_VOXEL_COUNT <= (0xffffffffU - SVO_MAX_LEAVES_COUNT) / 8U);

    struct SVOInfo {
        glm::vec3 origin;
        float width;
        uint32_t max_depth;
        uint32_t max_node_count;
        uint32_t max_leaves_count;
        uint32_t node_count;
        uint32_t leaves_count;
        uint32_t data_count;
        uint32_t max_deferred_task_count;
        uint32_t max_dynamic_node_count;
        uint32_t max_dynamic_leaves_count;
        uint32_t dynamic_node_count;
        uint32_t dynamic_leaves_count;
    };

    static_assert(sizeof(SVOInfo) == 60);
    static_assert(offsetof(SVOInfo, dynamic_node_count) == 52);

    struct SVONode {
        enum class ChildrenState {
            NODE = 0,
            EMPTY = 1,
            ALLOCATING = 2,
            LEAF = 3,
            ALLOCATING_DYNAMIC = 4,
            NODE_DYNAMIC = 5,
            LEAF_DYNAMIC = 6,
        };
        enum Subregion {
            XYZ_000,
            XYZ_001,
            XYZ_010,
            XYZ_011,
            XYZ_100,
            XYZ_101,
            XYZ_110,
            XYZ_111,
            MAX_SUBREGION,
        };
        // Mirrors the packed Slang description: state 31:29; leaf occupancy 28:21/block 20:0;
        // nodes use block 20:0 with bits 28:21 unused. Block indices address eight entries.
        uint32_t children_data;
        uint32_t data_index;
        int32_t parent_index;
        uint32_t original_children_data;
    };

    static_assert(sizeof(SVONode) == 16);
    static_assert(alignof(SVONode) == alignof(uint32_t));
    static_assert(offsetof(SVONode, data_index) == 4);
    static_assert(offsetof(SVONode, parent_index) == 8);
    static_assert(SVO_MAX_DEPTH >= 2);
    static_assert(SVO_MAX_NODE_COUNT % 8 == 0 && SVO_MAX_NODE_COUNT <= (1U << 21U) * 8U);
    static_assert(SVO_MAX_LEAVES_COUNT % 8 == 0 && SVO_MAX_LEAVES_COUNT <= (1U << 21U) * 8U);

    struct SVOLeaf {
        uint32_t data_index;
        int32_t parent_index;
    };

    struct SVODynamicLeafBlock {
        uint32_t original_children_data;
        SVOLeaf leaves[8];
    };
    static_assert(SVO_MAX_DYNAMIC_LEAVES_COUNT % 8 == 0 && SVO_MAX_DYNAMIC_LEAVES_COUNT <= (1U << 21U) * 8U);
    static_assert(sizeof(SVODynamicLeafBlock) == 68);
    static_assert(offsetof(SVODynamicLeafBlock, leaves) == 4);
    static_assert(offsetof(SVONode, original_children_data) == 12);
    static_assert(SVO_MAX_NODE_COUNT + SVO_MAX_DYNAMIC_NODE_COUNT <= (1U << 21U) * 8U);

    // Deferred-list entries are uint32_t voxel indices packing max_depth bits per axis.
    static_assert(SVO_MAX_DEPTH <= 10);

    void create_resources();
    void initialize();
    void compile_pipelines(std::string_view p_shader_prefix, std::string_view p_dynamic_shader_prefix);
    Ref<ROBufferVK> create_initialized_buffer(VkBufferUsageFlags p_usage, const void* p_data, size_t p_size) const;

    Ref<MeshResourcesVK> static_mesh_resources;
    // The cube the SVO covers, derived from the scene bounds.
    VoxelVolume svo_volume;
    // Leaves the static construction untriggered, so the static scene is reset and rebuilt every frame.
    bool rebuild_static_every_frame;
    Ref<ROBufferVK> dynamic_leaves;
    Ref<ROGraphicsPipelineVK> dynamic_build_pipeline;
    std::string shader_prefix;
    std::string dynamic_shader_prefix;
    Ref<ROComputePipelineVK> dynamic_clear_pipeline;
    Ref<ROGraphicsPipelineVK> svo_build_graphics_pipeline = MEM_NULL;
    Ref<ROComputePipelineVK> svo_reset_compute_pipeline = MEM_NULL;
    Ref<ROComputePipelineVK> svo_pre_deferred_debug_capture_compute_pipeline = MEM_NULL;
    Ref<ROComputePipelineVK> svo_debug_draw_setup_compute_pipeline = MEM_NULL;
    Ref<ROGraphicsPipelineVK> svo_debug_draw_graphics_pipeline = MEM_NULL;
    Ref<ROGraphicsPipelineVK> svo_pre_deferred_debug_draw_graphics_pipeline = MEM_NULL;
    Ref<ROBufferVK> svo_info;
    Ref<ROBufferVK> svo_nodes;
    Ref<ROBufferVK> svo_leaves;
    Ref<ROBufferVK> svo_debug_draw_index_buffer;
    Ref<ROBufferVK> svo_debug_draw_indirect_buffer;
    Ref<ROBufferVK> svo_pre_deferred_debug_draw_indirect_buffer;
};

} // namespace WhiteBirdEngine

#endif
