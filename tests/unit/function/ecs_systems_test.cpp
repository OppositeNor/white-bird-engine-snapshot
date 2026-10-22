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
#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/ecs/components.hh"
#include "function/ecs/systems.hh"
#include "function/function_utils.hh"
#include "function/scene/scene_node.hh"
#include "function/scene/world.hh"
#include "glm/matrix.hpp"
#include "global/global.hh"
#include "test_global.hh"
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <utility>

namespace WBE = WhiteBirdEngine;

namespace {

constexpr float EXPECTED_FLOAT_ERROR = 0.0001F;

WBE::ComponentTransform make_transform(glm::vec3 p_scale, glm::vec3 p_translation) {
    return WBE::ComponentTransform{
        .scale = p_scale,
        .rotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F),
        .translation = p_translation,
    };
}

glm::mat4 make_transform_mat(const WBE::ComponentTransform& p_transform) {
    glm::mat4 translation_mat = glm::translate(glm::mat4(1.0F), p_transform.translation);
    return glm::scale(translation_mat * glm::mat4_cast(p_transform.rotation), p_transform.scale);
}

void expect_mat_near(const glm::mat4& p_actual, const glm::mat4& p_expected) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(p_actual[col][row], p_expected[col][row], EXPECTED_FLOAT_ERROR);
        }
    }
}

struct DriverState {
    int setup_count = 0;
    int ready_count = 0;
    int update_count = 0;
    int teardown_count = 0;
    double last_delta = 0.0;
};

DriverState& get_driver_state(WBE::Ref<WBE::SceneNode> p_self) {
    auto& driver = p_self->get_world()->get_entities().get<WBE::ComponentDriver>(p_self->get_entity());
    return *static_cast<DriverState*>(driver.local_data);
}

void driver_setup(WBE::Ref<WBE::SceneNode> p_self) {
    ++get_driver_state(std::move(p_self)).setup_count;
}

void driver_ready(WBE::Ref<WBE::SceneNode> p_self) {
    ++get_driver_state(std::move(p_self)).ready_count;
}

void driver_update(WBE::Ref<WBE::SceneNode> p_self, double p_delta) {
    DriverState& state = get_driver_state(std::move(p_self));
    ++state.update_count;
    state.last_delta = p_delta;
}

void driver_teardown(WBE::Ref<WBE::SceneNode> p_self) {
    ++get_driver_state(std::move(p_self)).teardown_count;
}

class WBEWorldSystemTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
        world = WBE::make_ref<WBE::World>(global->engine_core->pool_allocator, global->engine_core->pool_allocator);
    }

    virtual void TearDown() override {
        world = WBE::MEM_NULL;
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;
    WBE::Ref<WBE::World> world;
};

class WBEDriverSystemTest : public WBEWorldSystemTest {
protected:
    WBE::Ref<WBE::SceneNode> add_driver(DriverState* p_state) {
        auto node = WBE::make_ref<WBE::SceneNode>(global->engine_core->pool_allocator, world);
        world->get_entities().emplace<WBE::ComponentDriver>(node->get_entity(),
            WBE::ComponentDriver{
                .setup = driver_setup,
                .ready = driver_ready,
                .update = driver_update,
                .teardown = driver_teardown,
                .local_data = p_state,
            });
        return node;
    }
};

class WBETransformSystemTest : public WBEWorldSystemTest {};

class WBECameraSystemTest : public WBEWorldSystemTest {};

} // namespace

TEST_F(WBEDriverSystemTest, RunsEveryDriverLifecycleCallback) {
    DriverState first_state;
    DriverState second_state;
    auto first_node = add_driver(&first_state);
    auto second_node = add_driver(&second_state);

    WBE::system_driver_setup(*world);
    WBE::system_driver_ready(*world);
    WBE::system_driver_update(*world, 0.25);
    WBE::system_driver_teardown(*world);

    for (const DriverState* state : {&first_state, &second_state}) {
        EXPECT_EQ(state->setup_count, 1);
        EXPECT_EQ(state->ready_count, 1);
        EXPECT_EQ(state->update_count, 1);
        EXPECT_EQ(state->teardown_count, 1);
        EXPECT_DOUBLE_EQ(state->last_delta, 0.25);
    }
}

TEST_F(WBEDriverSystemTest, SkipsNullCallbacks) {
    auto node = WBE::make_ref<WBE::SceneNode>(global->engine_core->pool_allocator, world);
    world->get_entities().emplace<WBE::ComponentDriver>(node->get_entity());

    EXPECT_NO_THROW(WBE::system_driver_setup(*world));
    EXPECT_NO_THROW(WBE::system_driver_ready(*world));
    EXPECT_NO_THROW(WBE::system_driver_update(*world, 1.0));
    EXPECT_NO_THROW(WBE::system_driver_teardown(*world));
}

