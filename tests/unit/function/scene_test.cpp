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
#include "core/allocator/stack_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/core_utils.hh"
#include "core/engine_core.hh"
#include "core/job/job_handler.hh"
#include "core/memory/reference_strong.hh"
#include "function/ecs/components.hh"
#include "function/function_utils.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_database.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/scene/scene.hh"
#include "function/scene/scene_manager.hh"
#include "function/scene/scene_node.hh"
#include "function/scene/world.hh"
#include "global/global.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "test_global.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <algorithm>
#include <entt/entt.hpp>
#include <functional>
#include <future>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace WBE = WhiteBirdEngine;

namespace {

template <typename T>
WBE::TypeID get_component_type_id() {
    return entt::type_hash<T>::value();
}

WBE::ComponentTransform make_transform() {
    return WBE::ComponentTransform{
        .scale = glm::vec3(1.0F, 2.0F, 3.0F),
        .rotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F),
        .translation = glm::vec3(4.0F, 5.0F, 6.0F),
    };
}

struct SceneDriverState {
    int setup_count = 0;
    int ready_count = 0;
    int update_count = 0;
    int teardown_count = 0;
    double last_delta = 0.0;
    std::vector<std::string>* events = nullptr;
    std::string label;
};

SceneDriverState& get_scene_driver_state(WBE::Ref<WBE::SceneNode> p_self) {
    auto& driver = p_self->get_world()->get_entities().get<WBE::ComponentDriver>(p_self->get_entity());
    return *static_cast<SceneDriverState*>(driver.local_data);
}

void scene_driver_setup(WBE::Ref<WBE::SceneNode> p_self) {
    SceneDriverState& state = get_scene_driver_state(std::move(p_self));
    ++state.setup_count;
    if (state.events != nullptr) {
        state.events->push_back(state.label + "_setup");
    }
}

void scene_driver_ready(WBE::Ref<WBE::SceneNode> p_self) {
    SceneDriverState& state = get_scene_driver_state(std::move(p_self));
    ++state.ready_count;
    if (state.events != nullptr) {
        state.events->push_back(state.label + "_ready");
    }
}

void scene_driver_update(WBE::Ref<WBE::SceneNode> p_self, double p_delta) {
    SceneDriverState& state = get_scene_driver_state(std::move(p_self));
    ++state.update_count;
    state.last_delta = p_delta;
}

void scene_driver_teardown(WBE::Ref<WBE::SceneNode> p_self) {
    SceneDriverState& state = get_scene_driver_state(std::move(p_self));
    ++state.teardown_count;
    if (state.events != nullptr) {
        state.events->push_back(state.label + "_teardown");
    }
}

class RecordingRenderer final : public WBE::IRenderer {
public:
    explicit RecordingRenderer(WBE::RenderAllocator* p_allocator)
        : render_database(WBE::make_ref<WBE::RenderDatabase>(p_allocator, p_allocator)) {
    }

    virtual uint32_t get_worker_nums() override {
        return 1;
    }

    virtual void tick(double p_delta) override {
        (void)p_delta;
    }

    virtual WBE::Ref<WBE::RenderDatabase> get_render_database() override {
        return render_database;
    }

    virtual void register_render_graph(std::string_view p_resource_key) override {
        registered_graphs.emplace_back(p_resource_key);
    }

    virtual void unregister_render_graph(std::string_view p_resource_key) override {
        unregistered_graphs.emplace_back(p_resource_key);
    }

    virtual void activate_render_graph(std::string_view p_resource_key) override {
        activated_graphs.emplace_back(p_resource_key);
    }

    virtual std::future<void> add_job(std::function<void()> p_job) override {
        (void)p_job;
        return WBE::create_completed_job_future();
    }

    std::vector<std::string> registered_graphs;
    std::vector<std::string> unregistered_graphs;
    std::vector<std::string> activated_graphs;

private:
    WBE::Ref<WBE::RenderDatabase> render_database;
};

class WBEWorldTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    WBE::HeapAllocatorDefault* get_allocator() {
        return global->engine_core->pool_allocator;
    }

    std::unique_ptr<WBE::Global> global;
};

} // namespace

