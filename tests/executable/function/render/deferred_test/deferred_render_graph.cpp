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
#include "../render_test.hh"
#include "application_vulkan_deferred_pbr_test.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/data/data_value.hh"
#include "core/logging/log.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/event/event_manager.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/mesh_acceleration_structure_vk.hh"
#include "function/render_vk/rend_graph_recipe_builder_vk.hh"
#include "function/render_vk/rend_graph_script_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_targets_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/rt_build_acceleration_structures_vk.hh"
#include "function/render_vk/rt_cpu_in_flight_acquire_vk.hh"
#include "function/render_vk/rt_generate_mipmaps_vk.hh"
#include "function/render_vk/rt_ping_pong_vk.hh"
#include "function/render_vk/rt_run_compute_pipeline_vk.hh"
#include "function/render_vk/rt_run_compute_pipelines_sequential_vk.hh"
#include "function/render_vk/rt_run_graphics_pipeline_vk.hh"
#include "function/render_vk/rt_toggle_vk.hh"
#include "function/render_vk/rt_triggered_vk.hh"
#include "glm/gtc/packing.hpp"
#include "glm/matrix.hpp"
#include "i_voxel_render_feature.hh"
#include "resource/resource_utils.hh"
#include "rt_imgui_vk.hh"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

constexpr size_t BLOOM_BYTES_PER_PIXEL = sizeof(uint16_t) * 4;

std::vector<uint8_t> convert_bloom_to_rgba8(std::span<const uint8_t> p_bloom_data) {
    if (p_bloom_data.size_bytes() % BLOOM_BYTES_PER_PIXEL != 0) {
        throw std::runtime_error("Failed to convert deferred bloom acquisition: byte count is not pixel aligned.");
    }
    const size_t pixel_count = p_bloom_data.size_bytes() / BLOOM_BYTES_PER_PIXEL;
    std::vector<uint8_t> rgba8(pixel_count * 4);
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        for (size_t channel = 0; channel < 3; ++channel) {
            uint16_t packed_channel = 0;
            std::memcpy(&packed_channel,
                p_bloom_data.data() + pixel * BLOOM_BYTES_PER_PIXEL + channel * sizeof(packed_channel),
                sizeof(packed_channel));
            float value = glm::unpackHalf1x16(packed_channel);
            if (!std::isfinite(value)) {
                value = value > 0.0F ? 1.0F : 0.0F;
            }
            rgba8[pixel * 4 + channel] = static_cast<uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
        }
        rgba8[pixel * 4 + 3] = UINT8_MAX;
    }
    return rgba8;
}

} // namespace

void ApplicationVulkanDeferredPBRTest::register_deferred_render_graph(std::string_view p_graph_id, bool p_use_summed_area_table) {
    // The graph calls this for its first build and again whenever it rebuilds: after a swap chain resize, or when
    // draw_pixel_sort_options() rebuilds it for new post-processing pipelines.
    Ref<RenderGraphVK> graph =
        renderer_vk->register_render_graph(p_graph_id, [this, p_use_summed_area_table](const RendGraphContextVK& p_context) {
            return make_deferred_render_graph_desc(p_use_summed_area_table, p_context);
        });
    write_descriptor_debug_output(std::format("{}_graph_descriptors", p_graph_id), graph->list_descriptors());
    deferred_render_graphs[p_use_summed_area_table ? 1U : 0U] = graph;
}

