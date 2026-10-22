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
#include "core/memory/unique.hh"
#include "core/parser/parser_json.hh"
#include "function/event/event_manager.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/rend_graph_recipe_builder_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/rt_toggle_vk.hh"
#include "function/render_vk/rt_triggered_vk.hh"
#include "global/global.hh"
#include "render_task_registry_vk_test_tasks.hh"
#include "test_global.hh"
#include "utils/defs.hh"
#include <format>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

// An import that binds one bare resource entry, so recipes can be built without GPU objects.
class TestRecipeImportVK final : public IBindableRenderResourceVK {
public:
    TestRecipeImportVK() = default;
    virtual ~TestRecipeImportVK() override = default;
    TestRecipeImportVK(const TestRecipeImportVK&) = delete;
    TestRecipeImportVK(TestRecipeImportVK&&) noexcept = delete;
    TestRecipeImportVK& operator=(const TestRecipeImportVK&) = delete;
    TestRecipeImportVK& operator=(TestRecipeImportVK&&) noexcept = delete;

    virtual void bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const override {
        p_graph.add_resource(RendResVK{.res_id = std::string(p_resource_id)});
    }
};

std::string build_error(const RendGraphRecipeBuilderVK& p_builder,
    std::string_view p_recipe,
    const RendGraphRecipeImportsVK& p_imports,
    const RenderContextVK* p_render_data) {
    try {
        (void)p_builder.build(ParserJSON::read(p_recipe), p_imports, p_render_data);
    } catch (const std::runtime_error& p_error) {
        return p_error.what();
    }
    return {};
}

class WBERendGraphRecipeBuilderVKTest : public ::testing::Test {
protected:
    RenderAllocator allocator{WBE_KI_B(256)};
    RenderContextVK render_data{&allocator};
    RendGraphRecipeBuilderVK builder;
    TestRecipeImportVK camera;
};

} // namespace

TEST_F(WBERendGraphRecipeBuilderVKTest, BuildsImportsResourcesTasksAliasesAndEdges) {
    RendGraphRecipeImportsVK imports;
    imports.add("camera", camera);
    const RendGraphDescVK graph = builder.build(ParserJSON::read(R"({
        "imports": ["camera"],
        "resources": [
            {"id": "args", "buffer": {"size": 64, "usage": "buffer_indirect", "domain": "host_device"}},
            {"id": "color_image", "image": {"format": "r8g8b8a8_unorm", "extent": {"swap_chain_scale": 0.5}}},
            {"id": "color", "image_view": {"image": "color_image", "view": {"base_mip_level": 0}}},
            {"id": "linear", "sampler": {"min_filter": "nearest"}}
        ],
        "tasks": [
            {"id": "first", "type": "registry_probe", "params": {"label": "a"},
             "aliases": {"input": "camera"}, "connect": [{"to": "second", "resource": "color"}]},
            {"id": "second", "type": "registry_preparing_probe", "params": {"label": "b"}}
        ]
    })"),
        imports,
        &render_data);

    EXPECT_TRUE(graph.contains_resource("camera"));
    const RendGraphContextVK context{.swap_chain_extent = {.width = 200, .height = 100}};
    // The recipe's buffer is declared before the tasks; the preparing probe adds its own buffer after it.
    ASSERT_EQ(graph.buffers.size(), 2U);
    EXPECT_EQ(graph.buffers[0].res_id, "args");
    EXPECT_EQ(graph.buffers[0].factory(context).size, 64U);
    EXPECT_EQ(graph.buffers[0].factory(context).usage, RendResPrimaryUsageVK::BUFFER_INDIRECT);
    ASSERT_EQ(graph.images.size(), 1U);
    EXPECT_EQ(graph.images[0].factory(context).extent.width, 100U);
    EXPECT_EQ(graph.images[0].factory(context).format, VK_FORMAT_R8G8B8A8_UNORM);
    ASSERT_EQ(graph.image_views.size(), 1U);
    EXPECT_EQ(graph.image_views[0].image_id, "color_image");
    ASSERT_EQ(graph.samplers.size(), 1U);
    EXPECT_EQ(graph.samplers[0].factory(context).min_filter, VK_FILTER_NEAREST);

    ASSERT_EQ(graph.tasks.size(), 2U);
    RendGraphDescVK mutable_graph = graph;
    const RendTaskDescVK& first = mutable_graph.get_task("first");
    EXPECT_EQ(first.resource_map.resolve("input"), "camera");
    ASSERT_EQ(first.next.size(), 1U);
    EXPECT_EQ(first.next[0].next_task_id, "second");
    EXPECT_EQ(first.next[0].res_id, "color");
    // The second task's prepare_graph() hook ran: it declared and aliased its buffer.
    EXPECT_EQ(mutable_graph.get_task("second").resource_map.resolve(RTRegistryPreparingProbeVK::INPUT_ID), "b.prepared");
    EXPECT_TRUE(graph.contains_resource("b.prepared"));
}

