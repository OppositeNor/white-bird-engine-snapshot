-- Copyright 2025 OppositeNor
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

-- In-place pixel sort post-processing of a color image, for recipes that include it as a feature. Sorts the masked pixels
-- of every column or row with the GPU radix sort. The application compiles the pipelines and owns the contrast buffer;
-- see PixelSort in tests/executable/function/render/deferred_test/pixel_sort.hh.

local type wbe = require("wbe")
local math_utils = require("utils.math")
local Graph = require("render_graph_utils.render_graph_utils")
local sort = require("render_graph_utils.sort")

local ceil_div = math_utils.ceil_div

-- Must match the numthreads of copy_pixel_sort_source.slang and write_sorted_pixels.slang.
local PIXEL_SORT_TILE_SIZE = 8
-- Must match the numthreads of count_pixel_sort_mask.slang and register_pixel_sort_keys.slang, which run one invocation
-- per sorted line.
local PIXEL_SORT_COLUMN_WORKGROUP_SIZE = 64
-- Must match WBE_PIXEL_SORT_COLUMN_OFFSETS_START in pixel_sort_utils.slang.
local PIXEL_SORT_COLUMN_OFFSETS_START = 4
local UINT32_SIZE = 4
local UINT16_MAX = 65535
local UINT32_MAX = 4294967295

-- The registered keys, sorted in place, and the radix sort's scratch buffers.
local RADIX_SORT_BUFFER_NAMES: sort.RadixSortBufferNames = {
    keys = "pixel_sort_keys",
    keys_swap = "pixel_sort_keys_swap",
    histogram = "pixel_sort_histogram",
    dispatch = "pixel_sort_radix_sort_dispatch",
}

local record pixel_sort
    record Target
        -- The color image view sorted in place, bound to the subgraph's `color` port; it must be sampled
        -- and storage.
        color_view: Graph.Resource
        -- The width of the color image.
        width: integer
        -- The height of the color image.
        height: integer
    end
end

-- The effect as a subgraph task with id p_id, which sorts the target's color view in place through its `color` port. The
-- params' toggle gates all of its tasks.
function pixel_sort.subgraph(p_id: string, p_params: wbe.PixelSortRecipeParams, p_target: pixel_sort.Target): Graph.Task
    local direction = Graph.enum_name(p_params.direction)
    local vertical = direction == "VERTICAL"
    if not vertical and direction ~= "HORIZONTAL" then
        error("pixel_sort: unknown direction '" .. direction .. "'.")
    end
    local scope = Graph.enum_name(p_params.scope)
    if scope ~= "LINE" and scope ~= "SPAN" then
        error("pixel_sort: unknown scope '" .. scope .. "'.")
    end
    local width = p_target.width
    local height = p_target.height
    local line_length = vertical and height or width
    local element_count = width * height
    -- The mask image stores each masked pixel's rank within its line, and with the span scope its span offset and span
    -- length, as 16-bit channels, with 0xFFFF marking unmasked pixels.
    if line_length >= UINT16_MAX then
        error(string.format("pixel_sort: a %d-pixel sorted line is too long for the 16-bit pixel sort mask.", line_length))
    end
    if element_count == 0 then
        error(string.format("pixel_sort: a %dx%d image is empty.", width, height))
    end
    -- A key packs the line, metric, and position along the line, so at least 2 metric levels must fit in 32 bits.
    if UINT32_MAX // element_count < 2 then
        error(string.format("pixel_sort: a %dx%d image is too large to pack its sort keys into 32 bits.", width, height))
    end

    local line_count = vertical and width or height
    local tile_group_count_x = ceil_div(width, PIXEL_SORT_TILE_SIZE)
    local tile_group_count_y = ceil_div(height, PIXEL_SORT_TILE_SIZE)
    local column_group_count = ceil_div(line_count, PIXEL_SORT_COLUMN_WORKGROUP_SIZE)

    local graph = Graph.new()
    local color = graph:port("color")
    local contrast = graph:import_resource(p_params.contrast_import)
    local source = graph:add_storage_image("pixel_sort_source_image", p_params.color_format, width, height)
        :view("pixel_sort_source")
    local mask = graph:add_storage_image(
        "pixel_sort_mask_image", scope == "SPAN" and "r16g16b16a16_uint" or "r16_uint", width, height):view("pixel_sort_mask")
    local column_offsets = graph:add_storage_buffer(
        "pixel_sort_column_offsets", (PIXEL_SORT_COLUMN_OFFSETS_START + line_count) * UINT32_SIZE)
    -- The sort is sized for every pixel inside the mask.
    local sort_buffers = sort.radix_sort_buffers(RADIX_SORT_BUFFER_NAMES, element_count, graph)

    local direction_suffix = "_" .. string.lower(direction) .. "_shader"
    local copy_source_pipeline = graph:add_compute_pipeline("copy_pixel_sort_source_shader")
    local count_mask_pipeline = graph:add_compute_pipeline("count_pixel_sort_mask" .. direction_suffix)
    local prefix_sum_pipeline = graph:add_compute_pipeline("prefix_sum_pixel_sort_columns_shader")
    local register_keys_pipeline = graph:add_compute_pipeline("register_pixel_sort_keys" .. direction_suffix)
    local write_pixels_pipeline = graph:add_compute_pipeline("write_sorted_pixels" .. direction_suffix)

    local copy_source = graph:compute("copy_pixel_sort_source", {
        pipeline = copy_source_pipeline,
        group_count_x = tile_group_count_x,
        group_count_y = tile_group_count_y,
        queue_family = p_params.queue_family,
    }, { uses = { source } }):alias("pixel_sort_input", color)
    local count_mask = graph:compute("count_pixel_sort_mask", {
        pipeline = count_mask_pipeline,
        group_count_x = column_group_count,
        group_count_y = 1,
        queue_family = p_params.queue_family,
    }, { uses = { source, contrast, column_offsets } })
    -- The prefix sums run in a single workgroup.
    local prefix_sum = graph:compute("prefix_sum_pixel_sort_columns", {
        pipeline = prefix_sum_pipeline,
        group_count_x = 1,
        group_count_y = 1,
        queue_family = p_params.queue_family,
    }, { uses = { column_offsets } })
    local register_keys = graph:compute("register_pixel_sort_keys", {
        pipeline = register_keys_pipeline,
        group_count_x = column_group_count,
        group_count_y = 1,
        queue_family = p_params.queue_family,
    }, { uses = { source, contrast, column_offsets, sort_buffers.keys, mask } })

    local radix_sort = sort.add_radix_sort_task(graph, "pixel_sort_radix_sort", sort_buffers, p_params.queue_family)
    local write_pixels = graph:compute("write_sorted_pixels", {
        pipeline = write_pixels_pipeline,
        group_count_x = tile_group_count_x,
        group_count_y = tile_group_count_y,
        queue_family = p_params.queue_family,
    }, { uses = { source, sort_buffers.keys, mask, contrast, column_offsets } }):alias("pixel_sort_destination", color)

    graph:chain(color, { copy_source, write_pixels })
    graph:chain(source, { copy_source, count_mask, register_keys, write_pixels })
    graph:chain(column_offsets, { count_mask, prefix_sum, register_keys, write_pixels })
    graph:chain(mask, { register_keys, write_pixels })
    graph:chain(sort_buffers.keys, { register_keys, radix_sort, write_pixels })
    local subgraph = graph:as_subgraph(p_id, { color = p_target.color_view })
    subgraph.toggle = { event = p_params.toggle_event, enabled = p_params.enabled }
    return subgraph
end

return pixel_sort
