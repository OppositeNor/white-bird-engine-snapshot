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

-- Types shared by the voxel structure feature modules (render_graph_utils.features.voxel_svo and voxel_octree), and the
-- recipes that include them. Declarations only: import them with `local type voxel = require("render_graph_utils.features.voxel")`.

local type wbe = require("wbe")
local type Graph = require("render_graph_utils.render_graph")

local record voxel
    -- What the including recipe supplies to a voxel structure's subgraph, whose ports `color` and `depth` the overlays draw
    -- through.
    record Target
        -- The transforms of the two dynamic mesh entities.
        dynamic_mesh_transform_ids: {string}
        -- The voxel debug view toggles by mode group.
        debug_view_toggles: wbe.VoxelDebugViewToggles
        -- The color target of the overlays.
        color_target: Graph.Resource
        -- The depth target the overlays test against.
        depth_target: Graph.Resource
    end
end

return voxel