TEST_F(WBERendGraphRecipeBuilderVKTest, RequiresDeclaredImportsToMatchSuppliedOnes) {
    RendGraphRecipeImportsVK imports;
    EXPECT_NE(build_error(builder, R"({"imports": ["camera"]})", imports, &render_data).find("imports[0] 'camera' is not supplied"),
        std::string::npos);
    imports.add("camera", camera);
    EXPECT_NE(build_error(builder, R"({"imports": []})", imports, &render_data).find("supplied import 'camera' is not declared"),
        std::string::npos);
    EXPECT_NE(build_error(builder, R"({"imports": ["camera", "camera"]})", imports, &render_data).find("listed more than once"),
        std::string::npos);
    EXPECT_THROW(imports.add("camera", camera), std::runtime_error);
    EXPECT_THROW(imports.add("", camera), std::runtime_error);
}

TEST_F(WBERendGraphRecipeBuilderVKTest, RejectsMalformedResourcesWithTheirLocation) {
    const RendGraphRecipeImportsVK imports;
    EXPECT_NE(build_error(builder, R"({"resources": [{"id": "both", "buffer": {"size": 4}, "sampler": {}}]})", imports, &render_data)
                  .find("resources[0] 'both' must set exactly one"),
        std::string::npos);
    EXPECT_NE(build_error(builder, R"({"resources": [{"id": "none"}]})", imports, &render_data).find("resources[0] 'none' must set exactly one"),
        std::string::npos);
    EXPECT_NE(build_error(builder, R"({"resources": [{"id": "x", "sampler": {}}, {"id": "x", "sampler": {}}]})", imports, &render_data)
                  .find("resources[1] 'x' repeats"),
        std::string::npos);
    // Strict recipe structs reject unknown keys.
    EXPECT_THROW((void)builder.build(ParserJSON::read(R"({"resource": []})"), imports, &render_data), std::runtime_error);
}

TEST_F(WBERendGraphRecipeBuilderVKTest, RejectsMalformedTasksWithTheirLocation) {
    const RendGraphRecipeImportsVK imports;
    EXPECT_NE(build_error(builder,
                  R"({"tasks": [{"id": "a", "type": "registry_probe", "params": {"label": "x"}}, {"id": "a", "type": "registry_probe", "params": {"label": "y"}}]})",
                  imports,
                  &render_data)
                  .find("tasks[1] 'a' repeats"),
        std::string::npos);
    EXPECT_NE(build_error(builder,
                  R"({"tasks": [{"id": "a", "type": "registry_probe", "params": {"label": "x"}, "connect": [{"to": "b", "resource": "r"}]}]})",
                  imports,
                  &render_data)
                  .find("tasks[0] 'a' connect[0] targets the unknown task 'b'"),
        std::string::npos);
    EXPECT_NE(build_error(builder, R"({"tasks": [{"id": "a", "type": "registry_probe", "params": {"label": "x"}, "aliases": {"input": 3}}]})", imports, &render_data)
                  .find("tasks[0] 'a' alias 'input'"),
        std::string::npos);
    EXPECT_NE(build_error(builder, R"({"tasks": [{"id": "a", "type": "missing_task"}]})", imports, &render_data).find("unknown render task type"),
        std::string::npos);
}

namespace {

// A supplied task with a caller-side alias, standing in for a task only C++ can build.
RendTaskDescVK make_supplied_task(RenderAllocator* p_allocator, std::string_view p_task_id) {
    RendTaskDescVK task(p_allocator, p_task_id, [](const RendGraphContextVK& /*p_context*/) {
        return Unique<IRenderTaskVK>();
    });
    task.alias("target", "camera");
    return task;
}

} // namespace

