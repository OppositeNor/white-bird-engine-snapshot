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
#ifndef WBE_FILE_RENDER_TEST_HH
#define WBE_FILE_RENDER_TEST_HH

#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/parser/parser_json.hh"
#include "function/ecs/components.hh"
#include "function/render/i_renderer.hh"
#include "function/render/material_texture_contract.hh"
#include "function/render/material_texture_contract_resolver.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render/shader_metadata.hh"
#include "function/render/shader_metadata_loader.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/graphics_pipeline_compiler_vk.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/mesh_buffer_compiler_vk.hh"
#include "function/render_vk/mesh_compiler_vk.hh"
#include "function/render_vk/mesh_draw_batch_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/rend_extent_2d_params_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_targets_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/ro_pipeline_vk.hh"
#include "function/render_vk/roc_multislots_vk.hh"
#include "function/render_vk/rt_graphics_draw_mesh_vk.hh"
#include "function/render_vk/rt_params_clear_value_vk.hh"
#include "function/render_vk/rt_params_graphics_draw_mesh_vk.hh"
#include "function/render_vk/rt_run_graphics_pipeline_indirect_vk.hh"
#include "function/render_vk/rt_run_graphics_pipeline_vk.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include "function/render_vk/vertex_input_description_vk.hh"
#include "function/window/i_window.hh"
#include "platform/file_system/path.hh"
#include "platform/os/os.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "utils/utils.hh"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WBE = WhiteBirdEngine;

inline constexpr std::string_view RT_RUN_PIPELINE = "run_pipeline";
inline constexpr std::string_view RT_RUN_PIPELINE_SHADOWMAP = "run_shadowmap";
inline constexpr std::string_view REND_RES_TEST_SHADOWMAP_COLOR_IMAGE_VIEW = "shadowmap_color";
inline constexpr std::string_view REND_RES_TEST_SHADOWMAP_DEPTH_IMAGE_VIEW = "shadowmap_depth";
inline constexpr std::string_view REND_RES_TEST_RSM_NORMAL_IMAGE_VIEW = "rsm_normal";
inline constexpr glm::vec3 TEST_DEFAULT_CAMERA_POSITION = glm::vec3(2.0F, 2.0F, 2.0F);
inline constexpr glm::vec3 TEST_DEFAULT_CAMERA_TARGET = glm::vec3(0.0F, 0.0F, 0.0F);
inline constexpr float TEST_CAMERA_MOVE_SPEED = 3.0F;
inline constexpr float TEST_CAMERA_TURN_SPEED = glm::radians(90.0F);
inline constexpr float TEST_CAMERA_PITCH_LIMIT = glm::radians(89.0F);

namespace WhiteBirdEngine {

inline std::string make_descriptor_debug_file_stem(std::string_view p_name) {
    std::string result;
    result.reserve(p_name.size());
    for (char character : p_name) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9')) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        } else {
            result.push_back('_');
        }
    }
    if (result.empty()) {
        throw std::runtime_error("Descriptor debug output name is empty.");
    }
    return result;
}

inline void write_descriptor_debug_output(std::string_view p_name, std::string_view p_json) {
    std::filesystem::path output_dir = std::filesystem::path("test_env") / "descriptor_debug";
    std::filesystem::create_directories(output_dir);
    std::filesystem::path output_path = output_dir / std::format("{}.json", make_descriptor_debug_file_stem(p_name));
    std::ofstream output(output_path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error(std::format("Failed to open descriptor debug output file: {}.", output_path.generic_string()));
    }
    output << ParserJSON::write(ParserJSON::read(p_json), 2) << '\n';
    if (!output.good()) {
        throw std::runtime_error(std::format("Failed to write descriptor debug output file: {}.", output_path.generic_string()));
    }
}

inline void write_descriptor_debug_output(std::string_view p_name, const DebugData& p_data) {
    write_descriptor_debug_output(p_name, ParserJSON::write(p_data));
}

/**
 * @brief Read an executable-test switch environment variable.
 *
 * Known switches: WBE_WAIT_PROCESS_INJECT waits for Enter before initialization, WBE_EXEC_TEST_RAY_TRACING builds and
 * imports ray-tracing acceleration structures, WBE_EXEC_TEST_TIMESTAMP_QUERY samples GPU task times and writes a report on
 * exit, and WBE_REAL_TIME_FULL_SCENE_RECONSTRUCT rebuilds the deferred test's static SVO every frame.
 *
 * @param p_variable The environment variable name.
 * @return True when the variable is "1"; false when it is unset, empty, or "0".
 * @throws std::runtime_error If the variable has any other value.
 */
