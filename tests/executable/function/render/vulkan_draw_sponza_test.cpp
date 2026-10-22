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
#include "core/allocator/i_allocator.hh"
#include "core/engine_core.hh"
#include "core/logging/log.hh"
#include "core/memory/reference_strong.hh"
#include "core/profiling/profiler.hh"
#include "function/application/application_game.hh"
#include "function/ecs/components.hh"
#include "function/function_utils.hh"
#include "function/input_manager/i_input_manager.hh"
#include "function/input_manager/key_codes.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/graphics_pipeline_compiler_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/mesh_compiler_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/rend_extent_2d_params_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_targets_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "function/render_vk/roc_multislots_vk.hh"
#include "function/render_vk/rt_params_graphics_draw_mesh_vk.hh"
#include "function/render_vk/rt_run_graphics_pipeline_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include "global/global.hh"
#include "input_manager_mocked.hh"
#include "platform/file_system/path.hh"
#include "render_test.hh"
#include "resource/resource_chunk/resource_chunk_load_file.hh"
#include "resource/resource_manager/resource_manager.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

constexpr std::string_view REND_RES_TEST_DRAW_SPONZA_DEPTH_IMAGE_VIEW = "sponza_depth";

namespace WBE = WhiteBirdEngine;

namespace {

constexpr VkFormat SHADOWMAP_DEPTH_FORMAT = WBE::ROImageVK::Builder::SHADOW_MAP_DEPTH_FORMAT;
constexpr VkFormat SHADOWMAP_COLOR_FORMAT = WBE::ROImageVK::Builder::SHADOW_MAP_COLOR_FORMAT;
constexpr VkFormat MAIN_DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
constexpr std::string_view RT_RUN_SHADOWMAP_OPAQUE = "run_shadowmap_opaque";
constexpr std::string_view RT_RUN_SHADOWMAP_MASKED = "run_shadowmap_masked";
constexpr std::string_view RT_RUN_MAIN_OPAQUE = "run_main_opaque";
constexpr std::string_view RT_RUN_MAIN_MASKED = "run_main_masked";
constexpr std::string_view RT_RUN_LIGHT_BILLBOARD = "run_light_billboard";
constexpr std::string_view SHADOW_LIGHT_INFO_BUFFER = "shadow_light_info";
constexpr std::string_view CAMERA_INFO_BUFFER = "camera_info";
constexpr std::string_view SPOT_LIGHT_INFO_BUFFER = "spot_light_info";
constexpr std::string_view SPOT_LIGHT_COUNT_BUFFER = "spot_light_count";
constexpr std::string_view SHADOWMAP_SAMPLED_IMAGE_VIEW = "shadowmap";
constexpr std::string_view SHADOW_SAMPLER = "shadow_sampler";
constexpr std::string_view GBUFFER_CAMERA_INFO_BUFFER = "gbuffer_camera_info";
constexpr std::string_view LIGHT_BILLBOARD_INFO_BUFFER = "light_billboard_info";
constexpr std::string_view LIGHT_BILLBOARD_TEXTURE_IMAGE_VIEW = "light_billboard_texture";
constexpr std::string_view LIGHT_BILLBOARD_SAMPLER = "light_billboard_sampler";
constexpr glm::vec2 LIGHT_BILLBOARD_SIZE = glm::vec2(0.35F, 0.35F);

struct ForwardSceneViewConfig {
    glm::vec3 camera_position = glm::vec3(0.0F);
    float camera_yaw = 0.0F;
    float camera_pitch = 0.0F;
    glm::vec3 light_position = glm::vec3(0.0F);
    float light_yaw = 0.0F;
    float light_pitch = 0.0F;
};

ForwardSceneViewConfig make_forward_sponza_view_config() {
    return ForwardSceneViewConfig{
        .camera_position = glm::vec3(1.894651F, 1.6352656F, 4.6991568F),
        .camera_yaw = glm::radians(-77.84428F),
        .camera_pitch = glm::radians(-8.191941F),
        .light_position = glm::vec3(2.9011555F, 1.7444739F, 2.8511076F),
        .light_yaw = glm::radians(-130.4772F),
        .light_pitch = glm::radians(-44.71062F),
    };
}

struct BillboardInfoGPU {
    glm::vec4 position = glm::vec4(0.0F);
    glm::vec2 size = glm::vec2(0.0F);
    glm::vec2 size_padding = glm::vec2(0.0F);
    glm::vec4 rotation_axis = glm::vec4(0.0F);
};

BillboardInfoGPU make_light_billboard_info(const WBE::ComponentSpotLight& p_spot_light_component) {
    return BillboardInfoGPU{
        .position = glm::vec4(p_spot_light_component.position, 1.0F),
        .size = LIGHT_BILLBOARD_SIZE,
        .rotation_axis = glm::vec4(0.0F, 1.0F, 0.0F, 0.0F),
    };
}

// Each frame's buffer is registered in the render database as `<p_key>[<slot>]`, which names it.
WBE::Ref<WBE::ROCMultislotsVK> create_per_frame_uniform_buffers(
    WBE::RenderAllocator* p_allocator, const WBE::RenderContextVK* p_render_data, std::string_view p_key, VkDeviceSize p_buffer_size) {
    return WBE::make_ref<WBE::ROCMultislotsVK>(p_allocator,
        p_allocator,
        p_render_data->frames_in_flight,
        WBE::ROCMultislotsVK::Factory{[p_render_data, key = std::string(p_key), p_buffer_size](
                                          uint32_t p_frame_slot) -> WBE::Ref<WBE::IRenderObjectVK> {
            WBE::Ref<WBE::ROBufferVK> buffer =
                p_render_data->create_buffer(WBE::ROBufferVK::Builder().set_properties(p_buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT));
            p_render_data->render_database->register_compiled_render_resource(std::format("{}[{}]", key, p_frame_slot), buffer);
            return buffer;
        }});
}

} // namespace