TEST_F(WBERendGraphRecipeBuilderVKTest, ImportsSuppliedTasksAndConnectsThem) {
    RendGraphRecipeImportsVK imports;
    imports.add("camera", camera).add_task(make_supplied_task(&allocator, "overlay"));
    const RendGraphDescVK graph = builder.build(ParserJSON::read(R"({
        "imports": ["camera"],
        "tasks": [
            {"id": "draw", "type": "registry_probe", "params": {"label": "a"}, "connect": [{"to": "overlay", "resource": "camera"}]},
            {"id": "overlay", "import": true, "connect": [{"to": "after", "resource": "camera"}]},
            {"id": "after", "type": "registry_probe", "params": {"label": "b"}}
        ]
    })"),
        imports,
        &render_data);

    ASSERT_EQ(graph.tasks.size(), 3U);
    RendGraphDescVK mutable_graph = graph;
    // Edges reach the imported task from recipe tasks and leave it to them.
    ASSERT_EQ(mutable_graph.get_task("draw").next.size(), 1U);
    EXPECT_EQ(mutable_graph.get_task("draw").next[0].next_task_id, "overlay");
    const RendTaskDescVK& overlay = mutable_graph.get_task("overlay");
    ASSERT_EQ(overlay.next.size(), 1U);
    EXPECT_EQ(overlay.next[0].next_task_id, "after");
    // The imported task keeps what the caller gave it.
    EXPECT_EQ(overlay.resource_map.resolve("target"), "camera");
    EXPECT_NE(overlay.factory, nullptr);
}

TEST_F(WBERendGraphRecipeBuilderVKTest, RequiresImportedTasksToMatchSuppliedOnes) {
    RendGraphRecipeImportsVK imports;
    EXPECT_NE(build_error(builder, R"({"tasks": [{"id": "overlay", "import": true}]})", imports, &render_data).find("tasks[0] 'overlay' is imported but not supplied"),
        std::string::npos);
    imports.add_task(make_supplied_task(&allocator, "overlay"));
    EXPECT_NE(build_error(builder, R"({"tasks": []})", imports, &render_data).find("supplied task 'overlay' is not imported"),
        std::string::npos);
    // An imported id cannot also be a recipe task.
    EXPECT_NE(build_error(builder,
                  R"({"tasks": [{"id": "overlay", "import": true}, {"id": "overlay", "type": "registry_probe", "params": {"label": "a"}}]})",
                  imports,
                  &render_data)
                  .find("tasks[1] 'overlay' repeats a task id"),
        std::string::npos);
    EXPECT_THROW(imports.add_task(make_supplied_task(&allocator, "overlay")), std::runtime_error);
    EXPECT_THROW(imports.add_task(make_supplied_task(&allocator, "")), std::runtime_error);
}

TEST_F(WBERendGraphRecipeBuilderVKTest, RejectsImportedTasksThatConfigureThemselves) {
    RendGraphRecipeImportsVK imports;
    imports.add_task(make_supplied_task(&allocator, "overlay"));
    for (const auto& [field, message] : {
             std::pair<std::string_view, std::string_view>{R"("type": "registry_probe")", "cannot set a type"},
             {R"("params": {"label": "a"})", "cannot set params"},
             {R"("aliases": {"target": "camera"})", "cannot set aliases"},
             {R"("trigger": {"event": "redraw"})", "cannot set a trigger"},
             {R"("toggle": {"event": "redraw"})", "cannot set a toggle"},
         }) {
        const std::string recipe = std::format(R"({{"tasks": [{{"id": "overlay", "import": true, {}}}]}})", field);
        EXPECT_NE(build_error(builder, recipe, imports, &render_data).find(message), std::string::npos) << field;
    }
    // A task that is not imported must name a type.
    EXPECT_NE(build_error(builder, R"({"tasks": [{"id": "draw"}]})", RendGraphRecipeImportsVK{}, &render_data).find("tasks[0] 'draw' sets no type"),
        std::string::npos);
}

