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
#ifndef WBE_FILE_MESH_COMPILER_VK_HH
#define WBE_FILE_MESH_COMPILER_VK_HH

#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <vulkan/vulkan_core.h>
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

class RendererVK;

/**
 * @class MeshCompilerVK
 * @brief Compiles a mesh's categories into shader-independent Vulkan resources. Neither copyable nor movable.
 *
 * Runs on the rendering thread. The same compiled resources serve meshes that never move and meshes moved every frame;
 * the whole-mesh transform is supplied per draw (see REND_RES_MESH_TRANSFORM). Compiled meshes are registered in the
 * renderer's RenderDatabase under their mesh resource key, so draw tasks can find them; see acquire().
 */
class MeshCompilerVK final {
public:
    /**
     * @class Config
     * @brief Material-contract and buffer-usage configuration used during resource compilation.
     */
    struct Config {
        Config() = default;
        ~Config() = default;
        Config(const Config&) = default;
        Config(Config&&) noexcept = default;
        Config& operator=(const Config&) = default;
        Config& operator=(Config&&) noexcept = default;

        /** Default material resource used to fill missing shader texture roles. */
        std::string default_material_resource_key = "default_material";
        /** Usage added to the shared vertex buffer, e.g. MeshAccelerationStructureVK::GEOMETRY_BUFFER_USAGE. */
        VkBufferUsageFlags vertex_buffer_usage = 0;
        /** Usage added to the shared index buffer, e.g. MeshAccelerationStructureVK::GEOMETRY_BUFFER_USAGE. */
        VkBufferUsageFlags index_buffer_usage = 0;
    };

    MeshCompilerVK() = delete;
    ~MeshCompilerVK() = default;
    MeshCompilerVK(const MeshCompilerVK&) = delete;
    MeshCompilerVK(MeshCompilerVK&&) noexcept = delete;
    MeshCompilerVK& operator=(const MeshCompilerVK&) = delete;
    MeshCompilerVK& operator=(MeshCompilerVK&&) noexcept = delete;

    /**
     * @brief Construct a compiler using renderer-owned Vulkan services.
     * @throws std::runtime_error If the renderer or its render data is missing.
     */
    explicit MeshCompilerVK(RendererVK* p_renderer);

    /**
     * @brief Compile a mesh's shared geometry with opaque, masked, and opaque double-sided draw batches.
     *
     * Every non-empty category is compiled; at least one must contain a drawable submesh. No shader is involved: batches
     * group draws by the mesh's vertex layout, and every material texture is compiled to a sampled view so that material
     * tables can be built later for any pipeline (MeshResourcesVK::acquire_material_table()). A mesh with a resource key is
     * registered in the RenderDatabase under that key, replacing an earlier registration. The returned object is neither
     * copyable nor movable and must be shared through Ref.
     *
     * @throws std::runtime_error If the mesh is null, or its resource data is invalid.
     */
    Ref<MeshResourcesVK> compile(RefRes<RUMesh3D> p_mesh, const Config& p_config) const;

    /**
     * @brief Compile a mesh with the default Config; see compile(RefRes<RUMesh3D>, const Config&).
     */
    Ref<MeshResourcesVK> compile(RefRes<RUMesh3D> p_mesh) const;

    /**
     * @brief Get the compiled mesh registered under a mesh resource key, compiling and registering it on first request.
     *
     * The mesh resource is loaded from the ResourceManager. p_config applies only when the mesh is compiled by this call; a
     * mesh registered earlier is returned as compiled.
     *
     * @throws std::runtime_error If p_mesh_resource_key is empty, no RUMesh3D resource has the key, or compilation fails.
     */
    Ref<MeshResourcesVK> acquire(std::string_view p_mesh_resource_key, const Config& p_config) const;

    /**
     * @brief Acquire a compiled mesh, compiling it with the default Config; see acquire(std::string_view, const Config&).
     */
    Ref<MeshResourcesVK> acquire(std::string_view p_mesh_resource_key) const;

private:
    RendererVK* renderer = nullptr;
};

} // namespace WhiteBirdEngine

#endif
