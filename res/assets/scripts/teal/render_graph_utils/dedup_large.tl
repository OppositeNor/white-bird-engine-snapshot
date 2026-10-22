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

-- GPU deduplication of large key buffers for render graph recipes, as the dispatches of a
-- run_compute_pipelines_sequential task, which may dispatch other passes before and after them. The radix sort orders the
-- 32-bit keys in place, the first key of every value is marked, and the large prefix scan of the marks gives each marked
-- key its index among the unique keys, to which it is scattered. The application compiles the pipelines under their shader ids
-- (dedup_large_prepare_dispatch, dedup_large_mark, dedup_large_scatter), and those of the radix sort (see
-- render_graph_utils.sort) and the large prefix scan (see render_graph_utils.large_prefix_scan). The key buffer and the
-- unique keys hold the key count in their metadata, followed by the keys; see dedup_large_utils.slang.

local Graph = require("render_graph_utils.render_graph_utils")
local sort = require("render_graph_utils.sort")
local large_prefix_scan = require("render_graph_utils.large_prefix_scan")

-- One VkDispatchIndirectCommand (3 uint32), the dispatch of the per-key passes.
local DEDUP_LARGE_DISPATCH_BUFFER_SIZE = 12

local record dedup_large
    -- Names of the graph-owned buffers created for a large deduplication. The scratch buffers are named after the keys,
    -- with a suffix.
    record DedupLargeBufferNames
        keys: string
        unique_keys: string
    end

    -- The graph resources of a large deduplication.
    record DedupLargeBuffers
        -- The keys, sorted in place. The deduplication reads and writes it, so order its producers before and its
        -- consumers after the task dispatching the deduplication on it.
        keys: Graph.Resource
        -- The unique keys in ascending order.
        unique_keys: Graph.Resource
        -- Scratch buffers the deduplication alone uses.
        sort: sort.RadixSortBuffers
        marks: Graph.Resource
        scan: large_prefix_scan.LargePrefixScanBuffers
        dispatch: Graph.Resource
    end
end

-- Graph-owned buffers sized to deduplicate up to p_max_element_count keys. The dispatches reference these handles, so
-- the graph discovers their storage when it builds the recipe.
function dedup_large.buffers(p_names: dedup_large.DedupLargeBufferNames, p_max_element_count: integer,
    p_graph?: Graph.Graph): dedup_large.DedupLargeBuffers
    local graph = p_graph or Graph.new()
    local keys = p_names.keys
    local sort_buffers = sort.radix_sort_buffers({
        keys = keys,
        keys_swap = keys .. "_swap",
        histogram = keys .. "_histogram",
        dispatch = keys .. "_sort_dispatch",
    }, p_max_element_count, graph)
    return {
        keys = sort_buffers.keys,
        -- At most every key is unique.
        unique_keys = graph:add_storage_buffer(p_names.unique_keys, large_prefix_scan.data_buffer_size(p_max_element_count)),
        sort = sort_buffers,
        marks = graph:add_storage_buffer(keys .. "_marks", large_prefix_scan.data_buffer_size(p_max_element_count)),
        scan = large_prefix_scan.buffers({ sections = keys .. "_mark_sections", dispatch = keys .. "_mark_dispatch" },
            p_max_element_count, graph),
        dispatch = graph:add_buffer(keys .. "_dedup_dispatch", "buffer_indirect", DEDUP_LARGE_DISPATCH_BUFFER_SIZE),
    }
end

-- The dispatches that sort p_buffers.keys in place and write its unique keys to p_buffers.unique_keys, bound to the
-- handles of p_buffers. Each pass follows the previous one, and the passes read the key count from the keys, so the count
-- must be written before they run and must not exceed the maximum element count p_buffers was sized for.
function dedup_large.dispatches(p_graph: Graph.Graph,
    p_buffers: dedup_large.DedupLargeBuffers): {Graph.RTParamsComputeDispatchVK}
    local keys = p_buffers.keys
    local marks = p_buffers.marks
    local dispatch = p_buffers.dispatch
    local dispatches: {Graph.RTParamsComputeDispatchVK} = {
        { pipeline = p_graph:add_compute_pipeline("dedup_large_prepare_dispatch"),
            aliases = { keys = keys, dispatch_buffer = dispatch } },
    }
    local function append(p_dispatches: {Graph.RTParamsComputeDispatchVK})
        for _, entry in ipairs(p_dispatches) do
            dispatches[#dispatches + 1] = entry
        end
    end
    append(sort.radix_sort_dispatches(p_graph, p_buffers.sort))
    dispatches[#dispatches + 1] =
        p_graph:indirect_dispatch(p_graph:add_compute_pipeline("dedup_large_mark"), dispatch, 0, { keys = keys, marks = marks })
    append(large_prefix_scan.dispatches(p_graph, marks, p_buffers.scan))
    dispatches[#dispatches + 1] = p_graph:indirect_dispatch(p_graph:add_compute_pipeline("dedup_large_scatter"), dispatch,
        0, { keys = keys, marks = marks, unique_keys = p_buffers.unique_keys })
    return dispatches
end

return dedup_large
