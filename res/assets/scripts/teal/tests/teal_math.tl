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

-- Test-only Teal module that another Teal module requires; see WBERendGraphScriptVKResourceTest.

local record Exports
    record Extent
        width: integer
        height: integer
    end
    tile_count: function(p_extent: Extent, p_tile_size: integer): integer
end

local exports: Exports = {}

function exports.tile_count(p_extent: Exports.Extent, p_tile_size: integer): integer
    return ((p_extent.width + p_tile_size - 1) // p_tile_size) * ((p_extent.height + p_tile_size - 1) // p_tile_size)
end

return exports
