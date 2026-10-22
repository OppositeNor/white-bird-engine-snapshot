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
#include "core/async/worker.hh"
#include "core/data/data_value.hh"
#include "core/engine_core.hh"
#include "core/logging/log.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/profiling/profiler.hh"
#include "function/application/application_game.hh"
#include "function/function_utils.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/compute_pipeline_compiler_vk.hh"
#include "function/render_vk/graphics_pipeline_compiler_vk.hh"
#include "function/render_vk/rend_graph_recipe_builder_vk.hh"
#include "function/render_vk/rend_graph_script_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_targets_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "glm/gtc/packing.hpp"
#include "global/global.hh"
#include "imgui.h"
#include "imgui_overlay_vk.hh"
#include "input_manager_mocked.hh"
#include "pixel_sorting_recipe_context.hh"
#include "platform/file_system/path.hh"
#include "render_test.hh"
#include "resource/resource_manager/resource_manager.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "resource/resource_utils.hh"
#include "rt_imgui_vk.hh"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <future>
#include <initializer_list>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WBE = WhiteBirdEngine;
using WhiteBirdEngine::PixelSortOrientation;

inline constexpr std::string_view RT_DRAW_IMGUI = "draw_imgui";
inline constexpr std::string_view REND_RES_PIXEL_SORT_SOURCE = "pixel_sort_source";
inline constexpr std::string_view REND_RES_PIXEL_SORTING_POST_PROCESSING_OUTPUT_IMAGE = "pixel_sorting_post_processing_output_image";
inline constexpr std::string_view REND_RES_PIXEL_SORTING_POST_PROCESSING_PARAMS = "pixel_sorting_post_processing_params";
inline constexpr std::string_view REND_RES_PIXEL_SORT_CONTRAST = "pixel_sort_contrast";
inline constexpr float PIXEL_SORT_DEFAULT_MIN_METRIC = 0.20F;
inline constexpr float PIXEL_SORT_DEFAULT_MAX_METRIC = 0.8F;
// Must match WBE_PIXEL_SORT_METRIC_SATURATION in pixel_sort_utils.slang: 0 sorts by luminance, 1 by hue, 2 by saturation.
inline constexpr uint32_t PIXEL_SORT_DEFAULT_METRIC = 0;
inline constexpr float PIXEL_SORTING_POST_PROCESSING_DEFAULT_GAMMA = 1.0F;
// Bytes of one R16G16B16A16_SFLOAT pixel of the result image.
inline constexpr size_t PIXEL_SORT_RESULT_BYTES_PER_PIXEL = 8;
// Directory, relative to the working directory, that receives the screenshots.
inline constexpr std::string_view PIXEL_SORT_SCREENSHOT_DIRECTORY = "build/screenshots";
// The delta, in seconds, of every frame of a recording, so that the frame sequence plays back at 60 frames per second.
inline constexpr double PIXEL_SORT_RECORDING_FIXED_DELTA = 1.0 / 60.0;
// The most frame captures, read back or being written, that a recording keeps in flight; it waits for one beyond that.
inline constexpr size_t PIXEL_SORT_MAX_CAPTURES_IN_FLIGHT = 8;

namespace {

bool is_srgb_format(VkFormat p_format) {
    return p_format == VK_FORMAT_R8G8B8A8_SRGB || p_format == VK_FORMAT_B8G8R8A8_SRGB;
}

// The sRGB transfer function, applied by the hardware when a linear color is written to an sRGB swap chain.
float encode_srgb(float p_linear) {
    if (p_linear <= 0.0031308F) {
        return p_linear * 12.92F;
    }
    return 1.055F * std::pow(p_linear, 1.0F / 2.4F) - 0.055F;
}

// Convert the R16G16B16A16_SFLOAT result image to opaque RGBA8, encoded as the swap chain displays it.
std::vector<uint8_t> convert_result_to_rgba8(std::span<const uint8_t> p_result_data, bool p_encode_srgb) {
    if (p_result_data.size_bytes() % PIXEL_SORT_RESULT_BYTES_PER_PIXEL != 0) {
        throw std::runtime_error("Failed to convert the pixel sorting screenshot: byte count is not pixel aligned.");
    }
    const size_t pixel_count = p_result_data.size_bytes() / PIXEL_SORT_RESULT_BYTES_PER_PIXEL;
    std::vector<uint8_t> rgba8(pixel_count * 4);
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        for (size_t channel = 0; channel < 3; ++channel) {
            uint16_t packed_channel = 0;
            std::memcpy(&packed_channel,
                p_result_data.data() + pixel * PIXEL_SORT_RESULT_BYTES_PER_PIXEL + channel * sizeof(packed_channel),
                sizeof(packed_channel));
            float value = glm::unpackHalf1x16(packed_channel);
            if (!std::isfinite(value)) {
                value = value > 0.0F ? 1.0F : 0.0F;
            }
            value = std::clamp(value, 0.0F, 1.0F);
            if (p_encode_srgb) {
                value = encode_srgb(value);
            }
            rgba8[pixel * 4 + channel] = static_cast<uint8_t>(std::lround(value * 255.0F));
        }
        rgba8[pixel * 4 + 3] = UINT8_MAX;
    }
    return rgba8;
}

