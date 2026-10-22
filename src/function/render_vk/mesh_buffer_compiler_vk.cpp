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
#include "function/render_vk/mesh_buffer_compiler_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/function_utils.hh"
#include "function/render/i_render_object.hh"
#include "function/render/render_database.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {
namespace {

struct MeshDrawBinaries {
    std::vector<RefRes<RUBinary>> vertex_binaries;
    RefRes<RUBinary> index_binary = MEM_NULL;
};

void validate_binary_view(const RUBinaryView& p_view, std::string_view p_context) {
    if (p_view.binary == nullptr) {
        throw std::runtime_error(std::format("Failed to compile mesh buffer: {} binary is missing.", p_context));
    }
    if (p_view.start > p_view.binary->size || p_view.size > p_view.binary->size - p_view.start) {
        throw std::runtime_error(std::format("Failed to compile mesh buffer: {} binary view exceeds binary size.", p_context));
    }
    if (p_view.size > 0 && p_view.binary->data == nullptr) {
        throw std::runtime_error(std::format("Failed to compile mesh buffer: {} binary data is missing.", p_context));
    }
}

void validate_submesh_views(const RUSubmesh3D& p_submesh, const RUIndexView& p_indices) {
    if (p_submesh.get_vertex_bindings().empty()) {
        throw std::runtime_error("Failed to compile mesh buffer: submesh has no vertex bindings.");
    }
    for (const RUVertexView& binding : p_submesh.get_vertex_bindings()) {
        validate_binary_view(binding.binary, "vertex");
        if (binding.vertex_stride == 0 || binding.binary.size != binding.vertex_count * binding.vertex_stride) {
            throw std::runtime_error("Failed to compile mesh buffer: vertex view count, stride, and size do not match.");
        }
        if (binding.vertex_count != p_submesh.vertex_count) {
            throw std::runtime_error("Failed to compile mesh buffer: vertex bindings hold different numbers of vertices.");
        }
    }
    validate_binary_view(p_indices.binary, "index");
    if (p_indices.binary.start % sizeof(uint32_t) != 0 || p_indices.binary.size != p_indices.index_count * sizeof(uint32_t)) {
        throw std::runtime_error("Failed to compile mesh buffer: index view is not a uint32 range.");
    }
}

void accumulate_shared_binaries(MeshDrawBinaries& p_binaries, const RUSubmesh3D& p_submesh, const RUIndexView& p_indices) {
    for (const RUVertexView& binding : p_submesh.get_vertex_bindings()) {
        if (std::ranges::find(p_binaries.vertex_binaries, binding.binary.binary) == p_binaries.vertex_binaries.end()) {
            p_binaries.vertex_binaries.push_back(binding.binary.binary);
        }
    }
    if (p_binaries.index_binary == nullptr) {
        p_binaries.index_binary = p_indices.binary.binary;
    }
    if (p_indices.binary.binary != p_binaries.index_binary) {
        throw std::runtime_error("Failed to compile mesh buffer: all drawable submeshes must share one index binary resource.");
    }
}

void accumulate_category_binaries(MeshDrawBinaries& p_binaries, const RUSubmeshInstance3D& p_category) {
    if (p_category.submesh_count > 0 && p_category.submeshes == nullptr) {
        throw std::runtime_error("Failed to compile mesh buffer: submesh storage is missing.");
    }
    if (p_category.instance_count > 0 && p_category.instances == nullptr) {
        throw std::runtime_error("Failed to compile mesh buffer: instance storage is missing.");
    }
    for (size_t i = 0; i < p_category.submesh_count; ++i) {
        const RUSubmesh3D& submesh = p_category.submeshes.get()[i];
        validate_submesh_views(submesh, submesh.indices);
        if (submesh.first_instance > p_category.instance_count ||
            submesh.instance_count > p_category.instance_count - submesh.first_instance) {
            throw std::runtime_error("Failed to compile mesh buffer: submesh instance range is out of bounds.");
        }
        if (submesh.indices.index_count > 0) {
            accumulate_shared_binaries(p_binaries, submesh, submesh.indices);
        }
    }
}

std::string build_buffer_cache_key(RefRes<RUBinary> p_binary, VkBufferUsageFlags p_usage) {
    if (p_binary == nullptr) {
        throw std::runtime_error("Failed to build mesh buffer cache key: binary resource is missing.");
    }
    if (p_binary->resource_key != nullptr && p_binary->resource_key[0] != '\0') {
        return std::format("{}#vk_usage_{}", p_binary->resource_key, static_cast<uint64_t>(p_usage));
    }
    return std::format("binary_resource_{}#vk_usage_{}", reinterpret_cast<uintptr_t>(p_binary.get()), static_cast<uint64_t>(p_usage));
}

Ref<ROBufferVK> build_binary_buffer(
    const RenderContextVK* p_render_data, RefRes<RUBinary> p_binary, VkBufferUsageFlags p_usage, std::string_view p_resource_kind) {
    if (p_render_data == nullptr || p_render_data->render_database == nullptr) {
        throw std::runtime_error("Failed to compile mesh buffer: render database is missing.");
    }
    if (p_binary == nullptr || p_binary->data == nullptr || p_binary->size == 0) {
        throw std::runtime_error(std::format("Failed to compile mesh buffer: {} binary data is missing.", p_resource_kind));
    }

    const std::string cache_key = build_buffer_cache_key(p_binary, p_usage);
    Ref<IRenderObject> registered_buffer =
        p_render_data->render_database->get_compiled_render_resource(cache_key, TypeUUID<ROBufferVK>::UUID);
    if (registered_buffer != MEM_NULL) {
        Ref<ROBufferVK> result = registered_buffer.dynamic_cast_ref<ROBufferVK>();
        if (result == MEM_NULL) {
            throw std::runtime_error("Failed to compile mesh buffer: registered geometry buffer has the wrong type.");
        }
        if (result->fields().buffer_size != p_binary->size) {
            throw std::runtime_error(
                "Failed to compile mesh buffer: registered geometry buffer size does not match its binary resource.");
        }
        return result;
    }

    Ref<ROBufferVK> result =
        p_render_data->create_buffer(ROBufferVK::Builder().set_domain(DeviceDomain::HOST_DEVICE).set_size(p_binary->size).set_usage(p_usage));
    result->update_buffer(p_binary->data.get(), p_binary->size);
    p_render_data->render_database->register_compiled_render_resource(cache_key, result);
    return result;
}

MeshBufferCompilerVK::BuildResult build_shared_buffers(const RenderContextVK* p_render_data,
    const MeshDrawBinaries& p_binaries,
    VkBufferUsageFlags p_vertex_usage,
    VkBufferUsageFlags p_index_usage) {
    if (p_binaries.vertex_binaries.empty() || p_binaries.index_binary == nullptr) {
        return {};
    }
    MeshBufferCompilerVK::BuildResult result;
    for (RefRes<RUBinary> vertex_binary : p_binaries.vertex_binaries) {
        result.vertex_buffers.push_back(build_binary_buffer(p_render_data, vertex_binary, p_vertex_usage, "vertex"));
        result.vertex_binaries.push_back(vertex_binary);
    }
    result.index_buffer = build_binary_buffer(p_render_data, p_binaries.index_binary, p_index_usage, "index");
    return result;
}

} // namespace

MeshBufferCompilerVK::MeshBufferCompilerVK(const RenderContextVK* p_render_data) : render_data(p_render_data) {
    if (render_data == nullptr || render_data->allocator == nullptr || render_data->render_database == nullptr) {
        throw std::runtime_error("Failed to create mesh buffer compiler: renderer state is missing.");
    }
}

MeshBufferCompilerVK::BuildResult MeshBufferCompilerVK::build(
    RefRes<RUMesh3D> p_mesh, VkBufferUsageFlags p_vertex_usage, VkBufferUsageFlags p_index_usage) const {
    if (p_mesh == nullptr) {
        return {};
    }
    MeshDrawBinaries binaries;
    accumulate_category_binaries(binaries, p_mesh->opaque_instances);
    accumulate_category_binaries(binaries, p_mesh->masked_instances);
    accumulate_category_binaries(binaries, p_mesh->double_sided_opaque_instances);
    return build_shared_buffers(render_data, binaries, p_vertex_usage, p_index_usage);
}

} // namespace WhiteBirdEngine