RendGraphDescVK ApplicationVulkanDeferredPBRTest::make_deferred_render_graph_desc(
    bool p_use_summed_area_table, const RendGraphContextVK& p_context) {
    RendGraphRecipeImportsVK imports = make_deferred_recipe_imports(p_use_summed_area_table);
    DeferredRecipeContext context = make_deferred_recipe_context(p_use_summed_area_table, p_context);
    context.post_processing = DataValue::make_map();
    post_processing->configure_recipe(context.post_processing, imports);
    context.voxel = DataValue::make_map();
    voxel_structure->configure_recipe(context.voxel,
        imports,
        VoxelRecipeInputs{
            .dynamic_mesh = dynamic_mesh.get(),
            .static_build_event = VOXEL_STATIC_BUILD_EVENT_ID,
            .static_build_pending = voxel_static_build_pending,
        });
    const DataValue recipe = RendGraphScriptVK::run(DEFERRED_RECIPE_SCRIPT, RendGraphScriptVK::serialize_context(context));
    RendGraphDescVK pipeline_desc = RendGraphRecipeBuilderVK().build(recipe, imports, render_context);

    // The recipe's mesh draws import the static and dynamic meshes. The acceleration structure and the dynamic entities stay
    // in C++.
    if (mesh_acceleration_structure != MEM_NULL) {
        pipeline_desc.add_resource(MESH_ACCELERATION_STRUCTURE_KEY, *mesh_acceleration_structure);
    }
    dynamic_mesh->register_resources(pipeline_desc);
    return pipeline_desc;
}

DeferredRecipeContext ApplicationVulkanDeferredPBRTest::make_deferred_recipe_context(
    bool p_use_summed_area_table, const RendGraphContextVK& p_context) const {
    const auto make_extent = [](VkExtent2D p_extent) {
        return RendRecipeExtent2DVK{.width = p_extent.width, .height = p_extent.height};
    };
    const auto format = [](VkFormat p_format) {
        return static_cast<ImageFormatVK>(p_format);
    };
    DeferredRecipeContext context;
    static_cast<RendRecipeContextVK&>(context) = RendGraphScriptVK::get_recipe_context(*render_context, p_context);
    context.summed_area_table = p_use_summed_area_table;
    context.spot_light_count = SPOT_LIGHT_COUNT;
    context.max_spot_light_info_count = MAX_SPOT_LIGHT_INFO_COUNT;
    context.shadow_extent = make_extent(shadow_extent);
    context.shadow_mipmap_level_count = shadow_mipmap_level_count;
    context.bloom_extent = make_extent(bloom_extent);
    context.bloom_blur_pass_count = BLOOM_BLUR_PASS_COUNT;
    context.formats = DeferredRecipeFormats{
        .gbuffer_albedo = format(GBUFFER_ALBEDO_FORMAT),
        .gbuffer_normal = format(GBUFFER_NORMAL_FORMAT),
        .gbuffer_rma = format(GBUFFER_RMA_FORMAT),
        .gbuffer_depth = format(GBUFFER_DEPTH_FORMAT),
        .shadowmap_depth = format(SHADOWMAP_DEPTH_FORMAT),
        .shadow_moments = format(SHADOW_MOMENTS_FORMAT),
        .rsm_flux = format(RSM_FLUX_FORMAT),
        .rsm_normal = format(RSM_NORMAL_FORMAT),
        .lighting_output = format(LIGHTING_OUTPUT_FORMAT),
    };
    const size_t graph_index = p_use_summed_area_table ? 1U : 0U;
    context.bloom_acquire_event = BLOOM_ACQUIRE_EVENT_IDS[graph_index];
    context.frame_sequence_event = FRAME_SEQUENCE_EVENT_IDS[graph_index];
    context.has_mesh_acceleration_structure = mesh_acceleration_structure != MEM_NULL;
    // A rebuild creates the toggled tasks in the mode the rendering thread applied last; later changes reach them through
    // the toggle events (see apply_voxel_debug_view_mode()).
    const auto make_toggle = [this](size_t p_group) {
        return RendRecipeToggleVK{.event = std::string(VOXEL_DEBUG_VIEW_TOGGLE_EVENT_IDS[p_group]),
            .enabled = is_voxel_debug_view_toggle_enabled(p_group, applied_voxel_debug_view_mode)};
    };
    context.voxel_debug_view_toggles = VoxelDebugViewToggles{
        .disabled = make_toggle(0),
        .enabled = make_toggle(1),
        .post_svo_build = make_toggle(2),
        .pre_deferred_propagation = make_toggle(3),
    };
    for (size_t entity = 0; entity < dynamic_mesh->get_entity_count(); ++entity) {
        context.dynamic_mesh_transform_ids.emplace_back(dynamic_mesh->get_entity_transform_id(entity));
    }
    return context;
}

