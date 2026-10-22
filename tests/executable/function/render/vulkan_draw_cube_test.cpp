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
#include "core/engine_core.hh"
#include "core/logging/log.hh"
#include "core/memory/reference_raw.hh"
#include "core/memory/reference_strong.hh"
#include "core/profiling/profiler.hh"
#include "function/application/application_game.hh"
#include "function/ecs/components.hh"
#include "function/function_utils.hh"
#include "function/input_manager/i_input_manager.hh"
#include "function/input_manager/key_codes.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/mesh_compiler_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "function/render_vk/rt_params_graphics_draw_mesh_vk.hh"
#include "glm/geometric.hpp"
#include "global/global.hh"
#include "input_manager_mocked.hh"
#include "platform/file_system/path.hh"
#include "render_test.hh"
#include "resource/resource_chunk/resource_chunk_load_file.hh"
#include "resource/resource_manager/resource_manager.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WBE = WhiteBirdEngine;

namespace {

constexpr VkFormat SHADOWMAP_DEPTH_FORMAT = WBE::ROImageVK::Builder::SHADOW_MAP_DEPTH_FORMAT;
constexpr VkFormat SHADOWMAP_COLOR_FORMAT = WBE::ROImageVK::Builder::SHADOW_MAP_COLOR_FORMAT;
constexpr std::string_view SHADOW_LIGHT_INFO_BUFFER = "shadow_light_info";
constexpr std::string_view CAMERA_INFO_BUFFER = "camera_info";
constexpr std::string_view SPOT_LIGHT_INFO_BUFFER = "spot_light_info";
constexpr std::string_view SPOT_LIGHT_COUNT_BUFFER = "spot_light_count";
constexpr std::string_view SHADOWMAP_SAMPLED_IMAGE_VIEW = "shadowmap";
constexpr std::string_view SHADOW_SAMPLER = "shadow_sampler";

} // namespace

class ApplicationVulkanDrawCubeTest : public WBE::ApplicationGame {
public:
    ApplicationVulkanDrawCubeTest()
        : WBE::ApplicationGame({}, WBE::make_ref<WBE::InputManagerMocked>(WBE::global_allocator(), WBE::global_allocator())) {
    }

    virtual void logic_update(double p_delta) override;
    virtual void render_update(double p_delta) override;

    void setup_draw_task();

private:
    void setup_render_context();
    void load_resources();
    void setup_spot_light();
    void register_draw_cube_render_graph(WBE::RenderTestMeshDraw p_shadow_task, WBE::RenderTestMeshDraw p_main_task);

    WBE::RendererVK* renderer_vk = nullptr;
    const WBE::RenderContextVK* render_context = nullptr;
    WBE::RenderAllocator* render_allocator = nullptr;
    WBE::RefRes<WBE::RUMesh3D> cube_mesh;
    WBE::RefRes<WBE::RUGraphicsPipeline> shadow_pipeline_resource;
    WBE::RefRes<WBE::RUGraphicsPipeline> main_pipeline_resource;
    WBE::Ref<WBE::MeshResourcesVK> static_mesh_resources;
    // Graph-owned camera buffer, updated from the logic thread through render jobs.
    WBE::Ref<WBE::ROBufferVK> camera_info_ubo;
    WBE::ComponentSpotLight spot_light_component{};
    VkExtent2D shadow_extent{};
    WBE::ComponentCamera camera_component{};
    glm::vec3 camera_position = TEST_DEFAULT_CAMERA_POSITION;
    float camera_yaw = default_test_camera_yaw();
    float camera_pitch = default_test_camera_pitch();
};