class ApplicationVulkanDrawSponzaTest : public WBE::ApplicationGame {
public:
    ApplicationVulkanDrawSponzaTest()
        : WBE::ApplicationGame({}, WBE::make_ref<WBE::InputManagerMocked>(WBE::global_allocator(), WBE::global_allocator())) {
    }

    virtual void logic_update(double p_delta) override;
    virtual void render_update(double p_delta) override;

    void setup_draw_task(const ForwardSceneViewConfig& p_view_config);
    void log_view_state() const;

private:
    void setup_render_context();
    void load_resources();
    void setup_static_scene_resources();
    void setup_spot_light();
    void setup_light_billboard_overlay_resources();
    void write_shadow_dynamic_descriptors();
    void write_main_dynamic_descriptors();
    void register_draw_sponza_render_graph(WBE::RenderTestMeshDraw p_shadow_opaque_task,
        WBE::RenderTestMeshDraw p_shadow_masked_task,
        WBE::RenderTestMeshDraw p_main_opaque_task,
        WBE::RenderTestMeshDraw p_main_masked_task);

    WBE::RendererVK* renderer_vk = nullptr;
    const WBE::RenderContextVK* render_context = nullptr;
    WBE::RenderAllocator* render_allocator = nullptr;
    WBE::RefRes<WBE::RUMesh3D> sponza_mesh;
    WBE::RefRes<WBE::RUGraphicsPipeline> shadow_opaque_pipeline;
    WBE::RefRes<WBE::RUGraphicsPipeline> shadow_masked_pipeline;
    WBE::RefRes<WBE::RUGraphicsPipeline> main_opaque_pipeline;
    WBE::RefRes<WBE::RUGraphicsPipeline> main_masked_pipeline;
    WBE::Ref<WBE::MeshResourcesVK> static_mesh_resources;
    WBE::Ref<WBE::ROGraphicsPipelineVK> shadow_opaque_graphics_pipeline;
    WBE::Ref<WBE::ROGraphicsPipelineVK> shadow_masked_graphics_pipeline;
    WBE::Ref<WBE::ROGraphicsPipelineVK> main_opaque_graphics_pipeline;
    WBE::Ref<WBE::ROGraphicsPipelineVK> main_masked_graphics_pipeline;
    WBE::Ref<WBE::ROCMultislotsVK> camera_info_ubos;
    WBE::Ref<WBE::ROCMultislotsVK> spot_light_info_ubos;
    WBE::Ref<WBE::ROCMultislotsVK> spot_light_count_ubos;
    WBE::ComponentSpotLight spot_light_component{};
    float spot_light_yaw = 0.0F;
    float spot_light_pitch = 0.0F;
    VkExtent2D shadow_extent{};
    WBE::Ref<WBE::ROBufferVK> light_billboard_ubo;
    WBE::Ref<WBE::ROImageViewVK> light_billboard_texture_view;
    WBE::Ref<WBE::ROGraphicsPipelineVK> light_billboard_pipeline;
    WBE::ComponentCamera camera_component{};
    glm::vec3 camera_position = glm::vec3(0.0F);
    float camera_yaw = 0.0F;
    float camera_pitch = 0.0F;
};