TEST_F(WBEWorldTest, WorldExposesAllocatorAndEntityRegistry) {
    WBE::World world(get_allocator());

    WBE::EntityID entity = world.get_entities().create();

    EXPECT_EQ(world.get_allocator(), get_allocator());
    EXPECT_TRUE(world.get_entities().valid(entity));
}

TEST_F(WBEWorldTest, WorldOwnsRenderGraphResourceKey) {
    WBE::World world(get_allocator());

    EXPECT_TRUE(world.get_render_graph_resource_key().empty());

    world.set_render_graph_resource_key("main_graph");

    EXPECT_EQ(world.get_render_graph_resource_key(), "main_graph");
}

TEST_F(WBEWorldTest, WorldOwnsStaticMeshReference) {
    WBE::World world(get_allocator());
    WBE::StackAllocator storage(WBE_KI_B(1));
    WBE::RefRes<WBE::RUMesh3D> mesh(storage.allocate(sizeof(WBE::RUMesh3D)), &storage);

    world.set_static_mesh(mesh);
    EXPECT_EQ(world.get_static_mesh(), mesh);
    world.set_static_mesh(WBE::MEM_NULL);
    EXPECT_EQ(world.get_static_mesh(), WBE::MEM_NULL);
}

TEST_F(WBEWorldTest, WorldCachesComponentListsByComponentTypeId) {
    WBE::World world(get_allocator());
    WBE::EntityID entity = world.get_entities().create();
    const WBE::TypeID transform_type_id = get_component_type_id<WBE::ComponentTransform>();

    EXPECT_EQ(world.get_component_list(transform_type_id), nullptr);

    world.get_entities().emplace<WBE::ComponentTransform>(entity, make_transform());
    entt::sparse_set* component_list = world.get_component_list(transform_type_id);

    ASSERT_NE(component_list, nullptr);
    EXPECT_TRUE(component_list->contains(entity));
    EXPECT_EQ(world.get_component_list(transform_type_id), component_list);
}

TEST_F(WBEWorldTest, WorldReturnsComponentTypeIdsAttachedToEntity) {
    WBE::World world(get_allocator());
    WBE::EntityID first_entity = world.get_entities().create();
    WBE::EntityID second_entity = world.get_entities().create();
    const WBE::TypeID transform_type_id = get_component_type_id<WBE::ComponentTransform>();
    const WBE::TypeID label_type_id = get_component_type_id<WBE::ComponentLabel>();

    world.get_entities().emplace<WBE::ComponentTransform>(first_entity, make_transform());
    world.get_entities().emplace<WBE::ComponentLabel>(first_entity);
    world.get_entities().emplace<WBE::ComponentLabel>(second_entity);

    WBE::Vector<WBE::TypeID, WBE::HeapAllocatorDefault> first_component_type_ids = world.get_component_type_ids(first_entity);
    WBE::Vector<WBE::TypeID, WBE::HeapAllocatorDefault> second_component_type_ids = world.get_component_type_ids(second_entity);

    EXPECT_NE(std::find(first_component_type_ids.begin(), first_component_type_ids.end(), transform_type_id),
        first_component_type_ids.end());
    EXPECT_NE(std::find(first_component_type_ids.begin(), first_component_type_ids.end(), label_type_id),
        first_component_type_ids.end());
    EXPECT_EQ(std::find(second_component_type_ids.begin(), second_component_type_ids.end(), transform_type_id),
        second_component_type_ids.end());
    EXPECT_NE(std::find(second_component_type_ids.begin(), second_component_type_ids.end(), label_type_id),
        second_component_type_ids.end());
}

TEST_F(WBEWorldTest, WorldReturnsEmptyComponentTypeIdsForEntityWithoutComponents) {
    WBE::World world(get_allocator());
    WBE::EntityID entity = world.get_entities().create();

    WBE::Vector<WBE::TypeID, WBE::HeapAllocatorDefault> component_type_ids = world.get_component_type_ids(entity);

    EXPECT_TRUE(component_type_ids.empty());
}

