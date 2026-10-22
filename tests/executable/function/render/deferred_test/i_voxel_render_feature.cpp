/* Copyright 2025 OppositeNor

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include "i_voxel_render_feature.hh"
#include "function/render/render_resource_unit.hh"
#include "glm/ext/vector_float3.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace WhiteBirdEngine {

VoxelVolume VoxelVolume::enclose(const RUAABB3D& p_bounds, uint32_t p_extent) {
    const float scene_extent = std::max({p_bounds.width, p_bounds.height, p_bounds.depth});
    if (!(scene_extent > 0.0F)) {
        throw std::runtime_error("VoxelVolume: the scene bounds must have a positive extent.");
    }
    if (p_extent <= 2) {
        throw std::runtime_error("VoxelVolume: the grid must have more than two voxels per axis.");
    }
    const float width = scene_extent * static_cast<float>(p_extent) / static_cast<float>(p_extent - 2);
    return VoxelVolume{
        .origin = p_bounds.center - glm::vec3(width * 0.5F),
        .width = width,
    };
}

} // namespace WhiteBirdEngine