RendGraphRecipeImportsVK ApplicationVulkanDeferredPBRTest::make_deferred_recipe_imports(bool p_use_summed_area_table) {
    RendGraphRecipeImportsVK imports;
    imports.add(REND_RES_SWAP_CHAIN, *renderer_vk->acquire_swap_chain_image_view_container())
        .add(GBUFFER_CAMERA_INFO_BUFFER, *gbuffer_camera_info_ubos)
        .add(COMPOSITION_CAMERA_INFO_BUFFER, *composition_camera_info_ubos)
        .add(SPOT_LIGHT_INFO_BUFFER, *spot_light_info_buffers)
        .add(LIGHT_BILLBOARD_TEXTURE_IMAGE_VIEW, *light_billboard_texture_view);
    for (uint32_t light_index = 0; light_index < SPOT_LIGHT_COUNT; ++light_index) {
        const SpotLightGraphIds ids = make_spot_light_graph_ids(light_index);
        imports.add(ids.shadow_light_info, *shadow_light_info_buffers[light_index]).add(ids.light_billboard_info, *light_billboard_ubos[light_index]);
    }
    imports.add_task(create_frame_sequence_acquire_task_desc(p_use_summed_area_table)).add_task(create_imgui_task_desc());
    if (mesh_acceleration_structure != MEM_NULL) {
        imports.add_task(create_mesh_acceleration_structure_build_task_desc());
    }
    return imports;
}

RendTaskDescVK ApplicationVulkanDeferredPBRTest::create_mesh_acceleration_structure_build_task_desc() {
    // Build the acceleration structures once, on the graph's first recording. A recreated task builds them again on its
    // first recording.
    return RendTaskDescVK(render_allocator,
        RT_BUILD_MESH_ACCELERATION_STRUCTURE,
        [this, acceleration_structure = mesh_acceleration_structure](const RendGraphContextVK& /*p_context*/) {
            auto build = make_unique<RTBuildAccelerationStructuresVK>(render_allocator,
                render_context,
                IBindableRenderResourceVK::make_member_resource_id(MESH_ACCELERATION_STRUCTURE_KEY, MeshAccelerationStructureVK::TOP_LEVEL),
                [acceleration_structure](VkCommandBuffer p_command_buffer) {
                    acceleration_structure->record_build(p_command_buffer);
                });
            return make_unique<RTTriggeredVK>(render_allocator, std::move(build), true);
        });
}

RendTaskDescVK ApplicationVulkanDeferredPBRTest::create_frame_sequence_acquire_task_desc(bool p_use_summed_area_table) {
    RendTaskDescVK task_desc(render_allocator);
    task_desc.task_id = RT_ACQUIRE_FRAME_SEQUENCE;
    task_desc.factory = RTTriggeredVK::wrap_factory(
        [this](const RendGraphContextVK& /*p_context*/) -> Unique<IRenderTaskVK> {
            if (frame_sequence_recording) {
                // A recreated wrapper loses its remaining frames, so the sequence would end early.
                throw std::runtime_error("The deferred render graph was recreated during a frame sequence capture.");
            }
            // Completed acquisitions reach write_frame_sequence_frame() through the event registered in setup_bloom_acquisition().
            return make_unique<RTCPUInFlightAcquireVK>(render_allocator, render_context, FRAME_SEQUENCE_IMAGE_VIEW);
        },
        std::string(FRAME_SEQUENCE_EVENT_IDS[p_use_summed_area_table ? 1U : 0U]),
        render_context);
    return task_desc;
}

