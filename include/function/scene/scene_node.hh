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
#ifndef WBE_FILE_SCENE_NODE_HH
#define WBE_FILE_SCENE_NODE_HH

#include "core/allocator/stl_allocator.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/function_utils.hh"
#include "utils/utils.hh"
#include "world.hh"
#include <stdexcept>
#include <utility>
namespace WhiteBirdEngine {
/**
 * @class SceneNode
 * @brief A node of the scene.
 *
 * @todo Test
 */
class WBE_META() SceneNode final : public ThisRef<SceneNode> {
protected:
public:
    SceneNode() = delete;
    virtual ~SceneNode() override;
    SceneNode(const SceneNode&) = delete;
    SceneNode(SceneNode&& p_other) noexcept;
    SceneNode& operator=(const SceneNode&) = delete;
    SceneNode& operator=(SceneNode&& p_other) noexcept;

    /**
     * @brief Set the reference referencing this instance.
     *
     * @param p_ref_of_this The reference referencing this instance.
     */
    void set_ref_of_this(Ref<SceneNode> p_ref_of_this);

    /**
     * @brief Constructor.
     *
     * @param p_world The world this node lives in.
     */
    SceneNode(Ref<World> p_world);

    /**
     * @brief Get the entity instance owned by this node.
     *
     * @return The entity instance owned by this node.
     */
    EntityID get_entity() const {
        return entity;
    }

    /**
     * @brief Get the world this node lives in.
     *
     * @return The world owning this node's entity.
     */
    Ref<World> get_world() const {
        return world;
    }

    /**
     * @brief Construct a component on this node's entity.
     *
     * The component is stored in the world registry under its entt type, so has_component(), remove_component(), and
     * get_component_type_ids() observe it. The returned reference is invalidated when components of the same type are added
     * to or removed from the world.
     *
     * @tparam TComponent The component type to construct.
     * @tparam TArgs The constructor argument types.
     * @param p_args Arguments forwarded to the component constructor or aggregate initializer.
     * @return The constructed component.
     * @throws std::runtime_error If this node has no entity or the entity already has a TComponent.
     */
    template <typename TComponent, typename... TArgs>
    TComponent& add_component(TArgs&&... p_args) {
        if (world.is_null() || entity == entt::null) {
            throw std::runtime_error("Failed to add component: the scene node has no entity.");
        }
        entt::registry& entities = world->get_entities();
        if (entities.all_of<TComponent>(entity)) {
            throw std::runtime_error("Failed to add component: the entity already has a component of this type.");
        }
        return entities.emplace<TComponent>(entity, std::forward<TArgs>(p_args)...);
    }

    /**
     * @brief Remove a component from this node's entity.
     *
     * @param p_component_type_id The component type id to remove.
     */
    void remove_component(TypeID p_component_type_id);

    /**
     * @brief Check if this node's entity has a component.
     *
     * @param p_component_type_id The component type id to check.
     * @return True if the node has the component type, false otherwise.
     */
    bool has_component(TypeID p_component_type_id);

    /**
     * @brief Get this node's entity component type list.
     *
     * @return This node's entity component type list.
     */
    Vector<TypeID, HeapAllocatorDefault> get_component_type_ids();

private:
    void destroy_entity();

    String<HeapAllocatorDefault> name;
    Ref<World> world;
    EntityID entity = entt::null;
};

} // namespace WhiteBirdEngine

#endif
