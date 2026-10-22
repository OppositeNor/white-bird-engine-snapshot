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
#include "core/memory/unique.hh"
#include "core/profiling/profiler.hh"
#include "function/application/application_game.hh"
#include "function/function_utils.hh"
#include "function/input_manager/i_input_manager.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render/shader_metadata_loader.hh"
#include "function/render_vk/graphics_pipeline_compiler_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_targets_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/rt_run_graphics_pipeline_vk.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include "function/render_vk/vertex_input_description_vk.hh"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/trigonometric.hpp"
#include "global/global.hh"
#include "imgui.h"
#include "imgui_overlay_vk.hh"
#include "input_manager_mocked.hh"
#include "platform/file_system/path.hh"
#include "render_test.hh"
#include "resource/resource_chunk/resource_chunk_load_file.hh"
#include "resource/resource_manager/resource_manager.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "rt_imgui_vk.hh"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <format>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WBE = WhiteBirdEngine;

// Prefix of the imported skinned mesh buffers.
inline constexpr std::string_view REND_RES_SKINNED_MESH = "skinned_mesh";
inline constexpr std::string_view REND_RES_SKINNED_MESH_CAMERA_INFO = "skinned_mesh_camera_info";
inline constexpr std::string_view REND_RES_SKINNED_MESH_SKINNING_TRANSFORMS = "skinned_mesh_skinning_transforms";
inline constexpr std::string_view RT_DRAW_SKINNED_MESH = "draw_skinned_mesh";
inline constexpr std::string_view RT_DRAW_SKINNED_MESH_IMGUI = "draw_skinned_mesh_imgui";

// The number of joint influences stored per vertex in a joint binding.
inline constexpr size_t JOINT_BINDING_INFLUENCE_COUNT = 4;
// The rate, in blend units per second, at which the left and right arrow keys move the blend slider.
inline constexpr float BLEND_KEY_SPEED = 0.5F;

/**
 * @struct SkinningTransformGPU
 * @brief CPU-side mirror of the shader `SkinningTransform` struct in skeleton/skeleton_utils.slang: the first three rows
 * of an affine skinning matrix.
 */
struct SkinningTransformGPU {
    /**
     * @brief The first row of the transform.
     */
    glm::vec4 row_0{1.0F, 0.0F, 0.0F, 0.0F};
    /**
     * @brief The second row of the transform.
     */
    glm::vec4 row_1{0.0F, 1.0F, 0.0F, 0.0F};
    /**
     * @brief The third row of the transform.
     */
    glm::vec4 row_2{0.0F, 0.0F, 1.0F, 0.0F};
};

static_assert(sizeof(SkinningTransformGPU) == 48, "SkinningTransformGPU must match the std430 layout of the shader SkinningTransform.");

/**
 * @brief Store the first three rows of an affine column-major matrix.
 *
 * @param p_matrix The affine matrix.
 * @return The rows of the matrix.
 */
SkinningTransformGPU to_skinning_transform(const glm::mat4& p_matrix) {
    return SkinningTransformGPU{
        .row_0 = glm::vec4(p_matrix[0][0], p_matrix[1][0], p_matrix[2][0], p_matrix[3][0]),
        .row_1 = glm::vec4(p_matrix[0][1], p_matrix[1][1], p_matrix[2][1], p_matrix[3][1]),
        .row_2 = glm::vec4(p_matrix[0][2], p_matrix[1][2], p_matrix[2][2], p_matrix[3][2]),
    };
}

/**
 * @struct JointPose
 * @brief The local transform of one joint, split into components that blend independently.
 */
struct JointPose {
    glm::vec3 translation{0.0F};
    glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    glm::vec3 scale{1.0F};
};

/**
 * @brief Split a local transform without shear into translation, rotation, and scale.
 *
 * @param p_transform The column-major local transform.
 * @return The pose components of the transform.
 */
JointPose decompose_joint_pose(const glm::mat4& p_transform) {
    JointPose result;
    result.translation = glm::vec3(p_transform[3]);
    glm::mat3 linear(p_transform);
    for (glm::length_t i = 0; i < 3; ++i) {
        result.scale[i] = glm::length(linear[i]);
        if (result.scale[i] == 0.0F) {
            throw std::runtime_error("Failed to setup skinned mesh test: a bind-pose joint transform has a zero scale.");
        }
        linear[i] /= result.scale[i];
    }
    result.rotation = glm::normalize(glm::quat_cast(linear));
    return result;
}

