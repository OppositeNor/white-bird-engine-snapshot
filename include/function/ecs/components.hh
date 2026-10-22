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
#ifndef WBE_FILE_COMPONENTS_HH
#define WBE_FILE_COMPONENTS_HH

#include "core/memory/reference_strong.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/function_utils.hh"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/fwd.hpp"
#include "utils/utils.hh"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string_view>
namespace WhiteBirdEngine {

class SceneNode;

/**
 * @class ComponentLabel
 * @brief Label of the entity.
 */
struct WBE_META(WBE_COMPONENT) ComponentLabel {
    static constexpr size_t MAX_NAME_SIZE = 15;
    // The buffer for the name.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    Buffer<MAX_NAME_SIZE + 1> name_buffer;

    ComponentLabel() : name_buffer({.buffer = ""}) {
    }

    /**
     * @brief Get the name of the entity.
     *
     * @return A view of the null-terminated name stored in this component's own buffer; it stays valid across copies
     * and moves of the component only as long as this instance is alive.
     */
    std::string_view get_name() const {
        return name_buffer.buffer;
    }
};

/**
 * @class ComponentTransform
 * @brief Component that stores a transform based on scale, rotation and
 * translation data. The transform will be performed in the order of: scale,
 * rotate and then translation (i.e. translation * rotation * scale).
 */
struct WBE_META(WBE_COMPONENT) ComponentTransform {
    // The scale value: x, y, z
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::vec3 scale;
    // The rotation value in quaternion: r, x, y, z
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::quat rotation;
    // The translation value: x, y, z
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::vec3 translation;
};

/**
 * @class ComponentTransform
 * @brief Transform component.
 */
struct WBE_META(WBE_COMPONENT) ComponentTransformMats {
    // The local transform matrix.
    glm::mat4 local_transform = glm::mat4(1);
    // The global transform matrix.
    glm::mat4 global_transform = glm::mat4(1);
    // A counter to check if this transform is up to date.
    int64_t version = -1;
};

/**
 * @class ComponentCamera
 * @brief Render-facing camera component mirror used by gameplay code.
 *
 * This mirrors the Vulkan camera uniform-buffer payload and adds a placeholder dirty
 * bit for future scene synchronization.
 */
struct WBE_META(WBE_COMPONENT) ComponentCamera {
    // The camera-local transform relative to the owning entity.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::mat4 local_transform = glm::mat4(1);
    // The view matrix.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::mat4 view = glm::mat4(1);
    // The projection matrix.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::mat4 proj = glm::mat4(1);
    // Placeholder dirty bit for future scene synchronization.
    WBE_META(WBE_REFLECT)
    bool is_dirty = false;
};

/**
 * @class ComponentHierarchy
 * @brief Hierarchy relation for an entity.
 */
struct WBE_META(WBE_COMPONENT) ComponentHierarchy {
    // The parent of this entity.
    EntityID parent = entt::null;
};

/**
 * @class ComponentDriver
 * @brief Connects an entity to its setup, ready, update, and teardown behavior.
 *
 * `local_data` is non-owning. A driver that allocates state must release it
 * from its teardown callback.
 */
struct WBE_META(WBE_COMPONENT) ComponentDriver {
    /**
     * @brief Callback invoked while the owning world is being set up.
     *
     * @param p_self The scene node associated with the driver entity.
     */
    using Setup = void (*)(Ref<SceneNode> p_self);

    /**
     * @brief Callback invoked when the owning world becomes ready.
     *
     * @param p_self The scene node associated with the driver entity.
     */
    using Ready = void (*)(Ref<SceneNode> p_self);

    /**
     * @brief Callback invoked for each world update.
     *
     * @param p_self The scene node associated with the driver entity.
     * @param p_delta The elapsed time since the previous update, in seconds.
     */
    using Update = void (*)(Ref<SceneNode> p_self, double p_delta);

    /**
     * @brief Callback invoked while the owning world is being torn down.
     *
     * @param p_self The scene node associated with the driver entity.
     */
    using Teardown = void (*)(Ref<SceneNode> p_self);

    /** @brief Setup callback, or nullptr when no setup work is required. */
    Setup setup = nullptr;
    /** @brief Ready callback, or nullptr when no ready work is required. */
    Ready ready = nullptr;
    /** @brief Update callback, or nullptr when no update work is required. */
    Update update = nullptr;
    /** @brief Teardown callback, or nullptr when no teardown work is required. */
    Teardown teardown = nullptr;
    /** @brief Non-owning driver-specific data managed by the callbacks. */
    void* local_data = nullptr;
};

/**
 * @class ComponentSpotLight
 * @brief A spot light source. The spot light shines from `position` along
 *        `direction` and is fully bright inside the inner cone, fades
 *        smoothly to zero between the inner and outer cones, and is also
 *        attenuated by distance up to `range`. Cone angles are stored as
 *        cosines of the half-angle to keep shader evaluation cheap.
 */
struct WBE_META(WBE_COMPONENT) ComponentSpotLight {
    /** @brief Light-space view matrix (world to light view space). */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::mat4 view = glm::mat4(1);
    /** @brief Light-space projection matrix (light view space to clip space). */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::mat4 proj = glm::mat4(1);
    // World-space position of the light source.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::vec3 position;
    // World-space direction the light is pointing (unit vector).
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::vec3 direction;
    // Linear-space light color.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::vec3 color;
    // Scalar light intensity multiplier (radiant flux scale).
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    float intensity;
    // Cosine of the inner cone half-angle. Inside this cone the spot
    // attenuation is 1.0.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    float inner_cone_cos;
    // Cosine of the outer cone half-angle. Outside this cone the spot
    // attenuation is 0.0.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    float outer_cone_cos;
    // Maximum effective light distance. Intensity smoothly falls to zero
    // as the surface-to-light distance approaches this value.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    float range;
    /**
     * @brief Projected emitter width in world units. Controls the shadow-filter footprint.
     */
    WBE_META(WBE_REFLECT)
    float emitter_width = 0.1F;
    // Side length, in texels, of the square shadow map allocated for this
    // light. Higher values give crisper shadows at the cost of more memory
    // and longer shadow-pass render times.
    WBE_META(WBE_REFLECT)
    uint32_t shadow_map_resolution = 1024;
};

} // namespace WhiteBirdEngine
#endif