TEST_F(WBEDriverSystemTest, ThrowsWhenCallbackEntityHasNoSceneNode) {
    WBE::EntityID entity = world->get_entities().create();
    world->get_entities().emplace<WBE::ComponentDriver>(entity, WBE::ComponentDriver{.setup = driver_setup});

    EXPECT_THROW(WBE::system_driver_setup(*world), std::runtime_error);
}

TEST_F(WBETransformSystemTest, EntityWithoutHierarchyIsTreatedAsRoot) {
    auto& entities = world->get_entities();
    WBE::EntityID entity = entities.create();
    WBE::ComponentTransform transform = make_transform(glm::vec3(2.0F, 3.0F, 4.0F), glm::vec3(5.0F, 6.0F, 7.0F));

    entities.emplace<WBE::ComponentTransform>(entity, transform);
    entities.emplace<WBE::ComponentTransformMats>(entity);

    WBE::update_transform_mats(*world, 7);

    const auto& transform_mats = entities.get<WBE::ComponentTransformMats>(entity);
    glm::mat4 expected_transform = make_transform_mat(transform);
    expect_mat_near(transform_mats.local_transform, expected_transform);
    expect_mat_near(transform_mats.global_transform, expected_transform);
    EXPECT_EQ(transform_mats.version, 7);
}

TEST_F(WBETransformSystemTest, ChildUsesParentGlobalTransformFromHierarchyComponent) {
    auto& entities = world->get_entities();
    WBE::EntityID parent = entities.create();
    WBE::EntityID child = entities.create();
    WBE::ComponentTransform parent_transform = make_transform(glm::vec3(1.0F), glm::vec3(10.0F, 0.0F, 0.0F));
    WBE::ComponentTransform child_transform = make_transform(glm::vec3(1.0F), glm::vec3(0.0F, 2.0F, 0.0F));

    entities.emplace<WBE::ComponentTransform>(parent, parent_transform);
    entities.emplace<WBE::ComponentTransformMats>(parent);
    entities.emplace<WBE::ComponentTransform>(child, child_transform);
    entities.emplace<WBE::ComponentTransformMats>(child);
    entities.emplace<WBE::ComponentHierarchy>(child, parent);

    WBE::update_transform_mats(*world, 3);

    const auto& parent_transform_mats = entities.get<WBE::ComponentTransformMats>(parent);
    const auto& child_transform_mats = entities.get<WBE::ComponentTransformMats>(child);
    glm::mat4 expected_parent_transform = make_transform_mat(parent_transform);
    glm::mat4 expected_child_local_transform = make_transform_mat(child_transform);
    expect_mat_near(parent_transform_mats.global_transform, expected_parent_transform);
    expect_mat_near(child_transform_mats.local_transform, expected_child_local_transform);
    expect_mat_near(child_transform_mats.global_transform, expected_parent_transform * expected_child_local_transform);
    EXPECT_EQ(parent_transform_mats.version, 3);
    EXPECT_EQ(child_transform_mats.version, 3);
}

TEST_F(WBETransformSystemTest, ChildWithMissingParentTransformIsTreatedAsRoot) {
    auto& entities = world->get_entities();
    WBE::EntityID parent = entities.create();
    WBE::EntityID child = entities.create();
    WBE::ComponentTransform child_transform = make_transform(glm::vec3(3.0F), glm::vec3(0.0F, 4.0F, 0.0F));

    entities.emplace<WBE::ComponentTransform>(child, child_transform);
    entities.emplace<WBE::ComponentTransformMats>(child);
    entities.emplace<WBE::ComponentHierarchy>(child, parent);

    WBE::update_transform_mats(*world, 11);

    const auto& child_transform_mats = entities.get<WBE::ComponentTransformMats>(child);
    glm::mat4 expected_child_transform = make_transform_mat(child_transform);
    expect_mat_near(child_transform_mats.local_transform, expected_child_transform);
    expect_mat_near(child_transform_mats.global_transform, expected_child_transform);
    EXPECT_EQ(child_transform_mats.version, 11);
}

TEST_F(WBETransformSystemTest, EntityWithNullHierarchyParentIsTreatedAsRoot) {
    auto& entities = world->get_entities();
    WBE::EntityID entity = entities.create();
    WBE::ComponentTransform transform = make_transform(glm::vec3(1.0F), glm::vec3(8.0F, 9.0F, 10.0F));

    entities.emplace<WBE::ComponentTransform>(entity, transform);
    entities.emplace<WBE::ComponentTransformMats>(entity);
    entities.emplace<WBE::ComponentHierarchy>(entity);

    WBE::update_transform_mats(*world, 13);

    const auto& transform_mats = entities.get<WBE::ComponentTransformMats>(entity);
    glm::mat4 expected_transform = make_transform_mat(transform);
    expect_mat_near(transform_mats.global_transform, expected_transform);
    EXPECT_EQ(transform_mats.version, 13);
}