TEST_F(WBEWorldTest, SceneNodeLookupForUnknownOrDestroyedEntityReturnsNull) {
    auto world = WBE::make_ref<WBE::World>(get_allocator(), get_allocator());
    WBE::EntityID unknown_entity = world->get_entities().create();
    WBE::EntityID node_entity = entt::null;

    EXPECT_TRUE(world->get_scene_node(unknown_entity).is_null());

    {
        auto node = WBE::make_ref<WBE::SceneNode>(get_allocator(), world);
        node_entity = node->get_entity();

        EXPECT_TRUE(world->get_scene_node(node_entity) == node);
    }

    EXPECT_TRUE(world->get_scene_node(node_entity).is_null());
}

TEST_F(WBEWorldTest, SceneNodeCreatesAndDestroysEntityInWorld) {
    auto world = WBE::make_ref<WBE::World>(get_allocator(), get_allocator());
    WBE::EntityID entity = entt::null;

    {
        auto node = WBE::make_ref<WBE::SceneNode>(get_allocator(), world);
        entity = node->get_entity();

        EXPECT_TRUE(world->get_entities().valid(entity));
        EXPECT_TRUE(world->get_scene_node(entity) == node);
    }

    EXPECT_FALSE(world->get_entities().valid(entity));
    EXPECT_TRUE(world->get_scene_node(entity).is_null());
}

TEST_F(WBEWorldTest, SceneNodeUsesWorldComponentListsForComponentState) {
    auto world = WBE::make_ref<WBE::World>(get_allocator(), get_allocator());
    auto node = WBE::make_ref<WBE::SceneNode>(get_allocator(), world);
    const WBE::TypeID transform_type_id = get_component_type_id<WBE::ComponentTransform>();

    EXPECT_FALSE(node->has_component(transform_type_id));

    world->get_entities().emplace<WBE::ComponentTransform>(node->get_entity(), make_transform());

    EXPECT_TRUE(node->has_component(transform_type_id));
    EXPECT_NE(world->get_component_list(transform_type_id), nullptr);
    WBE::Vector<WBE::TypeID, WBE::HeapAllocatorDefault> component_type_ids = node->get_component_type_ids();
    EXPECT_NE(std::find(component_type_ids.begin(), component_type_ids.end(), transform_type_id), component_type_ids.end());

    node->remove_component(transform_type_id);

    EXPECT_FALSE(node->has_component(transform_type_id));
    EXPECT_FALSE(world->get_entities().all_of<WBE::ComponentTransform>(node->get_entity()));
    EXPECT_THROW(node->remove_component(transform_type_id), std::runtime_error);
}

TEST_F(WBEWorldTest, SceneNodeAddComponentConstructsComponentOnEntity) {
    auto world = WBE::make_ref<WBE::World>(get_allocator(), get_allocator());
    auto node = WBE::make_ref<WBE::SceneNode>(get_allocator(), world);
    const WBE::TypeID transform_type_id = get_component_type_id<WBE::ComponentTransform>();

    WBE::ComponentTransform& transform = node->add_component<WBE::ComponentTransform>(make_transform());

    EXPECT_TRUE(node->has_component(transform_type_id));
    EXPECT_EQ(&transform, &world->get_entities().get<WBE::ComponentTransform>(node->get_entity()));
    EXPECT_EQ(transform.translation, make_transform().translation);
    WBE::ComponentHierarchy& hierarchy = node->add_component<WBE::ComponentHierarchy>();
    EXPECT_TRUE(hierarchy.parent == entt::null);
    EXPECT_EQ(node->get_component_type_ids().size(), 2U);
}

TEST_F(WBEWorldTest, SceneNodeAddComponentRejectsDuplicateComponent) {
    auto world = WBE::make_ref<WBE::World>(get_allocator(), get_allocator());
    auto node = WBE::make_ref<WBE::SceneNode>(get_allocator(), world);
    node->add_component<WBE::ComponentTransform>(make_transform());

    EXPECT_THROW(node->add_component<WBE::ComponentTransform>(make_transform()), std::runtime_error);

    node->remove_component(get_component_type_id<WBE::ComponentTransform>());
    EXPECT_NO_THROW(node->add_component<WBE::ComponentTransform>(make_transform()));
}

