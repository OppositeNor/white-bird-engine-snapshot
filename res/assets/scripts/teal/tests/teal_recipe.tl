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

-- Test-only Teal recipe module, which the ACP type-checks against tests.teal_math and compiles to Lua; see
-- WBERendGraphScriptVKResourceTest.

local teal_math = require("tests.teal_math")

local enum QueueFamily
    "compute_1"
    "compute_2"
end

local record Recipe
    imports: {string}
    queue_family: QueueFamily
end

local exports = {}

function exports.build(): Recipe
    return { imports = { tostring(teal_math.tile_count({ width = 40, height = 20 }, 16)) }, queue_family = "compute_2" }
end

return exports