inline bool exec_test_requested(std::string_view p_variable) {
    const std::string variable(p_variable);
    const char* value = std::getenv(variable.c_str());
    if (value == nullptr || std::string_view(value).empty() || std::string_view(value) == "0") {
        return false;
    }
    if (std::string_view(value) == "1") {
        return true;
    }
    throw std::runtime_error(std::format("{} must be 0 or 1, but is \"{}\".", variable, value));
}

/**
 * @brief Parse an executable-test frame number or frame count from an environment variable value.
 * @param p_text The variable value.
 * @param p_error_message The message thrown when the value is not a nonnegative decimal integer.
 * @return The parsed value.
 * @throws std::runtime_error If p_text is not entirely a nonnegative decimal integer.
 */
inline uint64_t parse_exec_test_frame_count(std::string_view p_text, std::string_view p_error_message) {
    const std::optional<uint64_t> value = try_parse_integer<uint64_t>(p_text);
    if (!value.has_value()) {
        throw std::runtime_error(std::string(p_error_message));
    }
    return *value;
}

/**
 * @brief Run the logic-thread executable-test hooks configured by the environment.
 *
 * WBE_EXEC_TEST_RESIZE=<width>x<height>@<frame> resizes the window to width by height once, when the logic thread reaches
 * that logic frame, counted from the first call. Call once per logic frame from the logic thread, which is the thread
 * IWindow::resize() requires.
 *
 * @param p_window The window to resize.
 * @throws std::runtime_error If the variable is malformed.
 */
inline void on_exec_test_logic(IWindow& p_window) {
    static uint64_t logic_frame = 0;
    static bool resized = false;
    const char* resize = std::getenv("WBE_EXEC_TEST_RESIZE");
    if (!resized && resize != nullptr && *resize != '\0') {
        constexpr std::string_view ERROR_MESSAGE = "WBE_EXEC_TEST_RESIZE must be <width>x<height>@<frame>, such as 1280x720@60.";
        const std::string_view value(resize);
        const size_t separator = value.find('x');
        const size_t at = value.find('@');
        if (separator == std::string_view::npos || at == std::string_view::npos || at < separator) {
            throw std::runtime_error(std::string(ERROR_MESSAGE));
        }
        const uint64_t width = parse_exec_test_frame_count(value.substr(0, separator), ERROR_MESSAGE);
        const uint64_t height = parse_exec_test_frame_count(value.substr(separator + 1, at - separator - 1), ERROR_MESSAGE);
        const uint64_t frame = parse_exec_test_frame_count(value.substr(at + 1), ERROR_MESSAGE);
        if (width == 0 || height == 0 || width > UINT32_MAX || height > UINT32_MAX) {
            throw std::runtime_error(std::string(ERROR_MESSAGE));
        }
        if (logic_frame >= frame) {
            p_window.resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
            resized = true;
        }
    }
    ++logic_frame;
}

/**
 * @brief Request the frame captures configured by the environment.
 *
 * WBE_CAPTURE_PNG is the absolute output path and WBE_CAPTURE_FRAME the first captured frame (default 5). Without
 * WBE_CAPTURE_INTERVAL, one frame is captured to WBE_CAPTURE_PNG. With a positive WBE_CAPTURE_INTERVAL, a frame is
 * captured every that many frames from WBE_CAPTURE_FRAME on, each to WBE_CAPTURE_PNG with `_frame<number>` inserted
 * before the extension.
 *
 * @param p_renderer The renderer to request captures from.
 * @param p_frame The current frame number.
 * @return True if a capture of the frame rendered next was requested by this call.
 * @throws std::runtime_error If a variable is malformed.
 */
