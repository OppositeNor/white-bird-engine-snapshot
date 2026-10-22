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

-- GPU sorting for render graph recipes. The radix sort sorts a key buffer of 32-bit keys in place, as one task that
-- dispatches every pass in one command buffer. The application compiles the radix sort pipelines under their shader ids
-- (radix_sort_prepare_dispatch, radix_sort_histogram_prefix_sum, radix_sort_histogram_overflow_prefix_sum,
-- radix_sort_histogram_add_overflow, and the per-pass radix_sort_histogram_workgroup_process_<bits> and
-- radix_sort_scatter_<bits>). The key buffer holds the element count in its metadata, followed by the keys; see
-- radix_sort_utils.slang.

local type wbe = require("wbe")
local math_utils = require("utils.math")
local Graph = require("render_graph_utils.render_graph_utils")

local ceil_div = math_utils.ceil_div

-- Must match WBE_RADIX_SORT_BUCKET_COUNT, WBE_RADIX_SORT_ELEMENTS_PER_WORKGROUP, WBE_RADIX_SORT_MAX_WORKGROUP_COUNT,
-- WBE_RADIX_SORT_DATA_START, WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM, WBE_RADIX_SORT_LOCAL_SORT_DISPATCH_START, and
-- WBE_RADIX_SORT_HISTOGRAM_SCAN_DISPATCH_START in radix_sort_utils.slang.
local RADIX_SORT_BUCKET_COUNT = 256
local RADIX_SORT_ELEMENTS_PER_WORKGROUP = 256
local RADIX_SORT_MAX_WORKGROUP_COUNT = 16384
local RADIX_SORT_DATA_START = 4
local RADIX_SORT_SCAN_HISTOGRAM_NUM = 16
local RADIX_SORT_LOCAL_SORT_DISPATCH_OFFSET = 0
-- One VkDispatchIndirectCommand (3 uint32) after the local sort dispatch.
local RADIX_SORT_HISTOGRAM_SCAN_DISPATCH_OFFSET = 12
local RADIX_SORT_DISPATCH_BUFFER_SIZE = 24
-- Each pass sorts by one 8-bit digit of the 32-bit keys.
local RADIX_SORT_PASS_COUNT = 4
local UINT32_SIZE = 4

-- The task-local ids the radix sort task binds, which its aliases map to the RadixSortBuffers.
local KEYS = "keys"
local KEYS_SWAP = "keys_swap"
local HISTOGRAM = "histogram"
local DISPATCH = "dispatch"

local record sort
    -- Names of the graph-owned buffers created for a radix sort.
    record RadixSortBufferNames
        keys: string
        keys_swap: string
        histogram: string
        dispatch: string
    end

    -- The graph resources of a radix sort.
    record RadixSortBuffers
        -- The keys, sorted in place. The sort reads and writes it, so order its producers before and its consumers after
        -- the sort task on it.
        keys: Graph.Resource
        -- Scratch buffers the sort alone uses: the keys between passes, the histograms, and the indirect dispatches.
        keys_swap: Graph.Resource
        histogram: Graph.Resource
        dispatch: Graph.Resource
    end
end

-- Graph-owned buffers sized to sort up to p_max_element_count keys. The sort task references these handles, so the graph
-- discovers their storage when it builds the recipe.
function sort.radix_sort_buffers(p_names: sort.RadixSortBufferNames, p_max_element_count: integer,
    p_graph?: Graph.Graph): sort.RadixSortBuffers
    local graph = p_graph or Graph.new()
    -- The sort has one local sort per RADIX_SORT_ELEMENTS_PER_WORKGROUP keys, up to RADIX_SORT_MAX_WORKGROUP_COUNT.
    local max_local_sort_num =
        math_utils.clamp(ceil_div(p_max_element_count, RADIX_SORT_ELEMENTS_PER_WORKGROUP), 1, RADIX_SORT_MAX_WORKGROUP_COUNT)
    local max_overflow_num = ceil_div(max_local_sort_num, RADIX_SORT_SCAN_HISTOGRAM_NUM)
    local key_buffer_size = (RADIX_SORT_DATA_START + p_max_element_count) * UINT32_SIZE
    return {
        keys = graph:add_storage_buffer(p_names.keys, key_buffer_size),
        keys_swap = graph:add_storage_buffer(p_names.keys_swap, key_buffer_size),
        -- The metadata, one sub-histogram per local sort, and one overflow histogram per histogram prefix scan workgroup.
        histogram = graph:add_storage_buffer(p_names.histogram,
            (RADIX_SORT_DATA_START + (max_local_sort_num + max_overflow_num) * RADIX_SORT_BUCKET_COUNT) * UINT32_SIZE),
        -- The indirect dispatches of the passes, written from the local sort count of the keys.
        dispatch = graph:add_buffer(p_names.dispatch, "buffer_indirect", RADIX_SORT_DISPATCH_BUFFER_SIZE),
    }
end

