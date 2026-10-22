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
#ifndef WBE_FILE_MESH_ACCELERATION_STRUCTURE_VK_HH
#define WBE_FILE_MESH_ACCELERATION_STRUCTURE_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/render_vk_defs.hh"
#include "function/render_vk/ro_acceleration_structure_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include <cstddef>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class MeshAccelerationStructureVK
 * @brief Ray-tracing acceleration structures of compiled mesh resources, with every resource their build needs.
 *
 * Holds one bottom-level acceleration structure per drawable opaque or opaque double-sided submesh, the top-level
 * acceleration structure instancing them with the mesh's instance transforms, the instance buffer, scratch storage, and the
 * source mesh resources whose geometry buffers the build reads. Created by MeshAccelerationStructureCompilerVK; the
 * structures hold no data until record_build() has executed on the GPU. bind() imports the top-level structure under
 * `<key>.top_level`. Neither copyable nor movable, shared through Ref, and confined to the rendering thread.
 */
class MeshAccelerationStructureVK final : public IBindableRenderResourceVK {
public:
    /** Member name of the top-level acceleration structure. */
    static constexpr std::string_view TOP_LEVEL = "top_level";
    /** Usage the mesh's vertex and index buffers need to serve as build inputs. */
    static constexpr VkBufferUsageFlags GEOMETRY_BUFFER_USAGE =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    MeshAccelerationStructureVK() = delete;
    virtual ~MeshAccelerationStructureVK() override = default;
    MeshAccelerationStructureVK(const MeshAccelerationStructureVK&) = delete;
    MeshAccelerationStructureVK(MeshAccelerationStructureVK&&) noexcept = delete;
    MeshAccelerationStructureVK& operator=(const MeshAccelerationStructureVK&) = delete;
    MeshAccelerationStructureVK& operator=(MeshAccelerationStructureVK&&) noexcept = delete;

    /**
     * @brief Construct empty storage owned by p_allocator.
     * @throws std::runtime_error If p_allocator is null.
     */
    explicit MeshAccelerationStructureVK(RenderAllocator* p_allocator);

    /** @brief Acquire the top-level acceleration structure. */
    Ref<ROAccelerationStructureVK> acquire_top_level() const {
        return top_level;
    }

    /** @brief Return the number of bottom-level acceleration structures. */
    size_t get_bottom_level_count() const {
        return bottom_levels.size();
    }

    /**
     * @brief Acquire one bottom-level acceleration structure.
     * @throws std::runtime_error If p_index is out of bounds.
     */
    Ref<ROAccelerationStructureVK> acquire_bottom_level(size_t p_index) const;

    /** @brief Acquire the mesh resources whose geometry the structures were built from. */
    Ref<MeshResourcesVK> acquire_static_mesh() const {
        return static_mesh;
    }

    /**
     * @brief Record every bottom-level build, then the top-level build, into p_command_buffer.
     *
     * The bottom-level builds share one scratch buffer and are serialized with barriers. A final barrier makes the
     * top-level structure readable by all later commands. The structures and inputs stay alive while this object does.
     */
    void record_build(VkCommandBuffer p_command_buffer) const;

    /**
     * @brief Import the top-level acceleration structure under `<p_resource_id>.top_level`.
     * @throws std::runtime_error If p_resource_id is empty or the structure is missing.
     */
    virtual void bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const override;

private:
    friend class MeshAccelerationStructureCompilerVK;

    struct BottomLevelBuild {
        VkAccelerationStructureGeometryKHR geometry{};
        VkAccelerationStructureBuildRangeInfoKHR build_range{};
        Ref<ROAccelerationStructureVK> acceleration_structure = MEM_NULL;
    };

    RenderAllocator* allocator = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR command_build_acceleration_structures = nullptr;
    Ref<MeshResourcesVK> static_mesh = MEM_NULL;
    Vector<BottomLevelBuild, RenderAllocator> bottom_level_builds;
    Vector<Ref<ROAccelerationStructureVK>, RenderAllocator> bottom_levels;
    Ref<ROBufferVK> bottom_level_scratch_buffer = MEM_NULL;
    VkDeviceAddress bottom_level_scratch_address = 0;
    Ref<ROAccelerationStructureVK> top_level = MEM_NULL;
    Ref<ROBufferVK> top_level_instance_buffer = MEM_NULL;
    Ref<ROBufferVK> top_level_scratch_buffer = MEM_NULL;
    VkDeviceAddress top_level_scratch_address = 0;
    VkAccelerationStructureGeometryKHR top_level_geometry{};
    VkAccelerationStructureBuildRangeInfoKHR top_level_build_range{};
};

} // namespace WhiteBirdEngine

#endif