inline bool on_exec_test_render(IRenderer& p_renderer, uint64_t p_frame) {
    static std::string capture_png_path;
    static uint64_t capture_frame = 5;
    static bool capture_requested = false;
    static uint64_t capture_interval = 0;
    static uint64_t next_capture_frame = 0;
    const char* capture_path = std::getenv("WBE_CAPTURE_PNG");
    if (capture_path != nullptr && *capture_path != '\0') {
        if (*capture_path != '/') {
            throw std::runtime_error("WBE_CAPTURE_PNG must be an absolute output path.");
        }
        capture_png_path = capture_path;
    }
    if (const char* frame = std::getenv("WBE_CAPTURE_FRAME"); frame != nullptr) {
        capture_frame = parse_exec_test_frame_count(frame, "WBE_CAPTURE_FRAME must be a nonnegative frame number.");
    }
    if (const char* interval = std::getenv("WBE_CAPTURE_INTERVAL"); interval != nullptr && *interval != '\0') {
        capture_interval = parse_exec_test_frame_count(interval, "WBE_CAPTURE_INTERVAL must be a positive frame count.");
        if (capture_interval == 0) {
            throw std::runtime_error("WBE_CAPTURE_INTERVAL must be a positive frame count.");
        }
    }
    if (capture_png_path.empty()) {
        return false;
    }
    if (capture_interval == 0) {
        if (!capture_requested && p_frame >= capture_frame) {
            p_renderer.request_frame_capture(capture_png_path);
            capture_requested = true;
            return true;
        }
        return false;
    }
    if (!capture_requested) {
        next_capture_frame = capture_frame;
        capture_requested = true;
    }
    if (p_frame >= next_capture_frame) {
        const Path path(capture_png_path);
        const Path frame_path =
            path.parent_path() / std::format("{}_frame{}{}", path.stem().string(), p_frame, path.extension().string());
        p_renderer.request_frame_capture(frame_path.string());
        next_capture_frame = p_frame + capture_interval;
        return true;
    }
    return false;
}

inline void on_exec_test_start() {

    if (exec_test_requested("WBE_WAIT_PROCESS_INJECT")) {
        std::println("WBE_WAIT_PROCESS_INJECT is set. Running on PID: {}, Press Enter to continue...", OS::get_pid());
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}

inline std::optional<MaterialTextureContract> load_mesh_material_contract(RefRes<RUGraphicsPipeline> p_pipeline) {
    if (p_pipeline == MEM_NULL || p_pipeline->vertex_stage == MEM_NULL || p_pipeline->fragment_stage == MEM_NULL) {
        throw std::runtime_error("Failed to load mesh material contract: graphics pipeline stages are missing.");
    }
    ShaderMetadata vertex_metadata = ShaderMetadataLoader().load(p_pipeline->vertex_stage);
    ShaderMetadata fragment_metadata = ShaderMetadataLoader().load(p_pipeline->fragment_stage);
    std::optional<ShaderMetadata> geometry_metadata;
    if (p_pipeline->geometry_stage != MEM_NULL) {
        geometry_metadata = ShaderMetadataLoader().load(p_pipeline->geometry_stage);
    }
    return MaterialTextureContractResolver().resolve(
        vertex_metadata, geometry_metadata.has_value() ? &*geometry_metadata : nullptr, fragment_metadata, "material_textures", REND_RES_MESH_MATERIAL_PARAMS);
}

/**
 * @brief Index of the material table create_mesh_resources() builds for its material pipeline.
 */
inline constexpr size_t RENDER_TEST_MATERIAL_TABLE_INDEX = 0;

/**
 * @brief Compile p_mesh into static-mesh resources, with the material table of p_material_pipeline's contract, and register
 * them in the render database under p_graph_key, the id graphics_draw_mesh tasks name and import them under.
 * @details The table is material table RENDER_TEST_MATERIAL_TABLE_INDEX, which make_mesh_resource_keys() names. A null
 * material pipeline builds no table.
 * @throws std::runtime_error If the mesh already has material tables, so the table would get another index.
 */
inline Ref<MeshResourcesVK> create_mesh_resources(RendererVK* p_renderer,
    std::string_view p_graph_key,
    RefRes<RUMesh3D> p_mesh,
    RefRes<RUGraphicsPipeline> p_material_pipeline,
    const MeshCompilerVK::Config& p_config = {}) {
    Ref<MeshResourcesVK> result = MeshCompilerVK(p_renderer).compile(p_mesh, p_config);
    // graphics_draw_mesh tasks look the mesh up, and import it, under the key they name.
    p_renderer->get_render_database()->register_compiled_render_resource(p_graph_key, result);
    const std::optional<MaterialTextureContract> material_contract =
        p_material_pipeline != MEM_NULL ? load_mesh_material_contract(p_material_pipeline) : std::nullopt;
    if (material_contract.has_value() && result->acquire_material_table(*material_contract) != RENDER_TEST_MATERIAL_TABLE_INDEX) {
        throw std::runtime_error("Failed to create render test mesh resources: the material table has an unexpected index.");
    }
    return result;
}

/**
 * @brief Copy a trivially copyable value into bytes used as initial graph-buffer data.
 * @param p_value The value to copy.
 * @return The object representation of p_value.
 */
template <typename T>
std::vector<uint8_t> to_initial_data(const T& p_value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&p_value);
    return std::vector<uint8_t>(bytes, bytes + sizeof(T));
}