int main(int p_argc, char* p_argv[]) {
    WBE::on_exec_test_start();
    std::unique_ptr<WBE::Global> global = std::make_unique<WBE::Global>(p_argc, p_argv, WBE::Path("test_env"));
    {
        ApplicationVulkanDrawSponzaTest game_application;
        {
            WBE_START_PROFILE(WBE::WBE_CHANNEL_GLOBAL, "Main profiler");
            game_application.setup_draw_task(make_forward_sponza_view_config());
            game_application.run();
            game_application.log_view_state();
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

void ApplicationVulkanDrawSponzaTest::logic_update(double p_delta) { // NOLINT
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

    bool spot_light_changed = false;
    if (input_manager->get_input_status(WBE::KEYCODE_I).is_down()) {
        spot_light_component.position.z -= move_delta;
        spot_light_changed = true;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_K).is_down()) {
        spot_light_component.position.z += move_delta;
        spot_light_changed = true;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_J).is_down()) {
        spot_light_component.position.x -= move_delta;
        spot_light_changed = true;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_L).is_down()) {
        spot_light_component.position.x += move_delta;
        spot_light_changed = true;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_U).is_down()) {
        spot_light_component.position.y += move_delta;
        spot_light_changed = true;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_O).is_down()) {
        spot_light_component.position.y -= move_delta;
        spot_light_changed = true;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_Y).is_down()) {
        spot_light_pitch += TEST_CAMERA_TURN_SPEED * delta;
        spot_light_changed = true;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_P).is_down()) {
        spot_light_pitch -= TEST_CAMERA_TURN_SPEED * delta;
        spot_light_changed = true;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_H).is_down()) {
        spot_light_yaw -= TEST_CAMERA_TURN_SPEED * delta;
        spot_light_changed = true;
    }
    if (input_manager->get_input_status(WBE::KEYCODE_SEMICOLON).is_down()) {
        spot_light_yaw += TEST_CAMERA_TURN_SPEED * delta;
        spot_light_changed = true;
    }
    if (spot_light_changed) {
        spot_light_pitch = std::clamp(spot_light_pitch, -TEST_CAMERA_PITCH_LIMIT, TEST_CAMERA_PITCH_LIMIT);
        spot_light_component.direction = test_camera_forward(spot_light_yaw, spot_light_pitch);
        glm::vec3 light_up =
            std::abs(spot_light_component.direction.y) > 0.99F ? glm::vec3(0.0F, 0.0F, 1.0F) : glm::vec3(0.0F, 1.0F, 0.0F);
        spot_light_component.view =
            glm::lookAt(spot_light_component.position, spot_light_component.position + spot_light_component.direction, light_up);
    }

    camera_component.view = test_camera_view(camera_position, camera_yaw, camera_pitch);

    if (camera_info_ubos == WBE::MEM_NULL || spot_light_info_ubos == WBE::MEM_NULL || light_billboard_ubo == WBE::MEM_NULL) {
        return;
    }

    CameraInfoGPU camera_info = make_camera_info_gpu(camera_component);
    SpotLightInfoGPU spot_light_info = make_spot_light_info_gpu(spot_light_component);
    BillboardInfoGPU light_billboard_info = make_light_billboard_info(spot_light_component);
    add_render_job([this, camera_info, spot_light_info, light_billboard_info] {
        camera_info_ubos->publish_buffer_update(&camera_info, 0, sizeof(camera_info));
        spot_light_info_ubos->publish_buffer_update(&spot_light_info, 0, sizeof(spot_light_info));
        light_billboard_ubo->update_buffer(&light_billboard_info, sizeof(light_billboard_info));
    });
}