/**
 * @brief Compose pose components into a column-major local transform.
 *
 * @param p_pose The pose components.
 * @return The local transform translation * rotation * scale.
 */
glm::mat4 compose_joint_pose(const JointPose& p_pose) {
    return glm::translate(glm::mat4(1.0F), p_pose.translation) * glm::mat4_cast(p_pose.rotation) *
           glm::scale(glm::mat4(1.0F), p_pose.scale);
}

/**
 * @brief Blend two poses of the same joint; rotations are interpolated spherically.
 *
 * @param p_first The pose at a blend factor of 0.
 * @param p_second The pose at a blend factor of 1.
 * @param p_blend The blend factor in [0, 1].
 * @return The blended pose.
 */
JointPose blend_joint_pose(const JointPose& p_first, const JointPose& p_second, float p_blend) {
    return JointPose{
        .translation = glm::mix(p_first.translation, p_second.translation, p_blend),
        .rotation = glm::normalize(glm::slerp(p_first.rotation, p_second.rotation, p_blend)),
        .scale = glm::mix(p_first.scale, p_second.scale, p_blend),
    };
}

/**
 * @class ApplicationVulkanDrawSkinnedMeshTest
 * @brief Draws the Khronos SimpleSkin sample blended between two frames of its animation, with the blend factor set by a slider.
 *
 * The compiler does not export animations yet, so the two frames are the glTF keyframes at 1.0s and 4.0s, which rotate
 * the upper joint by +90 and -90 degrees around z. The poses are blended on the CPU, which uploads the skinning
 * transform of every joint to a host-visible storage buffer whenever the blend changes. The vertex shader skins the
 * bind-pose vertices with these transforms and the joint bindings of the mesh, using skeleton/skeleton_utils.slang.
 * The left and right arrow keys also move the slider, so scripted input can drive the blend.
 */
class ApplicationVulkanDrawSkinnedMeshTest : public WBE::ApplicationGame {
public:
    ApplicationVulkanDrawSkinnedMeshTest()
        : WBE::ApplicationGame(WBE::RendererCapabilities{.descriptor_set = true},
              WBE::make_ref<WBE::InputManagerMocked>(WBE::global_allocator(), WBE::global_allocator())) {
    }

    virtual void logic_update(double p_delta) override {
        WBE::IInputManager* input_manager = WBE::IInputManager::get_singleton();
        float blend_delta = 0.0F;
        if (input_manager->get_input_status(WBE::KEYCODE_LEFT).is_down()) {
            blend_delta -= BLEND_KEY_SPEED * static_cast<float>(p_delta);
        }
        if (input_manager->get_input_status(WBE::KEYCODE_RIGHT).is_down()) {
            blend_delta += BLEND_KEY_SPEED * static_cast<float>(p_delta);
        }
        if (blend_delta != 0.0F) {
            add_render_job([this, blend_delta] {
                blend = std::clamp(blend + blend_delta, 0.0F, 1.0F);
            });
        }
    }

    virtual void render_update(double p_delta) override {
        auto* renderer = WBE::RendererVK::get_singleton();
        WBE::on_exec_test_render(*renderer, renderer->get_render_context()->current_frame);
        imgui_overlay->begin_frame(p_delta);
        draw_blend_window();
        imgui_overlay->end_frame();
        if (blend != skinned_blend) {
            evaluate_skinning_transforms();
            skinning_transforms_buffer->update_buffer(
                skinning_transforms.data(), skinning_transforms.size() * sizeof(SkinningTransformGPU));
            skinned_blend = blend;
        }
    }