/**
 * @brief Connect consecutive uses of one resource with resource edges.
 *
 * Every task is connected to the next one; the graph derives each edge's synchronization from the tasks' reported usages.
 *
 * @param p_graph Graph descriptor that already contains every linked task.
 * @param p_resource_id Graph resource carried by the edges.
 * @param p_task_ids Tasks that use the resource, in execution order.
 */
inline void connect_resource_chain(RendGraphDescVK& p_graph, std::string_view p_resource_id, std::span<const std::string_view> p_task_ids) {
    for (size_t i = 1; i < p_task_ids.size(); ++i) {
        p_graph.get_task(p_task_ids[i - 1]).connect(p_task_ids[i], p_resource_id);
    }
}

/**
 * @brief Graph key under which the render tests import their compiled static mesh.
 */
inline constexpr std::string_view STATIC_MESH_KEY = "static_mesh";

/**
 * @brief Graph key under which the deferred test imports its moving mesh.
 */
inline constexpr std::string_view DYNAMIC_MESH_KEY = "dynamic_mesh";

/**
 * @brief Graph key under which the deferred test imports the entity placing its moving mesh.
 */
inline constexpr std::string_view DYNAMIC_MESH_ENTITY_KEY = "dynamic_mesh_entity";

/**
 * @brief Graph resource ids of one drawn entity's mesh and transform that back the shader-side mesh resource ids.
 */
struct MeshResourceKeys {
    /** Graph id bound to REND_RES_MESH_SUBMESHES. */
    std::string submeshes;
    /** Graph id bound to REND_RES_MESH_NODES. */
    std::string nodes;
    /** Graph id bound to REND_RES_MESH_INSTANCES. */
    std::string instances;
    /** Graph id bound to REND_RES_MESH_TRANSFORM; empty for static meshes, whose shaders do not declare it. */
    std::string transform;
    /** Graph id bound to REND_RES_MESH_MATERIAL_PARAMS. */
    std::string material_params;
    /** Graph id bound to REND_RES_MESH_MATERIAL_SAMPLER. */
    std::string material_sampler;
};

/**
 * @brief Resource ids of a mesh imported under p_mesh_key, drawn with the whole-mesh transform imported as p_transform_id.
 * @details Leave p_transform_id empty for static meshes drawn with shader variants that do not declare a mesh transform.
 */
inline MeshResourceKeys make_mesh_resource_keys(std::string_view p_mesh_key, std::string_view p_transform_id = {}) {
    return MeshResourceKeys{
        .submeshes = IBindableRenderResourceVK::make_member_resource_id(p_mesh_key, MeshResourcesVK::SUBMESHES),
        .nodes = IBindableRenderResourceVK::make_member_resource_id(p_mesh_key, MeshResourcesVK::NODES),
        .instances = IBindableRenderResourceVK::make_member_resource_id(p_mesh_key, MeshResourcesVK::INSTANCES),
        .transform = std::string(p_transform_id),
        .material_params = IBindableRenderResourceVK::make_member_resource_id(
            p_mesh_key, MeshResourcesVK::get_material_params_member(RENDER_TEST_MATERIAL_TABLE_INDEX)),
        .material_sampler = IBindableRenderResourceVK::make_member_resource_id(p_mesh_key, MeshResourcesVK::MATERIAL_SAMPLER),
    };
}

/**
 * @brief Map the shader-side mesh resource ids a pipeline reflects to one mesh's graph resources.
 * @details Ids the pipeline does not declare are skipped because every alias must be used by the task.
 * @throws std::runtime_error If the pipeline declares an id that p_keys leaves empty.
 * @param p_task Task drawing the mesh with p_pipeline.
 * @param p_pipeline Pipeline whose reflected descriptor layout selects the aliased ids.
 * @param p_keys Graph keys of the drawn mesh.
 * @return p_task.
 */
inline RendTaskDescVK& alias_mesh_resources(RendTaskDescVK& p_task, const ROPipelineVK& p_pipeline, const MeshResourceKeys& p_keys) {
    const std::array<std::pair<std::string_view, std::string_view>, 6> mappings{
        {
            {REND_RES_MESH_SUBMESHES, p_keys.submeshes},
            {REND_RES_MESH_NODES, p_keys.nodes},
            {REND_RES_MESH_INSTANCES, p_keys.instances},
            {REND_RES_MESH_TRANSFORM, p_keys.transform},
            {REND_RES_MESH_MATERIAL_PARAMS, p_keys.material_params},
            {REND_RES_MESH_MATERIAL_SAMPLER, p_keys.material_sampler},
        },
    };
    const auto& fields = p_pipeline.get_descriptor_layout().fields;
    for (const auto& [shader_id, graph_id] : mappings) {
        if (!std::ranges::any_of(fields, [shader_id](const auto& p_field) {
                return p_field.resource_id == shader_id;
            })) {
            continue;
        }
        if (graph_id.empty()) {
            throw std::runtime_error(
                std::format("Pipeline declares mesh resource \"{}\" but no graph resource is provided for it.", shader_id));
        }
        p_task.alias(shader_id, graph_id);
    }
    return p_task;
}