void ApplicationVulkanDrawSponzaTest::render_update(double /* p_delta */) {
    WBE::on_exec_test_render(*renderer_vk, render_context->current_frame);
}

void ApplicationVulkanDrawSponzaTest::log_view_state() const {
    glm::vec3 camera_forward = test_camera_forward(camera_yaw, camera_pitch);
    WBE::stdout_log(WBE::WBE_CHANNEL_GLOBAL)
        ->message(std::format("Forward Sponza camera: position=({}, {}, {}), yaw_deg={}, pitch_deg={}, forward=({}, {}, {})",
            camera_position.x,
            camera_position.y,
            camera_position.z,
            glm::degrees(camera_yaw),
            glm::degrees(camera_pitch),
            camera_forward.x,
            camera_forward.y,
            camera_forward.z));
    WBE::stdout_log(WBE::WBE_CHANNEL_GLOBAL)
        ->message(std::format("Forward Sponza light: position=({}, {}, {}), yaw_deg={}, pitch_deg={}, direction=({}, {}, {})",
            spot_light_component.position.x,
            spot_light_component.position.y,
            spot_light_component.position.z,
            glm::degrees(spot_light_yaw),
            glm::degrees(spot_light_pitch),
            spot_light_component.direction.x,
            spot_light_component.direction.y,
            spot_light_component.direction.z));
}

void ApplicationVulkanDrawSponzaTest::setup_draw_task(const ForwardSceneViewConfig& p_view_config) {
    camera_position = p_view_config.camera_position;
    camera_yaw = p_view_config.camera_yaw;
    camera_pitch = p_view_config.camera_pitch;
    setup_render_context();
    load_resources();
    setup_static_scene_resources();
    setup_spot_light();
    spot_light_component.position = p_view_config.light_position;
    spot_light_yaw = p_view_config.light_yaw;
    spot_light_pitch = p_view_config.light_pitch;
    spot_light_component.direction = test_camera_forward(spot_light_yaw, spot_light_pitch);
    {
        glm::vec3 light_up =
            std::abs(spot_light_component.direction.y) > 0.99F ? glm::vec3(0.0F, 0.0F, 1.0F) : glm::vec3(0.0F, 1.0F, 0.0F);
        spot_light_component.view =
            glm::lookAt(spot_light_component.position, spot_light_component.position + spot_light_component.direction, light_up);
    }

    // The render database holds the pipelines weakly; the members keep them alive for the draw tasks, which look them up
    // by key.
    auto create_task = [](std::string_view p_task_id,
                           std::string_view p_pipeline_key,
                           WBE::MeshPartVK p_part,
                           std::string_view p_color_attachment,
                           std::string_view p_depth_attachment,
                           WBE::RendExtent2DParamsVK p_extent,
                           bool p_load) {
        const WBE::AttachmentLoadOpVK load_op = p_load ? WBE::AttachmentLoadOpVK::LOAD : WBE::AttachmentLoadOpVK::CLEAR;
        return WBE::RenderTestMeshDraw{
            .task_id = std::string(p_task_id),
            .params =
                WBE::RTParamsGraphicsDrawMeshVK{
                    .mesh = std::string(WBE::STATIC_MESH_KEY),
                    .pipeline = std::string(p_pipeline_key),
                    .parts = {p_part},
                    .render_extent = p_extent,
                    .clear_values = {WBE::make_clear_color(0.0F, 0.0F, 0.0F, 0.0F), WBE::make_clear_depth(0.0F)},
                    .color_load_op = load_op,
                    .depth_load_op = load_op,
                },
            .color_attachment_ids = {std::string(p_color_attachment)},
            .depth_attachment_id = std::string(p_depth_attachment),
        };
    };
    WBE::RenderTestMeshDraw shadow_opaque_task = create_task(RT_RUN_SHADOWMAP_OPAQUE,
        "forward_sponza_shadow_opaque",
        WBE::MeshPartVK::OPAQUE,
        REND_RES_TEST_SHADOWMAP_COLOR_IMAGE_VIEW,
        REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW,
        WBE::make_fixed_extent_params(shadow_extent),
        false);
    WBE::RenderTestMeshDraw shadow_masked_task = create_task(RT_RUN_SHADOWMAP_MASKED,
        "forward_sponza_shadow_masked",
        WBE::MeshPartVK::MASKED,
        REND_RES_TEST_SHADOWMAP_COLOR_IMAGE_VIEW,
        REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW,
        WBE::make_fixed_extent_params(shadow_extent),
        true);
    WBE::RenderTestMeshDraw main_opaque_task = create_task(
        RT_RUN_MAIN_OPAQUE,
        "forward_sponza_main_opaque",
        WBE::MeshPartVK::OPAQUE,
        WBE::REND_RES_SWAP_CHAIN,
        REND_RES_TEST_DRAW_SPONZA_DEPTH_IMAGE_VIEW,
        WBE::RendExtent2DParamsVK {}, false);
    WBE::RenderTestMeshDraw main_masked_task = create_task(
        RT_RUN_MAIN_MASKED,
        "forward_sponza_main_masked",
        WBE::MeshPartVK::MASKED,
        WBE::REND_RES_SWAP_CHAIN,
        REND_RES_TEST_DRAW_SPONZA_DEPTH_IMAGE_VIEW,
        WBE::RendExtent2DParamsVK {}, true);
    write_shadow_dynamic_descriptors();
    write_main_dynamic_descriptors();

    setup_light_billboard_overlay_resources();

    register_draw_sponza_render_graph(
        std::move(shadow_opaque_task), std::move(shadow_masked_task), std::move(main_opaque_task), std::move(main_masked_task));
}