void ApplicationVulkanDeferredPBRTest::write_completed_bloom_acquire(const RTCPUInFlightAcquireVK::AcquiredImage& p_image) {
    std::filesystem::path output_directory = std::filesystem::path("test_env") / "descriptor_debug";
    std::filesystem::create_directories(output_directory);
    std::filesystem::path output_path = output_directory / "deferred_bloom.png";
    std::vector<uint8_t> rgba8 = convert_bloom_to_rgba8(p_image.content);
    write_image_data_png(output_path, rgba8, p_image.extent.width, p_image.extent.height, 4);
    bloom_acquire_in_flight.store(false, std::memory_order_release);
    stdout_log(WBE_CHANNEL_DEBUG)->message(std::format("Stored deferred bloom acquisition in {}.", output_path.generic_string()));
}

RendTaskDescVK ApplicationVulkanDeferredPBRTest::create_imgui_task_desc() {
    RendTaskDescVK task_desc(render_allocator);
    task_desc.task_id = RT_DRAW_IMGUI;
    task_desc.alias(RenderTargetsVK::color_id(0), REND_RES_SWAP_CHAIN);
    task_desc.factory = [this](const RendGraphContextVK& /*p_context*/) {
        return make_unique<RTImGuiVK>(render_allocator, render_context, imgui_overlay.get());
    };
    return task_desc;
}

ApplicationVulkanDeferredPBRTest::CompositionCameraInfo ApplicationVulkanDeferredPBRTest::make_composition_camera_info() const {
    return CompositionCameraInfo{
        .inverse_view_projection = glm::inverse(camera_component.proj * camera_component.view),
        .camera_world_position = glm::vec4(camera_position, 1.0F),
    };
}

bool ApplicationVulkanDeferredPBRTest::is_voxel_debug_view_toggle_enabled(size_t p_group, VoxelDebugViewMode p_mode) {
    switch (p_group) {
    case 0:
        return p_mode == VoxelDebugViewMode::DISABLED;
    case 1:
        return p_mode != VoxelDebugViewMode::DISABLED;
    case 2:
        return p_mode == VoxelDebugViewMode::POST_SVO_BUILD;
    case 3:
        return p_mode == VoxelDebugViewMode::PRE_DEFERRED_PROPAGATION;
    default:
        break;
    }
    throw std::runtime_error(std::format("Unknown voxel debug view toggle group {}.", p_group));
}

void ApplicationVulkanDeferredPBRTest::apply_voxel_debug_view_mode() {
    const VoxelDebugViewMode mode = voxel_debug_view_mode.load(std::memory_order_relaxed);
    if (mode == applied_voxel_debug_view_mode) {
        return;
    }
    // Emitted between frames, so every toggled task of both graphs records the new mode from the same frame on.
    EventManager* event_manager = EventManager::get_singleton();
    for (size_t group = 0; group < VOXEL_DEBUG_VIEW_TOGGLE_EVENT_IDS.size(); ++group) {
        event_manager->emit(VOXEL_DEBUG_VIEW_TOGGLE_EVENT_IDS[group],
            RTToggleVK::Command{.enabled = is_voxel_debug_view_toggle_enabled(group, mode)});
    }
    applied_voxel_debug_view_mode = mode;
}

void ApplicationVulkanDeferredPBRTest::finish_voxel_static_build() {
    // The renderer advances current_frame only after a frame executes a graph, which records the pending construction.
    if (!voxel_static_build_pending || render_context->current_frame <= voxel_static_build_registration_frame) {
        return;
    }
    EventManager::get_singleton()->emit(VOXEL_STATIC_BUILD_EVENT_ID, RTTriggeredVK::Command::enable_for(0));
    voxel_static_build_pending = false;
    stdout_log(WBE_CHANNEL_DEBUG)->message("Voxel static construction recorded once.");
}

} // namespace WhiteBirdEngine
