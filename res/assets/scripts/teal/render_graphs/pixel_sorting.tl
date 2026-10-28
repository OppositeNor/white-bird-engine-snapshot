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

-- Sorts the masked pixels of every column or row of the source image with the GPU radix sort and presents the result.
-- See ApplicationVulkanPixelSortingTest in tests/executable/function/render/vulkan_pixel_sorting_test.cpp.
--
-- The application supplies a wbe.PixelSortingRecipeContext.
-- Imports: `swap_chain`, `pixel_sort_source` (the source image view), and the application-owned uniform buffers
-- `pixel_sort_contrast` and `pixel_sorting_post_processing_params`.
-- Imported tasks: `draw_imgui`, the application's ImGui overlay, drawn after `present_sorted_pixels`.

-- Must match the numthreads of write_sorted_pixels.slang and pixel_sorting_post_processing.slang.
local PIXEL_SORT_TILE_SIZE = 16
-- Must match the numthreads of count_pixel_sort_mask.slang and register_pixel_sort_keys.slang, which run one invocation
-- per line.
local PIXEL_SORT_COLUMN_WORKGROUP_SIZE = 64
-- Must match WBE_PIXEL_SORT_COLUMN_OFFSETS_START in pixel_sort_utils.slang.
local PIXEL_SORT_COLUMN_OFFSETS_START = 4
local UINT32_SIZE = 4

local type wbe = require("wbe")
local math_utils = require("utils.math")
local Graph = require("render_graph_utils.render_graph_utils")
local sort = require("render_graph_utils.sort")

local ceil_div = math_utils.ceil_div

-- The sizes and dispatch counts derived from the source image.
local record Layout
    width: integer
    height: integer
    -- The orientation as the shader resource keys name it, in lower case.
    orientation: string
    -- The number of sorted lines.
    line_count: integer
    -- The number of pixels, the most keys the sort may have to sort.
    element_count: integer
    column_group_count: integer
    tile_group_count_x: integer
    tile_group_count_y: integer
end

-- The registered keys, sorted in place, and the radix sort's scratch buffers.
local RADIX_SORT_BUFFER_NAMES: sort.RadixSortBufferNames = {
    keys = "pixel_sort_keys",
    keys_swap = "pixel_sort_keys_swap",
    histogram = "pixel_sort_histogram",
    dispatch = "radix_sort_dispatch",
}

local function make_layout(p_context: wbe.PixelSortingRecipeContext): Layout
    local width = p_context.source_width
    local height = p_context.source_height
    local orientation_name = Graph.enum_name(p_context.orientation)
    if orientation_name ~= "VERTICAL" and orientation_name ~= "HORIZONTAL" then
        error("pixel_sorting: unknown orientation '" .. orientation_name .. "'.")
    end
    local vertical = orientation_name == "VERTICAL"
    local element_count = width * height
    local line_count = vertical and width or height
    return {
        width = width,
        height = height,
        orientation = string.lower(orientation_name),
        line_count = line_count,
        element_count = element_count,
        column_group_count = ceil_div(line_count, PIXEL_SORT_COLUMN_WORKGROUP_SIZE),
        tile_group_count_x = ceil_div(width, PIXEL_SORT_TILE_SIZE),
        tile_group_count_y = ceil_div(height, PIXEL_SORT_TILE_SIZE),
    }
end

local record Resources
    column_offsets: Graph.Resource
    destination: Graph.Resource
    post_processing_output: Graph.Resource
    mask: Graph.Resource
    sampler: Graph.Resource
    sort_buffers: sort.RadixSortBuffers
    swap_chain: Graph.Resource
    source: Graph.Resource
    contrast: Graph.Resource
    post_processing_params: Graph.Resource
end

local function make_resources(p_layout: Layout): Resources
    local width = p_layout.width
    local height = p_layout.height
    return {
        column_offsets = Graph.storage_buffer("pixel_sort_column_offsets", (PIXEL_SORT_COLUMN_OFFSETS_START + p_layout.line_count) * UINT32_SIZE),
        destination = Graph.storage_image("pixel_sort_destination_image", "r16g16b16a16_sfloat", width, height)
            :view("pixel_sort_destination"),
        -- The screenshot reads the result back through a transfer.
        post_processing_output = Graph.storage_image("pixel_sorting_post_processing_output_image", "r16g16b16a16_sfloat",
            width, height, { "transfer_src" }):view("pixel_sorting_post_processing_output"),
        -- Stores each masked pixel's rank within its line, with 0xFFFF marking unmasked pixels.
        mask = Graph.storage_image("pixel_sort_mask_image", "r16_uint", width, height):view("pixel_sort_mask"),
        sampler = Graph.clamp_to_edge_sampler("pixel_sort_sampler"),
        -- The register pass writes and the write pass reads the keys; size the sort for every masked pixel.
        sort_buffers = sort.radix_sort_buffers(RADIX_SORT_BUFFER_NAMES, p_layout.element_count),
        swap_chain = Graph.import_resource("swap_chain"),
        source = Graph.import_resource("pixel_sort_source"),
        contrast = Graph.import_resource("pixel_sort_contrast"),
        post_processing_params = Graph.import_resource("pixel_sorting_post_processing_params"),
    }
end

local record KeyRegistrationTasks
    count_mask: Graph.Task
    prefix_sum: Graph.Task
    register_keys: Graph.Task