void ApplicationVulkanDrawSponzaTest::setup_render_context() {
    renderer_vk = WBE::RendererVK::get_singleton();
    render_context = renderer_vk->get_render_context();
    render_allocator = get_renderer_allocator().get();
    camera_component = make_default_test_camera_component(render_context->swap_chain_aspect_ratio, 100.0F);
    camera_component.view = test_camera_view(camera_position, camera_yaw, camera_pitch);
}

void ApplicationVulkanDrawSponzaTest::load_resources() {
    auto* resource_manager = WBE::ResourceManager::get_singleton();
    auto resource_chunk = make_ref<WBE::ResourceChunkLoadFile>(
        WBE::global_allocator(), "res_chunks/rendering_test_resources.json", resource_manager->get_allocator());
    resource_manager->register_chunk(resource_chunk);
    sponza_mesh = resource_manager->get_resource<WBE::RUMesh3D>("sponza.mesh");
    if (sponza_mesh == nullptr) {
        throw std::runtime_error("Failed to setup draw sponza test: static mesh resource was not found.");
    }
    shadow_opaque_pipeline = resource_manager->get_resource<WBE::RUGraphicsPipeline>("shadowmap_spot");
    if (shadow_opaque_pipeline == nullptr) {
        throw std::runtime_error("Failed to setup draw sponza test: shadow opaque graphics pipeline resource was not found.");
    }
    shadow_masked_pipeline = resource_manager->get_resource<WBE::RUGraphicsPipeline>("shadowmap_spot_masked");
    if (shadow_masked_pipeline == nullptr) {
        throw std::runtime_error("Failed to setup draw sponza test: shadow masked graphics pipeline resource was not found.");
    }
    main_opaque_pipeline = resource_manager->get_resource<WBE::RUGraphicsPipeline>("forward_cook_torrance_pbr");
    if (main_opaque_pipeline == nullptr) {
        throw std::runtime_error("Failed to setup draw sponza test: main opaque graphics pipeline resource was not found.");
    }
    main_masked_pipeline = resource_manager->get_resource<WBE::RUGraphicsPipeline>("forward_cook_torrance_pbr_masked");
    if (main_masked_pipeline == nullptr) {
        throw std::runtime_error("Failed to setup draw sponza test: main masked graphics pipeline resource was not found.");
    }
}

