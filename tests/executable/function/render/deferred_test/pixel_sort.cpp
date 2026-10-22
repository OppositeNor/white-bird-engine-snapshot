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
#include "pixel_sort.hh"
#include "../render_test.hh"
#include "core/allocator/i_allocator.hh"
#include "core/data/data_value.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/function_utils.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/compute_pipeline_compiler_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/rend_graph_recipe_builder_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include "generated/serializables_sd.gen.hh"
#include "i_post_processing.hh"
#include "pixel_sort_recipe_params.hh"
#include "resource/resource_manager/resource_manager.hh"
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

// The Lua module that adds the effect's resources and tasks to a recipe.
constexpr std::string_view PIXEL_SORT_RECIPE_MODULE = "render_graph_utils.features.pixel_sort";
// The import name, and render database key, of the contrast buffer.
constexpr std::string_view REND_RES_PIXEL_SORT_CONTRAST = "pixel_sort_contrast";

// GPU layout of PixelSortContrast in pixel_sort_utils.slang.
struct PixelSortContrastGPU {
    float min_metric = 0.0F;
    float max_metric = 0.0F;
    uint32_t sort_metric = static_cast<uint32_t>(PixelSortMetric::SATURATION);
    uint32_t sort_scope = static_cast<uint32_t>(PixelSortScope::LINE);
};

PixelSortContrastGPU make_contrast(const PixelSortSettings& p_settings) {
    return PixelSortContrastGPU{
        .min_metric = p_settings.min_metric,
        .max_metric = p_settings.max_metric,
        .sort_metric = static_cast<uint32_t>(p_settings.metric),
        .sort_scope = static_cast<uint32_t>(p_settings.scope),
    };
}

size_t direction_index(PixelSortDirection p_direction) {
    return p_direction == PixelSortDirection::VERTICAL ? 0 : 1;
}

std::string_view direction_name(PixelSortDirection p_direction) {
    return p_direction == PixelSortDirection::VERTICAL ? "vertical" : "horizontal";
}

std::string make_bit_region_name(uint32_t p_pass) {
    return std::format("{}_{}", p_pass * 8, (p_pass + 1) * 8);
}

// The id of the variant of a direction-dependent pixel sort shader, e.g. count_pixel_sort_mask_vertical_shader.
std::string make_direction_shader_id(std::string_view p_shader_name, PixelSortDirection p_direction) {
    return std::format("{}_{}_shader", p_shader_name, direction_name(p_direction));
}

} // namespace

void PixelSort::validate_layout(VkExtent2D p_extent, PixelSortDirection p_direction) {
    const bool vertical = p_direction == PixelSortDirection::VERTICAL;
    const uint32_t line_length = vertical ? p_extent.height : p_extent.width;
    const uint64_t element_count = static_cast<uint64_t>(p_extent.width) * p_extent.height;
    // The mask image stores each masked pixel's rank within its line, and with the span scope its span offset and span
    // length, as 16-bit channels, with 0xFFFF marking unmasked pixels.
    if (line_length >= UINT16_MAX) {
        throw std::runtime_error(std::format("Failed to lay out the pixel sort post-processing: a {}-pixel sorted line is too long for the 16-bit pixel sort mask.",
            line_length));
    }
    if (element_count == 0) {
        throw std::runtime_error(std::format(
            "Failed to lay out the pixel sort post-processing: a {}x{} image is empty.", p_extent.width, p_extent.height));
    }
    // A key packs the line, metric, and position along the line, so at least 2 metric levels must fit in 32 bits.
    if (UINT32_MAX / element_count < 2) {
        throw std::runtime_error(std::format("Failed to lay out the pixel sort post-processing: a {}x{} image is too large to pack its sort keys into 32 bits.",
            p_extent.width,
            p_extent.height));
    }
}

PixelSort::PixelSort(
    RenderAllocator* p_allocator, RendererVK* p_renderer, RenderTestExtentFactory p_extent, VkFormat p_color_format, const PixelSortSettings& p_settings)
    : IPostProcessing(p_allocator, TOGGLE_EVENT_ID), renderer(p_renderer), extent(std::move(p_extent)),
      color_format(p_color_format) {
    if (renderer == nullptr) {
        throw std::runtime_error("Failed to create the pixel sort post-processing: renderer is null.");
    }
    if (extent == nullptr) {
        throw std::runtime_error("Failed to create the pixel sort post-processing: extent factory is null.");
    }
    static_cast<void>(set_settings(p_settings));
    // Recipes cannot set initial data, so the effect owns the contrast buffer and every graph imports it.
    const PixelSortContrastGPU contrast = make_contrast(settings);
    contrast_buffer = renderer->get_render_context()->create_buffer(ROBufferVK::Builder()
                          .initial_data(&contrast, sizeof(contrast), DeviceDomain::HOST_DEVICE)
                          .set_size(sizeof(contrast))
                          .set_domain(DeviceDomain::HOST_DEVICE)
                          .add_usage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT));
    renderer->get_render_database()->register_compiled_render_resource(REND_RES_PIXEL_SORT_CONTRAST, contrast_buffer);
    copy_source_pipeline = compile("copy_pixel_sort_source_shader");
    count_mask_pipelines = compile_direction_variants("count_pixel_sort_mask");
    prefix_sum_columns_pipeline = compile("prefix_sum_pixel_sort_columns_shader");
    register_keys_pipelines = compile_direction_variants("register_pixel_sort_keys");
    write_sorted_pixels_pipelines = compile_direction_variants("write_sorted_pixels");
    radix_sort_prepare_dispatch_pipeline = compile("radix_sort_prepare_dispatch");
    histogram_prefix_sum_pipeline = compile("radix_sort_histogram_prefix_sum");
    histogram_overflow_prefix_sum_pipeline = compile("radix_sort_histogram_overflow_prefix_sum");
    histogram_add_overflow_pipeline = compile("radix_sort_histogram_add_overflow");
    for (uint32_t pass = 0; pass < RADIX_SORT_PASS_COUNT; ++pass) {
        const std::string bit_region = make_bit_region_name(pass);
        histogram_pipelines[pass] = compile(std::format("radix_sort_histogram_workgroup_process_{}", bit_region));
        scatter_pipelines[pass] = compile(std::format("radix_sort_scatter_{}", bit_region));
    }
}

