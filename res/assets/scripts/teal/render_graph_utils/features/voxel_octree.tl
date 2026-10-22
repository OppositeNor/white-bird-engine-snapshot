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

-- Mipmapped 3D-texture octree construction and debug views, for recipes that include it as a feature. The application
-- compiles the pipelines and owns the octree textures and buffers, which every graph imports. It also supplies the texture
-- clears and the construction passes as imported tasks, since they record image clears and report the full texture views
-- they write through the per-level views; see VoxelRenderFeatureOctree in
-- tests/executable/function/render/deferred_test/voxel_render_feature_octree.hh.

local type wbe = require("wbe")
local type voxel = require("render_graph_utils.features.voxel")
local Graph = require("render_graph_utils.render_graph_utils")

local voxel_octree = {}

-- The octree as a subgraph task with id p_id, whose overlays draw on its `color` and `depth` ports, bound to the target's.
function voxel_octree.subgraph(p_id: string, p_params: wbe.OctreeRecipeParams, p_target: voxel.Target): Graph.Task
    local graph = Graph.new()
    local resources: {string:Graph.Resource} = {}
    for _, id in ipairs(p_params.imports) do
        resources[id] = graph:import_resource(id)
    end
    local color = graph:port("color")
    local depth = graph:port("depth")
    local gather_leaves_pipeline = graph:add_compute_pipeline("octree_debug_gather_leaves_shader")
    local gather_nodes_pipeline = graph:add_compute_pipeline("octree_debug_gather_nodes_shader")
    local debug_draw_pipeline = graph:add_graphics_pipeline("octree_debug_draw_pipeline")
    local debug_node_draw_pipeline = graph:add_graphics_pipeline("octree_debug_node_draw_pipeline")
    local toggles = p_target.debug_view_toggles

    -- The frame-begin barrier orders this frame's mutation after the previous frame's consumers.
    local barrier_params: Graph.RTParamsFullMemoryBarrierVK = { resource = resources.octree_dynamic }
    local frame_begin = graph:full_memory_barrier("octree_frame_begin", barrier_params)
    -- Static scene: empty both textures, then occupy the rasterized leaves and their ancestors. Dynamic meshes: empty the
    -- dynamic texture, then rebuild it like the static scene.
    local reset = graph:import_task("reset_octree")
    local build = graph:import_task("run_build_octree")
    local build_double_sided = graph:import_task("run_build_octree_double_sided")
    local clear_dynamic = graph:import_task("clear_dynamic_octree")
    local dynamic = graph:import_task("dynamic_octree")
    local dynamic_second = graph:import_task("dynamic_octree_second")
    -- Debug views: the leaf gather serves both views, and the node gather only the node view.
    local leaf_groups = p_params.leaf_gather_group_count
    local leaf_gather_params: Graph.RTParamsRunComputePipelineVK = {
        pipeline = gather_leaves_pipeline,
        group_count_x = leaf_groups.x,
        group_count_y = leaf_groups.y,
        group_count_z = leaf_groups.z,
        queue_family = "compute_1",
    }
    local gather_debug_leaves = graph:compute("gather_octree_debug_leaves", leaf_gather_params, { toggle = toggles.enabled })
    local node_groups = p_params.node_gather_group_count
    local node_gather_params: Graph.RTParamsRunComputePipelineVK = {
        pipeline = gather_nodes_pipeline,
        group_count_x = node_groups,
        group_count_y = node_groups,
        group_count_z = node_groups,
        queue_family = "compute_1",
    }
    local gather_debug_nodes = graph:compute("gather_octree_debug_nodes", node_gather_params, {
        toggle = toggles.pre_deferred_propagation,
    })
    local function overlay(p_task_id: string, p_pipeline: Graph.Resource, p_indirect_buffer: Graph.Resource, p_draw_count: integer,
        p_toggle: wbe.RendRecipeToggleVK): Graph.Task
        local params: Graph.RTParamsRunGraphicsPipelineIndirectVK = {
            pipeline = p_pipeline,
            index_buffer = resources.octree_debug_draw_index_buffer,
            indirect_buffer = p_indirect_buffer,
            draw_count = p_draw_count,
            color_load_op = "load",
            depth_load_op = "load",
        }
        local task = graph:draw_indirect(p_task_id, params, {
            toggle = p_toggle,
        })
        task:color_target(0, color)
        task:depth_target(depth)
        return task
    end
    local debug_draw = overlay("run_octree_debug_draw", debug_draw_pipeline, resources.octree_debug_draw_indirect_buffer, p_params.leaf_draw_count,
        toggles.enabled)
    local debug_node_draw = overlay("run_octree_debug_node_draw", debug_node_draw_pipeline, resources.octree_debug_node_draw_indirect_buffer, 1,
        toggles.pre_deferred_propagation)

    -- The debug gathers read the full views after every pass writing the textures.
    graph:chain(resources.octree_dynamic, {
        frame_begin,
        reset,
        clear_dynamic,
        dynamic,
        dynamic_second,
        gather_debug_leaves,
        gather_debug_nodes,
    })
    graph:chain(resources.octree_static,
        { reset, build, build_double_sided, gather_debug_leaves, gather_debug_nodes })
    -- The raster passes write every mip level through the per-level views.
    for level = 0, p_params.level_count - 1 do
        graph:chain(resources["octree_static_levels[" .. level .. "]"], { reset, build, build_double_sided })
        graph:chain(resources["octree_dynamic_levels[" .. level .. "]"], { reset, clear_dynamic, dynamic, dynamic_second })
    end
    gather_debug_leaves:connect(debug_draw, { resources.octree_debug_draw_indirect_buffer, resources.octree_debug_leaf_masks })
    -- The leaf gather empties the node view before the node gather fills it.
    graph:chain({ resources.octree_debug_node_draw_indirect_buffer, resources.octree_debug_node_list },
        { gather_debug_leaves, gather_debug_nodes, debug_node_draw })
    -- The overlays draw in this order.
    graph:chain({ color, depth }, { debug_draw, debug_node_draw })
    return graph:as_subgraph(p_id, { color = p_target.color_target, depth = p_target.depth_target }, p_params.imports)
end

return voxel_octree