    /**
     * @brief Load the skinned mesh and its skeleton, prepare the two blended poses, and build the render graph.
     *
     * Must be called once, before run().
     *
     * @throws std::runtime_error If a resource is missing or the mesh is not the expected skinned SimpleSkin mesh.
     */
    void setup_render_graph() {
        auto* render_allocator = get_renderer_allocator().get();
        auto* renderer = WBE::RendererVK::get_singleton();
        const auto* render_context = renderer->get_render_context();
        auto* resource_manager = WBE::ResourceManager::get_singleton();
        imgui_overlay = WBE::make_unique<WBE::ImGuiOverlayVK>(WBE::global_allocator(), renderer);

        auto resource_chunk = WBE::make_ref<WBE::ResourceChunkLoadFile>(
            WBE::global_allocator(), "assets/models/models.json", resource_manager->get_allocator());
        resource_manager->register_chunk(resource_chunk);
        WBE::RefRes<WBE::RUMesh3D> mesh = resource_manager->get_resource<WBE::RUMesh3D>("model_simple_skin.mesh");
        if (mesh == nullptr) {
            throw std::runtime_error("Failed to setup skinned mesh test: mesh resource was not found.");
        }
        if (mesh->opaque_instances.submesh_count != 1) {
            throw std::runtime_error("Failed to setup skinned mesh test: expected exactly one opaque submesh.");
        }
        if (mesh->opaque_instances.instance_count != 1) {
            throw std::runtime_error("Failed to setup skinned mesh test: expected exactly one opaque instance.");
        }
        if (mesh->skeleton == nullptr) {
            throw std::runtime_error("Failed to setup skinned mesh test: the mesh has no skeleton.");
        }
        const WBE::RUSubmesh3D& submesh = mesh->opaque_instances.submeshes.get()[0];
        if (submesh.vertex_binding_count < 2) {
            throw std::runtime_error("Failed to setup skinned mesh test: the submesh has no joint bindings.");
        }
        load_skeleton(*mesh->skeleton);
        validate_joint_bindings(submesh);
        mesh_transform = mesh->opaque_instances.instances.get()[0].global_transform;
        evaluate_skinning_transforms();
        skinned_blend = blend;

        auto pipeline_resource = resource_manager->get_resource<WBE::RUGraphicsPipeline>("draw_skinned_mesh_pipeline");
        if (pipeline_resource == nullptr) {
            throw std::runtime_error("Failed to setup skinned mesh test: graphics pipeline resource was not found.");
        }
        auto shader_resource = resource_manager->get_resource<WBE::RUShader>("draw_skinned_mesh_shader");
        if (shader_resource == nullptr) {
            throw std::runtime_error("Failed to setup skinned mesh test: shader resource was not found.");
        }
        WBE::GraphicsPipelineCompilerVKContext compiler_context{
            .allocator = render_allocator,
            .render_data = render_context,
            .render_database = renderer->get_render_database(),
            .renderer = renderer,
            .color_attachment_formats = {render_context->swap_chain_format},
        };
        auto graphics_pipeline =
            WBE::GraphicsPipelineCompilerVK(compiler_context).compile("draw_skinned_mesh_pipeline", pipeline_resource).graphics_pipeline;
        CameraInfoGPU camera_info{
            .view = glm::lookAt(glm::vec3(0.0F, 1.0F, 4.5F), glm::vec3(0.0F, 1.0F, 0.0F), glm::vec3(0.0F, 1.0F, 0.0F)),
            .proj = glm::perspectiveRH_ZO(glm::radians(45.0F), render_context->swap_chain_aspect_ratio, 10.0F, 0.1F),
            .world_position = glm::vec4(0.0F, 1.0F, 4.5F, 1.0F),
        };
        // Inverse the y value since Vulkan makes y axis points downwards.
        camera_info.proj[1][1] *= -1;

        // The buffers start with the state at registration; equal descriptions keep them, and their runtime updates, across
        // rebuilds.
        const std::vector<uint8_t> camera_info_data = WBE::to_initial_data(camera_info);
        const std::vector<uint8_t> skinning_transforms_data = to_byte_vector(skinning_transforms);
        // The graph calls this for its first build and again whenever it rebuilds, such as after a swap chain resize.
        WBE::Ref<WBE::RenderGraphVK> graph = renderer->register_render_graph("test_render_graph",
            [this,
                render_allocator,
                renderer,
                render_context,
                mesh,
                submesh = &submesh,
                shader_resource,
                graphics_pipeline,
                camera_info_data,
                skinning_transforms_data](const WBE::RendGraphContextVK& /*p_graph_context*/) {
            WBE::RendGraphDescVK graph_desc(render_allocator);
            // The shader reads position from the geometry binding and the joint ids and weights from the joint-binding
            // binding, so the draw binds the buffers compiled from both binaries.
            const WBE::SubmeshDrawBuffersVK draw_buffers = WBE::import_submesh_draw_buffers(renderer,
                graph_desc,
                REND_RES_SKINNED_MESH,
                mesh,
                *submesh,
                WBE::ShaderMetadataLoader().load(shader_resource),
                "vertex_main");
            graph_desc.add_resource(WBE::REND_RES_SWAP_CHAIN, *renderer->acquire_swap_chain_image_view_container())
                .add_buffer(REND_RES_SKINNED_MESH_CAMERA_INFO,
                    [initial_data = camera_info_data](const WBE::RendGraphContextVK& /*p_context*/) {
                        return WBE::RendBufferDescVK{.usage = WBE::RendResPrimaryUsageVK::BUFFER_UNIFORM,
                            .domain = WBE::DeviceDomain::HOST_DEVICE,
                            .initial_data = initial_data,
                            };
                    })
                .add_buffer(REND_RES_SKINNED_MESH_SKINNING_TRANSFORMS,
                    [initial_data = skinning_transforms_data](const WBE::RendGraphContextVK& /*p_context*/) {
                        return WBE::RendBufferDescVK{.usage = WBE::RendResPrimaryUsageVK::BUFFER_STORAGE,
                            .domain = WBE::DeviceDomain::HOST_DEVICE,
                            .initial_data = initial_data,
                            };
                    });

            WBE::RendTaskDescVK draw_task_desc(render_allocator);
            draw_task_desc.task_id = RT_DRAW_SKINNED_MESH;
            draw_task_desc.alias(WBE::RenderTargetsVK::color_id(0), WBE::REND_RES_SWAP_CHAIN);
            const std::array<VkClearValue, 1> clear_values{VkClearValue{.color = {.float32 = {0.05F, 0.05F, 0.05F, 1.0F}}}};
            draw_task_desc.factory = [=](const WBE::RendGraphContextVK& p_context) {
                return WBE::RTRunGraphicsPipelineVK::Builder(render_allocator)
                    .set_pipeline(graphics_pipeline)
                    .set_render_extent(p_context.swap_chain_extent)
                    .set_clear_values(clear_values)
                    .set_indexed_draw(draw_buffers.get_vertex_buffer_sources(),
                        draw_buffers.index_buffer_id,
                        draw_buffers.vertex_input,
                        draw_buffers.indexed_draw_region)
                    .create_unique(render_context);
            };
            graph_desc.add_task(std::move(draw_task_desc));

            WBE::RendTaskDescVK imgui_task_desc(render_allocator);
            imgui_task_desc.task_id = RT_DRAW_SKINNED_MESH_IMGUI;
            imgui_task_desc.alias(WBE::RenderTargetsVK::color_id(0), WBE::REND_RES_SWAP_CHAIN);
            imgui_task_desc.factory = [this, render_allocator, render_context](const WBE::RendGraphContextVK& /*p_context*/) {
                return WBE::make_unique<WBE::RTImGuiVK>(render_allocator, render_context, imgui_overlay.get());
            };
            graph_desc.add_task(std::move(imgui_task_desc));
            WBE::connect_resource_chain(graph_desc, WBE::REND_RES_SWAP_CHAIN, {RT_DRAW_SKINNED_MESH, RT_DRAW_SKINNED_MESH_IMGUI});
            return graph_desc;
        });
        skinning_transforms_buffer =
            graph->acquire_resource(REND_RES_SKINNED_MESH_SKINNING_TRANSFORMS)->get().dynamic_cast_ref<WBE::ROBufferVK>();
        WBE::write_descriptor_debug_output("test_render_graph_graph_descriptors", graph->list_descriptors());
        renderer->activate_render_graph("test_render_graph");
    }

private:
    // Joint parents, in parent-before-child order, and the inverse bind-pose global transform of every joint.
    std::vector<int32_t> joint_parents;
    std::vector<glm::mat4> inverse_bind_globals;
    // The local joint poses of the two blended animation frames.
    std::vector<JointPose> first_frame_pose;
    std::vector<JointPose> second_frame_pose;
    // The joint binding of every vertex, the instance transform of the mesh, and the skinning transform of every joint.
    glm::mat4 mesh_transform{1.0F};
    std::vector<SkinningTransformGPU> skinning_transforms;
    // The blend factor set by the slider, and the one the skinning transform buffer holds; owned by the rendering thread.
    float blend = 0.5F;
    float skinned_blend = -1.0F;
    WBE::Ref<WBE::ROBufferVK> skinning_transforms_buffer;
    // Destroyed before the renderer, after the rendering thread has stopped.
    WBE::Unique<WBE::ImGuiOverlayVK> imgui_overlay;

