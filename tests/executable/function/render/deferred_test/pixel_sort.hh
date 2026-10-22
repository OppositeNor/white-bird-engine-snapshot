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
#ifndef WBE_FILE_PIXEL_SORT_HH
#define WBE_FILE_PIXEL_SORT_HH

#include "core/data/data_value.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/rend_graph_recipe_builder_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "i_post_processing.hh"
#include "pixel_sort_recipe_params.hh"
#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @enum PixelSortMetric
 * @brief The value the pixel sort orders the pixels of every line by.
 * @details The values must match the WBE_PIXEL_SORT_METRIC_ macros in pixel_sort_utils.slang.
 */
enum class PixelSortMetric : uint32_t {
    /** Sort by luminance, darkest first. */
    LUMINANCE = 0,
    /** Sort by hue, from red through green and blue to magenta. */
    HUE = 1,
    /** Sort by saturation, grayest first. */
    SATURATION = 2,
};

/**
 * @class PixelSortSettings
 * @brief Configuration of the pixel sort post-processing.
 */
struct PixelSortSettings {
    /** Whether the columns or the rows of the image are sorted. */
    PixelSortDirection direction = PixelSortDirection::VERTICAL;
    /** The value the pixels of every line are ordered and masked by. */
    PixelSortMetric metric = PixelSortMetric::SATURATION;
    /** Whether each sort covers a whole line or one span of consecutive masked pixels. */
    PixelSortScope scope = PixelSortScope::LINE;
    /** The smallest sort metric of a sorted pixel. */
    float min_metric = 0.3F;
    /** The largest sort metric of a sorted pixel. */
    float max_metric = 1.1F;
};

/**
 * @class PixelSort
 * @brief Post-processing effect that sorts, by a selectable metric, the pixels of every column or row of the color image
 * whose sort metric is inside a contrast range, either across the whole line or within every span of consecutive masked
 * pixels.
 * @details Copies the color image, counts the masked pixels of every sorted line, turns the counts into the range of
 * every line in a key buffer, registers the masked pixels into those ranges, radix sorts the keys in 4 passes of 8 bits, and
 * writes every masked pixel back from the sorted key at its registered index (see the pixel_sort and sort shaders). The
 * Lua module render_graph_utils.features.pixel_sort adds those tasks and their graph-owned resources to a recipe, so every
 * graph the effect is added to allocates its own; the effect compiles the pipelines they reference and owns the contrast
 * buffer every graph imports. Neither copyable nor movable.
 */
class PixelSort final : public IPostProcessing {
public:
    /**
     * @brief Event id the effect's tasks receive RTToggleVK::Command events under.
     */
    static constexpr std::string_view TOGGLE_EVENT_ID = "pixel_sort.toggle";

    PixelSort() = delete;
    /**
     * @brief Compile the effect's compute pipelines.
     * @param p_allocator Allocator of the effect's pipelines and tasks; must outlive the effect.
     * @param p_renderer The renderer; must outlive the effect.
     * @param p_extent Factory of the extent of the color images the effect is applied to. Every graph the effect is added to
     * evaluates it against its render graph context, and resizes the effect's resources and dispatches with it.
     * @param p_color_format Format of the color images the effect is applied to.
     * @param p_settings The initial configuration of the effect.
     * @throws std::runtime_error If a shader resource is missing, p_extent is null, or p_settings is invalid (see
     * set_settings()). Graphs throw the same extent errors when a later context yields an invalid extent.
     */
    PixelSort(RenderAllocator* p_allocator,
        RendererVK* p_renderer,
        std::function<VkExtent2D(const RendGraphContextVK&)> p_extent,
        VkFormat p_color_format,
        const PixelSortSettings& p_settings);
    virtual ~PixelSort() override = default;
    PixelSort(const PixelSort&) = delete;
    PixelSort(PixelSort&&) = delete;
    PixelSort& operator=(const PixelSort&) = delete;
    PixelSort& operator=(PixelSort&&) = delete;

    virtual std::string_view get_name() const override {
        return "pixel sort";
    }

    /**
     * @brief Describe the effect to the render_graph_utils.features.pixel_sort module, and supply the contrast buffer.
     *
     * Writes the current direction, scope, color format, queue family, toggle event, and enabled state.
     */
    virtual void configure_recipe(DataValue& p_parameters, RendGraphRecipeImportsVK& p_imports) const override;

    /**
     * @brief Get the configuration of the effect.
     * @return The current configuration.
     */
    const PixelSortSettings& get_settings() const {
        return settings;
    }

    /**
     * @brief Change the configuration of the effect.
     *
     * A changed direction or scope changes the effect's pipelines, resources, and dispatches: every graph the effect was
     * added to must then be rebuilt with RenderGraphVK::rebuild() before it executes again. The metric, range, and scope
     * reach the shaders once upload_settings() writes them to the contrast buffer.
     *
     * @param p_settings The new configuration.
     * @return True if every graph the effect was added to must be rebuilt.
     * @throws std::runtime_error If p_settings.metric is not a PixelSortMetric value, p_settings.scope is not a
     * PixelSortScope value, the metric range is negative or reversed, or a line of the extent of the current renderer state
     * in p_settings.direction is too long for the 16-bit pixel sort mask.
     */
    bool set_settings(const PixelSortSettings& p_settings);

    /**
     * @brief Write the metric, range, and scope of the configuration to the contrast buffer every graph imports.
     */
    void upload_settings();

private:
    // Each pass sorts by one 8-bit digit of the 32-bit keys.
    static constexpr uint32_t RADIX_SORT_PASS_COUNT = 4;

    // One pipeline per PixelSortDirection value, indexed by the direction.
    using DirectionPipelines = std::array<Ref<ROComputePipelineVK>, 2>;

    // Validates that an extent can be sorted in a direction.
    static void validate_layout(VkExtent2D p_extent, PixelSortDirection p_direction);
    static void validate_settings(const PixelSortSettings& p_settings);
    Ref<ROComputePipelineVK> compile(std::string_view p_shader_id) const;
    DirectionPipelines compile_direction_variants(std::string_view p_shader_name) const;

    RendererVK* renderer = nullptr;
    std::function<VkExtent2D(const RendGraphContextVK&)> extent;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    PixelSortSettings settings;
    // The contrast uniform buffer every graph imports as REND_RES_PIXEL_SORT_CONTRAST; upload_settings() writes it.
    Ref<ROBufferVK> contrast_buffer;
    Ref<ROComputePipelineVK> copy_source_pipeline;
    DirectionPipelines count_mask_pipelines;
    Ref<ROComputePipelineVK> prefix_sum_columns_pipeline;
    DirectionPipelines register_keys_pipelines;
    DirectionPipelines write_sorted_pixels_pipelines;
    Ref<ROComputePipelineVK> radix_sort_prepare_dispatch_pipeline;
    Ref<ROComputePipelineVK> histogram_prefix_sum_pipeline;
    Ref<ROComputePipelineVK> histogram_overflow_prefix_sum_pipeline;
    Ref<ROComputePipelineVK> histogram_add_overflow_pipeline;
    std::array<Ref<ROComputePipelineVK>, RADIX_SORT_PASS_COUNT> histogram_pipelines;
    std::array<Ref<ROComputePipelineVK>, RADIX_SORT_PASS_COUNT> scatter_pipelines;
};

} // namespace WhiteBirdEngine

#endif