TEST_F(WBETransformSystemTest, RecursivelyUpdatesMultiLevelHierarchyOutOfOrder) {
    auto& entities = world->get_entities();
    WBE::EntityID grandparent = entities.create();
    WBE::EntityID parent = entities.create();
    WBE::EntityID child = entities.create();
    WBE::ComponentTransform grandparent_transform = make_transform(glm::vec3(1.0F), glm::vec3(1.0F, 0.0F, 0.0F));
    WBE::ComponentTransform parent_transform = make_transform(glm::vec3(1.0F), glm::vec3(0.0F, 2.0F, 0.0F));
    WBE::ComponentTransform child_transform = make_transform(glm::vec3(1.0F), glm::vec3(0.0F, 0.0F, 3.0F));

    entities.emplace<WBE::ComponentTransform>(child, child_transform);
    entities.emplace<WBE::ComponentTransformMats>(child);
    entities.emplace<WBE::ComponentHierarchy>(child, parent);
    entities.emplace<WBE::ComponentTransform>(parent, parent_transform);
    entities.emplace<WBE::ComponentTransformMats>(parent);
    entities.emplace<WBE::ComponentHierarchy>(parent, grandparent);
    entities.emplace<WBE::ComponentTransform>(grandparent, grandparent_transform);
    entities.emplace<WBE::ComponentTransformMats>(grandparent);

    WBE::update_transform_mats(*world, 17);

    const auto& grandparent_transform_mats = entities.get<WBE::ComponentTransformMats>(grandparent);
    const auto& parent_transform_mats = entities.get<WBE::ComponentTransformMats>(parent);
    const auto& child_transform_mats = entities.get<WBE::ComponentTransformMats>(child);
    glm::mat4 expected_grandparent_transform = make_transform_mat(grandparent_transform);
    glm::mat4 expected_parent_transform = make_transform_mat(parent_transform);
    glm::mat4 expected_child_transform = make_transform_mat(child_transform);
    expect_mat_near(grandparent_transform_mats.global_transform, expected_grandparent_transform);
    expect_mat_near(parent_transform_mats.global_transform, expected_grandparent_transform * expected_parent_transform);
    expect_mat_near(child_transform_mats.global_transform,
        expected_grandparent_transform * expected_parent_transform * expected_child_transform);
    EXPECT_EQ(grandparent_transform_mats.version, 17);
    EXPECT_EQ(parent_transform_mats.version, 17);
    EXPECT_EQ(child_transform_mats.version, 17);
}

TEST_F(WBECameraSystemTest, CameraViewUsesEntityGlobalTransformInverse) {
    auto& entities = world->get_entities();
    WBE::EntityID entity = entities.create();
    WBE::ComponentTransform transform = make_transform(glm::vec3(1.0F), glm::vec3(3.0F, 4.0F, 5.0F));

    entities.emplace<WBE::ComponentTransform>(entity, transform);
    entities.emplace<WBE::ComponentTransformMats>(entity);
    entities.emplace<WBE::ComponentCamera>(entity);

    WBE::update_transform_mats(*world, 23);
    WBE::update_camera_views(*world);

    const auto& transform_mats = entities.get<WBE::ComponentTransformMats>(entity);
    const auto& camera = entities.get<WBE::ComponentCamera>(entity);
    expect_mat_near(camera.view, glm::inverse(transform_mats.global_transform));
}

TEST_F(WBECameraSystemTest, CameraLocalTransformParticipatesInViewMatrix) {
    auto& entities = world->get_entities();
    WBE::EntityID entity = entities.create();
    WBE::ComponentTransform transform = make_transform(glm::vec3(1.0F), glm::vec3(2.0F, 0.0F, 0.0F));
    glm::mat4 camera_local_transform = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 1.5F, -2.0F));

    entities.emplace<WBE::ComponentTransform>(entity, transform);
    entities.emplace<WBE::ComponentTransformMats>(entity);
    entities.emplace<WBE::ComponentCamera>(entity, WBE::ComponentCamera{.local_transform = camera_local_transform});

    WBE::update_transform_mats(*world, 29);
    WBE::update_camera_views(*world);

    const auto& transform_mats = entities.get<WBE::ComponentTransformMats>(entity);
    const auto& camera = entities.get<WBE::ComponentCamera>(entity);
    expect_mat_near(camera.view, glm::inverse(transform_mats.global_transform * camera_local_transform));
}
