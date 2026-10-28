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

-- Leaf gathering and deduplication of the third sparse voxel octree construction (svo_3), for recipes that build it. The
-- conservative rasterization registers the Morton code of every covered leaf in the deferred list, once per covering
-- triangle, and the large deduplication (see render_graph_utils.dedup_large) writes each code once to the unique leaves.
-- The application compiles the pipelines of svo_3.toml, those of the large deduplication, and
-- svo_deferred_count_reset_pong_shader, and owns the SVO info buffer.
-- TODO: Build the tree from the unique leaves.

local type wbe = require("wbe")
local Graph = require("render_graph_utils.render_graph_utils")
local dedup_large = require("render_graph_utils.dedup_large")

local record svo_3
    record LeafParams
        -- The SVO info buffer, whose max_deferred_task_count must equal max_leaf_count.
        svo_info: Graph.Resource
        -- The mesh rasterized into the SVO.
        mesh: Graph.Resource
        -- The SVO's max_depth, which sets the rasterization extent to 2^max_depth.
        max_depth: integer
        -- The capacity of the deferred list.
        max_leaf_count: integer
        -- The queue family of the compute tasks; graphics by default.
        queue_family: wbe.QueueFamilyVK | integer
    end

    record LeafDeduplication
        -- The unique leaves: their count in the metadata, followed by their Morton codes in ascending order, in the layout
        -- of a radix sort key buffer.
        unique_leaves: Graph.Resource
        -- The task that writes the unique leaves; order their consumers after it on unique_leaves.
        deduplicate: Graph.Task
    end
end

-- Adds to p_graph the tasks that gather the leaves of p_params.mesh and deduplicate them.
function svo_3.add_leaf_deduplication(p_graph: Graph.Graph, p_params: svo_3.LeafParams): svo_3.LeafDeduplication
    local buffers = dedup_large.buffers({ keys = "svo_3_deferred_list", unique_keys = "svo_3_unique_leaves" },
        p_params.max_leaf_count)
    local deferred_list = buffers.keys
    local queue_family = p_params.queue_family

    local reset = p_graph:compute("svo_3_reset_deferred_list", {
        pipeline = p_graph:add_compute_pipeline("svo_deferred_count_reset_pong_shader"),
        queue_family = queue_family,
    }, { aliases = { svo_deferred_list_pong = deferred_list } })
    local extent = 1 << p_params.max_depth
    local gather = p_graph:draw_mesh("svo_3_gather_leaves", {
        mesh = p_params.mesh,
        pipeline = { default = p_graph:add_graphics_pipeline("svo_3_gather_leaves_pipeline") },
        parts = { "opaque", "double_sided" },
        render_extent = { width = extent, height = extent },
    }, { aliases = { svo_info = p_params.svo_info, svo_deferred_list = deferred_list } })
    -- The clamping of the leaf count and the deduplication run as one task.
    local dispatches: {Graph.RTParamsComputeDispatchVK} = {
        { pipeline = p_graph:add_compute_pipeline("svo_3_prepare_leaves_shader"),
            aliases = { svo_info = p_params.svo_info, svo_deferred_list = deferred_list } },
    }
    for _, dispatch in ipairs(dedup_large.dispatches(buffers)) do
        dispatches[#dispatches + 1] = dispatch
    end
    local deduplicate = p_graph:compute_sequential("svo_3_deduplicate_leaves",
        { dispatches = dispatches, queue_family = queue_family })

    p_graph:chain(deferred_list, { reset, gather, deduplicate })
    return { unique_leaves = buffers.unique_keys, deduplicate = deduplicate }
end

return svo_3
