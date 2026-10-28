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

-- Draws the opaque part of the static cube mesh straight into the swap chain.
-- Imports: `swap_chain` (the swap chain image views) and `camera_info` (the application's camera uniform buffer).

local type wbe = require("wbe")
local Graph = require("render_graph_utils.render_graph_utils")

local exports = {}

function exports.build(_context: wbe.RendRecipeContextVK): wbe.RendGraphRecipeVK
    local graph = Graph.new()
    local swap_chain = graph:import_resource("swap_chain")
    local camera_info = graph:import_resource("camera_info")
    local static_mesh = graph:add_mesh("model_cube_static.mesh")
    local pipeline = graph:add_graphics_pipeline("draw_cube_unshaded_pipeline")
    graph:draw_mesh("run_static_mesh", {
        mesh = static_mesh,
        pipeline = { default = pipeline },
        parts = { "opaque" },
        clear_values = { { color = { 0.0, 0.0, 0.0, 0.0 } } },
    }, { uses = { camera_info } }):color_target(0, swap_chain)
    return graph:build()
end

return exports