// The current local time as a file name component, e.g. 20261007_183012.
std::string make_local_time_stamp() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const std::chrono::zoned_time local_time{std::chrono::current_zone(), now};
    return std::format("{:%Y%m%d_%H%M%S}", local_time);
}

// A screenshot path named after the current local time, e.g. build/screenshots/pixel_sorting_20261007_183012.png.
WBE::Path make_screenshot_path() {
    return WBE::Path(PIXEL_SORT_SCREENSHOT_DIRECTORY) / std::format("pixel_sorting_{}.png", make_local_time_stamp());
}

// A frame sequence directory named after the current local time, e.g. build/screenshots/pixel_sorting_20261007_183012_sequence.
WBE::Path make_sequence_directory() {
    return WBE::Path(PIXEL_SORT_SCREENSHOT_DIRECTORY) / std::format("pixel_sorting_{}_sequence", make_local_time_stamp());
}

} // namespace
// Each pass sorts by one 8-bit digit of the 32-bit keys.
inline constexpr uint32_t RADIX_SORT_PASS_COUNT = 4;
// The compute shaders the recipe dispatches, besides the per-pass radix_sort_histogram_workgroup_process_<bits> and
// radix_sort_scatter_<bits> shaders; compute pipelines are compiled under their shader ids.
inline constexpr std::array<std::string_view, 12> PIXEL_SORT_COMPUTE_SHADERS{
    "count_pixel_sort_mask_vertical_shader",
    "count_pixel_sort_mask_horizontal_shader",
    "register_pixel_sort_keys_vertical_shader",
    "register_pixel_sort_keys_horizontal_shader",
    "write_sorted_pixels_vertical_shader",
    "write_sorted_pixels_horizontal_shader",
    "prefix_sum_pixel_sort_columns_shader",
    "pixel_sorting_post_processing_shader",
    "radix_sort_prepare_dispatch",
    "radix_sort_histogram_prefix_sum",
    "radix_sort_histogram_overflow_prefix_sum",
    "radix_sort_histogram_add_overflow",
};

/**
 * @class PixelSortContrastGPU
 * @brief GPU layout of PixelSortContrast in pixel_sort_utils.slang.
 */
struct PixelSortContrastGPU {
    /** The smallest sort metric of a sorted pixel, in [0, 1]. */
    float min_metric = PIXEL_SORT_DEFAULT_MIN_METRIC;
    /** The largest sort metric of a sorted pixel, in [0, 1]. */
    float max_metric = PIXEL_SORT_DEFAULT_MAX_METRIC;
    /** The metric the sorted pixels are ordered and masked by: 0 for luminance, 1 for hue, 2 for saturation. */
    uint32_t sort_metric = PIXEL_SORT_DEFAULT_METRIC;
    /** The pixels each sort covers: 0 for the whole line, as the R16_UINT mask image requires, 1 for each span. */
    uint32_t sort_scope = 0;
};

/**
 * @class PixelSortingPostProcessingParamsGPU
 * @brief GPU layout of PixelSortingPostProcessingParams in pixel_sorting_post_processing.slang.
 */
struct PixelSortingPostProcessingParamsGPU {
    /** The exponent every color channel is raised to; 1 leaves the color unchanged. */
    float gamma = PIXEL_SORTING_POST_PROCESSING_DEFAULT_GAMMA;
};

/**
 * @class ApplicationVulkanPixelSortingTest
 * @brief Sorts the pixels of every column or row of an image that are inside a contrast mask with the GPU radix sort, and
 * presents the result.
 *
 * The orientation selects whether the columns or the rows are sorted; changing it rebuilds the render graph. One
 * invocation per line counts its masked pixels, a prefix sum turns the counts into the range of every line in the key
 * buffer, and one invocation per line registers its masked pixels into that range and stores each pixel's buffer index
 * in an index image. Every key orders by line, then metric, and stores the position along the line (see
 * pixel_sort_utils.slang), so one radix sort of all keys sorts every line in place within its range. A single invocation
 * writes the indirect dispatches of the radix sort from the number of registered keys. Each of the 4 radix sort passes
 * runs the histogram, histogram prefix scan, overflow prefix sum, overflow addition, and scatter shaders for one 8-bit
 * digit, alternating between two key buffers. Then, every masked pixel reads the sorted key at its stored index, and the other pixels keep their color.
 * Finally, a post-processing pass applies general effects to the sorted image before it is presented.
 */