/**
 * @brief Connect consecutive uses of one resource with resource edges.
 * @param p_graph Graph descriptor that already contains every linked task.
 * @param p_resource_id Graph resource carried by the edges.
 * @param p_task_ids Tasks that use the resource, in execution order.
 */
inline void connect_resource_chain(
    RendGraphDescVK& p_graph, std::string_view p_resource_id, std::initializer_list<std::string_view> p_task_ids) {
    connect_resource_chain(p_graph, p_resource_id, std::span<const std::string_view>(p_task_ids.begin(), p_task_ids.size()));
}

/**
 * @brief Map a graphics task's render targets to graph resources.
 * @param p_task Descriptor of a task that renders into `render_target_colors[<index>]` and `render_target_depth`.
 * @param p_color_resource_ids Graph resources of the color targets in pipeline order.
 * @param p_depth_resource_id Graph resource of the depth target, when the pipeline uses one.
 * @return p_task.
 */
inline RendTaskDescVK& alias_render_targets(RendTaskDescVK& p_task,
    std::span<const std::string_view> p_color_resource_ids,
    std::optional<std::string_view> p_depth_resource_id = std::nullopt) {
    for (size_t i = 0; i < p_color_resource_ids.size(); ++i) {
        p_task.alias(RenderTargetsVK::color_id(static_cast<uint32_t>(i)), p_color_resource_ids[i]);
    }
    if (p_depth_resource_id.has_value()) {
        p_task.alias(RenderTargetsVK::DEPTH_ID, *p_depth_resource_id);
    }
    return p_task;
}

/**
 * @brief Make a clear value for a color render target.
 */
inline RTParamsClearValueVK make_clear_color(float p_red, float p_green, float p_blue, float p_alpha) {
    return RTParamsClearValueVK{.color = std::vector<float>{p_red, p_green, p_blue, p_alpha}};
}

/**
 * @brief Make a clear value for a depth render target.
 */
inline RTParamsClearValueVK make_clear_depth(float p_depth) {
    return RTParamsClearValueVK{.depth = p_depth};
}

/**
 * @brief Make a fixed render extent.
 */
inline RendExtent2DParamsVK make_fixed_extent_params(VkExtent2D p_extent) {
    return RendExtent2DParamsVK{.width = p_extent.width, .height = p_extent.height};
}

/**
 * @class RenderTestMeshDraw
 * @brief A graphics_draw_mesh task of a render test, kept as parameters until a rebuild callback adds it to a graph.
 * Copyable and movable.
 */
struct RenderTestMeshDraw {
    /** The task id. */
    std::string task_id;
    /** The task parameters; the mesh must be registered in the render database under params.mesh. */
    RTParamsGraphicsDrawMeshVK params;
    /** Graph resources of the color render targets, in pipeline order. */
    std::vector<std::string> color_attachment_ids;
    /** Graph resource of the depth render target, when the pipeline uses one. */
    std::optional<std::string> depth_attachment_id;

    /**
     * @brief Add the task to a graph descriptor with RTGraphicsDrawMeshVK::add_to_graph() and alias its render targets.
     * @return The added task descriptor; invalidated when another task is added.
     * @throws std::runtime_error Under the conditions of RTGraphicsDrawMeshVK::add_to_graph().
     */
    RendTaskDescVK& add_to(RendGraphDescVK& p_graph, const RenderContextVK* p_render_data) const {
        RendTaskDescVK& task = RTGraphicsDrawMeshVK::add_to_graph(p_graph, task_id, params, p_render_data);
        const std::vector<std::string_view> colors(color_attachment_ids.begin(), color_attachment_ids.end());
        const std::optional<std::string_view> depth =
            depth_attachment_id.has_value() ? std::optional<std::string_view>(*depth_attachment_id) : std::nullopt;
        return alias_render_targets(task, colors, depth);
    }
};