    void load_skeleton(const WBE::RUSkeleton& p_skeleton) {
        // The glTF animation rotates its second skin joint, which the compiler places at index 2, after the scene root
        // and the first skin joint.
        constexpr size_t ANIMATED_JOINT_INDEX = 2;
        if (p_skeleton.joint_count <= ANIMATED_JOINT_INDEX) {
            throw std::runtime_error("Failed to setup skinned mesh test: the skeleton has too few joints.");
        }
        std::vector<glm::mat4> bind_globals;
        for (size_t i = 0; i < p_skeleton.joint_count; ++i) {
            const WBE::RUSkeleton::Joint joint = p_skeleton.get_joint(i);
            joint_parents.push_back(joint.parent_index);
            if (joint.parent_index < 0) {
                bind_globals.push_back(joint.bind_local_transform);
            } else {
                bind_globals.push_back(bind_globals[static_cast<size_t>(joint.parent_index)] * joint.bind_local_transform);
            }
            inverse_bind_globals.push_back(glm::inverse(bind_globals.back()));
            JointPose bind_pose = decompose_joint_pose(joint.bind_local_transform);
            first_frame_pose.push_back(bind_pose);
            second_frame_pose.push_back(bind_pose);
        }
        // The rotation keyframes of SimpleSkin.gltf at 1.0s and 4.0s, converted from glTF (x, y, z, w) order.
        first_frame_pose[ANIMATED_JOINT_INDEX].rotation = glm::normalize(glm::quat(0.707F, 0.0F, 0.0F, 0.707F));
        second_frame_pose[ANIMATED_JOINT_INDEX].rotation = glm::normalize(glm::quat(0.707F, 0.0F, 0.0F, -0.707F));
    }