class ApplicationVulkanPixelSortingTest : public WBE::ApplicationGame {
public:
    ApplicationVulkanPixelSortingTest()
        : WBE::ApplicationGame(
              WBE::RendererCapabilities{
                  .timestamp_query = WBE::exec_test_requested("WBE_EXEC_TEST_TIMESTAMP_QUERY"),
                  .descriptor_set = true,
              },
              WBE::make_ref<WBE::InputManagerMocked>(WBE::global_allocator(), WBE::global_allocator())) {
    }

    virtual void logic_update(double p_delta) override {
    }

    virtual void render_update(double p_delta) override {
        auto* renderer = WBE::RendererVK::get_singleton();
        WBE::on_exec_test_render(*renderer, renderer->get_render_context()->current_frame);
        poll_captures();
        if (recording) {
            record_frame();
        }
        if (ui_orientation != built_orientation) {
            rebuild_render_graph();
        }
        imgui_overlay->begin_frame(p_delta);
        draw_parameter_window(p_delta);
        imgui_overlay->end_frame();
    }

    /**
     * @brief Set the sort metric range of the sorted pixels.
     *
     * Before setup_render_graph(), the range becomes the initial contrast buffer content; afterwards the contrast is
     * updated through a render job. Must be called from the logic thread.
     *
     * @param p_min_metric The smallest sort metric of a sorted pixel.
     * @param p_max_metric The largest sort metric of a sorted pixel.
     * @throws std::runtime_error If the range is not within [0, 1] or p_min_metric exceeds p_max_metric.
     */
    void set_contrast(float p_min_metric, float p_max_metric) {
        if (!(0.0F <= p_min_metric && p_min_metric <= p_max_metric && p_max_metric <= 1.0F)) {
            throw std::runtime_error(std::format(
                "Invalid pixel sort contrast range [{}, {}]: it must satisfy 0 <= min <= max <= 1.", p_min_metric, p_max_metric));
        }
        contrast.min_metric = p_min_metric;
        contrast.max_metric = p_max_metric;
        if (!render_graph_built) {
            return;
        }
        // The rendering thread owns the buffer, so the update runs as a render job.
        add_render_job([this, p_min_metric, p_max_metric] {
            ui_contrast.min_metric = p_min_metric;
            ui_contrast.max_metric = p_max_metric;
            contrast_buffer->update_buffer(&ui_contrast, sizeof(PixelSortContrastGPU));
        });
    }

    /**
     * @brief Set the gamma the post-processing pass raises every color channel to.
     *
     * Before setup_render_graph(), the gamma becomes the initial parameter buffer content; afterwards the gamma is updated
     * through a render job. Must be called from the logic thread.
     *
     * @param p_gamma The exponent every color channel is raised to.
     * @throws std::runtime_error If p_gamma is not positive.
     */
    void set_gamma(float p_gamma) {
        if (!(p_gamma > 0.0F)) {
            throw std::runtime_error(std::format("Invalid pixel sorting post-processing gamma {}: it must be positive.", p_gamma));
        }
        post_processing_params.gamma = p_gamma;
        if (!render_graph_built) {
            return;
        }
        // The rendering thread owns the buffer, so the update runs as a render job.
        add_render_job([this, p_gamma] {
            ui_post_processing_params.gamma = p_gamma;
            post_processing_params_buffer->update_buffer(&ui_post_processing_params, sizeof(PixelSortingPostProcessingParamsGPU));
        });
    }

    /**
     * @brief Set whether the columns or the rows of the image are sorted.
     *
     * Before setup_render_graph(), the orientation selects the initial render graph; afterwards the graph is rebuilt for it
     * through a render job. Must be called from the logic thread.
     *
     * @param p_orientation The lines of the image to sort.
     */
    void set_orientation(PixelSortOrientation p_orientation) {
        orientation = p_orientation;
        if (!render_graph_built) {
            return;
        }
        add_render_job([this, p_orientation] {
            ui_orientation = p_orientation;
        });
    }