inline Ref<ROGraphicsPipelineVK> compile_render_test_graphics_pipeline(RendererVK* p_renderer,
    std::string_view p_key,
    RefRes<RUGraphicsPipeline> p_pipeline,
    std::span<const VkFormat> p_color_formats,
    VkFormat p_depth_format = VK_FORMAT_UNDEFINED) {
    const RenderContextVK* render_data = p_renderer->get_render_context();
    GraphicsPipelineCompilerVKContext context{
        .allocator = render_data->allocator,
        .render_data = render_data,
        .render_database = p_renderer->get_render_database(),
        .renderer = p_renderer,
        .color_attachment_formats = std::vector<VkFormat>(p_color_formats.begin(), p_color_formats.end()),
        .depth_attachment_format = p_depth_format,
    };
    return GraphicsPipelineCompilerVK(context).compile(p_key, p_pipeline).graphics_pipeline;
}

/**
 * @brief Extent of a render test pass, evaluated against the render graph context whenever its task is created.
 */
using RenderTestExtentFactory = std::function<VkExtent2D(const RendGraphContextVK&)>;

/**
 * @brief Make a render test extent factory that ignores the render graph context.
 *
 * @param p_extent The extent the factory returns.
 * @return The factory.
 */
inline RenderTestExtentFactory make_fixed_extent_factory(VkExtent2D p_extent) {
    return [p_extent](const RendGraphContextVK& /*p_context*/) {
        return p_extent;
    };
}

/**
 * @struct SubmeshDrawBuffersVK
 * @brief The graph resources and vertex input that draw one submesh with RTRunGraphicsPipelineVK. Copyable and movable.
 */
struct SubmeshDrawBuffersVK {
    /** Vertex input matched against the submesh vertex bindings. */
    VertexInputDescriptionVK vertex_input;
    /** The imported vertex buffer read by each vertex-input binding, parallel to vertex_input.bindings. */
    std::vector<std::string> vertex_buffer_ids;
    /** Byte offset of the submesh vertices in each vertex buffer, parallel to vertex_buffer_ids. */
    std::vector<VkDeviceSize> vertex_buffer_offsets;
    /** The imported index buffer. */
    std::string index_buffer_id;
    /** The submesh's range of the index buffer. */
    IndexedDrawRegionVK indexed_draw_region;

    /**
     * @brief Return the vertex buffers in the form RTRunGraphicsPipelineVK::Builder::set_indexed_draw() takes.
     *
     * The returned ids view this object, which must outlive the call they are passed to.
     */
    std::vector<VertexBufferSourceVK> get_vertex_buffer_sources() const {
        std::vector<VertexBufferSourceVK> result;
        result.reserve(vertex_buffer_ids.size());
        for (size_t i = 0; i < vertex_buffer_ids.size(); ++i) {
            result.push_back(VertexBufferSourceVK{.resource_id = vertex_buffer_ids[i], .offset = vertex_buffer_offsets[i]});
        }
        return result;
    }
};

/**
 * @brief Import the compiled vertex and index buffers of a mesh into a graph and wire one of its submeshes to a vertex entry.
 *
 * The mesh binaries are compiled, or reused from the render database, by MeshBufferCompilerVK and imported without copying:
 * vertex buffer i as `<p_key>.vertices.<i>` and the index buffer as `<p_key>.indices`. Only the vertex bindings the entry
 * reads are bound.
 *
 * @param p_renderer The renderer owning the buffers.
 * @param p_graph The graph descriptor to import the buffers into.
 * @param p_key Prefix of the imported resource ids.
 * @param p_mesh The mesh whose binaries are compiled.
 * @param p_submesh The drawn submesh of p_mesh.
 * @param p_vertex_metadata Metadata of the vertex shader.
 * @param p_vertex_entry Name of the vertex entry point.
 * @return The resources and vertex input of the draw.
 * @throws std::runtime_error If the mesh has no indexed geometry, the submesh index range is not uint32 aligned, or the vertex
 * input cannot be matched.
 */
