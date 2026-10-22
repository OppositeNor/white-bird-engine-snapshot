/* Copyright 2026 OppositeNor

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
#ifndef WBE_FILE_I_VOXEL_RENDER_FEATURE_HH
#define WBE_FILE_I_VOXEL_RENDER_FEATURE_HH

#include "core/data/data_value.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/rend_graph_recipe_builder_vk.hh"
#include "glm/ext/vector_float3.hpp"
#include <array>
#include <cstdint>
#include <string_view>

namespace WhiteBirdEngine {

class DynamicMeshRenderFeature;

/**
 * @enum VoxelDebugViewMode
 * @brief Selects the voxel construction state visualized by the deferred rendering test.
 * @details Each voxel structure names what it draws for a mode through IVoxelRenderFeature::get_debug_view_name().
 */
enum class VoxelDebugViewMode : uint32_t {
    DISABLED,
    POST_SVO_BUILD,
    PRE_DEFERRED_PROPAGATION,
};

/**
 * @brief Line-list indices of the twelve edges of the unit cube whose corner i has the coordinates ((i >> 2) & 1, (i >> 1) & 1, i & 1).
 * @details The voxel debug draws instance this cube per leaf or node and generate its corners in the vertex shader. Each edge
 * joins corner i to the corner that differs in one coordinate bit.
 */
inline constexpr std::array<uint32_t, 24> VOXEL_DEBUG_CUBE_INDICES{
    // Edges along z.
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    // Edges along y.
    0,
    2,
    1,
    3,
    4,
    6,
    5,
    7,
    // Edges along x.
    0,
    4,
    1,
    5,
    2,
    6,
    3,
    7,
};

/**
 * @class VoxelRecipeInputs
 * @brief What the application supplies to a voxel structure configuring a recipe. Copyable and movable.
 */
struct VoxelRecipeInputs {
    /** The dynamic mesh whose entities the construction passes rasterize; it must outlive the configured graphs. */
    const DynamicMeshRenderFeature* dynamic_mesh = nullptr;
    /** The RTTriggeredVK event of the static construction tasks; the application disables them after their first frame. */
    std::string_view static_build_event;
    /** Whether the static construction has yet to record; a rebuild after it recorded creates the tasks disabled. */
    bool static_build_pending = true;
};

/**
 * @class VoxelVolume
 * @brief The world-space cube a voxel structure covers.
 */
struct VoxelVolume {
    /** World-space minimum corner of the cube. */
    glm::vec3 origin{0.0F};
    /** Edge length of the cube. */
    float width = 0.0F;

    /**
     * @brief Get the cube centered on a scene's bounds that a grid of p_extent leaf voxels per axis covers.
     * @details The width is the bounds' largest extent plus one leaf voxel of margin on each side, which keeps geometry on
     * the bounds' faces inside the grid.
     * @param p_bounds World-space bounds of the scene.
     * @param p_extent Number of leaf voxels along each axis of the grid.
     * @throws std::runtime_error When the bounds have no positive extent, or p_extent leaves no voxel inside the margin.
     */
    static VoxelVolume enclose(const RUAABB3D& p_bounds, uint32_t p_extent);
};

/**
 * @class IVoxelRenderFeature
 * @brief Voxel structure built over the deferred test's scene, with its construction and debug-overlay render tasks.
 * @details Implementations own their buffers and pipelines, and describe their tasks to a Lua recipe module (see
 * configure_recipe()). Neither copyable nor movable. All methods and destruction are confined to the rendering thread;
 * registered graphs retain the imported resources and task factories.
 */
class IVoxelRenderFeature {
public:
    IVoxelRenderFeature() = default;
    virtual ~IVoxelRenderFeature() = default;
    IVoxelRenderFeature(const IVoxelRenderFeature&) = delete;
    IVoxelRenderFeature(IVoxelRenderFeature&&) = delete;
    IVoxelRenderFeature& operator=(const IVoxelRenderFeature&) = delete;
    IVoxelRenderFeature& operator=(IVoxelRenderFeature&&) = delete;

    /**
     * @brief Human-readable name of the voxel structure and its construction algorithm.
     */
    virtual std::string_view get_name() const = 0;

    /**
     * @brief Human-readable name of what the debug overlay draws in a mode.
     */
    virtual const char* get_debug_view_name(VoxelDebugViewMode p_mode) const = 0;

    /**
     * @brief Describe the structure to a recipe that includes it, and supply its resources and C++ tasks.
     *
     * Writes `module`, the name of the Lua module whose `add(graph, params, target)` adds the structure's tasks to a recipe
     * under construction and returns its overlays, and every parameter that module reads. The recipe passes the map to
     * `add()` as `params`, and orders the overlays it returns on the swap chain and `gbuffer_depth`.
     *
     * @param p_parameters The map to write the module name and parameters into.
     * @param p_imports The imports of the recipe, to supply the structure's resources and C++ tasks to.
     * @param p_inputs What the application supplies.
     */
    virtual void configure_recipe(
        DataValue& p_parameters, RendGraphRecipeImportsVK& p_imports, const VoxelRecipeInputs& p_inputs) const = 0;
};

} // namespace WhiteBirdEngine

#endif