    /**
     * @brief Load the source image, size the window to it, and build the render graph for the configured orientation.
     *
     * Must be called once, before run().
     *
     * @throws std::runtime_error If a resource is missing, the image is too large to sort, or the graph cannot be built.
     */
    void setup_render_graph() {
        auto* render_allocator = get_renderer_allocator().get();
        auto* renderer = WBE::RendererVK::get_singleton();
        const auto* render_context = renderer->get_render_context();
        auto* resource_manager = WBE::ResourceManager::get_singleton();
        imgui_overlay = WBE::make_unique<WBE::ImGuiOverlayVK>(WBE::global_allocator(), renderer);

        auto texture_resource = resource_manager->get_resource<WBE::RUTexture>(REND_RES_PIXEL_SORT_SOURCE);
        if (texture_resource == nullptr) {
            throw std::runtime_error("Failed to setup pixel sorting test: source texture resource was not found.");
        }
        auto present_pipeline_resource = resource_manager->get_resource<WBE::RUGraphicsPipeline>("present_pixel_sort_pipeline");
        if (present_pipeline_resource == nullptr) {
            throw std::runtime_error("Failed to setup pixel sorting test: present pipeline resource was not found.");
        }

        WBE::GraphicsPipelineCompilerVKContext graphics_compiler_context{
            .allocator = render_allocator,
            .render_data = render_context,
            .render_database = renderer->get_render_database(),
            .renderer = renderer,
            .color_attachment_formats = {render_context->swap_chain_format},
        };
        WBE::GraphicsPipelineCompilerVK graphics_compiler(graphics_compiler_context);
        source_view = graphics_compiler.compile_texture_view(REND_RES_PIXEL_SORT_SOURCE, texture_resource);
        present_pipeline = graphics_compiler.compile("present_pixel_sort_pipeline", present_pipeline_resource).graphics_pipeline;

        const VkExtent3D source_extent = source_view->fields().image->fields().extent;
        const uint64_t element_count = static_cast<uint64_t>(source_extent.width) * source_extent.height;
        // A key packs the line, metric, and position along the line, so at least 2 metric levels must fit in 32 bits.
        if (element_count == 0 || UINT32_MAX / element_count < 2) {
            throw std::runtime_error(std::format("Failed to setup pixel sorting test: a {}x{} image is too large to pack its pixel sort keys into 32 bits.",
                source_extent.width,
                source_extent.height));
        }
        // Show the image at its own size. This runs before the logic and rendering threads start, and the renderer
        // resizes the swap chain, and with it the graph, on the first frame.
        get_window()->resize(source_extent.width, source_extent.height);
        ui_contrast = contrast;
        ui_post_processing_params = post_processing_params;
        ui_orientation = orientation;
        // The recipe's tasks look their pipelines up by key in the render database, which holds them weakly; compile every
        // pipeline either orientation uses once and keep them alive for every graph.
        for (const std::string_view shader_id : PIXEL_SORT_COMPUTE_SHADERS) {
            compute_pipelines.push_back(compile_compute_pipeline(shader_id));
        }
        for (uint32_t pass = 0; pass < RADIX_SORT_PASS_COUNT; ++pass) {
            const std::string bit_region = std::format("{}_{}", pass * 8, (pass + 1) * 8);
            compute_pipelines.push_back(compile_compute_pipeline(std::format("radix_sort_histogram_workgroup_process_{}", bit_region)));
            compute_pipelines.push_back(compile_compute_pipeline(std::format("radix_sort_scatter_{}", bit_region)));
        }
        // Recipes cannot set initial data, so the application owns the parameter buffers and every graph imports them.
        contrast_buffer = create_uniform_buffer(REND_RES_PIXEL_SORT_CONTRAST, &ui_contrast, sizeof(PixelSortContrastGPU));
        post_processing_params_buffer = create_uniform_buffer(
            REND_RES_PIXEL_SORTING_POST_PROCESSING_PARAMS, &ui_post_processing_params, sizeof(PixelSortingPostProcessingParamsGPU));
        build_render_graph(orientation);
        render_graph_built = true;
    }

private:
    static constexpr std::string_view PIXEL_SORTING_GRAPH_ID = "pixel_sorting_graph";
    // The `script` resource of the graph recipe, res/assets/scripts/teal/render_graphs/pixel_sorting.tl.
    static constexpr std::string_view PIXEL_SORTING_RECIPE_SCRIPT = "render_graphs.pixel_sorting";

    /**
     * @brief Replace the render graph with one for the orientation selected on the rendering thread.
     *
     * Must be called on the rendering thread, outside of the renderer tick; the renderer releases the previous graph once
     * its frames have finished.
     */
    void rebuild_render_graph() {
        WBE::RendererVK::get_singleton()->unregister_render_graph(PIXEL_SORTING_GRAPH_ID);
        build_render_graph(ui_orientation);
    }

