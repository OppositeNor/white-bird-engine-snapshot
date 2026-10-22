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

-- The deferred PBR test's render graph: per-light shadow and reflective shadow maps, a G-buffer, direct and indirect
-- lighting, post-processing, bloom, presentation, and overlays. See ApplicationVulkanDeferredPBRTest in
-- tests/executable/function/render/deferred_test/.
--
-- The application imports every pass it builds in C++ as a task (the acceleration structure build, the frame sequence
-- acquisition, and ImGui), and adds its acceleration structure and dynamic mesh entities to the built descriptor. The
-- mesh draws import the meshes the application registered in the render database as "static_mesh" and "dynamic_mesh".
-- It supplies a wbe.DeferredRecipeContext, whose voxel and post_processing entries are the RecipeFeatureParams-derived
-- parameters of the feature modules it selects: a VoxelFeature and a PostProcessingFeature.

local type wbe = require("wbe")
local type voxel = require("render_graph_utils.features.voxel")
local math_utils = require("utils.math")
local Graph = require("render_graph_utils.render_graph_utils")

local ceil_div = math_utils.ceil_div


-- VK_REMAINING_MIP_LEVELS: a view of every mip level from the base level on.
local REMAINING_MIP_LEVELS = 4294967295
-- One workgroup per 8x8 tile of the lit images, which have the swap chain extent.
local SCREEN_TILE_SIZE = 8
-- One invocation per shadow-map line in the summed-area-table passes.
local SUMMED_AREA_TABLE_WORKGROUP_SIZE = 64
-- One workgroup per 8x8 tile of the bloom images.
local BLOOM_TILE_SIZE = 8

local record VoxelFeature
    -- The structure as a subgraph task, whose overlays draw on the target's color and depth.
    subgraph: function(p_id: string, p_params: any, p_target: voxel.Target): Graph.Task
end

local record PostProcessingTarget
    color_view: Graph.Resource
    width: integer
    height: integer
end

local record PostProcessingFeature
    -- The effect as a subgraph task, which modifies the target's color view in place.
    subgraph: function(p_id: string, p_params: any, p_target: PostProcessingTarget): Graph.Task
end

local function light_id(p_base: string, p_light: integer): string
    return p_base .. "_" .. p_light
end

-- The ids of the feature subgraphs.
local VOXEL = "voxel"
local POST_PROCESSING = "post_processing"

local CLEAR_BLACK: wbe.RTParamsClearValueVK = { color = { 0.0, 0.0, 0.0, 0.0 } }
local CLEAR_DEPTH: wbe.RTParamsClearValueVK = { depth = 0.0 }
-- The moments clear to the farthest depth, so unrendered texels never shadow.
local CLEAR_MOMENTS: wbe.RTParamsClearValueVK = { color = { 1.0, 1.0, 0.0, 0.0 } }

-- The recipe under construction, shared by the build steps. The handle map lets the per-light passes share their
-- resources; the graph discovers declarations only from task references and connections.
local record LightTasks
    run_shadow_opaque: Graph.Task
    run_shadow_double_sided: Graph.Task
    dynamic_shadow: Graph.Task
    dynamic_shadow_second: Graph.Task
    build_shadow_sat: Graph.Task
    generate_shadow_mipmaps: Graph.Task
    run_light_billboard: Graph.Task
end

local record Tasks
    build_mesh_acceleration_structure: Graph.Task
    acquire_frame_sequence: Graph.Task
    draw_imgui: Graph.Task
    voxel: Graph.Task
    run_gbuffer: Graph.Task
    run_gbuffer_double_sided: Graph.Task
    dynamic_gbuffer: Graph.Task
    dynamic_gbuffer_second: Graph.Task
    run_direct_lighting: Graph.Task
    indirect_lighting: {Graph.Task}
    combine_lighting: Graph.Task
    post_processing: Graph.Task
    extract_bloom: Graph.Task
    blur_bloom: Graph.Task
    acquire_bloom: Graph.Task
    present_result_image: Graph.Task
    present_frame_sequence: Graph.Task
end

local record Pipelines
    shadow_opaque: Graph.Resource
    shadow_double_sided: Graph.Resource
    shadow_dynamic: Graph.Resource
    gbuffer_opaque: Graph.Resource
    gbuffer_double_sided: Graph.Resource
    gbuffer_dynamic: Graph.Resource
    shadow_sat_horizontal: Graph.Resource
    shadow_sat_vertical: Graph.Resource
    direct_lighting: Graph.Resource
    direct_lighting_sat: Graph.Resource
    indirect_lighting: Graph.Resource
    indirect_lighting_debug_view: Graph.Resource
    combine_lighting: Graph.Resource
    extract_bloom: Graph.Resource
    blur_ping: Graph.Resource
    blur_pong: Graph.Resource
    present: Graph.Resource
    light_billboard: Graph.Resource