int main(int p_argc, char* p_argv[]) {
    WBE::on_exec_test_start();
    std::unique_ptr<WBE::Global> global = std::make_unique<WBE::Global>(p_argc, p_argv, WBE::Path("test_env"));
    {
        ApplicationVulkanDrawCubeTest game_application;
        {
            WBE_START_PROFILE(WBE::WBE_CHANNEL_GLOBAL, "Main profiler");
            game_application.setup_draw_task();
            game_application.run();
            size_t renderer_alloc_max = game_application.get_renderer_allocator()->get_internal_fragmentation_tracker();
            auto global_alloc_max_by_arena = WBE::EngineCore::get_singleton()->pool_allocator->get_internal_fragmentation_tracker();
            for (size_t arena_index = 0; arena_index < global_alloc_max_by_arena.size(); ++arena_index) {
                WBE::stdout_log(WBE::WBE_CHANNEL_DEBUG)
                    ->message(std::format("Global allocator arena {} max used: {}", arena_index, global_alloc_max_by_arena[arena_index]));
            }
            WBE::stdout_log(WBE::WBE_CHANNEL_DEBUG)->message(std::format("Renderer allocator max used: {}", renderer_alloc_max));
        }
    }
    const auto& global_profiles = global->engine_core->profiling_manager->get_profile_data(WBE::WBE_CHANNEL_GLOBAL);
    for (const auto& profile : global_profiles) {
        WBE::stdout_log(WBE::WBE_CHANNEL_GLOBAL)->message(std::format("{}: Delta: {} seconds", profile.message, profile.delta));
    }
    return EXIT_SUCCESS;
}

void ApplicationVulkanDrawCubeTest::logic_update(double p_delta) {
    WBE::IInputManager* input_manager = WBE::IInputManager::get_singleton();
    if (input_manager == nullptr) {
        return;
    }

    float delta = static_cast<float>(p_delta);
    if (input_manager->get_input_status(WBE::KEYCODE_LEFT).is_down()) {
        camera_yaw -= TEST_CAMERA_TURN_SPEED * delta;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_RIGHT).is_down()) {
        camera_yaw += TEST_CAMERA_TURN_SPEED * delta;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_UP).is_down()) {
        camera_pitch += TEST_CAMERA_TURN_SPEED * delta;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_DOWN).is_down()) {
        camera_pitch -= TEST_CAMERA_TURN_SPEED * delta;
    }
    camera_pitch = std::clamp(camera_pitch, -TEST_CAMERA_PITCH_LIMIT, TEST_CAMERA_PITCH_LIMIT);

    glm::vec3 forward = test_camera_forward(camera_yaw, camera_pitch);
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0F, 1.0F, 0.0F)));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));
    float move_delta = TEST_CAMERA_MOVE_SPEED * delta;

    if (input_manager->get_input_status(WBE::KEYCODE_W).is_down()) {
        camera_position += forward * move_delta;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_S).is_down()) {
        camera_position -= forward * move_delta;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_D).is_down()) {
        camera_position += right * move_delta;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_A).is_down()) {
        camera_position -= right * move_delta;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_E).is_down()) {
        camera_position += up * move_delta;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_Q).is_down()) {
        camera_position -= up * move_delta;
    }

    camera_component.view = test_camera_view(camera_position, camera_yaw, camera_pitch);

    WBE::ROBufferVK* camera_info_buffer = camera_info_ubo.get();
    if (camera_info_buffer == nullptr) {
        return;
    }
    CameraInfoGPU camera_info = make_camera_info_gpu(camera_component);
    add_render_job([camera_info, camera_info_buffer] {
        camera_info_buffer->update_buffer(&camera_info, sizeof(CameraInfoGPU));
    });
}

void ApplicationVulkanDrawCubeTest::render_update(double /* p_delta */) {
    WBE::on_exec_test_render(*renderer_vk, render_context->current_frame);
}