    /**
     * @brief Build, register, and activate the render graph that sorts the lines of p_orientation.
     *
     * The graph is built, and rebuilt, by make_render_graph_desc() from the render_graphs.pixel_sorting recipe script.
     *
     * @param p_orientation The lines of the image to sort.
     * @throws std::runtime_error If a sorted line is too long for the 16-bit pixel sort mask, or the recipe fails to build.
     */
    void build_render_graph(PixelSortOrientation p_orientation) {
        auto* renderer = WBE::RendererVK::get_singleton();
        const VkExtent3D source_extent = source_view->fields().image->fields().extent;
        const bool vertical = p_orientation == PixelSortOrientation::VERTICAL;
        // The mask image stores each masked pixel's rank within its line as R16_UINT, with 0xFFFF marking unmasked pixels.
        const uint32_t line_length = vertical ? source_extent.height : source_extent.width;
        if (line_length >= UINT16_MAX) {
            throw std::runtime_error(std::format(
                "Failed to build the pixel sorting graph: a {}-pixel line is too long for the 16-bit pixel sort mask.", line_length));
        }
        // The graph re-runs the recipe whenever it rebuilds, such as after the first-frame swap chain resize.
        WBE::Ref<WBE::RenderGraphVK> graph =
            renderer->register_render_graph(PIXEL_SORTING_GRAPH_ID, [this, vertical](const WBE::RendGraphContextVK& p_context) {
                return make_render_graph_desc(vertical, p_context);
            });
        result_image = graph->acquire_resource(REND_RES_PIXEL_SORTING_POST_PROCESSING_OUTPUT_IMAGE);
        WBE::write_descriptor_debug_output("pixel_sorting_graph_descriptors", graph->list_descriptors());
        renderer->activate_render_graph(PIXEL_SORTING_GRAPH_ID);
        built_orientation = p_orientation;
    }

    // The parameters configured on the logic thread before setup_render_graph().
    PixelSortContrastGPU contrast;
    PixelSortOrientation orientation = PixelSortOrientation::VERTICAL;
    // Whether setup_render_graph() has run; afterwards the configuration is forwarded to the rendering thread.
    bool render_graph_built = false;
    // The source image and the present pipeline, shared by every graph built for an orientation.
    WBE::Ref<WBE::ROImageViewVK> source_view;
    WBE::Ref<WBE::ROGraphicsPipelineVK> present_pipeline;
    // Every compute pipeline the recipe's tasks use, for both orientations.
    std::vector<WBE::Ref<WBE::ROComputePipelineVK>> compute_pipelines;
    // The orientation selected in the parameter window, and the one the current graph sorts; owned by the rendering
    // thread.
    PixelSortOrientation ui_orientation = PixelSortOrientation::VERTICAL;
    PixelSortOrientation built_orientation = PixelSortOrientation::VERTICAL;
    // The contrast edited in the parameter window; owned by the rendering thread.
    PixelSortContrastGPU ui_contrast;
    // Contrast buffer imported by every graph, updated by the parameter window and by render jobs of the logic thread.
    WBE::Ref<WBE::ROBufferVK> contrast_buffer;
    PixelSortingPostProcessingParamsGPU post_processing_params;
    // The post-processing parameters edited in the parameter window; owned by the rendering thread.
    PixelSortingPostProcessingParamsGPU ui_post_processing_params;
    // Post-processing parameter buffer imported by every graph, updated by the parameter window and by render jobs of the
    // logic thread.
    WBE::Ref<WBE::ROBufferVK> post_processing_params_buffer;
    // Destroyed before the renderer, after the rendering thread has stopped.
    WBE::Unique<WBE::ImGuiOverlayVK> imgui_overlay;
    // Graph-owned container of the post-processed result image, the content of the screenshots.
    WBE::Ref<WBE::RenderObjectContainerVK> result_image;
    // A frame capture whose result image readback has not completed yet.
    struct PendingCapture {
        std::future<std::vector<uint8_t>> readback;
        VkExtent2D extent{};
        WBE::Path path;
    };

    // The captures in progress, owned by the rendering thread: first the readbacks of the result image, then the worker
    // tasks that convert and write them.
    std::deque<PendingCapture> capture_readbacks;
    std::deque<std::future<void>> capture_writes;
    // Whether every frame is captured into sequence_directory; owned by the rendering thread.
    bool recording = false;
    WBE::Path sequence_directory;
    // The number of frames of the current or last recording.
    uint64_t sequence_frame_count = 0;
    // The delta settings before the current recording fixed the delta, restored when it stops.
    DeltaSettings pre_recording_delta_settings;

    bool animated = false;
    double curr_time = 0.0;

    bool is_capture_in_progress() const {
        return !capture_readbacks.empty() || !capture_writes.empty();
    }

