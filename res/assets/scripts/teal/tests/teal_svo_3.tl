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

-- Builds the svo_3 leaf deduplication, and with it the large deduplication and prefix scan, through both the ACP type checker and the
-- script backend; see WBERendGraphScriptVKResourceTest.

local type wbe = require("wbe")
local Graph = require("render_graph_utils.render_graph_utils")
local svo_3 = require("render_graph_utils.svo_3")

local exports = {}

function exports.build(): wbe.RendGraphRecipeVK
    local graph = Graph.new()
    svo_3.add_leaf_deduplication(graph, {
        svo_info = graph:import_resource("svo_info"),
        mesh = graph:add_mesh("static_mesh"),
        max_depth = 6,
        max_leaf_count = 100000,
        queue_family = "compute_1",
    })
    return graph:build()
end

return exports
