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
#ifndef WBE_FILE_RENDER_RESOURCE_LIST_VK_HH
#define WBE_FILE_RENDER_RESOURCE_LIST_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/i_render_object.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include <cstddef>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class RenderResourceListVK
 * @brief CPU-side ordered group of render objects that can be bound to a render graph as one aggregate resource.
 *
 * The list retains shared ownership of its entries but does not create GPU resources. It is neither copyable nor movable;
 * share it through `Ref<RenderResourceListVK>`. Binding registers the aggregate under the requested id and every entry as
 * `<id>[<index>]`.
 */
class RenderResourceListVK final : public ThisRef<RenderResourceListVK>, public IBindableRenderResourceVK {
public:
    RenderResourceListVK() = delete;
    ~RenderResourceListVK() = default;
    RenderResourceListVK(const RenderResourceListVK&) = delete;
    RenderResourceListVK(RenderResourceListVK&&) noexcept = delete;
    RenderResourceListVK& operator=(const RenderResourceListVK&) = delete;
    RenderResourceListVK& operator=(RenderResourceListVK&&) noexcept = delete;

    /**
     * @brief Construct an empty resource list.
     * @param p_allocator Allocator retained by the internal vector; it must outlive this list.
     * @throws std::runtime_error If the allocator is null.
     */
    explicit RenderResourceListVK(RenderAllocator* p_allocator);

    /**
     * @brief Append one render object.
     * @param p_resource Resource whose shared ownership is transferred into the list.
     * @return This resource list.
     * @throws std::runtime_error If the resource is null.
     */
    RenderResourceListVK& add_resource(Ref<IRenderObject> p_resource);

    /**
     * @brief Get the number of retained resources.
     * @return Resource count.
     */
    size_t get_resource_count() const;

    /**
     * @brief Acquire shared ownership of one retained resource.
     * @param p_index Zero-based resource index.
     * @return The indexed render object.
     * @throws std::runtime_error If the index is out of bounds.
     */
    Ref<IRenderObject> acquire_resource(size_t p_index) const;

    /**
     * @brief Import this list under p_resource_id.
     * @throws std::runtime_error If the list was not created with make_ref() or p_resource_id is empty.
     */
    virtual void bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const override;

private:
    Vector<Ref<IRenderObject>, RenderAllocator> resources;
};

} // namespace WhiteBirdEngine

#endif