end

local function make_pipelines(p_graph: Graph.Graph): Pipelines
    return {
        shadow_opaque = p_graph:add_graphics_pipeline("deferred_shadow_opaque"),
        shadow_double_sided = p_graph:add_graphics_pipeline("deferred_shadow_double_sided"),
        shadow_dynamic = p_graph:add_graphics_pipeline("deferred_shadow_dynamic"),
        gbuffer_opaque = p_graph:add_graphics_pipeline("deferred_gbuffer_opaque"),
        gbuffer_double_sided = p_graph:add_graphics_pipeline("deferred_gbuffer_double_sided"),
        gbuffer_dynamic = p_graph:add_graphics_pipeline("deferred_gbuffer_dynamic"),
        shadow_sat_horizontal = p_graph:add_compute_pipeline("shadow_moments_summed_area_table_horizontal_shader"),
        shadow_sat_vertical = p_graph:add_compute_pipeline("shadow_moments_summed_area_table_vertical_shader"),
        direct_lighting = p_graph:add_compute_pipeline("direct_lighting_shader"),
        direct_lighting_sat = p_graph:add_compute_pipeline("direct_lighting_summed_area_table_shader"),
        indirect_lighting = p_graph:add_compute_pipeline("indirect_lighting_rsm_shader"),
        indirect_lighting_debug_view = p_graph:add_compute_pipeline("indirect_lighting_rsm_debug_view_shader"),
        combine_lighting = p_graph:add_compute_pipeline("combine_lighting_shader"),
        extract_bloom = p_graph:add_compute_pipeline("bloom_shader_extract"),
        blur_ping = p_graph:add_compute_pipeline("kawase_blur_pong_write_shader"),
        blur_pong = p_graph:add_compute_pipeline("kawase_blur_ping_write_shader"),
        present = p_graph:add_graphics_pipeline("present_post_processing"),
        light_billboard = p_graph:add_graphics_pipeline("deferred_light_billboard_pipeline"),
    }
end

local record Builder
    context: wbe.DeferredRecipeContext
    shadow_extent: wbe.RendExtent2DParamsVK
    bloom_extent: wbe.RendExtent2DParamsVK
    imports: {string}
    resources: {string:Graph.Resource}
    static_mesh: Graph.Resource
    dynamic_mesh: Graph.Resource
    pipelines: Pipelines
    graph: Graph.Graph
    tasks: Tasks
    light_tasks: {LightTasks}
end

