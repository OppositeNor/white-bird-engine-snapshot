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
#include "function/render_vk/render_object_container_vk.hh"
#include "core/memory/reference_strong.hh"
#include "core/parser/parser_json.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace WhiteBirdEngine {

namespace {

std::string descriptor_rows_to_string(const DebugDataRows& p_rows) {
    DebugDataArray rows;
    rows.reserve(p_rows.size());
    for (const DebugData& row : p_rows) {
        rows.push_back(row);
    }
    DebugData root;
    root.set(std::move(rows));
    return ParserJSON::write(root);
}

} // namespace

std::string RenderObjectContainerVK::to_string() const {
    return descriptor_rows_to_string(descriptor_debug_data());
}

void RenderObjectContainerVK::bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const {
    bind_container(p_graph, p_resource_id, acquire_this_ref());
}

} // namespace WhiteBirdEngine