void ApplicationVulkanDrawSponzaTest::setup_static_scene_resources() {
    static_mesh_resources = WBE::create_mesh_resources(renderer_vk, WBE::STATIC_MESH_KEY, sponza_mesh, main_opaque_pipeline);
    const std::array shadow_formats{SHADOWMAP_COLOR_FORMAT};
    shadow_opaque_graphics_pipeline = WBE::compile_render_test_graphics_pipeline(
        renderer_vk, "forward_sponza_shadow_opaque", shadow_opaque_pipeline, shadow_formats, SHADOWMAP_DEPTH_FORMAT);
    shadow_masked_graphics_pipeline = WBE::compile_render_test_graphics_pipeline(
        renderer_vk, "forward_sponza_shadow_masked", shadow_masked_pipeline, shadow_formats, SHADOWMAP_DEPTH_FORMAT);
    const std::array main_formats{render_context->swap_chain_format};
    main_opaque_graphics_pipeline = WBE::compile_render_test_graphics_pipeline(
        renderer_vk, "forward_sponza_main_opaque", main_opaque_pipeline, main_formats, MAIN_DEPTH_FORMAT);
    main_masked_graphics_pipeline = WBE::compile_render_test_graphics_pipeline(
        renderer_vk, "forward_sponza_main_masked", main_masked_pipeline, main_formats, MAIN_DEPTH_FORMAT);
}

void ApplicationVulkanDrawSponzaTest::setup_spot_light() {
    spot_light_component = make_default_test_spot_light_component(128.0F);
    spot_light_yaw = std::atan2(spot_light_component.direction.z, spot_light_component.direction.x);
    spot_light_pitch = std::asin(std::clamp(spot_light_component.direction.y, -1.0F, 1.0F));
    shadow_extent = VkExtent2D{
        .width = spot_light_component.shadow_map_resolution,
        .height = spot_light_component.shadow_map_resolution,
    };
}

void ApplicationVulkanDrawSponzaTest::setup_light_billboard_overlay_resources() {
    auto* resource_manager = WBE::ResourceManager::get_singleton();
    auto pipeline_resource = resource_manager->get_resource<WBE::RUGraphicsPipeline>("billboard_face_camera_pipeline");
    if (pipeline_resource == nullptr) {
        throw std::runtime_error("Failed to setup light billboard overlay: graphics pipeline resource was not found.");
    }
    auto texture_resource = resource_manager->get_resource<WBE::RUTexture>("spot_light_billboard");
    if (texture_resource == nullptr) {
        throw std::runtime_error("Failed to setup light billboard overlay: texture resource was not found.");
    }

    if (camera_info_ubos == WBE::MEM_NULL) {
        camera_info_ubos = create_per_frame_uniform_buffers(render_allocator, render_context, "camera_info", sizeof(CameraInfoGPU));
    }

    WBE::GraphicsPipelineCompilerVKContext compiler_context{
        .allocator = render_allocator,
        .render_data = render_context,
        .render_database = renderer_vk->get_render_database(),
        .renderer = renderer_vk,
        .color_attachment_formats = {render_context->swap_chain_format},
    };
    WBE::GraphicsPipelineCompilerVK compiler(compiler_context);
    light_billboard_texture_view = compiler.compile_texture_view("spot_light_billboard", texture_resource);
    auto compiled = compiler.compile("light_billboard_pipeline", pipeline_resource);
    light_billboard_pipeline = compiled.graphics_pipeline;
}

void ApplicationVulkanDrawSponzaTest::write_shadow_dynamic_descriptors() {
    if (spot_light_info_ubos == WBE::MEM_NULL) {
        spot_light_info_ubos =
            create_per_frame_uniform_buffers(render_allocator, render_context, "spot_light_info", sizeof(SpotLightInfoGPU));
    }
    SpotLightInfoGPU spot_light_info = make_spot_light_info_gpu(spot_light_component);
    spot_light_info_ubos->publish_buffer_update(&spot_light_info, 0, sizeof(spot_light_info));
    log_uniform_buffer_descriptors(spot_light_info_ubos, "forward sponza shadow light buffer");
}