-- Indirect lighting runs as two toggled tasks, the normal one and the voxel debug view. Expand its first handle to both.
local function chain(p_builder: Builder, p_resources: any, p_tasks: {Graph.Task})
    local expanded: {Graph.Task} = {}
    for _, task in ipairs(p_tasks) do
        if task == p_builder.tasks.indirect_lighting[1] then
            for _, indirect in ipairs(p_builder.tasks.indirect_lighting) do
                expanded[#expanded + 1] = indirect
            end
        else
            expanded[#expanded + 1] = task
        end
    end
    if (p_resources as Graph.ResourceBundle).resources ~= nil then
        p_builder.graph:chain(p_resources as Graph.ResourceBundle, expanded)
    elseif (p_resources as Graph.Resource).id ~= nil then
        p_builder.graph:chain(p_resources as Graph.Resource, expanded)
    else
        p_builder.graph:chain(p_resources as {Graph.Resource}, expanded)
    end
end

local function compute(p_builder: Builder, p_id: string, p_pipeline: Graph.Resource, p_group_count_x: integer,
    p_group_count_y: integer, p_aliases?: {string:Graph.Resource}): Graph.Task
    local params: Graph.RTParamsRunComputePipelineVK =
        { pipeline = p_pipeline, group_count_x = p_group_count_x, group_count_y = p_group_count_y, queue_family = "compute_1" }
    return p_builder.graph:compute(p_id, params, { aliases = p_aliases })
end

-- Shadow and reflective-shadow-map targets of every light. Every light shares one shadow sampler.
local function create_light_resources(p_builder: Builder)
    local graph = p_builder.graph
    local context = p_builder.context
    local formats = context.formats
    local shadow_extent = p_builder.shadow_extent
    local resources = p_builder.resources
    for light = 0, context.spot_light_count - 1 do
        local light_info = light_id("shadow_light_info", light)
        local billboard_info = light_id("light_billboard_info", light)
        p_builder.imports[#p_builder.imports + 1] = light_info
        p_builder.imports[#p_builder.imports + 1] = billboard_info
        resources[light_info] = graph:import_resource(light_info)
        resources[billboard_info] = graph:import_resource(billboard_info)
        local depth = graph:add_image(light_id("shadowmap_depth_image", light), "image_depth_target", formats.shadowmap_depth,
            shadow_extent)
        local color = graph:add_image(light_id("shadowmap_color_image", light), "image_render_target", formats.rsm_flux,
            shadow_extent)
        local normal = graph:add_image(light_id("rsm_normal_image", light), "image_render_target", formats.rsm_normal,
            shadow_extent)
        local moments = graph:add_image(light_id("shadow_moments_image", light), "image_render_target", formats.shadow_moments,
            shadow_extent, { "transfer_src", "transfer_dst" }, context.shadow_mipmap_level_count)
        local horizontal = graph:add_image(light_id("shadow_moments_sat_horizontal_image", light), "image_read_write",
            formats.shadow_moments, shadow_extent)
        local summed = graph:add_image(light_id("shadow_moments_sat_image", light), "image_read_write", formats.shadow_moments,
            shadow_extent)
        resources[light_id("shadowmap_depth", light)] = depth:view(light_id("shadowmap_depth", light))
        resources[light_id("shadowmap_color", light)] = color:view(light_id("shadowmap_color", light))
        resources[light_id("rsm_normal", light)] = normal:view(light_id("rsm_normal", light))
        resources[light_id("shadow_moments", light)] = moments:view(light_id("shadow_moments", light))
        -- Direct lighting samples the complete mip chain through a second view of the moments image.
        resources[light_id("shadow_moments_sampled", light)] = moments:view(light_id("shadow_moments_sampled", light),
            { mip_level_count = REMAINING_MIP_LEVELS })
        resources[light_id("shadow_sat_horizontal", light)] = horizontal:view(light_id("shadow_sat_horizontal", light))
        resources[light_id("shadow_sat", light)] = summed:view(light_id("shadow_sat", light))
    end
    resources.shadow_sampler = graph:add_clamp_to_edge_sampler("shadow_sampler",
        { min_lod = 0.0, max_lod = context.shadow_mipmap_level_count - 1.0 })
end

-- The G-buffer targets and the lighting and post-processing outputs. Views retain their source image handles.
local function create_screen_resources(p_builder: Builder)
    local graph = p_builder.graph
    local context = p_builder.context
    local formats = context.formats
    local bloom_extent = p_builder.bloom_extent
    local resources = p_builder.resources
    resources.gbuffer_albedo = graph:add_image("gbuffer_albedo_image", "image_render_target", formats.gbuffer_albedo):view("gbuffer_albedo")
    resources.gbuffer_normal = graph:add_image("gbuffer_normal_image", "image_render_target", formats.gbuffer_normal):view("gbuffer_normal")
    resources.gbuffer_rma = graph:add_image("gbuffer_rma_image", "image_render_target", formats.gbuffer_rma):view("gbuffer_rma")
    resources.gbuffer_depth = graph:add_image("gbuffer_depth_image", "image_depth_target", formats.gbuffer_depth):view("gbuffer_depth")
    resources.gbuffer_sampler = graph:add_clamp_to_edge_sampler("gbuffer_sampler", { min_filter = "nearest", mag_filter = "nearest" })

    -- The bloom images keep the extent of the initial swap chain, because the bloom readback buffer is sized by it.
    resources.direct_lighting = graph:add_image("direct_lighting_image", "image_read_write", formats.lighting_output):view("direct_lighting")
    resources.indirect_lighting = graph:add_image("indirect_lighting_image", "image_read_write", formats.lighting_output):view("indirect_lighting")
    resources.result_image = graph:add_image("result_color_image", "image_read_write", formats.lighting_output):view("result_image")
    resources.bloom_ping = graph:add_image("bloom_ping_image", "image_read_write", formats.lighting_output,
        bloom_extent, { "transfer_src" }):view("bloom_ping")
    resources.bloom_pong = graph:add_image("bloom_pong_image", "image_read_write", formats.lighting_output,
        bloom_extent):view("bloom_pong")
    -- The present pipeline renders in the swap-chain format, so the sequence matches the presented image.
    resources.frame_sequence = graph:add_image("frame_sequence_image", "image_render_target", context.swap_chain_format):view("frame_sequence")
    resources.bloom_sampler = graph:add_clamp_to_edge_sampler("bloom_sampler")
    resources.light_billboard_sampler = graph:add_clamp_to_edge_sampler("light_billboard_sampler")
end

-- Tasks the application builds in C++.
local function import_application_tasks(p_builder: Builder)
    if p_builder.context.has_mesh_acceleration_structure then
        p_builder.tasks.build_mesh_acceleration_structure = p_builder.graph:import_task("build_mesh_acceleration_structure")
    end
    p_builder.tasks.acquire_frame_sequence = p_builder.graph:import_task("acquire_frame_sequence")
    p_builder.tasks.draw_imgui = p_builder.graph:import_task("draw_imgui")
end

-- The voxel structure, a subgraph whose overlays draw over the lit scene.
local function add_voxel(p_builder: Builder)
    local resources = p_builder.resources
    local context = p_builder.context
    local voxel_params = context.voxel as wbe.RecipeFeatureParams
    local voxel_feature = require(voxel_params.module) as VoxelFeature
    p_builder.tasks.voxel = p_builder.graph:add(voxel_feature.subgraph(VOXEL, voxel_params, {
        dynamic_mesh_transform_ids = context.dynamic_mesh_transform_ids,
        debug_view_toggles = context.voxel_debug_view_toggles,
        color_target = resources.swap_chain,
        depth_target = resources.gbuffer_depth,
    }))
end

-- Mesh draws. The double-sided and dynamic passes load what the previous pass of their targets wrote.
local function add_mesh_draws(p_builder: Builder)
    local graph = p_builder.graph
    local resources = p_builder.resources
    local dynamic_transforms = p_builder.context.dynamic_mesh_transform_ids
    if #dynamic_transforms ~= 2 then
        error("deferred: the dynamic mesh draws two entities, but " .. #dynamic_transforms .. " transforms are supplied.")
    end
    local shadow_extent = p_builder.shadow_extent
    for light = 0, p_builder.context.spot_light_count - 1 do
        local tasks: LightTasks = {}
        p_builder.light_tasks[light + 1] = tasks
        local shadow_color = resources[light_id("shadowmap_color", light)]
        local rsm_normal = resources[light_id("rsm_normal", light)]
        local shadow_moments = resources[light_id("shadow_moments", light)]
        local shadow_depth = resources[light_id("shadowmap_depth", light)]
        local clear_values = { CLEAR_BLACK, CLEAR_BLACK, CLEAR_MOMENTS, CLEAR_DEPTH }
        local light_aliases: {string:Graph.Resource} = {
            shadow_light_info = resources[light_id("shadow_light_info", light)],
        }

        tasks.run_shadow_opaque = graph:draw_mesh(light_id("run_shadow_opaque", light), {
            mesh = p_builder.static_mesh,
            pipeline = p_builder.pipelines.shadow_opaque,
            parts = { "opaque" },
            render_extent = shadow_extent,
            clear_values = clear_values,
            color_load_op = "clear",
            depth_load_op = "clear",
        }, { aliases = light_aliases }):depth_target(shadow_depth)
            :color_target(0, shadow_color)
            :color_target(1, rsm_normal)
            :color_target(2, shadow_moments)

        tasks.run_shadow_double_sided = graph:draw_mesh(light_id("run_shadow_double_sided", light), {
            mesh = p_builder.static_mesh,
            pipeline = p_builder.pipelines.shadow_double_sided,
            parts = { "double_sided" },
            render_extent = shadow_extent,
            clear_values = clear_values,
            color_load_op = "load",
            depth_load_op = "load",
        }, { aliases = light_aliases }):depth_target(shadow_depth)
            :color_target(0, shadow_color)
            :color_target(1, rsm_normal)
            :color_target(2, shadow_moments)

        tasks.dynamic_shadow = graph:draw_mesh(light_id("dynamic_shadow", light), {
            mesh = p_builder.dynamic_mesh,
            pipeline = p_builder.pipelines.shadow_dynamic,
            parts = { "opaque" },
            transform = graph:reference(dynamic_transforms[1]),
            render_extent = shadow_extent,
            clear_values = {},
            color_load_op = "load",
            depth_load_op = "load",
        }, { aliases = light_aliases }):depth_target(shadow_depth)
            :color_target(0, shadow_color)
            :color_target(1, rsm_normal)
            :color_target(2, shadow_moments)

        tasks.dynamic_shadow_second = graph:draw_mesh(light_id("dynamic_shadow_second", light), {
            mesh = p_builder.dynamic_mesh,
            pipeline = p_builder.pipelines.shadow_dynamic,
            parts = { "opaque" },
            transform = graph:reference(dynamic_transforms[2]),
            render_extent = shadow_extent,
            clear_values = {},
            color_load_op = "load",
            depth_load_op = "load",
        }, { aliases = light_aliases }):depth_target(shadow_depth)
            :color_target(0, shadow_color)
            :color_target(1, rsm_normal)
            :color_target(2, shadow_moments)
    end

    local gbuffer_clear_values = { CLEAR_BLACK, CLEAR_BLACK, CLEAR_BLACK, CLEAR_DEPTH }

    p_builder.tasks.run_gbuffer = graph:draw_mesh("run_gbuffer", {
        mesh = p_builder.static_mesh,
        pipeline = p_builder.pipelines.gbuffer_opaque,
        parts = { "opaque" },
        clear_values = gbuffer_clear_values,
        color_load_op = "clear",
        depth_load_op = "clear",
    })
        :depth_target(resources.gbuffer_depth)
        :color_target(0, resources.gbuffer_albedo)
        :color_target(1, resources.gbuffer_normal)
        :color_target(2, resources.gbuffer_rma)

    p_builder.tasks.run_gbuffer_double_sided = graph:draw_mesh("run_gbuffer_double_sided", {
        mesh = p_builder.static_mesh,
        pipeline = p_builder.pipelines.gbuffer_double_sided,
        parts = { "double_sided" },
        clear_values = gbuffer_clear_values,
        color_load_op = "load",
        depth_load_op = "load",
    })
        :depth_target(resources.gbuffer_depth)
        :color_target(0, resources.gbuffer_albedo)
        :color_target(1, resources.gbuffer_normal)
        :color_target(2, resources.gbuffer_rma)

    p_builder.tasks.dynamic_gbuffer = graph:draw_mesh("dynamic_gbuffer", {
        mesh = p_builder.dynamic_mesh,
        pipeline = p_builder.pipelines.gbuffer_dynamic,
        parts = { "opaque" },
        transform = graph:reference(dynamic_transforms[1]),
        clear_values = {},
        color_load_op = "load",
        depth_load_op = "load",
    })
        :depth_target(resources.gbuffer_depth)
        :color_target(0, resources.gbuffer_albedo)
        :color_target(1, resources.gbuffer_normal)
        :color_target(2, resources.gbuffer_rma)

    p_builder.tasks.dynamic_gbuffer_second = graph:draw_mesh("dynamic_gbuffer_second", {
        mesh = p_builder.dynamic_mesh,
        pipeline = p_builder.pipelines.gbuffer_dynamic,
        parts = { "opaque" },
        transform = graph:reference(dynamic_transforms[2]),
        clear_values = {},
        color_load_op = "load",
        depth_load_op = "load",
    })
        :depth_target(resources.gbuffer_depth)
        :color_target(0, resources.gbuffer_albedo)
        :color_target(1, resources.gbuffer_normal)
        :color_target(2, resources.gbuffer_rma)
end

-- Shadow filtering of every light.
local function add_shadow_filtering(p_builder: Builder)
    local resources = p_builder.resources
    local context = p_builder.context
    local shadow = context.shadow_extent
    for light = 0, context.spot_light_count - 1 do
        local tasks = p_builder.light_tasks[light + 1]
        if context.summed_area_table then
            tasks.build_shadow_sat = p_builder.graph:compute_sequential(light_id("build_shadow_sat", light), {
                queue_family = "compute_1",
                dispatches = {
                    {
                        pipeline = p_builder.pipelines.shadow_sat_horizontal,
                        group_count_x = ceil_div(shadow.height, SUMMED_AREA_TABLE_WORKGROUP_SIZE)
                    },
                    {
                        pipeline = p_builder.pipelines.shadow_sat_vertical,
                        group_count_x = ceil_div(shadow.width, SUMMED_AREA_TABLE_WORKGROUP_SIZE)
                    },
                },
            }, {
                aliases = {
                    shadow_sat_horizontal = resources[light_id("shadow_sat_horizontal", light)],
                    shadow_sat = resources[light_id("shadow_sat", light)],
                },
            })
        else
            local params: Graph.RTParamsGenerateMipmapsVK = {
                source_view = resources[light_id("shadow_moments", light)],
                mip_chain_view = resources[light_id("shadow_moments_sampled", light)],
                base_extent = p_builder.shadow_extent,
                mipmap_level_count = context.shadow_mipmap_level_count,
                format = context.formats.shadow_moments,
            }
            tasks.generate_shadow_mipmaps = p_builder.graph:generate_mipmaps(light_id("generate_shadow_mipmaps", light), params)
        end
    end
end

-- Lighting. Every slot of the lighting shaders' lights[] array maps to a light's shadow resources; slots past the light
-- count reuse light zero, so that every reflected descriptor binds.
local function add_lighting(p_builder: Builder)
    local resources = p_builder.resources
    local context = p_builder.context
    local summed_area_table = context.summed_area_table
    local light_count = context.spot_light_count
    local screen = context.swap_chain_extent
    local light_slots: {string:string}
    if summed_area_table then
        light_slots = { shadow_moments = "shadow_moments", shadow_sat = "shadow_sat", shadowmap_color = "shadowmap_color", rsm_normal = "rsm_normal" }
    else
        light_slots = { shadow_moments_sampled = "shadow_moments_sampled", shadowmap_color = "shadowmap_color", rsm_normal = "rsm_normal" }
    end
    local direct_lighting_aliases: {string:Graph.Resource} = {
        gbuffer_sampler = resources.gbuffer_sampler,
        shadow_sampler = resources.shadow_sampler,
    }
    for slot = 0, context.max_spot_light_info_count - 1 do
        local light = slot < light_count and slot or 0
        for task_resource, graph_resource in pairs(light_slots) do
            direct_lighting_aliases[task_resource .. "[" .. slot .. "]"] = resources[light_id(graph_resource, light)]
        end
    end
    local screen_tiles_x = ceil_div(screen.width, SCREEN_TILE_SIZE)
    local screen_tiles_y = ceil_div(screen.height, SCREEN_TILE_SIZE)
    p_builder.tasks.run_direct_lighting = compute(p_builder, "run_direct_lighting",
        summed_area_table and p_builder.pipelines.direct_lighting_sat or p_builder.pipelines.direct_lighting, screen_tiles_x,
        screen_tiles_y, direct_lighting_aliases)
    -- Indirect lighting only loads mip level zero of the moments, so its mip-chain slot binds the base moments view, which
    -- the summed-area-table graph orders without a mip generation pass. The debug view variant replaces it while a voxel
    -- debug view is shown.
    local indirect_lighting_aliases: {string:Graph.Resource} = {
        gbuffer_sampler = resources.gbuffer_sampler,
        shadow_sampler = resources.shadow_sampler,
    }
    for slot = 0, context.max_spot_light_info_count - 1 do
        local light = slot < light_count and slot or 0
        indirect_lighting_aliases["shadow_moments_sampled[" .. slot .. "]"] = resources[light_id("shadow_moments", light)]
        indirect_lighting_aliases["shadowmap_color[" .. slot .. "]"] = resources[light_id("shadowmap_color", light)]
        indirect_lighting_aliases["rsm_normal[" .. slot .. "]"] = resources[light_id("rsm_normal", light)]
    end
    local debug_view_toggles = context.voxel_debug_view_toggles
    for index, pipeline in ipairs({ p_builder.pipelines.indirect_lighting, p_builder.pipelines.indirect_lighting_debug_view }) do
        local params: Graph.RTParamsRunComputePipelineVK =
            { pipeline = pipeline, group_count_x = screen_tiles_x, group_count_y = screen_tiles_y, queue_family = "compute_2" }
        p_builder.tasks.indirect_lighting[index] = p_builder.graph:compute(
            index == 1 and "run_indirect_lighting" or "run_indirect_lighting_debug_view", params, {
            aliases = indirect_lighting_aliases,
            toggle = index == 1 and debug_view_toggles.disabled or debug_view_toggles.enabled,
        })
    end
    p_builder.tasks.combine_lighting = compute(p_builder, "combine_lighting", p_builder.pipelines.combine_lighting, screen_tiles_x, screen_tiles_y)
end

-- Post-processing, a subgraph, modifies the result image in place after composition. Returns the last task that writes
-- the result image.
local function add_post_processing(p_builder: Builder): Graph.Task
    local resources = p_builder.resources
    local post_processing = p_builder.context.post_processing as wbe.RecipeFeatureParams
    if post_processing == nil then
        return p_builder.tasks.combine_lighting
    end
    local screen = p_builder.context.swap_chain_extent
    local post_processing_feature = require(post_processing.module) as PostProcessingFeature
    p_builder.tasks.post_processing = p_builder.graph:add(post_processing_feature.subgraph(POST_PROCESSING, post_processing,
        { color_view = resources.result_image, width = screen.width, height = screen.height }))
    chain(p_builder, resources.result_image, { p_builder.tasks.combine_lighting, p_builder.tasks.post_processing })
    return p_builder.tasks.post_processing
end

-- Bloom, presentation, and on-demand acquisition.
local function add_bloom_and_presentation(p_builder: Builder)
    local resources = p_builder.resources
    local context = p_builder.context
    local bloom = context.bloom_extent
    local graph = p_builder.graph
    local bloom_tiles_x = ceil_div(bloom.width, BLOOM_TILE_SIZE)
    local bloom_tiles_y = ceil_div(bloom.height, BLOOM_TILE_SIZE)
    p_builder.tasks.extract_bloom = compute(p_builder, "extract_bloom", p_builder.pipelines.extract_bloom, bloom_tiles_x, bloom_tiles_y,
        { bloom_sampler = resources.bloom_sampler })
    -- Starting from ping, even iterations blur ping into pong and odd iterations blur pong back into ping, so an even
    -- iteration count leaves the result in ping.
    p_builder.tasks.blur_bloom = graph:ping_pong("blur_bloom", {
        ping_pipeline = p_builder.pipelines.blur_ping,
        pong_pipeline = p_builder.pipelines.blur_pong,
        iterations = context.bloom_blur_pass_count,
        group_count_x = bloom_tiles_x,
        group_count_y = bloom_tiles_y,
        queue_family = "compute_1",
    }, {
        aliases = {
            kawase_blur_ping = resources.bloom_ping,
            kawase_blur_pong = resources.bloom_pong,
            kawase_blur_sampler = resources.bloom_sampler
        },
    })
    local acquire_params: Graph.RTParamsCPUInFlightAcquireVK = { image_view = resources.bloom_ping }
    p_builder.tasks.acquire_bloom = graph:cpu_acquire_image("acquire_bloom", acquire_params, {
        trigger = { event = context.bloom_acquire_event },
    })
    local present_params: Graph.RTParamsRunGraphicsPipelineVK = {
        pipeline = p_builder.pipelines.present,
        clear_values = {
            {
                color = { 0.0, 0.0, 0.0, 1.0 }
            }
        }
    }
    p_builder.tasks.present_result_image = graph:draw("present_result_image", present_params,
        { aliases = { bloom_sampler = resources.bloom_sampler } }):color_target(0, resources.swap_chain)
    p_builder.tasks.present_frame_sequence = graph:draw("present_frame_sequence", present_params, {
        aliases = { bloom_sampler = resources.bloom_sampler },
        trigger = { event = context.frame_sequence_event },
    }):color_target(0, resources.frame_sequence)
end

-- A billboard per light, drawn over the swap chain.
local function add_light_billboards(p_builder: Builder)
    local resources = p_builder.resources
    local billboard_params: Graph.RTParamsRunGraphicsPipelineVK = {
        pipeline = p_builder.pipelines.light_billboard,
        color_load_op = "load",
        depth_load_op = "load",
        draw_vertex_count = 6,
    }
    for light = 0, p_builder.context.spot_light_count - 1 do
        p_builder.light_tasks[light + 1].run_light_billboard = p_builder.graph:draw(light_id("run_light_billboard", light), billboard_params, {
            aliases = {
                light_billboard_info = resources[light_id("light_billboard_info", light)],
                light_billboard_texture = resources.light_billboard_texture,
                light_billboard_sampler = resources.light_billboard_sampler,
            },
        }):color_target(0, resources.swap_chain):depth_target(resources.gbuffer_depth)
    end
end

-- Shadow and reflective-shadow-map passes of every light.
local function connect_shadow_passes(p_builder: Builder)
    local resources = p_builder.resources
    local tasks = p_builder.tasks
    for light = 0, p_builder.context.spot_light_count - 1 do
        local light_tasks = p_builder.light_tasks[light + 1]
        local dynamic_second = light_tasks.dynamic_shadow_second
        local passes = {
            light_tasks.run_shadow_opaque,
            light_tasks.run_shadow_double_sided,
            light_tasks.dynamic_shadow,
            dynamic_second
        }
        local color = resources[light_id("shadowmap_color", light)]
        local normal = resources[light_id("rsm_normal", light)]
        local colors = p_builder.graph:resource_bundle({ color, normal })
        local moments = resources[light_id("shadow_moments", light)]
        chain(p_builder, { color, normal, moments, resources[light_id("shadowmap_depth", light)] }, passes)
        chain(p_builder, colors, { dynamic_second, tasks.run_direct_lighting })
        chain(p_builder, colors, { dynamic_second, tasks.indirect_lighting[1] })
        if p_builder.context.summed_area_table then
            local build = light_tasks.build_shadow_sat
            build:inputs_from(dynamic_second, { shadow_moments = moments })
            chain(p_builder, moments, { dynamic_second, tasks.run_direct_lighting })
            chain(p_builder, moments, { dynamic_second, tasks.indirect_lighting[1] })
            chain(p_builder, resources[light_id("shadow_sat", light)], { build, tasks.run_direct_lighting })
        else
            local mipmaps = light_tasks.generate_shadow_mipmaps
            chain(p_builder, moments, { dynamic_second, mipmaps, tasks.indirect_lighting[1] })
            chain(p_builder, resources[light_id("shadow_moments_sampled", light)], { mipmaps, tasks.run_direct_lighting })
        end
    end
end

-- G-buffer passes, and the overlays that depth-test against the G-buffer depth.
local function connect_gbuffer_passes(p_builder: Builder)
    local resources = p_builder.resources
    local tasks = p_builder.tasks
    local gbuffer_passes = {
        tasks.run_gbuffer,
        tasks.run_gbuffer_double_sided,
        tasks.dynamic_gbuffer,
        tasks.dynamic_gbuffer_second,
        tasks.run_direct_lighting
    }
    local gbuffer_resources = p_builder.graph:resource_bundle({
        resources.gbuffer_albedo,
        resources.gbuffer_normal,
        resources.gbuffer_rma,
        resources.gbuffer_depth
    })
    chain(p_builder, gbuffer_resources, gbuffer_passes)
    chain(p_builder, gbuffer_resources, { tasks.dynamic_gbuffer_second, tasks.indirect_lighting[1] })
    chain(p_builder, resources.gbuffer_rma, { tasks.dynamic_gbuffer_second, tasks.combine_lighting })
    -- The voxel overlays depth-test against the G-buffer depth after lighting has sampled it.
    chain(p_builder, resources.gbuffer_depth, { tasks.run_direct_lighting, tasks.voxel })
    local depth_overlays = { tasks.indirect_lighting[1], tasks.voxel }
    for light = 0, p_builder.context.spot_light_count - 1 do
        depth_overlays[#depth_overlays + 1] = p_builder.light_tasks[light + 1].run_light_billboard
    end
    chain(p_builder, resources.gbuffer_depth, depth_overlays)
end

-- Lighting composition, post-processing, bloom, and presentation. p_result_writer is the last task that writes the
-- result image.
local function connect_composition(p_builder: Builder, p_result_writer: Graph.Task)
    local resources = p_builder.resources
    local tasks = p_builder.tasks
    chain(p_builder, resources.direct_lighting, { tasks.run_direct_lighting, tasks.combine_lighting })
    chain(p_builder, resources.indirect_lighting, { tasks.indirect_lighting[1], tasks.combine_lighting })
    p_result_writer:connect_all({ tasks.extract_bloom, tasks.present_result_image, tasks.present_frame_sequence }, resources.result_image)
    -- The blur task ping-pongs between the bloom images internally; only its final ping result leaves the task.
    chain(p_builder, resources.bloom_ping, { tasks.extract_bloom, tasks.blur_bloom, tasks.acquire_bloom, tasks.present_result_image })
    -- The frame sequence presents the same result and bloom into its own image, which it then acquires.
    chain(p_builder, resources.bloom_ping, { tasks.blur_bloom, tasks.present_frame_sequence })
    chain(p_builder, resources.frame_sequence, { tasks.present_frame_sequence, tasks.acquire_frame_sequence })
    local swap_chain_users = { tasks.present_result_image, tasks.voxel }
    for light = 0, p_builder.context.spot_light_count - 1 do
        swap_chain_users[#swap_chain_users + 1] = p_builder.light_tasks[light + 1].run_light_billboard
    end
    swap_chain_users[#swap_chain_users + 1] = tasks.draw_imgui
    chain(p_builder, resources.swap_chain, swap_chain_users)
end

local exports = {}

function exports.build(context: wbe.DeferredRecipeContext): wbe.RendGraphRecipeVK
    local shadow = context.shadow_extent
    local bloom = context.bloom_extent
    local graph = Graph.new()
    local builder: Builder = {
        context = context,
        shadow_extent = { width = shadow.width, height = shadow.height },
        bloom_extent = { width = bloom.width, height = bloom.height },
        imports = { "swap_chain", "gbuffer_camera_info", "composition_camera_info", "spot_light_infos", "light_billboard_texture" },
        resources = {},
        static_mesh = graph:add_mesh("static_mesh"),
        dynamic_mesh = graph:add_mesh("dynamic_mesh"),
        pipelines = make_pipelines(graph),
        graph = graph,
        tasks = { indirect_lighting = {} },
        light_tasks = {},
    }
    for _, id in ipairs(builder.imports) do
        builder.resources[id] = graph:import_resource(id)
    end
    create_light_resources(builder)
    create_screen_resources(builder)
    import_application_tasks(builder)
    add_voxel(builder)
    add_mesh_draws(builder)
    add_shadow_filtering(builder)
    add_lighting(builder)
    local result_writer = add_post_processing(builder)
    add_bloom_and_presentation(builder)
    add_light_billboards(builder)
    connect_shadow_passes(builder)
    connect_gbuffer_passes(builder)
    connect_composition(builder, result_writer)
    return builder.graph:build(builder.imports)
end

return exports