void ApplicationVulkanDrawCubeTest::setup_draw_task() {
    setup_render_context();
    load_resources();
    setup_spot_light();

    static_mesh_resources = WBE::create_mesh_resources(renderer_vk, WBE::STATIC_MESH_KEY, cube_mesh, main_pipeline_resource);
    const std::array shadow_formats{SHADOWMAP_COLOR_FORMAT};
    // The render database holds the pipelines weakly; the draw tasks look them up by key and keep them alive.
    WBE::Ref<WBE::ROGraphicsPipelineVK> shadow_pipeline = WBE::compile_render_test_graphics_pipeline(
        renderer_vk, "draw_cube_shadow_pipeline", shadow_pipeline_resource, shadow_formats, SHADOWMAP_DEPTH_FORMAT);
    WBE::RenderTestMeshDraw shadow_task{
        .task_id = std::string(RT_RUN_PIPELINE_SHADOWMAP),
        .params =
            WBE::RTParamsGraphicsDrawMeshVK{
                .mesh = std::string(WBE::STATIC_MESH_KEY),
                .pipeline = "draw_cube_shadow_pipeline",
                .parts = {WBE::MeshPartVK::OPAQUE},
                .render_extent = WBE::make_fixed_extent_params(shadow_extent),
                .clear_values = {WBE::make_clear_color(0.0F, 0.0F, 0.0F, 0.0F), WBE::make_clear_depth(0.0F)},
            },
        .color_attachment_ids = {std::string(REND_RES_TEST_SHADOWMAP_COLOR_IMAGE_VIEW)},
        .depth_attachment_id = std::string(REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW),
    };

    const std::array main_formats{render_context->swap_chain_format};
    WBE::Ref<WBE::ROGraphicsPipelineVK> main_pipeline =
        WBE::compile_render_test_graphics_pipeline(renderer_vk, "draw_cube_main_pipeline", main_pipeline_resource, main_formats);
    WBE::RenderTestMeshDraw main_task{
        .task_id = std::string(RT_RUN_PIPELINE),
        .params =
            WBE::RTParamsGraphicsDrawMeshVK{
                .mesh = std::string(WBE::STATIC_MESH_KEY),
                .pipeline = "draw_cube_main_pipeline",
                .parts = {WBE::MeshPartVK::OPAQUE},
                .clear_values = {WBE::make_clear_color(0.0F, 0.0F, 0.0F, 0.0F)},
            },
        .color_attachment_ids = {std::string(WBE::REND_RES_SWAP_CHAIN)},
    };

    register_draw_cube_render_graph(std::move(shadow_task), std::move(main_task));
}

void ApplicationVulkanDrawCubeTest::setup_render_context() {
    renderer_vk = WBE::RendererVK::get_singleton();
    render_context = renderer_vk->get_render_context();
    render_allocator = get_renderer_allocator().get();
    camera_component = make_default_test_camera_component(render_context->swap_chain_aspect_ratio);
}

void ApplicationVulkanDrawCubeTest::load_resources() {
    auto* resource_manager = WBE::ResourceManager::get_singleton();
    auto resource_chunk = WBE::make_ref<WBE::ResourceChunkLoadFile>(
        WBE::global_allocator(), "assets/models/models.json", resource_manager->get_allocator());
    resource_manager->register_chunk(resource_chunk);

    cube_mesh = resource_manager->get_resource<WBE::RUMesh3D>("model_cube_static.mesh");
    if (cube_mesh == nullptr) {
        throw std::runtime_error("Failed to setup draw cube test: static mesh resource was not found.");
    }
    shadow_pipeline_resource = resource_manager->get_resource<WBE::RUGraphicsPipeline>("shadowmap_spot");
    if (shadow_pipeline_resource == nullptr) {
        throw std::runtime_error("Failed to setup draw cube test: static shadow graphics pipeline resource not found.");
    }
    main_pipeline_resource = resource_manager->get_resource<WBE::RUGraphicsPipeline>("forward_cook_torrance_pbr");
    if (main_pipeline_resource == nullptr) {
        throw std::runtime_error("Failed to setup draw cube test: static main graphics pipeline resource not found.");
    }
}

void ApplicationVulkanDrawCubeTest::setup_spot_light() {
    spot_light_component = make_default_test_spot_light_component(32.0F);
    shadow_extent = VkExtent2D{
        .width = spot_light_component.shadow_map_resolution,
        .height = spot_light_component.shadow_map_resolution,
    };
}

