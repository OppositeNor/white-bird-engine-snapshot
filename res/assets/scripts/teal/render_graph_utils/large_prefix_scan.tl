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

-- GPU inclusive prefix sum of large buffers for render graph recipes, as the dispatches of a
-- run_compute_pipelines_sequential task, which may dispatch other passes before and after them: each 256-element block
-- is scanned and its total stored, 256 sections of the block totals are scanned and their totals stored, the section
-- totals are scanned in one workgroup, and every block adds the sum of the blocks before it. The application compiles the pipelines under their shader ids (large_prefix_scan_prepare_dispatch,
-- large_prefix_scan_blocks, large_prefix_scan_sections, large_prefix_scan_totals, and large_prefix_scan_add). The
-- scanned buffer holds the element count in its metadata, followed by the elements; see large_prefix_scan_utils.slang.

local math_utils = require("utils.math")
local Graph = require("render_graph_utils.render_graph_utils")

local ceil_div = math_utils.ceil_div

-- Must match WBE_LARGE_PREFIX_SCAN_BLOCK_SIZE, WBE_LARGE_PREFIX_SCAN_SECTION_NUM, and WBE_LARGE_PREFIX_SCAN_DATA_START in
-- large_prefix_scan_utils.slang.
local LARGE_PREFIX_SCAN_BLOCK_SIZE = 256
local LARGE_PREFIX_SCAN_SECTION_NUM = 256
local LARGE_PREFIX_SCAN_DATA_START = 4
-- The block passes dispatch one workgroup per block, within the maxComputeWorkGroupCount every device supports.
local LARGE_PREFIX_SCAN_MAX_BLOCK_NUM = 65535
-- One VkDispatchIndirectCommand (3 uint32), the block pass dispatch.
local LARGE_PREFIX_SCAN_DISPATCH_BUFFER_SIZE = 12
local UINT32_SIZE = 4

local record large_prefix_scan
    -- Names of the graph-owned buffers created for a large prefix scan.
    record LargePrefixScanBufferNames
        sections: string
        dispatch: string
    end

    -- The scratch buffers of a large prefix scan, which the scan alone uses: the block and section totals, and the
    -- indirect dispatch of the block passes.
    record LargePrefixScanBuffers
        sections: Graph.Resource
        dispatch: Graph.Resource
    end
end

-- Graph-owned scratch buffers sized to scan up to p_max_element_count elements. The scan dispatches reference these
-- handles, so the graph discovers their storage when it builds the recipe.
function large_prefix_scan.buffers(p_names: large_prefix_scan.LargePrefixScanBufferNames,
    p_max_element_count: integer): large_prefix_scan.LargePrefixScanBuffers
    local max_block_num = ceil_div(p_max_element_count, LARGE_PREFIX_SCAN_BLOCK_SIZE)
    if max_block_num > LARGE_PREFIX_SCAN_MAX_BLOCK_NUM then
        error(string.format("large_prefix_scan: %d elements need more than %d blocks.", p_max_element_count,
            LARGE_PREFIX_SCAN_MAX_BLOCK_NUM))
    end
    return {
        -- The section totals, then one block total per block.
        sections = Graph.storage_buffer(p_names.sections,
            (LARGE_PREFIX_SCAN_SECTION_NUM + math.max(max_block_num, 1)) * UINT32_SIZE),
        dispatch = Graph.buffer(p_names.dispatch, "buffer_indirect", LARGE_PREFIX_SCAN_DISPATCH_BUFFER_SIZE),
    }
end

-- The size in bytes of a buffer scanned by a large prefix scan of up to p_max_element_count elements.
function large_prefix_scan.data_buffer_size(p_max_element_count: integer): integer
    return (LARGE_PREFIX_SCAN_DATA_START + p_max_element_count) * UINT32_SIZE
end

-- The dispatches that compute the inclusive prefix sum of p_data in place, bound to the handles of p_data and p_buffers.
-- Each pass follows the previous one, and the passes read the element count from p_data, so the count must be written
-- before they run and must not exceed the maximum element count p_buffers was sized for.
function large_prefix_scan.dispatches(p_data: Graph.Resource,
    p_buffers: large_prefix_scan.LargePrefixScanBuffers): {Graph.RTParamsComputeDispatchVK}
    local sections = p_buffers.sections
    local dispatch = p_buffers.dispatch
    return {
        { pipeline = Graph.compute_pipeline("large_prefix_scan_prepare_dispatch"),
            aliases = { data_buffer = p_data, dispatch_buffer = dispatch } },
        Graph.indirect_dispatch(Graph.compute_pipeline("large_prefix_scan_blocks"), dispatch, 0,
            { data_buffer = p_data, sections_buffer = sections }),
        { pipeline = Graph.compute_pipeline("large_prefix_scan_sections"),
            group_count_x = LARGE_PREFIX_SCAN_SECTION_NUM, aliases = { data_buffer = p_data, sections_buffer = sections } },
        { pipeline = Graph.compute_pipeline("large_prefix_scan_totals"), aliases = { sections_buffer = sections } },
        Graph.indirect_dispatch(Graph.compute_pipeline("large_prefix_scan_add"), dispatch, 0,
            { data_buffer = p_data, sections_buffer = sections }),
    }
end

return large_prefix_scan
