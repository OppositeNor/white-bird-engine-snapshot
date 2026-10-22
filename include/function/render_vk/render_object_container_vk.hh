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
#ifndef WBE_FILE_RENDER_OBJECT_CONTAINER_VK_HH
#define WBE_FILE_RENDER_OBJECT_CONTAINER_VK_HH

#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/i_render_object.hh"
#include "function/render/i_render_object_container.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class RenderObjectContainerVK
 * @brief Abstract container that resolves the render object bound to one graph resource.
 *
 * Every container creates its render objects with a factory, one call per slot, when it is constructed and again when
 * recreate() is called. Containers created with make_ref() bind themselves under the requested graph resource id.
 */
class RenderObjectContainerVK : public IRenderObjectContainer,
                                public ThisRef<RenderObjectContainerVK>,
                                public IBindableRenderResourceVK {
public:
    /**
     * @brief Factory of the render object of one slot.
     *
     * Called with every slot index when the container is constructed and when it is recreated. It must return a non-null
     * render object, and is called on the thread that constructs or recreates the container.
     */
    using Factory = std::function<Ref<IRenderObjectVK>(uint32_t)>;

    virtual ~RenderObjectContainerVK() = default;
    RenderObjectContainerVK(const RenderObjectContainerVK&) = delete;
    RenderObjectContainerVK(RenderObjectContainerVK&&) noexcept = delete;
    RenderObjectContainerVK& operator=(const RenderObjectContainerVK&) = delete;
    RenderObjectContainerVK& operator=(RenderObjectContainerVK&&) noexcept = delete;

    virtual Ref<IRenderObject> get() override = 0;
    virtual Ref<IRenderObject> get() const override = 0;

    /**
     * @brief Resolve the resource for an explicit slot.
     * @param p_slot Slot in the container.
     * @return Resolved render object.
     */
    virtual Ref<IRenderObject> get(uint32_t p_slot) override = 0;

    /**
     * @brief Resolve the resource for an explicit slot.
     * @param p_slot Slot in the container.
     * @return Resolved render object.
     */
    virtual Ref<IRenderObject> get(uint32_t p_slot) const override = 0;

    /**
     * @brief Replace the resource bound to one explicit slot.
     *
     * @param p_slot Slot index to update.
     * @param p_resource Replacement resource.
     */
    virtual void set(uint32_t p_slot, Ref<IRenderObjectVK> p_resource) = 0;

    /**
     * @brief Clear the resource bound to one explicit slot.
     *
     * @param p_slot Slot index to clear.
     */
    virtual void clear(uint32_t p_slot) = 0;

    /**
     * @brief Clear all slot bindings retained by this container.
     */
    virtual void clear() = 0;
    /**
     * @brief Replace the render object of every slot with a new one from the container's factory.
     *
     * The previous render objects are released; users that resolve the container observe the new objects. Must be called on
     * the rendering thread while no submitted work uses the previous objects.
     *
     * @throws std::runtime_error If the factory returns null for a slot.
     */
    virtual void recreate() = 0;

    /**
     * @brief Return structured descriptor-debug rows owned by this container.
     *
     * Implementations must inspect only already-bound resources and must not allocate descriptors or synchronize per-slot state.
     *
     * @return Flattened descriptor-debug rows.
     */
    virtual DebugDataRows descriptor_debug_data() const = 0;

    /**
     * @brief Serialize the descriptor-debug rows into one compact JSON array.
     *
     * @return Compact JSON array string.
     */
    std::string to_string() const;

    /**
     * @brief Import this container under p_resource_id.
     * @throws std::runtime_error If the container was not created with make_ref() or p_resource_id is empty.
     */
    virtual void bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const override;

protected:
    RenderObjectContainerVK() = default;
};

} // namespace WhiteBirdEngine

#endif