void ApplicationVulkanDrawCubeTest::register_draw_cube_render_graph(
    WBE::RenderTestMeshDraw p_shadow_task, WBE::RenderTestMeshDraw p_main_task) {
    static constexpr std::string_view SHADOWMAP_DEPTH_IMAGE = "shadowmap_depth_image";
    static constexpr std::string_view SHADOWMAP_COLOR_IMAGE = "shadowmap_color_image";
    const VkExtent3D shadow_image_extent{.width = shadow_extent.width, .height = shadow_extent.height, .depth = 1};
    const auto make_uniform_buffer = [](std::vector<uint8_t> p_initial_data) {
        return WBE::RendBufferDescVK{
            .usage = WBE::RendResPrimaryUsageVK::BUFFER_UNIFORM,
            .domain = WBE::DeviceDomain::HOST_DEVICE,
            .initial_data = std::move(p_initial_data),
        };
    };
    const uint32_t spot_light_count = 1;

    // The buffers start with the state at registration; equal descriptions keep them, and their runtime updates, across
    // rebuilds.
    const std::vector<uint8_t> spot_light_info_data = WBE::to_initial_data(make_spot_light_info_gpu(spot_light_component));
    const std::vector<uint8_t> camera_info_data = WBE::to_initial_data(make_camera_info_gpu(camera_component));
    const std::vector<uint8_t> spot_light_count_data = WBE::to_initial_data(spot_light_count);
    // The graph calls this for its first build and again whenever it rebuilds, such as after a swap chain resize.
    WBE::Ref<WBE::RenderGraphVK> graph = renderer_vk->register_render_graph("draw_cube_pipeline",
        [this,
            shadow_task = std::move(p_shadow_task),
            main_task = std::move(p_main_task),
            shadow_image_extent,
            make_uniform_buffer,
            spot_light_info_data,
            camera_info_data,
            spot_light_count_data](const WBE::RendGraphContextVK& /*p_graph_context*/) {
            WBE::RendGraphDescVK pipeline_desc(render_allocator);
            pipeline_desc.add_resource(WBE::REND_RES_SWAP_CHAIN, *renderer_vk->acquire_swap_chain_image_view_container())
                .add_image(SHADOWMAP_DEPTH_IMAGE,
                    [=](const WBE::RendGraphContextVK& /*p_context*/) {
                        return WBE::RendImageDescVK{
                            .usage = WBE::RendResPrimaryUsageVK::IMAGE_DEPTH_TARGET,
                            .format = SHADOWMAP_DEPTH_FORMAT,
                            .extent = shadow_image_extent,
                        };
                    })
                .add_image(SHADOWMAP_COLOR_IMAGE,
                    [=](const WBE::RendGraphContextVK& /*p_context*/) {
                        return WBE::RendImageDescVK{
                            .usage = WBE::RendResPrimaryUsageVK::IMAGE_RENDER_TARGET,
                            .format = SHADOWMAP_COLOR_FORMAT,
                            .extent = shadow_image_extent,
                        };
                    })
                .add_image_view(REND_RES_TEST_SHADOWMAP_COLOR_IMAGE_VIEW, SHADOWMAP_COLOR_IMAGE)
                .add_image_view(REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW, SHADOWMAP_DEPTH_IMAGE)
                .add_sampler(SHADOW_SAMPLER,
                    [=](const WBE::RendGraphContextVK& /*p_context*/) {
                        return WBE::RendSamplerDescVK{
                            .address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                            .address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                            .address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                        };
                    })
                .add_buffer(SHADOW_LIGHT_INFO_BUFFER,
                    [make_uniform_buffer, initial_data = spot_light_info_data](const WBE::RendGraphContextVK& /*p_context*/) {
                        return make_uniform_buffer(initial_data);
                    })
                .add_buffer(SPOT_LIGHT_INFO_BUFFER,
                    [make_uniform_buffer, initial_data = spot_light_info_data](const WBE::RendGraphContextVK& /*p_context*/) {
                        return make_uniform_buffer(initial_data);
                    })
                .add_buffer(CAMERA_INFO_BUFFER,
                    [make_uniform_buffer, initial_data = camera_info_data](const WBE::RendGraphContextVK& /*p_context*/) {
                        return make_uniform_buffer(initial_data);
                    })
                .add_buffer(SPOT_LIGHT_COUNT_BUFFER,
                    [make_uniform_buffer, initial_data = spot_light_count_data](const WBE::RendGraphContextVK& /*p_context*/) {
                        return make_uniform_buffer(initial_data);
                    });

            // The main pass samples the shadow depth written by the shadow pass through its reflected "shadowmap" resource.
            // The draw tasks import the mesh themselves.
            shadow_task.add_to(pipeline_desc, render_context).connect(RT_RUN_PIPELINE, REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW);
            main_task.add_to(pipeline_desc, render_context).alias(SHADOWMAP_SAMPLED_IMAGE_VIEW, REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW);
            return pipeline_desc;
        });
    camera_info_ubo = graph->acquire_resource(CAMERA_INFO_BUFFER)->get().dynamic_cast_ref<WBE::ROBufferVK>();
    WBE::write_descriptor_debug_output("draw_cube_pipeline_graph_descriptors", graph->list_descriptors());
    renderer_vk->activate_render_graph("draw_cube_pipeline");
}
