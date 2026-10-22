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
#include "function/render_vk/mesh_draw_batch_vk.hh"
#include "function/render/vertex_input_requirement.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include "function/render_vk/vertex_input_description_vk.hh"
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace WhiteBirdEngine {

VertexInputDescriptionVK MeshDrawBatchVK::resolve_vertex_input(
    const VertexInputRequirement& p_requirement, std::vector<VertexBufferBindingVK>& p_vertex_buffers) const {
    if (vertex_buffers.size() != vertex_bindings.size()) {
        throw std::runtime_error("Failed to resolve mesh draw batch vertex input: vertex buffers do not parallel the vertex bindings.");
    }
    VertexInputDescriptionVK result = build_vertex_input_description_vk(p_requirement, vertex_bindings);
    p_vertex_buffers.clear();
    p_vertex_buffers.reserve(result.source_bindings.size());
    for (size_t source_binding : result.source_bindings) {
        p_vertex_buffers.push_back(vertex_buffers[source_binding]);
    }
    return result;
}

} // namespace WhiteBirdEngine