TEST_F(WBEWorldTest, SceneNodeComponentQueriesHandleUnknownTypesAndRemovedComponents) {
    auto world = WBE::make_ref<WBE::World>(get_allocator(), get_allocator());
    auto node = WBE::make_ref<WBE::SceneNode>(get_allocator(), world);
    const WBE::TypeID transform_type_id = get_component_type_id<WBE::ComponentTransform>();
    const WBE::TypeID spot_light_type_id = get_component_type_id<WBE::ComponentSpotLight>();

    EXPECT_FALSE(node->has_component(transform_type_id));
    EXPECT_TRUE(node->get_component_type_ids().empty());
    EXPECT_THROW(node->remove_component(spot_light_type_id), std::runtime_error);

    world->get_entities().emplace<WBE::ComponentTransform>(node->get_entity(), make_transform());
    node->remove_component(transform_type_id);

    EXPECT_FALSE(node->has_component(transform_type_id));
    EXPECT_TRUE(node->get_component_type_ids().empty());
}

TEST_F(WBEWorldTest, SceneNodeHierarchyIsTrackedByComponent) {
    auto world = WBE::make_ref<WBE::World>(get_allocator(), get_allocator());
    auto parent = WBE::make_ref<WBE::SceneNode>(get_allocator(), world);
    auto child = WBE::make_ref<WBE::SceneNode>(get_allocator(), world);
    auto next_parent = WBE::make_ref<WBE::SceneNode>(get_allocator(), world);

    WBE::ComponentHierarchy& hierarchy = world->get_entities().emplace<WBE::ComponentHierarchy>(child->get_entity());
    hierarchy.parent = parent->get_entity();

    EXPECT_TRUE(world->get_scene_node(parent->get_entity()) == parent);
    EXPECT_TRUE(world->get_scene_node(child->get_entity()) == child);
    EXPECT_TRUE(hierarchy.parent == parent->get_entity());

    hierarchy.parent = next_parent->get_entity();

    EXPECT_TRUE(hierarchy.parent == next_parent->get_entity());
}

TEST_F(WBEWorldTest, SceneOrchestratesWorldLifecycleAndDistinctRenderGraphs) {
    auto render_allocator = WBE::make_ref<WBE::RenderAllocator>(get_allocator(), WBE_MI_B(4));
    auto renderer = WBE::make_ref<RecordingRenderer>(get_allocator(), render_allocator.get());
    auto scene = WBE::make_ref<WBE::Scene>(get_allocator(), get_allocator(), renderer, "res_chunks/test_scene.json");
    auto first_world = WBE::make_ref<WBE::World>(get_allocator(), get_allocator());
    auto second_world = WBE::make_ref<WBE::World>(get_allocator(), get_allocator());
    SceneDriverState first_state;
    SceneDriverState second_state;
    auto first_node = WBE::make_ref<WBE::SceneNode>(get_allocator(), first_world);
    auto second_node = WBE::make_ref<WBE::SceneNode>(get_allocator(), second_world);

    first_world->set_render_graph_resource_key("main_graph");
    second_world->set_render_graph_resource_key("main_graph");
    first_world->get_entities().emplace<WBE::ComponentDriver>(first_node->get_entity(),
        WBE::ComponentDriver{
            .setup = scene_driver_setup,
            .ready = scene_driver_ready,
            .update = scene_driver_update,
            .teardown = scene_driver_teardown,
            .local_data = &first_state,
        });
    second_world->get_entities().emplace<WBE::ComponentDriver>(second_node->get_entity(),
        WBE::ComponentDriver{
            .setup = scene_driver_setup,
            .ready = scene_driver_ready,
            .update = scene_driver_update,
            .teardown = scene_driver_teardown,
            .local_data = &second_state,
        });
    scene->add_world(first_world);
    scene->add_world(second_world);
    scene->set_active_world(1);

    EXPECT_EQ(scene->get_resource_manifest_key(), "res_chunks/test_scene.json");
    EXPECT_EQ(scene->get_world_count(), 2);
    EXPECT_TRUE(scene->get_active_world() == second_world);
    EXPECT_THROW(scene->ready(), std::runtime_error);

    scene->setup();
    scene->ready();
    scene->update(0.5);
    scene->teardown();

    ASSERT_EQ(renderer->registered_graphs.size(), 1);
    EXPECT_EQ(renderer->registered_graphs[0], "main_graph");
    ASSERT_EQ(renderer->activated_graphs.size(), 1);
    EXPECT_EQ(renderer->activated_graphs[0], "main_graph");
    ASSERT_EQ(renderer->unregistered_graphs.size(), 1);
    EXPECT_EQ(renderer->unregistered_graphs[0], "main_graph");
    for (const SceneDriverState* state : {&first_state, &second_state}) {
        EXPECT_EQ(state->setup_count, 1);
        EXPECT_EQ(state->ready_count, 1);
        EXPECT_EQ(state->update_count, 1);
        EXPECT_EQ(state->teardown_count, 1);
        EXPECT_DOUBLE_EQ(state->last_delta, 0.5);
    }
    EXPECT_THROW(scene->update(0.5), std::runtime_error);
    EXPECT_THROW(scene->teardown(), std::runtime_error);
}