TEST_F(WBERendGraphRecipeBuilderVKTest, BuildsSubgraphTasksWithTheirPortsImportsAndSuppliedTasks) {
    RendGraphRecipeImportsVK imports;
    imports.add("camera", camera).add_task(make_supplied_task(&allocator, "overlay"));
    RendGraphDescVK graph = builder.build(ParserJSON::read(R"({
        "imports": ["camera"],
        "tasks": [
            {"id": "draw", "type": "registry_probe", "params": {"label": "a"}, "connect": [{"to": "post", "resource": "camera"}]},
            {"id": "post", "ports": {"color": "camera"}, "connect": [{"to": "after", "resource": "camera"}], "subgraph": {
                "imports": ["camera"],
                "ports": ["color"],
                "resources": [{"id": "scratch", "buffer": {"size": 16}}],
                "tasks": [
                    {"id": "sort", "type": "registry_probe", "params": {"label": "b"}, "aliases": {"input": "color"},
                     "connect": [{"to": "overlay", "resource": "scratch"}]},
                    {"id": "overlay", "import": true}
                ]
            }},
            {"id": "after", "type": "registry_probe", "params": {"label": "c"}, "connect": [{"to": "post.sort", "resource": "camera"}]}
        ]
    })"),
        imports,
        &render_data);

    ASSERT_EQ(graph.tasks.size(), 4U);
    const RendTaskDescVK& sort = graph.get_task("post.sort");
    EXPECT_EQ(sort.resource_map.resolve("input"), "camera");
    EXPECT_EQ(sort.resource_map.resolve("scratch"), "post.scratch");
    ASSERT_EQ(sort.next.size(), 1U);
    EXPECT_EQ(sort.next[0].next_task_id, "post.overlay");
    EXPECT_EQ(sort.next[0].res_id, "post.scratch");
    // The supplied task keeps its alias, now in the subgraph's scope.
    EXPECT_EQ(graph.get_task("post.overlay").resource_map.resolve("target"), "camera");
    ASSERT_EQ(graph.buffers.size(), 1U);
    EXPECT_EQ(graph.buffers[0].res_id, "post.scratch");
    // The subgraph and the graph import the camera once.
    EXPECT_EQ(graph.resources.size(), 1U);
    EXPECT_EQ(graph.get_task("draw").next[0].next_task_id, "post");
    ASSERT_EQ(graph.get_subgraph("post").next.size(), 1U);
    EXPECT_EQ(graph.get_subgraph("post").next[0].next_task_id, "after");
    // Recipes may connect to a subgraph's task directly.
    EXPECT_EQ(graph.get_task("after").next[0].next_task_id, "post.sort");
}

TEST_F(WBERendGraphRecipeBuilderVKTest, RejectsMalformedSubgraphsWithTheirLocation) {
    const RendGraphRecipeImportsVK imports;
    const auto error = [&](std::string_view p_recipe) {
        return build_error(builder, p_recipe, imports, &render_data);
    };
    EXPECT_NE(error(R"({"ports": ["color"], "tasks": []})").find("only a subgraph's nested recipe declares ports"), std::string::npos);
    EXPECT_NE(error(R"({"tasks": [{"id": "post", "type": "registry_probe", "subgraph": {"tasks": []}}]})").find("tasks[0] 'post' is a subgraph"),
        std::string::npos);
    EXPECT_NE(error(R"({"tasks": [{"id": "a", "type": "registry_probe", "params": {"label": "x"}, "ports": {"color": "c"}}]})")
                  .find("tasks[0] 'a' sets ports but is not a subgraph"),
        std::string::npos);
    EXPECT_NE(error(R"({"tasks": [{"id": "post", "subgraph": {"tasks": [{"id": "x"}]}}]})").find("tasks[0] 'post' subgraph tasks[0] 'x' sets no type"),
        std::string::npos);
    EXPECT_NE(error(R"({"tasks": [{"id": "post", "subgraph": {"ports": ["color"], "tasks": []}}]})").find("port 'color' is bound 0 times"),
        std::string::npos);
    EXPECT_NE(error(R"({"tasks": [{"id": "post", "ports": {"color": 3}, "subgraph": {"ports": ["color"], "tasks": []}}]})")
                  .find("tasks[0] 'post' port 'color' must map to a graph resource id string"),
        std::string::npos);
}

class WBERendGraphRecipeBuilderVKTriggerTest : public WBERendGraphRecipeBuilderVKTest {
protected:
    virtual void SetUp() override {
        global = make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<Global> global;
};

TEST_F(WBERendGraphRecipeBuilderVKTriggerTest, WrapsTriggeredTasks) {
    const RendGraphRecipeImportsVK imports;
    RendGraphDescVK graph = builder.build(ParserJSON::read(R"({
        "tasks": [{"id": "rebuild", "type": "registry_probe", "params": {"label": "x"},
                   "trigger": {"event": "recipe.rebuild", "activate": true}}]
    })"),
        imports,
        &render_data);
    const Unique<IRenderTaskVK> task = graph.get_task("rebuild").factory(RendGraphContextVK{});
    const auto* triggered = dynamic_cast<const RTTriggeredVK*>(task.get());
    ASSERT_NE(triggered, nullptr);
    EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTTriggeredVK::Command>("recipe.rebuild"), 1U);
}