void ApplicationVulkanDrawSponzaTest::write_main_dynamic_descriptors() {
    if (camera_info_ubos == WBE::MEM_NULL) {
        camera_info_ubos = create_per_frame_uniform_buffers(render_allocator, render_context, "camera_info", sizeof(CameraInfoGPU));
    }
    if (spot_light_count_ubos == WBE::MEM_NULL) {
        spot_light_count_ubos =
            create_per_frame_uniform_buffers(render_allocator, render_context, "spot_light_count", sizeof(uint32_t));
    }
    if (spot_light_info_ubos == WBE::MEM_NULL) {
        spot_light_info_ubos =
            create_per_frame_uniform_buffers(render_allocator, render_context, "spot_light_info", sizeof(SpotLightInfoGPU));
    }
    CameraInfoGPU camera_info = make_camera_info_gpu(camera_component);
    SpotLightInfoGPU spot_light_info = make_spot_light_info_gpu(spot_light_component);
    uint32_t spot_light_count = 1;
    camera_info_ubos->publish_buffer_update(&camera_info, 0, sizeof(camera_info));
    spot_light_info_ubos->publish_buffer_update(&spot_light_info, 0, sizeof(spot_light_info));
    spot_light_count_ubos->publish_buffer_update(&spot_light_count, 0, sizeof(spot_light_count));

    log_uniform_buffer_descriptors(camera_info_ubos, "forward sponza camera buffer");
    log_uniform_buffer_descriptors(spot_light_info_ubos, "forward sponza main light buffer");
    log_uniform_buffer_descriptors(spot_light_count_ubos, "forward sponza light count buffer");
}

