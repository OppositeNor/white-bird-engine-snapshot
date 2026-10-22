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
#ifndef WBE_FILE_REND_GRAPH_RESOURCE_DIFF_VK_HH
#define WBE_FILE_REND_GRAPH_RESOURCE_DIFF_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include <cstdint>
#include <string>
#include <variant>

namespace WhiteBirdEngine {

/**
 * @enum RendResKindVK
 * @brief What a render graph resource is: one of the graph-owned kinds, or an import owned outside the graph.
 */
enum class RendResKindVK : uint8_t {
    BUFFER,
    IMAGE,
    IMAGE_VIEW,
    SAMPLER,
    IMPORT,
};

/**
 * @class RendResSignatureVK
 * @brief Everything that decides whether a render graph resource can be kept across a rebuild. Copyable and movable.
 */
struct RendResSignatureVK {
    /**
     * @brief The kind of the resource.
     */
    RendResKindVK kind = RendResKindVK::IMPORT;
    /**
     * @brief The configuration a graph-owned resource was evaluated to for the context: the description matching kind, or
     * std::monostate for an import.
     */
    std::variant<std::monostate, RendBufferDescVK, RendImageDescVK, RendImageViewDescVK, RendSamplerDescVK> desc;
    /**
     * @brief The id of the image an image view views; empty for every other kind.
     */
    std::string image_id;
    /**
     * @brief Whether the resource holds one object per frame in flight.
     */
    bool per_frame = false;
    /**
     * @brief The identity of an import: the address of the container or resource list it binds. Null for graph-owned
     * resources; never dereferenced.
     */
    const void* import_identity = nullptr;
};

/**
 * @enum RendResRebuildActionVK
 * @brief What a graph rebuild does with one resource of the new descriptor.
 */
enum class RendResRebuildActionVK : uint8_t {
    /** The id is new: create a container. */
    CREATE,
    /** Nothing that shapes the resource changed: keep the container and its objects, with their content. */
    KEEP,
    /** The configuration changed but the container still fits: recreate its objects in place. */
    RECREATE,
    /** The kind, the per-frame layout, or the imported object changed: the old container cannot be reused. */
    REPLACE,
};

/**
 * @class RendGraphResourceDiffVK
 * @brief Decides, per resource id, how a render graph rebuild treats each resource of its new descriptor.
 *
 * A resource is kept only when its id, kind, per-frame layout, and evaluated description all match, an import binds the
 * same object, and, for an image view, it views the same image id and that image is itself kept. Ids missing from the
 * new signatures are removed by the caller. Not instantiable.
 */
class RendGraphResourceDiffVK final {
public:
    RendGraphResourceDiffVK() = delete;
    ~RendGraphResourceDiffVK() = delete;
    RendGraphResourceDiffVK(const RendGraphResourceDiffVK&) = delete;
    RendGraphResourceDiffVK(RendGraphResourceDiffVK&&) noexcept = delete;
    RendGraphResourceDiffVK& operator=(const RendGraphResourceDiffVK&) = delete;
    RendGraphResourceDiffVK& operator=(RendGraphResourceDiffVK&&) noexcept = delete;

    /**
     * @brief Resource signatures keyed by resource id.
     */
    using Signatures = Map<std::string, RendResSignatureVK, RenderAllocator>;
    /**
     * @brief Rebuild actions keyed by resource id.
     */
    using Actions = Map<std::string, RendResRebuildActionVK, RenderAllocator>;

    /**
     * @brief Plan a rebuild.
     *
     * @param p_previous The signatures of the current build; empty for a first build.
     * @param p_next The signatures of the descriptor being built.
     * @param p_allocator The allocator of the result.
     * @return One action for every id of p_next.
     */
    static Actions plan(const Signatures& p_previous, const Signatures& p_next, RenderAllocator* p_allocator);
};

} // namespace WhiteBirdEngine

#endif