    template <typename TElement>
    static std::vector<uint8_t> to_byte_vector(const std::vector<TElement>& p_elements) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(p_elements.data());
        return std::vector<uint8_t>(bytes, bytes + p_elements.size() * sizeof(TElement));
    }

    // Check that every joint binding references a skeleton joint, since the shader indexes the skinning transforms with
    // them.
    void validate_joint_bindings(const WBE::RUSubmesh3D& p_submesh) const {
        const WBE::RUVertexView& view = p_submesh.get_vertex_bindings()[p_submesh.get_vertex_binding_index("joint_binding_ids")];
        const size_t joint_ids_offset = view.get_vertex_attribute_offset("joint_binding_ids");
        const char* binding_data = view.binary.binary->data.get() + view.binary.start;
        for (size_t i = 0; i < view.vertex_count; ++i) {
            std::array<int32_t, JOINT_BINDING_INFLUENCE_COUNT> ids{};
            std::memcpy(ids.data(), binding_data + i * view.vertex_stride + joint_ids_offset, sizeof(ids));
            for (int32_t id : ids) {
                if (id < 0 || static_cast<size_t>(id) >= joint_parents.size()) {
                    throw std::runtime_error("Failed to setup skinned mesh test: a vertex references a joint outside the skeleton.");
                }
            }
        }
    }

    void evaluate_skinning_transforms() {
        std::vector<glm::mat4> pose_globals;
        skinning_transforms.clear();
        for (size_t i = 0; i < joint_parents.size(); ++i) {
            glm::mat4 local = compose_joint_pose(blend_joint_pose(first_frame_pose[i], second_frame_pose[i], blend));
            if (joint_parents[i] < 0) {
                pose_globals.push_back(local);
            } else {
                pose_globals.push_back(pose_globals[static_cast<size_t>(joint_parents[i])] * local);
            }
            // The instance transform places the posed mesh in model space.
            skinning_transforms.push_back(to_skinning_transform(mesh_transform * pose_globals.back() * inverse_bind_globals[i]));
        }
    }

    void draw_blend_window() {
        ImGui::SetNextWindowPos(ImVec2(16.0F, 16.0F), ImGuiCond_FirstUseEver);
        ImGui::Begin("Skinned Mesh", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::SliderFloat("Frame blend", &blend, 0.0F, 1.0F);
        ImGui::TextUnformatted("0: keyframe at 1.0s, 1: keyframe at 4.0s");
        ImGui::End();
    }
};

int main(int p_argc, char* p_argv[]) {
    WBE::on_exec_test_start();
    std::unique_ptr<WBE::Global> global = std::make_unique<WBE::Global>(p_argc, p_argv, WBE::Path("test_env"));
    {
        ApplicationVulkanDrawSkinnedMeshTest game_application;
        {
            WBE_START_PROFILE(WBE::WBE_CHANNEL_GLOBAL, "Main profiler");
            game_application.setup_render_graph();
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