inline SubmeshDrawBuffersVK import_submesh_draw_buffers(RendererVK* p_renderer,
    RendGraphDescVK& p_graph,
    std::string_view p_key,
    RefRes<RUMesh3D> p_mesh,
    const RUSubmesh3D& p_submesh,
    const ShaderMetadata& p_vertex_metadata,
    std::string_view p_vertex_entry) {
    if (p_submesh.indices.binary.start % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Failed to import submesh draw buffers: the index range is not uint32 aligned.");
    }
    MeshBufferCompilerVK::BuildResult buffers =
        MeshBufferCompilerVK(p_renderer->get_render_context()).build(p_mesh, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (buffers.vertex_buffers.empty() || buffers.index_buffer == MEM_NULL) {
        throw std::runtime_error("Failed to import submesh draw buffers: the mesh has no indexed geometry.");
    }
    std::vector<std::string> buffer_ids;
    for (size_t i = 0; i < buffers.vertex_buffers.size(); ++i) {
        buffer_ids.push_back(IBindableRenderResourceVK::make_member_resource_id(p_key, MeshResourcesVK::get_vertex_buffer_member(i)));
        p_graph.add_resource(buffer_ids.back(), *buffers.vertex_buffers[i]);
    }
    SubmeshDrawBuffersVK result;
    result.index_buffer_id = IBindableRenderResourceVK::make_member_resource_id(p_key, MeshResourcesVK::INDICES);
    p_graph.add_resource(result.index_buffer_id, *buffers.index_buffer);
    result.vertex_input = build_vertex_input_description_vk(p_vertex_metadata, p_vertex_entry, p_submesh.get_vertex_bindings());
    for (size_t source_binding : result.vertex_input.source_bindings) {
        const RUVertexView& binding = p_submesh.get_vertex_bindings()[source_binding];
        const auto buffer = std::ranges::find(buffers.vertex_binaries, binding.binary.binary);
        if (buffer == buffers.vertex_binaries.end()) {
            throw std::runtime_error("Failed to import submesh draw buffers: a vertex binding reads a binary that was not compiled.");
        }
        result.vertex_buffer_ids.push_back(buffer_ids[static_cast<size_t>(buffer - buffers.vertex_binaries.begin())]);
        result.vertex_buffer_offsets.push_back(binding.binary.start);
    }
    result.indexed_draw_region = IndexedDrawRegionVK{
        .first_index = static_cast<uint32_t>(p_submesh.indices.binary.start / sizeof(uint32_t)),
        .index_count = static_cast<uint32_t>(p_submesh.indices.index_count),
        .vertex_offset = 0,
    };
    return result;
}

} // namespace WhiteBirdEngine

inline glm::vec3 default_test_camera_direction() {
    return glm::normalize(TEST_DEFAULT_CAMERA_TARGET - TEST_DEFAULT_CAMERA_POSITION);
}

inline float default_test_camera_yaw() {
    glm::vec3 direction = default_test_camera_direction();
    return std::atan2(direction.z, direction.x);
}

inline float default_test_camera_pitch() {
    return std::asin(default_test_camera_direction().y);
}

inline glm::vec3 test_camera_forward(float p_yaw, float p_pitch) {
    return glm::normalize(glm::vec3(std::cos(p_pitch) * std::cos(p_yaw), std::sin(p_pitch), std::cos(p_pitch) * std::sin(p_yaw)));
}

inline glm::mat4 test_camera_view(const glm::vec3& p_position, float p_yaw, float p_pitch) {
    glm::vec3 forward = test_camera_forward(p_yaw, p_pitch);
    glm::vec3 up = std::abs(forward.y) > 0.99F ? glm::vec3(0.0F, 0.0F, 1.0F) : glm::vec3(0.0F, 1.0F, 0.0F);
    return glm::lookAt(p_position, p_position + forward, up);
}

inline WBE::ComponentCamera make_default_test_camera_component(float p_aspect_ratio, float p_near_plane = 10.0F) {
    WBE::ComponentCamera camera_component{
        .view = test_camera_view(TEST_DEFAULT_CAMERA_POSITION, default_test_camera_yaw(), default_test_camera_pitch()),
        .proj = glm::perspectiveRH_ZO(glm::radians(45.0F), p_aspect_ratio, p_near_plane, 0.1F),
        .is_dirty = false,
    };
    camera_component.proj[1][1] *= -1;
    return camera_component;
}

/**
 * @struct CameraInfoGPU
 * @brief CPU-side mirror of the shader `CameraInfo` struct.
 */
struct CameraInfoGPU {
    /**
     * @brief The view matrix.
     */
    glm::mat4 view;
    /**
     * @brief The projection matrix.
     */
    glm::mat4 proj;
    /**
     * @brief The world-space camera position.
     */
    glm::vec4 world_position;
};

/**
 * @brief Maximum number of spot lights in a SpotLightInfosGPU table.
 */
constexpr uint32_t MAX_SPOT_LIGHT_INFO_COUNT = 4;

/**
 * @struct SpotLightInfoGPU
 * @brief CPU-side mirror of the shader `SpotLightInfo` struct.
 */