TEST_F(WBERendGraphRecipeBuilderVKTriggerTest, WrapsToggledTasksInTheirInitialState) {
    const RendGraphRecipeImportsVK imports;
    RendGraphDescVK graph = builder.build(ParserJSON::read(R"({
        "tasks": [
            {"id": "effect", "type": "registry_probe", "params": {"label": "x"}, "toggle": {"event": "recipe.effect", "enabled": true}},
            {"id": "off", "type": "registry_probe", "params": {"label": "y"}, "toggle": {"event": "recipe.effect"}}
        ]
    })"),
        imports,
        &render_data);
    const Unique<IRenderTaskVK> effect = graph.get_task("effect").factory(RendGraphContextVK{});
    const Unique<IRenderTaskVK> off = graph.get_task("off").factory(RendGraphContextVK{});
    const auto* effect_toggle = dynamic_cast<const RTToggleVK*>(effect.get());
    const auto* off_toggle = dynamic_cast<const RTToggleVK*>(off.get());
    ASSERT_NE(effect_toggle, nullptr);
    ASSERT_NE(off_toggle, nullptr);
    EXPECT_TRUE(effect_toggle->is_enabled());
    EXPECT_FALSE(off_toggle->is_enabled());
    // Both follow commands under their shared event.
    EventManager::get_singleton()->emit("recipe.effect", RTToggleVK::Command::enable());
    EXPECT_TRUE(off_toggle->is_enabled());
    EXPECT_NE(build_error(builder, R"({"tasks": [{"id": "a", "type": "registry_probe", "params": {"label": "x"}, "toggle": {"event": ""}}]})", imports, &render_data)
                  .find("tasks[0] 'a' toggle event is empty"),
        std::string::npos);
}

TEST_F(WBERendGraphRecipeBuilderVKTriggerTest, TogglesGateTriggeredTasks) {
    const RendGraphRecipeImportsVK imports;
    RendGraphDescVK graph = builder.build(ParserJSON::read(R"({
        "tasks": [{"id": "capture", "type": "registry_probe", "params": {"label": "x"},
                   "trigger": {"event": "recipe.capture"}, "toggle": {"event": "recipe.capture_allowed", "enabled": true}}]
    })"),
        imports,
        &render_data);
    const Unique<IRenderTaskVK> task = graph.get_task("capture").factory(RendGraphContextVK{});
    // The toggle is the outer wrapper; the triggered task inside it still subscribes to its own event.
    EXPECT_NE(dynamic_cast<const RTToggleVK*>(task.get()), nullptr);
    EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTToggleVK::Command>("recipe.capture_allowed"), 1U);
    EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTTriggeredVK::Command>("recipe.capture"), 1U);
}

TEST_F(WBERendGraphRecipeBuilderVKTriggerTest, SubgraphGatingWrapsEveryTaskOfTheSubgraph) {
    const RendGraphRecipeImportsVK imports;
    RendGraphDescVK graph = builder.build(ParserJSON::read(R"({
        "tasks": [{"id": "post", "toggle": {"event": "recipe.post", "enabled": true}, "subgraph": {"tasks": [
            {"id": "first", "type": "registry_probe", "params": {"label": "a"}},
            {"id": "nested", "subgraph": {"tasks": [{"id": "second", "type": "registry_probe", "params": {"label": "b"}}]}}
        ]}}]
    })"),
        imports,
        &render_data);
    const Unique<IRenderTaskVK> first = graph.get_task("post.first").factory(RendGraphContextVK{});
    const Unique<IRenderTaskVK> second = graph.get_task("post.nested.second").factory(RendGraphContextVK{});
    EXPECT_NE(dynamic_cast<const RTToggleVK*>(first.get()), nullptr);
    EXPECT_NE(dynamic_cast<const RTToggleVK*>(second.get()), nullptr);
    EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTToggleVK::Command>("recipe.post"), 2U);
}

} // namespace WhiteBirdEngine