    /**
     * @brief Start reading back the result image into a PNG at p_path. Must be called on the rendering thread.
     *
     * The result image is the post-processed image before the present and UI passes, so the UI is not in the capture.
     * The readback copies the result of the previous frame at the start of the next one.
     *
     * @param p_path The PNG file to write; its directory is created when missing.
     * @throws std::runtime_error If the result image is not an image.
     */
    void request_capture(WBE::Path p_path) {
        WBE::Ref<WBE::ROImageVK> image = result_image->get().dynamic_cast_ref<WBE::ROImageVK>();
        if (image == WBE::MEM_NULL) {
            throw std::runtime_error("Failed to capture the pixel sorting result: the result image is not an image.");
        }
        const VkExtent3D extent = image->fields().extent;
        capture_readbacks.push_back(PendingCapture{
            .readback = image->acquire_image_content(),
            .extent = {.width = extent.width, .height = extent.height},
            .path = std::move(p_path),
        });
    }

    /**
     * @brief Start recording every following frame into a new sequence directory. Must be called on the rendering thread.
     *
     * The delta is fixed at 60 frames per second until the recording stops, so the frames advance evenly however long a
     * capture takes.
     */
    void start_recording() {
        pre_recording_delta_settings = get_delta_settings();
        set_delta_settings(DeltaSettings{.is_fixed = true, .sleep_to_delta = true, .fixed_delta = PIXEL_SORT_RECORDING_FIXED_DELTA});
        sequence_directory = make_sequence_directory();
        sequence_frame_count = 0;
        recording = true;
        WBE::stdout_log(WBE::WBE_CHANNEL_DEBUG)
            ->message(std::format("Recording pixel sorting frames to {}.", sequence_directory.string()));
    }

    /**
     * @brief Stop recording and restore the delta settings from before it; the captures in flight are still written. Must
     * be called on the rendering thread.
     */
    void stop_recording() {
        recording = false;
        set_delta_settings(pre_recording_delta_settings);
        WBE::stdout_log(WBE::WBE_CHANNEL_DEBUG)->message(std::format("Stopped recording pixel sorting frames: {} captured.", sequence_frame_count));
    }

    /**
     * @brief Capture the frame rendered next as the next frame of the sequence. Must be called on the rendering thread.
     *
     * The frames are numbered consecutively from 0, and none is skipped: when too many captures are in flight, this waits
     * for the oldest, so recording slows rendering down to the capture rate. Every earlier readback was submitted by a
     * renderer tick since it was requested, so the wait does not depend on the rendering thread.
     *
     * @throws std::runtime_error If a readback, conversion, or PNG write failed.
     */
    void record_frame() {
        while (capture_readbacks.size() + capture_writes.size() >= PIXEL_SORT_MAX_CAPTURES_IN_FLIGHT) {
            if (!capture_writes.empty()) {
                capture_writes.front().wait();
            } else {
                capture_readbacks.front().readback.wait();
            }
            poll_captures();
        }
        request_capture(sequence_directory / std::format("frame_{:06}.png", sequence_frame_count));
        ++sequence_frame_count;
    }

    /**
     * @brief Advance the captures in progress: hand completed readbacks to a worker, and collect completed writes.
     *
     * Must be called on the rendering thread.
     *
     * @throws std::runtime_error If a readback, conversion, or PNG write failed.
     */
    void poll_captures() {
        constexpr std::chrono::seconds NO_WAIT{0};
        while (!capture_readbacks.empty() && capture_readbacks.front().readback.wait_for(NO_WAIT) == std::future_status::ready) {
            PendingCapture capture = std::move(capture_readbacks.front());
            capture_readbacks.pop_front();
            std::vector<uint8_t> result_data = capture.readback.get();
            const bool encode_srgb = is_srgb_format(WBE::RendererVK::get_singleton()->get_render_context()->swap_chain_format);
            // Converting and encoding a full-size image takes long enough to stall a frame, so a worker does it.
            capture_writes.push_back(WBE::Worker::get_singleton()->submit_task(
                [result_data = std::move(result_data), extent = capture.extent, encode_srgb, path = std::move(capture.path)] {
                    const std::vector<uint8_t> rgba8 = convert_result_to_rgba8(result_data, encode_srgb);
                    std::filesystem::create_directories(path.parent_path());
                    WBE::write_image_data_png(path, rgba8, extent.width, extent.height, 4);
                    WBE::stdout_log(WBE::WBE_CHANNEL_DEBUG)->message(std::format("Saved pixel sorting capture to {}.", path.string()));
                }));
        }
        while (!capture_writes.empty() && capture_writes.front().wait_for(NO_WAIT) == std::future_status::ready) {
            capture_writes.front().get();
            capture_writes.pop_front();
        }
    }