struct SpotLightInfoGPU {
    /**
     * @brief Light-space view matrix.
     */
    glm::mat4 view;
    /**
     * @brief Light-space projection matrix.
     */
    glm::mat4 proj;
    /**
     * @brief Inverse light-space view-projection matrix used to reconstruct world positions from shadow-map depth.
     */
    glm::mat4 inv_view_proj;
    /**
     * @brief World-space position of the spot light.
     */
    glm::vec3 position;
    /**
     * @brief Scalar intensity multiplier.
     */
    float intensity = 0.0F;
    /**
     * @brief World-space direction the spot is pointing.
     */
    glm::vec3 direction;
    /**
     * @brief Cosine of the inner cone half-angle.
     */
    float inner_cone_cos = 0.0F;
    /**
     * @brief Linear-space color of the spot light.
     */
    glm::vec3 color;
    /**
     * @brief Cosine of the outer cone half-angle.
     */
    float outer_cone_cos = 0.0F;
    /**
     * @brief Maximum effective light distance.
     */
    float range = 0.0F;
    /**
     * @brief Projected emitter width in world units.
     */
    float emitter_width = 0.0F;
    /**
     * @brief Padding to match the shader struct array stride.
     */
    std::array<uint32_t, 2> padding{};
};

static_assert(sizeof(SpotLightInfoGPU) == 256);

/**
 * @struct SpotLightInfosGPU
 * @brief Spot light table with its active-entry count.
 */
struct SpotLightInfosGPU {
    /**
     * @brief Active spot light data consumed by the shader.
     */
    std::array<SpotLightInfoGPU, MAX_SPOT_LIGHT_INFO_COUNT> spot_light_info{};
    /**
     * @brief Number of active entries in spot_light_info.
     */
    uint32_t spot_light_count = 0;
    /**
     * @brief Pads the table to the 16-byte-aligned size of the shader `SpotLightInfoBuffer` struct.
     */
    std::array<uint32_t, 3> padding{};
};

static_assert(sizeof(SpotLightInfosGPU) == MAX_SPOT_LIGHT_INFO_COUNT * sizeof(SpotLightInfoGPU) + 16);

inline CameraInfoGPU make_camera_info_gpu(const WBE::ComponentCamera& p_camera_component) {
    return CameraInfoGPU{
        .view = p_camera_component.view,
        .proj = p_camera_component.proj,
        .world_position = glm::inverse(p_camera_component.view)[3],
    };
}

inline WBE::ComponentSpotLight make_default_test_spot_light_component(float p_intensity) {
    WBE::ComponentSpotLight spot_light_component{
        .position = glm::vec3(2.5F, 3.5F, 2.5F),
        .direction = glm::normalize(glm::vec3(-1.0F, -1.4F, -1.0F)),
        .color = glm::vec3(1.0F, 0.95F, 0.85F),
        .intensity = p_intensity,
        .inner_cone_cos = std::cos(glm::radians(18.0F)),
        .outer_cone_cos = std::cos(glm::radians(28.0F)),
        .range = 12.0F,
        .emitter_width = 0.05F,
    };

    glm::vec3 up = std::abs(spot_light_component.direction.y) > 0.99F ? glm::vec3(0.0F, 0.0F, 1.0F) : glm::vec3(0.0F, 1.0F, 0.0F);
    spot_light_component.view =
        glm::lookAt(spot_light_component.position, spot_light_component.position + spot_light_component.direction, up);
    float outer_half_angle = std::acos(std::clamp(spot_light_component.outer_cone_cos, -1.0F, 1.0F));
    float far_plane = std::max(spot_light_component.range, 0.2F);
    spot_light_component.proj = glm::perspectiveRH_ZO(2.0F * outer_half_angle, 1.0F, far_plane, 0.1F);
    spot_light_component.proj[1][1] *= -1.0F;
    return spot_light_component;
}

inline SpotLightInfoGPU make_spot_light_info_gpu(const WBE::ComponentSpotLight& p_spot_light_component) {
    return SpotLightInfoGPU{
        .view = p_spot_light_component.view,
        .proj = p_spot_light_component.proj,
        .inv_view_proj = glm::inverse(p_spot_light_component.proj * p_spot_light_component.view),
        .position = p_spot_light_component.position,
        .intensity = p_spot_light_component.intensity,
        .direction = p_spot_light_component.direction,
        .inner_cone_cos = p_spot_light_component.inner_cone_cos,
        .color = p_spot_light_component.color,
        .outer_cone_cos = p_spot_light_component.outer_cone_cos,
        .range = p_spot_light_component.range,
        .emitter_width = p_spot_light_component.emitter_width,
    };
}

inline void log_uniform_buffer_descriptors(WBE::Ref<WBE::ROCMultislotsVK> p_buffers, std::string_view p_label) {
    WBE::write_descriptor_debug_output(std::format("{}_descriptors", p_label), p_buffers->to_string());
}

#endif
