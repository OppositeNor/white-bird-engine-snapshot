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
#include "voxel_render_feature_octree.hh"
#include "../render_test.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/data/data_value.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "dynamic_mesh_render_feature.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/compute_pipeline_compiler_vk.hh"
#include "function/render_vk/graphics_pipeline_compiler_vk.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/rend_extent_2d_params_vk.hh"
#include "function/render_vk/rend_graph_recipe_builder_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "function/render_vk/rt_graphics_draw_mesh_vk.hh"
#include "function/render_vk/rt_params_graphics_draw_mesh_vk.hh"
#include "function/render_vk/rt_run_compute_pipeline_vk.hh"
#include "function/render_vk/rt_run_graphics_pipeline_indirect_vk.hh"
#include "function/render_vk/rt_triggered_vk.hh"
#include "generated/serializables_sd.gen.hh"
#include "i_voxel_render_feature.hh"
#include "octree_recipe_params.hh"
#include "resource/resource_manager/resource_manager.hh"
#include "utils/defs.hh"
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {
namespace {

// Render database keys of the construction pipelines, which the graphics_draw_mesh construction tasks name.
constexpr std::string_view BUILD_PIPELINE = "octree_build_static_mesh_pipeline";
constexpr std::string_view DYNAMIC_BUILD_PIPELINE = "octree_dynamic_build_static_mesh_pipeline";

/**
 * @brief Clears octree textures to OCTREE_EMPTY in every mip level; neither copyable nor movable.
 * @details The first reported writes must be the textures' full views, so the graph moves every mip level into the unified
 * layout before the first clear. The retained images must outlive the task.
 */
class RTOctreeClearVK final : public IRenderTaskVK {
public:
    RTOctreeClearVK() = delete;
    RTOctreeClearVK(Vector<Ref<ROImageVK>, RenderAllocator> p_images, Vector<std::string, RenderAllocator> p_written_resource_ids)
        : images(std::move(p_images)), written_resource_ids(std::move(p_written_resource_ids)) {
    }
    virtual ~RTOctreeClearVK() override = default;
    RTOctreeClearVK(const RTOctreeClearVK&) = delete;
    RTOctreeClearVK(RTOctreeClearVK&&) = delete;
    RTOctreeClearVK& operator=(const RTOctreeClearVK&) = delete;
    RTOctreeClearVK& operator=(RTOctreeClearVK&&) = delete;

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::COMPUTE_1;
    }

    virtual void append_accesses(
        Vector<ResAccessVK, RenderAllocator>& /*p_reads*/, Vector<ResAccessVK, RenderAllocator>& p_writes) const override {
        for (const std::string& resource_id : written_resource_ids) {
            p_writes.push_back({.res_id = resource_id, .usage = RendResUsageVK::TRANSFER_DST});
        }
    }

    virtual void record(VkCommandBuffer p_command_buffer, double /*p_delta*/) override {
        // Mirrors OCTREE_EMPTY of octree/octree_common.slang.
        const VkClearColorValue empty{.uint32 = {255, 255, 255, 255}};
        const VkImageSubresourceRange every_level{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        for (const Ref<ROImageVK>& image : images) {
            vkCmdClearColorImage(p_command_buffer, image->get_image(), VK_IMAGE_LAYOUT_GENERAL, &empty, 1, &every_level);
        }
    }

private:
    Vector<Ref<ROImageVK>, RenderAllocator> images;
    Vector<std::string, RenderAllocator> written_resource_ids;
};

/**
 * @brief Reports a texture written through its per-level views as also written through its full view; neither copyable nor
 * movable.
 * @details The traversal reads the full view, so its resource edges order the reads after this task's writes. The full view
 * is reported first, so a first write moves every mip level into the unified layout.
 */
class RTOctreeTextureWriteVK final : public IRenderTaskVK {
public:
    RTOctreeTextureWriteVK() = delete;
    RTOctreeTextureWriteVK(Unique<IRenderTaskVK> p_task, std::string_view p_texture_view_id)
        : task(std::move(p_task)), texture_view_id(p_texture_view_id) {
        WBE_DEBUG_ASSERT(task != MEM_NULL);
    }
    virtual ~RTOctreeTextureWriteVK() override = default;
    RTOctreeTextureWriteVK(const RTOctreeTextureWriteVK&) = delete;
    RTOctreeTextureWriteVK(RTOctreeTextureWriteVK&&) = delete;
    RTOctreeTextureWriteVK& operator=(const RTOctreeTextureWriteVK&) = delete;
    RTOctreeTextureWriteVK& operator=(RTOctreeTextureWriteVK&&) = delete;

    virtual QueueFamilyVK queue_family() const override {
        return task->queue_family();
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override {
        p_writes.push_back({.res_id = texture_view_id, .usage = RendResUsageVK::STORAGE_IMAGE});
        task->append_accesses(p_reads, p_writes);
    }

    virtual void bind_resources(const RendResViewVK& p_resources) override {
        task->bind_resources(p_resources);
    }

    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override {
        task->record(p_command_buffer, p_delta);
    }

private:
    Unique<IRenderTaskVK> task;
    std::string texture_view_id;
};

} // namespace

VoxelRenderFeatureOctree::VoxelRenderFeatureOctree(RendererVK* p_renderer,
    Ref<MeshResourcesVK> p_static_mesh_resources,
    const RUAABB3D& p_scene_bounds,
    bool p_rebuild_static_every_frame)
    : renderer_vk(p_renderer), render_context(nullptr), render_allocator(nullptr),
      static_mesh_resources(std::move(p_static_mesh_resources)), octree_volume(VoxelVolume::enclose(p_scene_bounds, OCTREE_EXTENT)),
      rebuild_static_every_frame(p_rebuild_static_every_frame) {
    if (renderer_vk == nullptr) {
        throw std::runtime_error("VoxelRenderFeatureOctree: renderer is required.");
    }
    if (static_mesh_resources == MEM_NULL) {
        throw std::runtime_error("VoxelRenderFeatureOctree: static scene is required.");
    }
    render_context = renderer_vk->get_render_context();
    render_allocator = render_context->allocator;
    compile_pipelines();
    create_resources();
}

std::string_view VoxelRenderFeatureOctree::get_name() const {
    return "octree (mipmapped 3D texture)";
}

const char* VoxelRenderFeatureOctree::get_debug_view_name(VoxelDebugViewMode p_mode) const {
    switch (p_mode) {
    case VoxelDebugViewMode::DISABLED:
        return "Disabled";
    case VoxelDebugViewMode::POST_SVO_BUILD:
        return "Octree leaves";
    case VoxelDebugViewMode::PRE_DEFERRED_PROPAGATION:
        return "Octree nodes";
    }
    throw std::runtime_error("VoxelRenderFeatureOctree: unknown voxel debug view mode.");
}

std::string VoxelRenderFeatureOctree::get_static_level_resource_id(uint32_t p_level) {
    return std::format("octree_static_levels[{}]", p_level);
}

std::string VoxelRenderFeatureOctree::get_dynamic_level_resource_id(uint32_t p_level) {
    return std::format("octree_dynamic_levels[{}]", p_level);
}

Ref<ROComputePipelineVK> VoxelRenderFeatureOctree::compile_compute_pipeline(std::string_view p_resource_id) const {
    ComputePipelineCompilerVK compiler(ComputePipelineCompilerVKContext{
        .allocator = render_allocator,
        .render_data = render_context,
        .render_database = renderer_vk->get_render_database(),
    });
    return compiler.compile(p_resource_id, ResourceManager::get_singleton()->get_resource<RUShader>(p_resource_id)).compute_pipeline;
}

Ref<ROGraphicsPipelineVK> VoxelRenderFeatureOctree::compile_debug_draw_pipeline(std::string_view p_resource_id) const {
    GraphicsPipelineCompilerVKContext context{
        .allocator = render_allocator,
        .render_data = render_context,
        .render_database = renderer_vk->get_render_database(),
        .renderer = renderer_vk,
        .color_attachment_formats = {render_context->swap_chain_format},
        .depth_attachment_format = GBUFFER_DEPTH_FORMAT,
        .primitive_topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    };
    return GraphicsPipelineCompilerVK(context)
        .compile(p_resource_id, ResourceManager::get_singleton()->get_resource<RUGraphicsPipeline>(p_resource_id))
        .graphics_pipeline;
}

void VoxelRenderFeatureOctree::compile_pipelines() {
    auto* resource_manager = ResourceManager::get_singleton();
    GraphicsPipelineCompilerVK build_compiler(GraphicsPipelineCompilerVKContext{
        .allocator = render_allocator,
        .render_data = render_context,
        .render_database = renderer_vk->get_render_database(),
        .renderer = renderer_vk,
    });
    build_pipeline =
        build_compiler.compile(BUILD_PIPELINE, resource_manager->get_resource<RUGraphicsPipeline>(BUILD_PIPELINE)).graphics_pipeline;
    dynamic_build_pipeline =
        build_compiler
            .compile(DYNAMIC_BUILD_PIPELINE, resource_manager->get_resource<RUGraphicsPipeline>(DYNAMIC_BUILD_PIPELINE))
            .graphics_pipeline;
    debug_gather_leaves_pipeline = compile_compute_pipeline("octree_debug_gather_leaves_shader");
    debug_gather_nodes_pipeline = compile_compute_pipeline("octree_debug_gather_nodes_shader");
    debug_draw_pipeline = compile_debug_draw_pipeline("octree_debug_draw_pipeline");
    debug_node_draw_pipeline = compile_debug_draw_pipeline("octree_debug_node_draw_pipeline");
}

Ref<ROImageVK> VoxelRenderFeatureOctree::create_octree_image() const {
    return render_context->create_image(ROImageVK::Builder()
        .image_3d(VkExtent3D{.width = OCTREE_EXTENT, .height = OCTREE_EXTENT, .depth = OCTREE_EXTENT})
        .set_format(OCTREE_FORMAT)
        .set_mipmap_level(OCTREE_LEVEL_COUNT)
        .set_usage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
}

Ref<ROImageViewVK> VoxelRenderFeatureOctree::create_octree_image_view(
    const Ref<ROImageVK>& p_image, uint32_t p_base_level, uint32_t p_level_count) const {
    return render_context->create_image_view(ROImageViewVK::Builder()
        .set_image(p_image)
        .set_view_type(VK_IMAGE_VIEW_TYPE_3D)
        .set_format(OCTREE_FORMAT)
        .set_subresource_range(VkImageSubresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = p_base_level,
            .levelCount = p_level_count,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }));
}

Ref<ROBufferVK> VoxelRenderFeatureOctree::create_initialized_buffer(VkBufferUsageFlags p_usage, const void* p_data, size_t p_size) const {
    return render_context->create_buffer(ROBufferVK::Builder().set_usage(p_usage).initial_data(p_data, p_size));
}

void VoxelRenderFeatureOctree::create_resources() {
    static_image = create_octree_image();
    dynamic_image = create_octree_image();
    static_view = create_octree_image_view(static_image, 0, OCTREE_LEVEL_COUNT);
    dynamic_view = create_octree_image_view(dynamic_image, 0, OCTREE_LEVEL_COUNT);
    for (uint32_t level = 0; level < OCTREE_LEVEL_COUNT; ++level) {
        static_level_views[level] = create_octree_image_view(static_image, level, 1);
        dynamic_level_views[level] = create_octree_image_view(dynamic_image, level, 1);
    }
    const OctreeInfo info{
        .origin = octree_volume.origin,
        .width = octree_volume.width,
        .max_depth = OCTREE_MAX_DEPTH,
        .max_debug_node_count = OCTREE_MAX_DEBUG_NODE_COUNT,
        .padding = {},
    };
    octree_info = create_initialized_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &info, sizeof(info));
    debug_draw_index_buffer = create_initialized_buffer(
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VOXEL_DEBUG_CUBE_INDICES.data(), sizeof(VOXEL_DEBUG_CUBE_INDICES));
    const VkDrawIndexedIndirectCommand initial_debug_draw_command{
        .indexCount = VOXEL_DEBUG_CUBE_INDICES.size(),
        .instanceCount = 0,
        .firstIndex = 0,
        .vertexOffset = 0,
        .firstInstance = 0,
    };
    // The leaf gather writes every command before each leaf-view draw.
    debug_draw_indirect_buffer = render_context->create_buffer(ROBufferVK::Builder()
                                     .set_properties(sizeof(VkDrawIndexedIndirectCommand),
                                         OCTREE_DEBUG_LEAF_DRAW_COUNT,
                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT));
    debug_leaf_masks = render_context->create_buffer(ROBufferVK::Builder()
                           .set_properties(OCTREE_DEBUG_LEAF_MASK_SIZE, OCTREE_DEBUG_SUBGRID_COUNT, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    debug_node_draw_indirect_buffer = create_initialized_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        &initial_debug_draw_command,
        sizeof(initial_debug_draw_command));
    debug_node_list = render_context->create_buffer(ROBufferVK::Builder()
                          .set_properties(sizeof(uint32_t),
                              size_t{OCTREE_DEBUG_NODE_LIST_HEADER_COUNT} + OCTREE_MAX_DEBUG_NODE_COUNT,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
}

RendTaskDescVK VoxelRenderFeatureOctree::make_texture_write(RendTaskDescVK p_task, std::string_view p_texture_view_id) const {
    p_task.factory = [this, task_factory = p_task.factory, texture_view_id = std::string(p_texture_view_id)](
                         const RendGraphContextVK& p_context) {
        return make_unique<RTOctreeTextureWriteVK>(render_allocator, task_factory(p_context), texture_view_id);
    };
    return p_task;
}

RendTaskDescVK VoxelRenderFeatureOctree::get_clear_render_task_desc(std::string_view p_task_id, bool p_clear_static) const {
    RendTaskDescVK task_desc(render_allocator);
    task_desc.task_id = p_task_id;
    task_desc.factory = [this, p_clear_static](const RendGraphContextVK& /*p_context*/) {
        Vector<Ref<ROImageVK>, RenderAllocator> images(render_allocator);
        Vector<std::string, RenderAllocator> written_resource_ids(render_allocator);
        // The full views come first, so the graph's first-use layout transition covers every mip level.
        if (p_clear_static) {
            images.push_back(static_image);
            written_resource_ids.emplace_back(OCTREE_STATIC);
        }
        images.push_back(dynamic_image);
        written_resource_ids.emplace_back(OCTREE_DYNAMIC);
        for (uint32_t level = 0; level < OCTREE_LEVEL_COUNT; ++level) {
            if (p_clear_static) {
                written_resource_ids.push_back(get_static_level_resource_id(level));
            }
            written_resource_ids.push_back(get_dynamic_level_resource_id(level));
        }
        return make_unique<RTOctreeClearVK>(render_allocator, std::move(images), std::move(written_resource_ids));
    };
    return task_desc;
}

RendTaskDescVK& VoxelRenderFeatureOctree::add_construction_render_task(RendGraphDescVK& p_graph,
    std::string_view p_task_id,
    bool p_double_sided,
    const DynamicMeshRenderFeature* p_dynamic_mesh,
    size_t p_dynamic_entity_index) const {
    const RendExtent2DParamsVK extent{.width = OCTREE_EXTENT, .height = OCTREE_EXTENT};
    if (p_dynamic_mesh != nullptr) {
        RendTaskDescVK& task = p_dynamic_mesh->add_task(p_graph, p_task_id, p_dynamic_entity_index, DYNAMIC_BUILD_PIPELINE, extent);
        task = make_texture_write(task, OCTREE_DYNAMIC);
        return task;
    }
    // The construction pass rasterizes without attachments.
    RendTaskDescVK& task = RTGraphicsDrawMeshVK::add_to_graph(p_graph,
        p_task_id,
        RTParamsGraphicsDrawMeshVK{
            .mesh = std::string(STATIC_MESH_KEY),
            .pipeline = std::string(BUILD_PIPELINE),
            .parts = {p_double_sided ? MeshPartVK::DOUBLE_SIDED : MeshPartVK::OPAQUE},
            .render_extent = extent,
        },
        render_context);
    task = make_texture_write(task, OCTREE_STATIC);
    return task;
}

RendTaskDescVK VoxelRenderFeatureOctree::make_static_build(RendTaskDescVK p_task, const VoxelRecipeInputs& p_inputs) const {
    if (rebuild_static_every_frame) {
        // The reset stage empties both textures, so every frame rebuilds from an empty octree.
        return p_task;
    }
    p_task.factory = RTTriggeredVK::wrap_factory(
        std::move(p_task.factory), std::string(p_inputs.static_build_event), render_context, p_inputs.static_build_pending);
    return p_task;
}

void VoxelRenderFeatureOctree::configure_recipe(
    DataValue& p_parameters, RendGraphRecipeImportsVK& p_imports, const VoxelRecipeInputs& p_inputs) const {
    if (p_inputs.dynamic_mesh == nullptr) {
        throw std::runtime_error("VoxelRenderFeatureOctree: the dynamic mesh is required to configure a recipe.");
    }
    OctreeRecipeParams params;
    params.module = "render_graph_utils.features.voxel_octree";
    params.level_count = OCTREE_LEVEL_COUNT;
    params.leaf_gather_group_count = OctreeGroupCount{
        .x = OCTREE_DEBUG_SUBGRID_GRID_EXTENT.width,
        .y = OCTREE_DEBUG_SUBGRID_GRID_EXTENT.height,
        .z = OCTREE_DEBUG_SUBGRID_GRID_EXTENT.depth,
    };
    params.node_gather_group_count = OCTREE_DEBUG_GATHER_GROUP_COUNT;
    params.leaf_draw_count = OCTREE_DEBUG_LEAF_DRAW_COUNT;

    // Textures and buffers.
    const auto import = [&p_imports, &params](std::string_view p_name, const IBindableRenderResourceVK& p_resource) {
        p_imports.add(p_name, p_resource);
        params.imports.emplace_back(p_name);
    };
    import(OCTREE_INFO, *octree_info);
    import(OCTREE_STATIC, *static_view);
    import(OCTREE_DYNAMIC, *dynamic_view);
    import(OCTREE_DEBUG_DRAW_INDEX_BUFFER, *debug_draw_index_buffer);
    import(OCTREE_DEBUG_DRAW_INDIRECT_BUFFER, *debug_draw_indirect_buffer);
    import(OCTREE_DEBUG_LEAF_MASKS, *debug_leaf_masks);
    import(OCTREE_DEBUG_NODE_DRAW_INDIRECT_BUFFER, *debug_node_draw_indirect_buffer);
    import(OCTREE_DEBUG_NODE_LIST, *debug_node_list);
    for (uint32_t level = 0; level < OCTREE_LEVEL_COUNT; ++level) {
        import(get_static_level_resource_id(level), *static_level_views[level]);
        import(get_dynamic_level_resource_id(level), *dynamic_level_views[level]);
    }
    SerializableSD<OctreeRecipeParams>::serialize(p_parameters, params);

    // The clears record vkCmdClearColorImage, and the construction passes report the full texture view they write through
    // the per-level views; both are C++ tasks the recipe imports. The static ones record once.
    p_imports.add_task(make_static_build(get_clear_render_task_desc(RT_RESET_OCTREE, true), p_inputs))
        .add_task(get_clear_render_task_desc(RT_CLEAR_DYNAMIC_OCTREE, false));
    // The construction draws are prepared on a descriptor of their own, which must hold nothing but the mesh imports the
    // recipe's own mesh draws provide.
    RendGraphDescVK construction(render_allocator);
    add_construction_render_task(construction, RT_RUN_BUILD_OCTREE, false, nullptr, 0);
    add_construction_render_task(construction, RT_RUN_BUILD_OCTREE_DOUBLE_SIDED, true, nullptr, 0);
    add_construction_render_task(construction, RT_RUN_BUILD_DYNAMIC_OCTREE, false, p_inputs.dynamic_mesh, 0);
    add_construction_render_task(construction, RT_RUN_BUILD_DYNAMIC_OCTREE_SECOND, false, p_inputs.dynamic_mesh, 1);
    if (!construction.buffers.empty() || !construction.images.empty() || !construction.image_views.empty() ||
        !construction.samplers.empty()) {
        throw std::runtime_error("VoxelRenderFeatureOctree: the construction passes declared graph-owned resources.");
    }
    for (const RendResVK& resource : construction.resources) {
        const std::string_view id = resource.res_id;
        const auto is_mesh_import = [id](std::string_view p_mesh_key) {
            return id == p_mesh_key || id.starts_with(std::format("{}.", p_mesh_key));
        };
        if (!is_mesh_import(STATIC_MESH_KEY) && !is_mesh_import(DYNAMIC_MESH_KEY)) {
            throw std::runtime_error(std::format(
                "VoxelRenderFeatureOctree: the construction passes import '{}', which the recipe does not provide.", id));
        }
    }
    for (RendTaskDescVK& task : construction.tasks) {
        const bool is_static = task.task_id == RT_RUN_BUILD_OCTREE || task.task_id == RT_RUN_BUILD_OCTREE_DOUBLE_SIDED;
        p_imports.add_task(is_static ? make_static_build(std::move(task), p_inputs) : std::move(task));
    }
}

} // namespace WhiteBirdEngine
