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
#ifndef WBE_FILE_MESH_BUFFER_COMPILER_VK_HH
#define WBE_FILE_MESH_BUFFER_COMPILER_VK_HH

#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class MeshBufferCompilerVK
 * @brief Validates one mesh and compiles its shared vertex and index buffers for Vulkan use.
 */
class MeshBufferCompilerVK final {
public:
    /**
     * @class BuildResult
     * @brief Shared Vulkan buffers compiled for one mesh.
     */
    struct BuildResult {
        /** One buffer per distinct vertex binary, in first-use order. */
        std::vector<Ref<ROBufferVK>> vertex_buffers;
        /** The binary each vertex buffer was compiled from, parallel to vertex_buffers. */
        std::vector<RefRes<RUBinary>> vertex_binaries;
        Ref<ROBufferVK> index_buffer = MEM_NULL;
    };

    MeshBufferCompilerVK() = delete;
    ~MeshBufferCompilerVK() = default;
    MeshBufferCompilerVK(const MeshBufferCompilerVK&) = default;
    MeshBufferCompilerVK(MeshBufferCompilerVK&&) noexcept = default;
    MeshBufferCompilerVK& operator=(const MeshBufferCompilerVK&) = default;
    MeshBufferCompilerVK& operator=(MeshBufferCompilerVK&&) noexcept = default;

    /**
     * @brief Construct a compiler bound to one renderer's Vulkan state.
     */
    explicit MeshBufferCompilerVK(const RenderContextVK* p_render_data);

    /**
     * @brief Compile or reuse one vertex buffer per vertex binary and the index buffer shared by every category of one mesh.
     *
     * Every binary read by a vertex binding of an indexed submesh becomes one vertex buffer. Returns empty results when the
     * mesh is null or has no submesh with indices.
     * @throws std::runtime_error When storage is missing, views or instance ranges are malformed, or indexed submeshes use
     * different index binaries.
     */
    BuildResult build(RefRes<RUMesh3D> p_mesh, VkBufferUsageFlags p_vertex_usage, VkBufferUsageFlags p_index_usage) const;

private:
    const RenderContextVK* render_data = nullptr;
};

} // namespace WhiteBirdEngine

#endif