local function radix_sort_params(p_graph: Graph.Graph, p_queue_family?: wbe.QueueFamilyVK | integer): Graph.RTParamsRunComputePipelinesSequentialVK
    -- The element count is fixed once the keys are registered, so every pass uses the same dispatches.
    local dispatches: {Graph.RTParamsComputeDispatchVK} = {
        { pipeline = p_graph:add_compute_pipeline("radix_sort_prepare_dispatch"), aliases = { input_buffer = KEYS, dispatch_buffer = DISPATCH } },
    }
    local function indirect(p_pipeline: string, p_offset: integer, p_aliases: {string:string})
        dispatches[#dispatches + 1] =
            { pipeline = p_graph:add_compute_pipeline(p_pipeline), indirect_buffer = DISPATCH, indirect_offset = p_offset, aliases = p_aliases }
    end
    for pass = 0, RADIX_SORT_PASS_COUNT - 1 do
        -- Pass p reads the keys from one buffer and scatters them into the other, so after an even number of passes
        -- the sorted keys are back in the key buffer.
        local input_keys = pass % 2 == 0 and KEYS or KEYS_SWAP
        local output_keys = pass % 2 == 0 and KEYS_SWAP or KEYS
        local bit_region = string.format("%d_%d", pass * 8, (pass + 1) * 8)
        indirect("radix_sort_histogram_workgroup_process_" .. bit_region, RADIX_SORT_LOCAL_SORT_DISPATCH_OFFSET,
            { input_buffer = input_keys, histogram_buffer = HISTOGRAM })
        indirect("radix_sort_histogram_prefix_sum", RADIX_SORT_HISTOGRAM_SCAN_DISPATCH_OFFSET, { histogram_buffer = HISTOGRAM })
        -- The overflow histograms are scanned in a single workgroup.
        dispatches[#dispatches + 1] =
            { pipeline = p_graph:add_compute_pipeline("radix_sort_histogram_overflow_prefix_sum"), aliases = { histogram_buffer = HISTOGRAM } }
        indirect("radix_sort_histogram_add_overflow", RADIX_SORT_LOCAL_SORT_DISPATCH_OFFSET, { histogram_buffer = HISTOGRAM })
        indirect("radix_sort_scatter_" .. bit_region, RADIX_SORT_LOCAL_SORT_DISPATCH_OFFSET,
            { input_buffer = input_keys, histogram_buffer = HISTOGRAM, output_buffer = output_keys })
    end
    return { dispatches = dispatches, queue_family = p_queue_family }
end

local function radix_sort_aliases(p_buffers: sort.RadixSortBuffers): {string:Graph.Resource}
    return {
        [KEYS] = p_buffers.keys,
        [KEYS_SWAP] = p_buffers.keys_swap,
        [HISTOGRAM] = p_buffers.histogram,
        [DISPATCH] = p_buffers.dispatch,
    }
end

-- A task with id p_id that sorts p_buffers.keys in place on p_queue_family (graphics by default). Every pass follows
-- the previous one in the task's command buffer, and the passes read the element count from the keys, so the count
-- must be written before the task runs. Add the returned description with graph:add(), or use add_radix_sort_task().
function sort.radix_sort_task(p_id: string, p_buffers: sort.RadixSortBuffers,
    p_queue_family?: wbe.QueueFamilyVK | integer): Graph.Task
    local params = radix_sort_params(Graph.new(), p_queue_family)
    local wire_params: wbe.RTParamsRunComputePipelinesSequentialVK = { dispatches = {}, queue_family = params.queue_family }
    -- graph:add() takes the serialized recipe params; these dispatches use only task-local string bindings.
    for _, dispatch in ipairs(params.dispatches) do
        local wire_dispatch: {string:any} = {}
        for name, value in pairs(dispatch as {string:any}) do
            wire_dispatch[name] = value
        end
        wire_dispatch.pipeline = dispatch.pipeline.id
        wire_params.dispatches[#wire_params.dispatches + 1] = wire_dispatch as wbe.RTParamsComputeDispatchVK
    end
    return {
        id = p_id,
        type = "run_compute_pipelines_sequential",
        params = wire_params,
        aliases = radix_sort_aliases(p_buffers),
    }
end

-- The dispatches that sort p_buffers.keys in place, bound to the buffers' handles, for a run_compute_pipelines_sequential
-- task that may dispatch other passes before and after them. Like the sort task, they read the element count from the
-- keys, so the count must be written before they run.
function sort.radix_sort_dispatches(p_graph: Graph.Graph, p_buffers: sort.RadixSortBuffers): {Graph.RTParamsComputeDispatchVK}
    local bindings = radix_sort_aliases(p_buffers)
    local dispatches = radix_sort_params(p_graph).dispatches
    for _, dispatch in ipairs(dispatches) do
        if dispatch.indirect_buffer ~= nil then
            dispatch.indirect_buffer = bindings[dispatch.indirect_buffer as string]
        end
        local aliases: {string:Graph.Resource | string} = {}
        for name, local_name in pairs(dispatch.aliases) do
            aliases[name] = bindings[local_name as string]
        end
        dispatch.aliases = aliases
    end
    return dispatches
end

-- Adds the sort to p_graph and returns its registered task object.
function sort.add_radix_sort_task(p_graph: Graph.Graph, p_id: string, p_buffers: sort.RadixSortBuffers,
    p_queue_family?: wbe.QueueFamilyVK | integer): Graph.Task
    return p_graph:compute_sequential(p_id, radix_sort_params(p_graph, p_queue_family), { aliases = radix_sort_aliases(p_buffers) })
end

return sort