    /**
     * @brief Draw the parameter window and upload the contrast and post-processing parameters when they change. Must be called on the rendering thread.
     */
    void draw_parameter_window(double p_delta) {
        static constexpr std::array<const char*, 3> SORT_METRIC_NAMES{"Luminance", "Hue", "Saturation"};
        bool changed = false;
        ImGui::Begin("Pixel Sorting", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        changed |= ImGui::SliderFloat("Min metric", &ui_contrast.min_metric, 0.0F, 1.0F);
        changed |= ImGui::SliderFloat("Max metric", &ui_contrast.max_metric, 0.0F, 1.0F);
        ImGui::Checkbox("Start animation", &animated);
        curr_time += p_delta;
        if (animated) {
            changed = true;
            ui_contrast.min_metric = static_cast<float>(std::abs(std::sin(curr_time * 0.25)));
        }
        int sort_metric = static_cast<int>(ui_contrast.sort_metric);
        // Switching rebuilds the graph, which would release the result image of a capture in progress.
        static constexpr std::array<const char*, 2> ORIENTATION_NAMES{"Vertical", "Horizontal"};
        int selected_orientation = static_cast<int>(ui_orientation);
        ImGui::BeginDisabled(recording || is_capture_in_progress());
        if (ImGui::Combo("Orientation", &selected_orientation, ORIENTATION_NAMES.data(), static_cast<int>(ORIENTATION_NAMES.size()))) {
            ui_orientation = static_cast<PixelSortOrientation>(selected_orientation);
        }
        ImGui::EndDisabled();
        if (ImGui::Combo("Sort metric", &sort_metric, SORT_METRIC_NAMES.data(), static_cast<int>(SORT_METRIC_NAMES.size()))) {
            ui_contrast.sort_metric = static_cast<uint32_t>(sort_metric);
            changed = true;
        }
        if (ImGui::SliderFloat("Gamma", &ui_post_processing_params.gamma, 0.1F, 4.0F)) {
            ui_post_processing_params.gamma = std::max(ui_post_processing_params.gamma, 0.1F);
            post_processing_params_buffer->update_buffer(&ui_post_processing_params, sizeof(PixelSortingPostProcessingParamsGPU));
        }
        const bool capture_in_progress = is_capture_in_progress();
        ImGui::BeginDisabled(recording);
        if (ImGui::Button("Screenshot")) {
            request_capture(make_screenshot_path());
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(recording ? "Stop recording" : "Record")) {
            if (recording) {
                stop_recording();
            } else {
                start_recording();
            }
        }
        if (capture_in_progress) {
            ImGui::SameLine();
            ImGui::TextUnformatted("Saving...");
        }
        if (recording || sequence_frame_count != 0) {
            ImGui::Text("%s %llu frames", recording ? "Recording" : "Recorded", static_cast<unsigned long long>(sequence_frame_count));
        }
        // The measured frame rate reflects the fixed delta rather than the real frame cost.
        if (!get_delta_settings().is_fixed) {
            ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0F / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        }
        ImGui::End();
        if (!changed) {
            return;
        }
        ui_contrast.max_metric = std::max(ui_contrast.min_metric, ui_contrast.max_metric);
        contrast_buffer->update_buffer(&ui_contrast, sizeof(PixelSortContrastGPU));
    }

    /**
     * @brief Run the recipe for the orientation and the graph context, supplying the ImGui overlay task it imports.
     * @param p_vertical Whether the columns, rather than the rows, are sorted.
     * @param p_context The context the graph is being built for.
     * @return The graph descriptor.
     */
    WBE::RendGraphDescVK make_render_graph_desc(bool p_vertical, const WBE::RendGraphContextVK& p_context) {
        auto* render_allocator = get_renderer_allocator().get();
        auto* renderer = WBE::RendererVK::get_singleton();
        const auto* render_context = renderer->get_render_context();
        const VkExtent3D source_extent = source_view->fields().image->fields().extent;
        WBE::PixelSortingRecipeContext recipe_context;
        static_cast<WBE::RendRecipeContextVK&>(recipe_context) = WBE::RendGraphScriptVK::get_recipe_context(*render_context, p_context);
        recipe_context.source_width = source_extent.width;
        recipe_context.source_height = source_extent.height;
        recipe_context.orientation = p_vertical ? WBE::PixelSortOrientation::VERTICAL : WBE::PixelSortOrientation::HORIZONTAL;
        const WBE::DataValue context = WBE::RendGraphScriptVK::serialize_context(recipe_context);
        const WBE::DataValue recipe = WBE::RendGraphScriptVK::run(PIXEL_SORTING_RECIPE_SCRIPT, context);

        const auto swap_chain = renderer->acquire_swap_chain_image_view_container();
        // The ImGui overlay is owned by the application, so the recipe imports its task.
        WBE::RendTaskDescVK imgui_task_desc(render_allocator);
        imgui_task_desc.task_id = RT_DRAW_IMGUI;
        imgui_task_desc.alias(WBE::RenderTargetsVK::color_id(0), WBE::REND_RES_SWAP_CHAIN);
        imgui_task_desc.factory = [this, render_allocator, render_context](const WBE::RendGraphContextVK& /*p_context*/) {
            return WBE::make_unique<WBE::RTImGuiVK>(render_allocator, render_context, imgui_overlay.get());
        };
        WBE::RendGraphRecipeImportsVK imports;
        imports.add(WBE::REND_RES_SWAP_CHAIN, *swap_chain)
            .add(REND_RES_PIXEL_SORT_SOURCE, *source_view)
            .add(REND_RES_PIXEL_SORT_CONTRAST, *contrast_buffer)
            .add(REND_RES_PIXEL_SORTING_POST_PROCESSING_PARAMS, *post_processing_params_buffer)
            .add_task(std::move(imgui_task_desc));
        WBE::RendGraphDescVK pipeline_desc = WBE::RendGraphRecipeBuilderVK().build(recipe, imports, render_context);
        return pipeline_desc;
    }

    /**
     * @brief Create a host-visible uniform buffer holding p_data and register it in the render database under p_key.
     * @param p_key The render database key, which is also the name the recipe imports the buffer under.
     * @param p_data The initial content.
     * @param p_size The size of the buffer, in bytes.
     * @return The buffer; the render database holds it weakly.
     */
    static WBE::Ref<WBE::ROBufferVK> create_uniform_buffer(std::string_view p_key, const void* p_data, size_t p_size) {
        auto* renderer = WBE::RendererVK::get_singleton();
        WBE::Ref<WBE::ROBufferVK> buffer = renderer->get_render_context()->create_buffer(WBE::ROBufferVK::Builder()
                                               .initial_data(p_data, p_size, WBE::DeviceDomain::HOST_DEVICE)
                                               .set_size(p_size)
                                               .set_domain(WBE::DeviceDomain::HOST_DEVICE)
                                               .add_usage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT));
        renderer->get_render_database()->register_compiled_render_resource(p_key, buffer);
        return buffer;
    }

