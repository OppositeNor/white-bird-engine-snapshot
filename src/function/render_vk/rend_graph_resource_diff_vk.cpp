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
#include "function/render_vk/rend_graph_resource_diff_vk.hh"
#include "function/render_vk/render_vk_defs.hh"
#include <string>

namespace WhiteBirdEngine {

namespace {

RendResRebuildActionVK decide(const RendGraphResourceDiffVK::Signatures& p_previous,
    const RendGraphResourceDiffVK::Actions& p_decided,
    const std::string& p_resource_id,
    const RendResSignatureVK& p_next) {
    const auto previous = p_previous.find(p_resource_id);
    if (previous == p_previous.end()) {
        return RendResRebuildActionVK::CREATE;
    }
    const RendResSignatureVK& old = previous->second;
    if (old.kind != p_next.kind || old.per_frame != p_next.per_frame) {
        return RendResRebuildActionVK::REPLACE;
    }
    if (p_next.kind == RendResKindVK::IMPORT) {
        return old.import_identity == p_next.import_identity ? RendResRebuildActionVK::KEEP : RendResRebuildActionVK::REPLACE;
    }
    if (old.desc != p_next.desc || old.image_id != p_next.image_id) {
        return RendResRebuildActionVK::RECREATE;
    }
    if (p_next.kind == RendResKindVK::IMAGE_VIEW) {
        // A view of a new, recreated, or replaced image must be recreated for the new image object.
        const auto image = p_decided.find(p_next.image_id);
        if (image != p_decided.end() && image->second != RendResRebuildActionVK::KEEP) {
            return RendResRebuildActionVK::RECREATE;
        }
    }
    return RendResRebuildActionVK::KEEP;
}

} // namespace

RendGraphResourceDiffVK::Actions RendGraphResourceDiffVK::plan(
    const Signatures& p_previous, const Signatures& p_next, RenderAllocator* p_allocator) {
    Actions actions(p_allocator);
    actions.reserve(p_next.size());
    // Views are decided after everything else, because their action depends on the action of their image.
    for (const auto& [resource_id, signature] : p_next) {
        if (signature.kind != RendResKindVK::IMAGE_VIEW) {
            actions.emplace(resource_id, decide(p_previous, actions, resource_id, signature));
        }
    }
    for (const auto& [resource_id, signature] : p_next) {
        if (signature.kind == RendResKindVK::IMAGE_VIEW) {
            actions.emplace(resource_id, decide(p_previous, actions, resource_id, signature));
        }
    }
    return actions;
}

} // namespace WhiteBirdEngine