void PixelSort::validate_settings(const PixelSortSettings& p_settings) {
    if (p_settings.metric != PixelSortMetric::LUMINANCE && p_settings.metric != PixelSortMetric::HUE &&
        p_settings.metric != PixelSortMetric::SATURATION) {
        throw std::runtime_error(
            std::format("Invalid pixel sort settings: {} is not a pixel sort metric.", static_cast<uint32_t>(p_settings.metric)));
    }
    if (p_settings.scope != PixelSortScope::LINE && p_settings.scope != PixelSortScope::SPAN) {
        throw std::runtime_error(
            std::format("Invalid pixel sort settings: {} is not a pixel sort scope.", static_cast<uint32_t>(p_settings.scope)));
    }
    if (p_settings.direction != PixelSortDirection::VERTICAL && p_settings.direction != PixelSortDirection::HORIZONTAL) {
        throw std::runtime_error(std::format(
            "Invalid pixel sort settings: {} is not a pixel sort direction.", static_cast<uint32_t>(p_settings.direction)));
    }
    if (p_settings.min_metric < 0.0F) {
        throw std::runtime_error(std::format("Invalid pixel sort settings: the minimum metric {} is negative.", p_settings.min_metric));
    }
    if (p_settings.min_metric > p_settings.max_metric) {
        throw std::runtime_error(std::format("Invalid pixel sort settings: the minimum metric {} exceeds the maximum metric {}.",
            p_settings.min_metric,
            p_settings.max_metric));
    }
}

bool PixelSort::set_settings(const PixelSortSettings& p_settings) {
    validate_settings(p_settings);
    // Reject a direction whose lines are too long for the extent of the current renderer state before any graph uses it.
    validate_layout(extent(RendGraphContextVK::from_render_data(*renderer->get_render_context())), p_settings.direction);
    const bool requires_rebuild = p_settings.direction != settings.direction || p_settings.scope != settings.scope;
    settings = p_settings;
    return requires_rebuild;
}

void PixelSort::upload_settings() {
    const PixelSortContrastGPU contrast = make_contrast(settings);
    contrast_buffer->update_buffer(&contrast, sizeof(contrast));
}

void PixelSort::configure_recipe(DataValue& p_parameters, RendGraphRecipeImportsVK& p_imports) const {
    PixelSortRecipeParams params;
    params.module = PIXEL_SORT_RECIPE_MODULE;
    params.direction = settings.direction;
    params.scope = settings.scope;
    params.color_format = static_cast<ImageFormatVK>(color_format);
    params.queue_family = QueueFamilyVK::COMPUTE_1;
    params.contrast_import = REND_RES_PIXEL_SORT_CONTRAST;
    params.toggle_event = get_toggle_event_id();
    params.enabled = is_enabled();
    SerializableSD<PixelSortRecipeParams>::serialize(p_parameters, params);
    p_imports.add(REND_RES_PIXEL_SORT_CONTRAST, *contrast_buffer);
}

Ref<ROComputePipelineVK> PixelSort::compile(std::string_view p_shader_id) const {
    auto shader_resource = ResourceManager::get_singleton()->get_resource<RUShader>(p_shader_id);
    if (shader_resource == nullptr) {
        throw std::runtime_error(
            std::format("Failed to create the pixel sort post-processing: shader resource {} was not found.", p_shader_id));
    }
    ComputePipelineCompilerVKContext compute_compiler_context{
        .allocator = get_allocator(),
        .render_data = renderer->get_render_context(),
        .render_database = renderer->get_render_database(),
    };
    return ComputePipelineCompilerVK(compute_compiler_context).compile(p_shader_id, shader_resource).compute_pipeline;
}

PixelSort::DirectionPipelines PixelSort::compile_direction_variants(std::string_view p_shader_name) const {
    DirectionPipelines pipelines;
    pipelines[direction_index(PixelSortDirection::VERTICAL)] =
        compile(make_direction_shader_id(p_shader_name, PixelSortDirection::VERTICAL));
    pipelines[direction_index(PixelSortDirection::HORIZONTAL)] =
        compile(make_direction_shader_id(p_shader_name, PixelSortDirection::HORIZONTAL));
    return pipelines;
}

} // namespace WhiteBirdEngine