    /**
     * @brief Compile a compute shader resource.
     * @param p_shader_id The resource id of the compute shader.
     * @return The compiled compute pipeline.
     * @throws std::runtime_error If the shader resource is not found.
     */
    WBE::Ref<WBE::ROComputePipelineVK> compile_compute_pipeline(std::string_view p_shader_id) {
        auto* renderer = WBE::RendererVK::get_singleton();
        auto shader_resource = WBE::ResourceManager::get_singleton()->get_resource<WBE::RUShader>(p_shader_id);
        if (shader_resource == nullptr) {
            throw std::runtime_error(std::format("Failed to setup pixel sorting test: shader resource {} was not found.", p_shader_id));
        }
        WBE::ComputePipelineCompilerVKContext compute_compiler_context{
            .allocator = get_renderer_allocator().get(),
            .render_data = renderer->get_render_context(),
            .render_database = renderer->get_render_database(),
        };
        return WBE::ComputePipelineCompilerVK(compute_compiler_context).compile(p_shader_id, shader_resource).compute_pipeline;
    }
};

int main(int p_argc, char* p_argv[]) {
    WBE::on_exec_test_start();
    std::unique_ptr<WBE::Global> global = std::make_unique<WBE::Global>(p_argc, p_argv, WBE::Path("test_env"));
    {
        ApplicationVulkanPixelSortingTest game_application;
        {
            WBE_START_PROFILE(WBE::WBE_CHANNEL_GLOBAL, "Main profiler");
            game_application.setup_render_graph();
            game_application.run();
            size_t renderer_alloc_max = game_application.get_renderer_allocator()->get_internal_fragmentation_tracker();
            WBE::stdout_log(WBE::WBE_CHANNEL_DEBUG)->message(std::format("Renderer allocator max used: {}", renderer_alloc_max));
        }
    }
    const auto& global_profiles = global->engine_core->profiling_manager->get_profile_data(WBE::WBE_CHANNEL_GLOBAL);
    for (const auto& profile : global_profiles) {
        WBE::stdout_log(WBE::WBE_CHANNEL_GLOBAL)->message(std::format("{}: Delta: {} seconds", profile.message, profile.delta));
    }
    return EXIT_SUCCESS;
}