TEST_F(WBEWorldTest, SceneManagerTransitionsScenesInLifecycleOrder) {
    auto render_allocator = WBE::make_ref<WBE::RenderAllocator>(get_allocator(), WBE_MI_B(4));
    auto renderer = WBE::make_ref<RecordingRenderer>(get_allocator(), render_allocator.get());
    auto first_scene = WBE::make_ref<WBE::Scene>(get_allocator(), get_allocator(), renderer);
    auto second_scene = WBE::make_ref<WBE::Scene>(get_allocator(), get_allocator(), renderer);
    auto first_world = WBE::make_ref<WBE::World>(get_allocator(), get_allocator());
    auto second_world = WBE::make_ref<WBE::World>(get_allocator(), get_allocator());
    auto first_node = WBE::make_ref<WBE::SceneNode>(get_allocator(), first_world);
    auto second_node = WBE::make_ref<WBE::SceneNode>(get_allocator(), second_world);
    std::vector<std::string> events;
    SceneDriverState first_state{.events = &events, .label = "first"};
    SceneDriverState second_state{.events = &events, .label = "second"};

    first_world->set_render_graph_resource_key("first_graph");
    second_world->set_render_graph_resource_key("second_graph");
    first_world->get_entities().emplace<WBE::ComponentDriver>(first_node->get_entity(),
        WBE::ComponentDriver{
            .setup = scene_driver_setup,
            .ready = scene_driver_ready,
            .teardown = scene_driver_teardown,
            .local_data = &first_state,
        });
    second_world->get_entities().emplace<WBE::ComponentDriver>(second_node->get_entity(),
        WBE::ComponentDriver{
            .setup = scene_driver_setup,
            .ready = scene_driver_ready,
            .teardown = scene_driver_teardown,
            .local_data = &second_state,
        });
    first_scene->add_world(first_world);
    second_scene->add_world(second_world);

    WBE::Map<std::string, WBE::Ref<WBE::Scene>, WBE::HeapAllocatorGlobal> scenes(get_allocator());
    scenes.emplace("first", first_scene);
    scenes.emplace("second", second_scene);
    {
        WBE::SceneManager manager(std::move(scenes));

        EXPECT_TRUE(manager.get_current_scene().is_null());
        EXPECT_THROW(manager.change_scene("missing"), std::runtime_error);
        manager.change_scene("first");
        manager.change_scene("first");
        manager.change_scene("second");
        manager.change_scene("first");

        EXPECT_TRUE(manager.get_current_scene() == first_scene);
        EXPECT_EQ(events,
            (std::vector<std::string>{
                "first_setup", "first_ready", "first_teardown", "second_setup", "second_ready", "second_teardown", "first_setup", "first_ready"}));
    }

    EXPECT_EQ(events,
        (std::vector<std::string>{
            "first_setup", "first_ready", "first_teardown", "second_setup", "second_ready", "second_teardown", "first_setup", "first_ready", "first_teardown"}));
    ASSERT_EQ(renderer->registered_graphs.size(), 3);
    ASSERT_EQ(renderer->unregistered_graphs.size(), 3);
}