void ApplicationVulkanDrawSponzaTest::register_draw_sponza_render_graph(WBE::RenderTestMeshDraw p_shadow_opaque_task,
    WBE::RenderTestMeshDraw p_shadow_masked_task,
    WBE::RenderTestMeshDraw p_main_opaque_task,
    WBE::RenderTestMeshDraw p_main_masked_task) {
    static constexpr std::string_view SHADOWMAP_DEPTH_IMAGE = "shadowmap_depth_image";
    static constexpr std::string_view SHADOWMAP_COLOR_IMAGE = "shadowmap_color_image";
    static constexpr std::string_view MAIN_DEPTH_IMAGE = "draw_sponza_depth_image";
    const VkExtent3D shadow_image_extent{.width = shadow_extent.width, .height = shadow_extent.height, .depth = 1};
    const WBE::RendSamplerDescVK clamped_linear_sampler{
        .address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };

    // The billboard buffer starts with the state at registration; an equal description keeps it, and its runtime updates,
    // across rebuilds.
    const std::vector<uint8_t> light_billboard_info_data = WBE::to_initial_data(make_light_billboard_info(spot_light_component));
    // The graph calls this for its first build and again whenever it rebuilds, such as after a swap chain resize.
    WBE::Ref<WBE::RenderGraphVK> graph = renderer_vk->register_render_graph("draw_sponza_pipeline",
        [this,
            shadow_opaque_task = std::move(p_shadow_opaque_task),
            shadow_masked_task = std::move(p_shadow_masked_task),
            main_opaque_task = std::move(p_main_opaque_task),
            main_masked_task = std::move(p_main_masked_task),
            shadow_image_extent,
            clamped_linear_sampler,
            light_billboard_info_data](const WBE::RendGraphContextVK& /*p_graph_context*/) {
            WBE::RendGraphDescVK pipeline_desc(render_allocator);
            // The draw tasks below import the mesh themselves.
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
                .add_image(MAIN_DEPTH_IMAGE,
                    [](const WBE::RendGraphContextVK& p_context) {
                        return WBE::RendImageDescVK{
                            .usage = WBE::RendResPrimaryUsageVK::IMAGE_DEPTH_TARGET,
                            .format = MAIN_DEPTH_FORMAT,
                            .extent = {.width = p_context.swap_chain_extent.width, .height = p_context.swap_chain_extent.height, .depth = 1},
                        };
                    })
                .add_image_view(REND_RES_TEST_DRAW_SPONZA_DEPTH_IMAGE_VIEW, MAIN_DEPTH_IMAGE)
                .add_image_view(REND_RES_TEST_SHADOWMAP_COLOR_IMAGE_VIEW, SHADOWMAP_COLOR_IMAGE)
                .add_image_view(REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW, SHADOWMAP_DEPTH_IMAGE)
                .add_resource(SHADOW_LIGHT_INFO_BUFFER, *spot_light_info_ubos)
                .add_resource(CAMERA_INFO_BUFFER, *camera_info_ubos)
                .add_resource(SPOT_LIGHT_INFO_BUFFER, *spot_light_info_ubos)
                .add_resource(SPOT_LIGHT_COUNT_BUFFER, *spot_light_count_ubos)
                .add_sampler(SHADOW_SAMPLER,
                    [=](const WBE::RendGraphContextVK& /*p_context*/) {
                        return clamped_linear_sampler;
                    })
                .add_resource(GBUFFER_CAMERA_INFO_BUFFER, *camera_info_ubos)
                .add_buffer(LIGHT_BILLBOARD_INFO_BUFFER,
                    [initial_data = light_billboard_info_data](const WBE::RendGraphContextVK& /*p_context*/) {
                        return WBE::RendBufferDescVK{
                            .usage = WBE::RendResPrimaryUsageVK::BUFFER_UNIFORM,
                            .domain = WBE::DeviceDomain::HOST_DEVICE,
                            .initial_data = initial_data,
                        };
                    })
                .add_resource(LIGHT_BILLBOARD_TEXTURE_IMAGE_VIEW, *light_billboard_texture_view)
                .add_sampler(LIGHT_BILLBOARD_SAMPLER, [=](const WBE::RendGraphContextVK& /*p_context*/) {
                    return clamped_linear_sampler;
                });

            shadow_opaque_task.add_to(pipeline_desc, render_context)
                .connect(RT_RUN_SHADOWMAP_MASKED, REND_RES_TEST_SHADOWMAP_COLOR_IMAGE_VIEW)
                .connect(RT_RUN_SHADOWMAP_MASKED, REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW);
            shadow_masked_task.add_to(pipeline_desc, render_context)
                .connect(RT_RUN_MAIN_OPAQUE, REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW)
                .connect(RT_RUN_MAIN_MASKED, REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW);
            // The main passes sample the shadow depth through their reflected "shadowmap" resource.
            main_opaque_task.add_to(pipeline_desc, render_context)
                .alias(SHADOWMAP_SAMPLED_IMAGE_VIEW, REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW)
                .connect(RT_RUN_MAIN_MASKED, WBE::REND_RES_SWAP_CHAIN)
                .connect(RT_RUN_MAIN_MASKED, REND_RES_TEST_DRAW_SPONZA_DEPTH_IMAGE_VIEW);
            main_masked_task.add_to(pipeline_desc, render_context)
                .alias(SHADOWMAP_SAMPLED_IMAGE_VIEW, REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW)
                .connect(RT_RUN_LIGHT_BILLBOARD, WBE::REND_RES_SWAP_CHAIN);

            WBE::RendTaskDescVK light_billboard_task_desc(render_allocator);
            light_billboard_task_desc.task_id = RT_RUN_LIGHT_BILLBOARD;
            light_billboard_task_desc.alias(WBE::RenderTargetsVK::color_id(0), WBE::REND_RES_SWAP_CHAIN);
            light_billboard_task_desc.factory = [this](const WBE::RendGraphContextVK& p_context) {
                return WBE::RTRunGraphicsPipelineVK::Builder(render_allocator)
                    .set_pipeline(light_billboard_pipeline)
                    .set_render_extent(p_context.swap_chain_extent)
                    .set_color_load_op(VK_ATTACHMENT_LOAD_OP_LOAD)
                    .set_draw_vertex_count(6)
                    .create_unique(render_context);
            };
            pipeline_desc.tasks.emplace_back(std::move(light_billboard_task_desc));
            return pipeline_desc;
        });
    light_billboard_ubo = graph->acquire_resource(LIGHT_BILLBOARD_INFO_BUFFER)->get().dynamic_cast_ref<WBE::ROBufferVK>();
    WBE::write_descriptor_debug_output("draw_sponza_pipeline_graph_descriptors", graph->list_descriptors());
    renderer_vk->activate_render_graph("draw_sponza_pipeline");
}
