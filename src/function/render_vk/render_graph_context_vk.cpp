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
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_context_vk.hh"

namespace WhiteBirdEngine {

RendGraphContextVK RendGraphContextVK::from_render_data(const RenderContextVK& p_render_data) {
    return RendGraphContextVK{.swap_chain_extent = p_render_data.swap_chain_extent};
}

bool RendGraphContextVK::operator==(const RendGraphContextVK& p_other) const {
    return swap_chain_extent.width == p_other.swap_chain_extent.width &&
           swap_chain_extent.height == p_other.swap_chain_extent.height;
}

} // namespace WhiteBirdEngine