end

-- Count the masked pixels of every line, sum them into the column offsets, and register their sort keys.
local function add_key_registration(p_graph: Graph.Graph, p_layout: Layout,
    p_resources: Resources): KeyRegistrationTasks
    local orientation = p_layout.orientation
    local count_mask_pipeline = p_graph:add_compute_pipeline("count_pixel_sort_mask_" .. orientation .. "_shader")
    local prefix_sum_pipeline = p_graph:add_compute_pipeline("prefix_sum_pixel_sort_columns_shader")
    local register_keys_pipeline = p_graph:add_compute_pipeline("register_pixel_sort_keys_" .. orientation .. "_shader")
    local count_mask = p_graph:compute("count_pixel_sort_mask", {
        pipeline = count_mask_pipeline,
        group_count_x = p_layout.column_group_count,
        group_count_y = 1,
    }, { uses = { p_resources.source, p_resources.contrast, p_resources.column_offsets } })
    -- The prefix sum runs in a single workgroup.
    local prefix_sum = p_graph:compute("prefix_sum_pixel_sort_columns", {
        pipeline = prefix_sum_pipeline, group_count_x = 1, group_count_y = 1,
    }, { uses = { p_resources.column_offsets } })
    local register_keys = p_graph:compute("register_pixel_sort_keys", {
        pipeline = register_keys_pipeline,
        group_count_x = p_layout.column_group_count,
        group_count_y = 1,
    }, { uses = { p_resources.source, p_resources.contrast, p_resources.column_offsets,
        p_resources.sort_buffers.keys, p_resources.mask } })
    return { count_mask = count_mask, prefix_sum = prefix_sum, register_keys = register_keys }
end

local record OutputTasks
    write_pixels: Graph.Task
    post_processing: Graph.Task
    present: Graph.Task
    draw_imgui: Graph.Task
end

-- Write the sorted pixels, post-process them, and present the result under the application's ImGui overlay.
local function add_output(p_graph: Graph.Graph, p_layout: Layout, p_resources: Resources): OutputTasks
    local tile_group_count_x = p_layout.tile_group_count_x
    local tile_group_count_y = p_layout.tile_group_count_y
    local write_pixels_pipeline = p_graph:add_compute_pipeline("write_sorted_pixels_" .. p_layout.orientation .. "_shader")
    local post_processing_pipeline = p_graph:add_compute_pipeline("pixel_sorting_post_processing_shader")
    local present_pipeline = p_graph:add_graphics_pipeline("present_pixel_sort_pipeline")
    local write_pixels = p_graph:compute("write_sorted_pixels", {
        pipeline = write_pixels_pipeline,
        group_count_x = tile_group_count_x,
        group_count_y = tile_group_count_y,
    }, { uses = { p_resources.source, p_resources.sort_buffers.keys, p_resources.mask, p_resources.contrast,
        p_resources.column_offsets, p_resources.destination } })
    local post_processing = p_graph:compute("pixel_sorting_post_processing", {
        pipeline = post_processing_pipeline,
        group_count_x = tile_group_count_x,
        group_count_y = tile_group_count_y,
    }, { uses = { p_resources.destination, p_resources.sampler, p_resources.post_processing_params,
        p_resources.post_processing_output } })
    local present = p_graph:draw("present_sorted_pixels", {
        pipeline = present_pipeline,
        clear_values = { { color = { 0.0, 0.0, 0.0, 1.0 } } },
        draw_vertex_count = 3,
    }, {
        uses = { p_resources.post_processing_output, p_resources.sampler },
    })
    present:color_target(0, p_resources.swap_chain)
    local draw_imgui = p_graph:import_task("draw_imgui")
    return { write_pixels = write_pixels, post_processing = post_processing, present = present, draw_imgui = draw_imgui }
end

-- Order the line bookkeeping and the keys before the write, and the output images through post-processing and
-- presentation.
local function connect_tasks(p_graph: Graph.Graph, p_resources: Resources, p_registration: KeyRegistrationTasks,
    p_radix_sort: Graph.Task, p_output: OutputTasks)
    p_graph:chain(p_resources.column_offsets,
        { p_registration.count_mask, p_registration.prefix_sum, p_registration.register_keys, p_output.write_pixels })
    p_graph:chain(p_resources.mask, { p_registration.register_keys, p_output.write_pixels })
    p_graph:chain(p_resources.sort_buffers.keys, { p_registration.register_keys, p_radix_sort, p_output.write_pixels })
    p_graph:chain(p_resources.destination, { p_output.write_pixels, p_output.post_processing })
    p_graph:chain(p_resources.post_processing_output, { p_output.post_processing, p_output.present })
    p_graph:chain(p_resources.swap_chain, { p_output.present, p_output.draw_imgui })
end

local exports = {}

function exports.build(context: wbe.PixelSortingRecipeContext): wbe.RendGraphRecipeVK
    local layout = make_layout(context)
    local graph = Graph.new()
    local resources = make_resources(layout)
    local registration = add_key_registration(graph, layout, resources)
    local radix_sort = sort.add_radix_sort_task(graph, "radix_sort", resources.sort_buffers)
    local output = add_output(graph, layout, resources)
    connect_tasks(graph, resources, registration, radix_sort, output)
    return graph:build()
end

return exports
