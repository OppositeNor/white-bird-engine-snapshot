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
#include "core/data/data_value.hh"
#include "core/engine_core.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/rend_graph_recipe_builder_vk.hh"
#include "function/render_vk/rend_graph_script_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "global/global.hh"
#include "resource/resource_chunk/resource_chunk_load_file.hh"
#include "resource/resource_manager/resource_manager.hh"
#include "test_global.hh"
#include "utils/defs.hh"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

constexpr std::string_view TEST_RECIPE_SCRIPT = R"(
local exports = {}

function exports.build(context)
    local tasks = {}
    for i = 1, context.frames_in_flight do
        tasks[#tasks + 1] = { id = "probe_" .. i, type = "registry_probe", params = { label = context.swap_chain_format } }
    end
    tasks[1].connect = { { to = "probe_2", resource = "shared" } }
    return {
        resources = { { id = "shared", sampler = { min_filter = "nearest" } } },
        tasks = tasks,
    }
end

return exports
)";

} // namespace

TEST(WBERendGraphScriptVKTest, MakesContextFromTheRenderData) {
    RenderAllocator allocator(WBE_KI_B(16));
    RenderContextVK render_data(&allocator);
    render_data.frames_in_flight = 3;
    render_data.swap_chain_format = VK_FORMAT_B8G8R8A8_SRGB;
    const RendGraphContextVK graph_context{.swap_chain_extent = {.width = 1920, .height = 1080}};
    DataValue context = RendGraphScriptVK::make_context(render_data, graph_context);
    EXPECT_EQ(context.get_value<uint32_t>("frames_in_flight"), 3U);
    EXPECT_EQ(context.get_value<std::string>("swap_chain_format"), "B8G8R8A8_SRGB");
    EXPECT_EQ(context.get_entry("swap_chain_extent").get_value<uint32_t>("width"), 1920U);
    EXPECT_EQ(context.get_entry("swap_chain_extent").get_value<uint32_t>("height"), 1080U);
    // A format ImageFormatVK has no name for is passed as its value.
    render_data.swap_chain_format = VK_FORMAT_R4G4_UNORM_PACK8;
    context = RendGraphScriptVK::make_context(render_data, graph_context);
    EXPECT_EQ(context.get_value<int64_t>("swap_chain_format"), static_cast<int64_t>(VK_FORMAT_R4G4_UNORM_PACK8));
}

TEST(WBERendGraphScriptVKTest, RunsBuildAndTheRecipeBuilds) {
    RenderAllocator allocator(WBE_KI_B(256));
    RenderContextVK render_data(&allocator);
    render_data.frames_in_flight = 2;
    render_data.swap_chain_format = VK_FORMAT_B8G8R8A8_UNORM;
    const DataValue recipe = RendGraphScriptVK::run(
        "test_recipe", TEST_RECIPE_SCRIPT, RendGraphScriptVK::make_context(render_data, RendGraphContextVK{}));
    RendGraphDescVK graph = RendGraphRecipeBuilderVK().build(recipe, RendGraphRecipeImportsVK{}, &render_data);
    ASSERT_EQ(graph.tasks.size(), 2U);
    EXPECT_EQ(graph.get_task("probe_1").next.size(), 1U);
    EXPECT_EQ(graph.samplers.size(), 1U);
}

TEST(WBERendGraphScriptVKTest, RecipesSizeDispatchesFromTheSwapChainExtent) {
    constexpr std::string_view TILED_RECIPE = R"(
return {
    build = function(context)
        local extent = context.swap_chain_extent
        return {
            tasks = {
                {
                    id = "tiled",
                    type = "run_compute_pipeline",
                    params = { pipeline = "tiles", group_count_x = (extent.width + 7) // 8, group_count_y = (extent.height + 7) // 8 },
                },
            },
        }
    end,
}
)";
    RenderAllocator allocator(WBE_KI_B(16));
    RenderContextVK render_data(&allocator);
    auto group_counts = [&render_data, TILED_RECIPE](uint32_t p_width, uint32_t p_height) {
        const RendGraphContextVK graph_context{.swap_chain_extent = {.width = p_width, .height = p_height}};
        const DataValue recipe =
            RendGraphScriptVK::run("tiled", TILED_RECIPE, RendGraphScriptVK::make_context(render_data, graph_context));
        const DataValue& params = recipe.get_entry("tasks").as_list().at(0).get_entry("params");
        return std::pair{params.get_value<uint32_t>("group_count_x"), params.get_value<uint32_t>("group_count_y")};
    };
    // A resize re-runs the recipe with the new extent, which rounds partial tiles up.
    EXPECT_EQ(group_counts(1920, 1080), std::pair(240U, 135U));
    EXPECT_EQ(group_counts(1921, 1080), std::pair(241U, 135U));
}

TEST(WBERendGraphScriptVKTest, RejectsScriptsWithoutBuild) {
    EXPECT_THROW((void)RendGraphScriptVK::run("no_build", "return {}", DataValue::make_map()), std::runtime_error);
    EXPECT_THROW((void)RendGraphScriptVK::run("broken", "return {", DataValue::make_map()), std::runtime_error);
}

class WBERendGraphScriptVKResourceTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = make_test_global();
        ResourceManager* resource_manager = ResourceManager::get_singleton();
        resource_manager->register_chunk(make_ref<ResourceChunkLoadFile>(
            global_allocator(), "assets/scripts/teal/lua_scripts.json", resource_manager->get_allocator()));
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<Global> global;
};

TEST_F(WBERendGraphScriptVKResourceTest, ScriptsRequireScriptResources) {
    const DataValue recipe = RendGraphScriptVK::run("requiring_recipe",
        R"(
local math_utils = require("utils.math")
return {
    build = function()
        return { imports = { tostring(math_utils.ceil_div(42, 5)) } }
    end,
}
)",
        DataValue::make_map());
    EXPECT_EQ(recipe.get_entry("imports").as_list().at(0).as_string(), "9");
    EXPECT_THROW((void)RendGraphScriptVK::run("missing", "require('utils.missing') return {}", DataValue::make_map()), std::runtime_error);
}

TEST_F(WBERendGraphScriptVKResourceTest, RunsTealScriptsCompiledByTheACP) {
    // tests.teal_recipe is a Teal module requiring another, which the ACP compiled to Lua.
    const DataValue recipe = RendGraphScriptVK::run("tests.teal_recipe", DataValue::make_map());
    EXPECT_EQ(recipe.get_entry("imports").as_list().at(0).as_string(), "6");
    EXPECT_EQ(recipe.get_value<std::string>("queue_family"), "compute_2");
}

TEST_F(WBERendGraphScriptVKResourceTest, SharedGraphDeclarationsLoadBeforeTheRuntimeAndTaskInstaller) {
    const DataValue recipe = RendGraphScriptVK::run("shared_graph_api", R"(
local common = require("render_graph_utils.render_graph")
local constructors = require("render_graph_utils.render_graph_tasks")
assert(type(constructors.install) == "function")
assert(common.new == nil)
local graph_utils = require("render_graph_utils.render_graph_utils")
assert(graph_utils == common)
assert(require("render_graph_utils.render_graph") == common)
assert(require("render_graph_utils.render_graph_tasks") == constructors)
return { build = function()
    local graph = graph_utils.new()
    local pipeline = graph:add_compute_pipeline("pipeline")
    local compute = graph:compute("compute", { pipeline = pipeline })
    assert(graph:get("compute") == compute)
    return graph:build()
end }
)", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    ASSERT_EQ(tasks.size(), 1U);
    EXPECT_EQ(tasks[0].get_value<std::string>("type"), "run_compute_pipeline");
    EXPECT_EQ(tasks[0].get_entry("params").get_value<std::string>("pipeline"), "pipeline");
    EXPECT_TRUE(recipe.get_entry("resources").as_list().empty());
    EXPECT_TRUE(recipe.get_entry("imports").as_list().empty());
}

TEST_F(WBERendGraphScriptVKResourceTest, CollectsHandleBindingsEdgesAndImageDependencies) {
    const DataValue recipe = RendGraphScriptVK::run("handle_resources", R"(
local Graph = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = Graph.new()
    local image = graph:add_storage_image("output_image", "r16g16b16a16_sfloat", 16, 8)
    local view = graph:add_image_view("output", image)
    local sampler = graph:add_clamp_to_edge_sampler("sampler")
    local scratch = graph:add_buffer("scratch", "buffer_storage", 64)
    local unused = graph:add_storage_buffer("unused", 32)
    assert(Graph.enum_name("image_read_write") == "IMAGE_READ_WRITE")
    local write = graph:add({ id = "write", type = "registry_probe", params = { label = "write" }, uses = { scratch } })
    local read = graph:add({ id = "read", type = "registry_probe", params = { label = "read" } })
    read:alias("input", view):alias("sampler", sampler)
    write:connect(read, view)
    return graph:build()
end }
)", DataValue::make_map());
    const auto& resources = recipe.get_entry("resources").as_list();
    ASSERT_EQ(resources.size(), 4U);
    auto find_resource = [&resources](std::string_view p_id) -> const DataValue* {
        for (const auto& resource : resources) {
            if (resource.get_value<std::string>("id") == p_id) {
                return &resource;
            }
        }
        return nullptr;
    };
    ASSERT_NE(find_resource("output_image"), nullptr);
    const DataValue* view = find_resource("output");
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->get_entry("image_view").get_value<std::string>("image"), "output_image");
    EXPECT_NE(find_resource("sampler"), nullptr);
    EXPECT_NE(find_resource("scratch"), nullptr);
    EXPECT_EQ(find_resource("unused"), nullptr);
    const auto& tasks = recipe.get_entry("tasks").as_list();
    ASSERT_EQ(tasks.size(), 2U);
    EXPECT_EQ(tasks[0].get_entry("connect").as_list()[0].get_value<std::string>("resource"), "output");
    EXPECT_EQ(tasks[1].get_entry("aliases").get_value<std::string>("input"), "output");
    EXPECT_FALSE(tasks[0].contains("uses"));
    EXPECT_FALSE(tasks[1].contains("uses"));
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_context(&allocator);
    const auto graph = RendGraphRecipeBuilderVK().build(recipe, RendGraphRecipeImportsVK{}, &render_context);
    EXPECT_EQ(graph.images.size(), 1U);
    EXPECT_EQ(graph.image_views.size(), 1U);
    EXPECT_EQ(graph.samplers.size(), 1U);
    EXPECT_EQ(graph.buffers.size(), 1U);
}

TEST_F(WBERendGraphScriptVKResourceTest, LowersImportedHandlesAndCollectsParentPortResources) {
    const DataValue recipe = RendGraphScriptVK::run("handle_ports", R"(
local Graph = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = Graph.new()
    local target_image = graph:add_storage_image("target_image", "r16g16b16a16_sfloat", 16, 8)
    local target = graph:add_image_view("target", target_image)
    local input = graph:import_resource("input")
    local member = graph:reference("input.member")
    local nested = Graph.new()
    local color = nested:port("color")
    nested:add({ id = "draw", type = "registry_probe", params = { label = "draw" }, aliases = { output = color } })
    local prepare = graph:add({ id = "prepare", type = "registry_probe", params = { label = "prepare" }, uses = { input, member } })
    local effect = graph:add(nested:as_subgraph("effect", { unused = target, color = target }, { "nested_only" }))
    graph:chain(target, { prepare, effect })
    return graph:build({ "input", "application_only" })
end }
)", DataValue::make_map());
    EXPECT_EQ(recipe.get_entry("resources").as_list().size(), 2U);
    const auto& imports = recipe.get_entry("imports").as_list();
    ASSERT_EQ(imports.size(), 2U);
    EXPECT_TRUE(std::ranges::any_of(imports, [](const DataValue& p_import) {
        return p_import.as_string() == "input";
    }));
    EXPECT_TRUE(std::ranges::any_of(imports, [](const DataValue& p_import) {
        return p_import.as_string() == "application_only";
    }));
    const auto& effect = recipe.get_entry("tasks").as_list()[1];
    EXPECT_EQ(effect.get_entry("ports").get_value<std::string>("color"), "target");
    EXPECT_EQ(effect.get_entry("ports").get_value<std::string>("unused"), "target");
    const auto& nested = effect.get_entry("subgraph");
    EXPECT_TRUE(nested.get_entry("resources").as_list().empty());
    ASSERT_EQ(nested.get_entry("imports").as_list().size(), 1U);
    EXPECT_EQ(nested.get_entry("imports").as_list()[0].as_string(), "nested_only");
    ASSERT_EQ(nested.get_entry("ports").as_list().size(), 2U);
    EXPECT_EQ(nested.get_entry("ports").as_list()[0].as_string(), "color");
    EXPECT_EQ(nested.get_entry("ports").as_list()[1].as_string(), "unused");
    EXPECT_EQ(nested.get_entry("tasks").as_list()[0].get_entry("aliases").get_value<std::string>("output"), "color");
}

TEST_F(WBERendGraphScriptVKResourceTest, RejectsDifferentOwnedHandlesWithTheSameId) {
    EXPECT_THROW((void)RendGraphScriptVK::run("conflicting_handles", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
return { build = function()
    local first = graph_utils.storage_buffer("same", 16)
    local second = graph_utils.storage_buffer("same", 32)
    local graph = graph_utils.graph()
    graph:add({ id = "probe", type = "registry_probe", uses = { first, second } })
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
}

TEST_F(WBERendGraphScriptVKResourceTest, RejectsCyclicHandleDependencies) {
    EXPECT_THROW((void)RendGraphScriptVK::run("cyclic_handles", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
return { build = function()
    local resource = graph_utils.storage_buffer("cycle", 16)
    resource.dependencies = { resource }
    local graph = graph_utils.graph()
    graph:add({ id = "probe", type = "registry_probe", uses = { resource } })
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
}

TEST_F(WBERendGraphScriptVKResourceTest, ReusesHandlesWithoutLeakingDeclarationsBetweenGraphs) {
    const DataValue result = RendGraphScriptVK::run("independent_graphs", R"(
local Graph = require("render_graph_utils.render_graph_utils")
return { build = function()
    local first = Graph.new()
    local shared = first:add_storage_buffer("shared", 16)
    first:add({ id = "first", type = "registry_probe", aliases = { input = shared }, uses = { shared, shared } })
    local second = Graph.new()
    second:add({ id = "second", type = "registry_probe" })
    local third = Graph.new()
    third:add({ id = "third", type = "registry_probe", uses = { shared } })
    return { first = first:build(), second = second:build(), third = third:build() }
end }
)", DataValue::make_map());
    EXPECT_EQ(result.get_entry("first").get_entry("resources").as_list().size(), 1U);
    EXPECT_TRUE(result.get_entry("second").get_entry("resources").as_list().empty());
    EXPECT_EQ(result.get_entry("third").get_entry("resources").as_list().size(), 1U);
}

TEST_F(WBERendGraphScriptVKResourceTest, TaskAliasesResolveIndependentlyAndConnectionsLowerHandles) {
    const DataValue recipe = RendGraphScriptVK::run("task_handles", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
return { build = function()
    local first_resource = graph_utils.storage_buffer("first_resource", 16)
    local second_resource = graph_utils.storage_buffer("second_resource", 32)
    local graph = graph_utils.graph()
    local first = graph:add({ id = "first", type = "registry_probe" })
    local second = graph:add({ id = "second", type = "registry_probe" })
    assert(first:alias("input", first_resource) == first)
    assert(second:alias("input", second_resource) == second)
    assert(first:connect(second, first_resource) == first)
    return graph:build()
end }
)", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    ASSERT_EQ(tasks.size(), 2U);
    EXPECT_EQ(tasks[0].get_entry("aliases").get_value<std::string>("input"), "first_resource");
    EXPECT_EQ(tasks[1].get_entry("aliases").get_value<std::string>("input"), "second_resource");
    const auto& edge = tasks[0].get_entry("connect").as_list()[0];
    EXPECT_EQ(edge.get_value<std::string>("to"), "second");
    EXPECT_EQ(edge.get_value<std::string>("resource"), "first_resource");
    for (const auto& task : tasks) {
        EXPECT_FALSE(task.contains("alias"));
        EXPECT_FALSE(task.contains("graph"));
        EXPECT_FALSE(task.contains("edges"));
    }
}

TEST_F(WBERendGraphScriptVKResourceTest, TaskAliasMapsAreOwnedByEachTask) {
    const DataValue recipe = RendGraphScriptVK::run("independent_aliases", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
return { build = function()
    local shared = graph_utils.storage_buffer("shared", 16)
    local extra = graph_utils.storage_buffer("extra", 32)
    local aliases = { input = shared }
    local graph = graph_utils.graph()
    local first = graph:add({ id = "first", type = "registry_probe", aliases = aliases })
    graph:add({ id = "second", type = "registry_probe", aliases = aliases })
    first:alias("extra", extra)
    assert(aliases.extra == nil)
    return graph:build()
end }
)", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    EXPECT_TRUE(tasks[0].get_entry("aliases").contains("extra"));
    EXPECT_FALSE(tasks[1].get_entry("aliases").contains("extra"));
}

TEST_F(WBERendGraphScriptVKResourceTest, BatchedConnectionsLowerEachResourceAndPreserveSingleResourceCalls) {
    const DataValue recipe = RendGraphScriptVK::run("batched_connections", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function compute_pipeline(p_key)
    return helper_graph:add_compute_pipeline(p_key)
end
return { build = function()
    local color = graph_utils.storage_buffer("color", 16)
    local depth = graph_utils.storage_buffer("depth", 32)
    local graph = graph_utils.graph()
    local first = graph:compute("first", { pipeline = compute_pipeline("first") })
    local second = graph:compute("second", { pipeline = compute_pipeline("second") })
    local third = graph:compute("third", { pipeline = compute_pipeline("third") })
    local last = graph:compute("last", { pipeline = compute_pipeline("last") })
    local sink = graph:compute("sink", { pipeline = compute_pipeline("sink") })
    graph:chain({ color, depth }, { first, second, third })
    assert(third:connect(last, { color, depth }) == third)
    graph:chain(color, {})
    graph:chain(depth, { last })
    last:connect(sink, color)
    return graph:build()
end }
)", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    ASSERT_EQ(tasks.size(), 5U);
    for (size_t i = 0; i < 3; ++i) {
        const auto& edges = tasks[i].get_entry("connect").as_list();
        ASSERT_EQ(edges.size(), 2U);
        EXPECT_EQ(edges[0].get_value<std::string>("to"), tasks[i + 1].get_value<std::string>("id"));
        EXPECT_EQ(edges[1].get_value<std::string>("to"), tasks[i + 1].get_value<std::string>("id"));
        EXPECT_EQ(edges[0].get_value<std::string>("resource"), "color");
        EXPECT_EQ(edges[1].get_value<std::string>("resource"), "depth");
    }
    EXPECT_EQ(tasks[3].get_entry("connect").as_list().size(), 1U);
    EXPECT_EQ(tasks[3].get_entry("connect").as_list()[0].get_value<std::string>("resource"), "color");
    EXPECT_FALSE(tasks[4].contains("connect"));
    EXPECT_EQ(recipe.get_entry("resources").as_list().size(), 2U);
}

TEST_F(WBERendGraphScriptVKResourceTest, ConnectionFanOutLeavesConsumersUnordered) {
    const DataValue recipe = RendGraphScriptVK::run("connection_fan_out", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function compute_pipeline(p_key)
    return helper_graph:add_compute_pipeline(p_key)
end
return { build = function()
    local color = graph_utils.storage_buffer("color", 16)
    local depth = graph_utils.storage_buffer("depth", 32)
    local graph = graph_utils.graph()
    local producer = graph:compute("producer", { pipeline = compute_pipeline("producer") })
    local first = graph:compute("first", { pipeline = compute_pipeline("first") })
    local second = graph:compute("second", { pipeline = compute_pipeline("second") })
    assert(producer:connect_all({ first, second }, { color, depth }) == producer)
    return graph:build()
end }
)", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    const auto& edges = tasks[0].get_entry("connect").as_list();
    ASSERT_EQ(edges.size(), 4U);
    EXPECT_EQ(edges[0].get_value<std::string>("to"), "first");
    EXPECT_EQ(edges[0].get_value<std::string>("resource"), "color");
    EXPECT_EQ(edges[1].get_value<std::string>("to"), "second");
    EXPECT_EQ(edges[1].get_value<std::string>("resource"), "color");
    EXPECT_EQ(edges[2].get_value<std::string>("to"), "first");
    EXPECT_EQ(edges[2].get_value<std::string>("resource"), "depth");
    EXPECT_EQ(edges[3].get_value<std::string>("to"), "second");
    EXPECT_EQ(edges[3].get_value<std::string>("resource"), "depth");
    EXPECT_FALSE(tasks[1].contains("connect"));
    EXPECT_FALSE(tasks[2].contains("connect"));
}

TEST_F(WBERendGraphScriptVKResourceTest, ResourceBundlesSnapshotMembersAndPreserveConnectionOrderAndDuplicates) {
    const DataValue recipe = RendGraphScriptVK::run("resource_bundle_connections", R"(
local Graph = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = Graph.new()
    local color = graph:add_storage_buffer("color", 16)
    local depth = graph:add_storage_buffer("depth", 32)
    local source = { depth, color, depth }
    local bundle = graph:resource_bundle(source)
    assert(bundle.id == nil)
    assert(bundle.resources ~= source)
    assert(bundle.resources[1] == depth and bundle.resources[2] == color and bundle.resources[3] == depth)
    source[1], source[2], source[3] = color, nil, nil
    local first = graph:import_task("first")
    local second = graph:import_task("second")
    local third = graph:import_task("third")
    local fourth = graph:import_task("fourth")
    local fifth = graph:import_task("fifth")
    local sixth = graph:import_task("sixth")
    graph:chain(bundle, { first, second, third })
    assert(third:connect(fourth, bundle) == third)
    assert(fourth:connect_all({ fifth, sixth }, bundle) == fourth)
    return graph:build()
end }
)", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    ASSERT_EQ(tasks.size(), 6U);
    for (size_t i = 0; i < 3; ++i) {
        const auto& edges = tasks[i].get_entry("connect").as_list();
        ASSERT_EQ(edges.size(), 3U);
        for (const auto& edge : edges) {
            EXPECT_EQ(edge.get_value<std::string>("to"), tasks[i + 1].get_value<std::string>("id"));
        }
        EXPECT_EQ(edges[0].get_value<std::string>("resource"), "depth");
        EXPECT_EQ(edges[1].get_value<std::string>("resource"), "color");
        EXPECT_EQ(edges[2].get_value<std::string>("resource"), "depth");
    }
    const auto& fan_out = tasks[3].get_entry("connect").as_list();
    ASSERT_EQ(fan_out.size(), 6U);
    for (size_t i = 0; i < fan_out.size(); ++i) {
        EXPECT_EQ(fan_out[i].get_value<std::string>("to"), i % 2 == 0 ? "fifth" : "sixth");
        EXPECT_EQ(fan_out[i].get_value<std::string>("resource"), i / 2 == 1 ? "color" : "depth");
    }
    EXPECT_FALSE(tasks[4].contains("connect"));
    EXPECT_FALSE(tasks[5].contains("connect"));
    EXPECT_EQ(recipe.get_entry("resources").as_list().size(), 2U);
    for (const auto& task : tasks) {
        EXPECT_FALSE(task.contains("resources"));
        EXPECT_FALSE(task.contains("resource_bundle"));
    }
}

TEST_F(WBERendGraphScriptVKResourceTest, ResourceBundlesCollectOnlyUsedMembersAndRetainImageViewDependenciesAcrossGraphs) {
    const DataValue recipe = RendGraphScriptVK::run("resource_bundle_collection", R"(
local Graph = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = Graph.new()
    local image = graph:add_storage_image("image", "r16g16b16a16_sfloat", 16, 8)
    local view = image:view()
    local scratch = graph:add_storage_buffer("scratch", 64)
    local unused = graph:add_storage_buffer("unused", 32)
    local bundle = graph:resource_bundle({ view, scratch })
    graph:resource_bundle({ unused })
    assert(#graph:build().resources == 0)
    local producer = graph:import_task("producer")
    local consumer = graph:import_task("consumer")
    graph:chain(bundle, { producer, consumer })
    local other = Graph.new()
    local other_producer = other:import_task("other_producer")
    local other_consumer = other:import_task("other_consumer")
    other_producer:connect(other_consumer, bundle)
    local other_recipe = other:build()
    assert(#other_recipe.resources == 3)
    assert(other_recipe.tasks[1].connect[1].resource == "image.view")
    assert(other_recipe.tasks[1].connect[2].resource == "scratch")
    return graph:build()
end }
)", DataValue::make_map());
    const auto& resources = recipe.get_entry("resources").as_list();
    ASSERT_EQ(resources.size(), 3U);
    EXPECT_EQ(resources[0].get_value<std::string>("id"), "image");
    EXPECT_EQ(resources[1].get_value<std::string>("id"), "image.view");
    EXPECT_EQ(resources[1].get_entry("image_view").get_value<std::string>("image"), "image");
    EXPECT_EQ(resources[2].get_value<std::string>("id"), "scratch");
    EXPECT_TRUE(recipe.get_entry("imports").as_list().empty());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    EXPECT_EQ(tasks[0].get_entry("connect").as_list()[0].get_value<std::string>("resource"), "image.view");
    EXPECT_EQ(tasks[0].get_entry("connect").as_list()[1].get_value<std::string>("resource"), "scratch");
}

TEST_F(WBERendGraphScriptVKResourceTest, ResourceBundleConstructionRejectsInvalidAndNestedMembersWithoutCollectingResources) {
    const DataValue recipe = RendGraphScriptVK::run("resource_bundle_validation", R"(
local Graph = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = Graph.new()
    local resource = graph:add_storage_buffer("resource", 16)
    local other = graph:add_storage_buffer("other", 32)
    local mesh = graph:add_mesh("mesh")
    local compute = graph:add_compute_pipeline("compute")
    local graphics = graph:add_graphics_pipeline("graphics")
    local bundle = graph:resource_bundle({ resource })
    local invalid_lists = {
        {},
        { [1] = resource, [3] = other },
        { resource, extra = other },
        { resource, "invalid" },
        { resource, {} },
        { resource, { id = "malformed", kind = "buffer" } },
        { resource, mesh },
        { resource, compute },
        { resource, graphics },
        { resource, bundle },
        "invalid",
    }
    for _, resources in ipairs(invalid_lists) do
        assert(not pcall(function() graph:resource_bundle(resources) end))
        assert(#graph:build().resources == 0)
    end
    assert(not pcall(function() graph:resource_bundle(nil) end))
    return graph:build()
end }
)", DataValue::make_map());
    EXPECT_TRUE(recipe.get_entry("resources").as_list().empty());
    EXPECT_TRUE(recipe.get_entry("tasks").as_list().empty());
}

TEST_F(WBERendGraphScriptVKResourceTest, ResourceBundleConnectionFailuresLeaveExistingEdgesUnchanged) {
    const DataValue recipe = RendGraphScriptVK::run("resource_bundle_connection_validation", R"(
local Graph = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = Graph.new()
    local resource = graph:add_storage_buffer("resource", 16)
    local other = graph:add_storage_buffer("other", 32)
    local database = graph:add_mesh("mesh")
    local producer = graph:import_task("producer")
    local consumer = graph:import_task("consumer")
    local third = graph:import_task("third")
    local foreign = Graph.new():import_task("foreign")
    local bundle = graph:resource_bundle({ resource, other })
    producer:connect(consumer, resource)
    local original_edge = producer.edges[1]
    local function assert_unchanged()
        assert(#producer.edges == 1 and producer.edges[1] == original_edge)
        assert(consumer.edges == nil and third.edges == nil)
    end
    local function reject(p_call)
        assert(not pcall(p_call))
        assert_unchanged()
    end
    reject(function() graph:chain(bundle, { producer, consumer, foreign }) end)
    reject(function() producer:connect(foreign, bundle) end)
    reject(function() producer:connect_all({ consumer, foreign }, bundle) end)
    local invalid_members = {
        {},
        { [1] = resource, [3] = other },
        { resource, extra = other },
        { resource, "invalid" },
        { resource, database },
        { resource, graph:resource_bundle({ other }) },
        "invalid",
    }
    for _, members in ipairs(invalid_members) do
        bundle.resources = members
        reject(function() graph:chain(bundle, { producer, consumer, third }) end)
        reject(function() producer:connect(consumer, bundle) end)
        reject(function() producer:connect_all({ consumer, third }, bundle) end)
    end
    bundle.resources = nil
    reject(function() graph:chain(bundle, { producer, consumer }) end)
    reject(function() producer:connect(consumer, bundle) end)
    reject(function() producer:connect_all({ consumer, third }, bundle) end)
    return graph:build()
end }
)", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    ASSERT_EQ(tasks.size(), 3U);
    ASSERT_EQ(tasks[0].get_entry("connect").as_list().size(), 1U);
    EXPECT_EQ(tasks[0].get_entry("connect").as_list()[0].get_value<std::string>("to"), "consumer");
    EXPECT_EQ(tasks[0].get_entry("connect").as_list()[0].get_value<std::string>("resource"), "resource");
    EXPECT_FALSE(tasks[1].contains("connect"));
    EXPECT_FALSE(tasks[2].contains("connect"));
    ASSERT_EQ(recipe.get_entry("resources").as_list().size(), 1U);
    EXPECT_EQ(recipe.get_entry("resources").as_list()[0].get_value<std::string>("id"), "resource");
}

TEST_F(WBERendGraphScriptVKResourceTest, ResourceBundlesPreserveConflictingOwnedHandlesForBuildValidation) {
    EXPECT_THROW((void)RendGraphScriptVK::run("conflicting_bundle_handles", R"(
local Graph = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = Graph.new()
    local first = graph:add_storage_buffer("same", 16)
    local second = graph:add_storage_buffer("same", 32)
    local bundle = graph:resource_bundle({ first, second })
    local producer = graph:import_task("producer")
    local consumer = graph:import_task("consumer")
    producer:connect(consumer, bundle)
    assert(#producer.edges == 2)
    assert(producer.edges[1].resource == first and producer.edges[2].resource == second)
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
}

TEST_F(WBERendGraphScriptVKResourceTest, InputsFromBindsTaskLocalNamesAndConnectsEachHandleOnce) {
    const DataValue recipe = RendGraphScriptVK::run("input_connections", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function compute_pipeline(p_key)
    return helper_graph:add_compute_pipeline(p_key)
end
return { build = function()
    local color = graph_utils.storage_buffer("color", 16)
    local normal = graph_utils.storage_buffer("normal", 32)
    local other = graph_utils.storage_buffer("other", 64)
    local graph = graph_utils.graph()
    local producer = graph:compute("producer", { pipeline = compute_pipeline("producer") })
    local first = graph:compute("first", { pipeline = compute_pipeline("first") })
    local second = graph:compute("second", { pipeline = compute_pipeline("second") })
    assert(first:inputs_from(producer, { normal = normal, albedo = color, albedo_again = color }) == first)
    second:inputs_from(producer, { albedo = other })
    return graph:build()
end }
)", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    const auto& edges = tasks[0].get_entry("connect").as_list();
    ASSERT_EQ(edges.size(), 3U);
    EXPECT_EQ(edges[0].get_value<std::string>("to"), "first");
    EXPECT_EQ(edges[0].get_value<std::string>("resource"), "color");
    EXPECT_EQ(edges[1].get_value<std::string>("to"), "first");
    EXPECT_EQ(edges[1].get_value<std::string>("resource"), "normal");
    EXPECT_EQ(edges[2].get_value<std::string>("to"), "second");
    EXPECT_EQ(edges[2].get_value<std::string>("resource"), "other");
    const auto& first_aliases = tasks[1].get_entry("aliases");
    EXPECT_EQ(first_aliases.get_value<std::string>("albedo"), "color");
    EXPECT_EQ(first_aliases.get_value<std::string>("albedo_again"), "color");
    EXPECT_EQ(first_aliases.get_value<std::string>("normal"), "normal");
    EXPECT_EQ(tasks[2].get_entry("aliases").get_value<std::string>("albedo"), "other");
    EXPECT_FALSE(tasks[1].contains("connect"));
    EXPECT_FALSE(tasks[2].contains("connect"));
    EXPECT_FALSE(tasks[1].contains("inputs_from"));
    EXPECT_FALSE(tasks[0].contains("connect_all"));
    EXPECT_EQ(recipe.get_entry("resources").as_list().size(), 3U);
}

TEST_F(WBERendGraphScriptVKResourceTest, ConnectionBatchesValidateBeforeChangingTheGraph) {
    const DataValue recipe = RendGraphScriptVK::run("connection_batch_validation", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function compute_pipeline(p_key)
    return helper_graph:add_compute_pipeline(p_key)
end
return { build = function()
    local resource = graph_utils.storage_buffer("resource", 16)
    local other = graph_utils.storage_buffer("other", 32)
    local graph = graph_utils.graph()
    local producer = graph:compute("producer", { pipeline = compute_pipeline("producer") })
    local consumer = graph:compute_indirect("consumer", { pipeline = compute_pipeline("consumer"), indirect_buffer = resource })
    local foreign = graph_utils.graph():compute("foreign", { pipeline = compute_pipeline("foreign") })
    consumer:alias("z_existing", other)
    local invalid_calls = {
        function() producer:connect_all({ consumer, foreign }, resource) end,
        function() producer:connect(consumer, { resource, "invalid" }) end,
        function() graph:chain({ resource, "invalid" }, { producer, consumer }) end,
        function() graph:chain(resource, { producer, consumer, foreign }) end,
        function() consumer:inputs_from(foreign, { a_input = resource }) end,
        function() consumer:inputs_from(producer, { a_input = resource, z_existing = other }) end,
        function() consumer:inputs_from(producer, { a_input = other, resource = other }) end,
        function() consumer:inputs_from(producer, { a_input = resource, z_invalid = "invalid" }) end,
        function() producer:connect(consumer, {}) end,
        function() producer:connect_all({}, resource) end,
        function() consumer:inputs_from(producer, {}) end,
        function() producer:connect(consumer, { [1] = resource, [3] = other }) end,
        function() producer:connect_all({ [1] = consumer, [3] = consumer }, resource) end,
        function() graph:chain(resource, { [1] = producer, [3] = consumer }) end,
        function() producer:connect(consumer, { resource, extra = other }) end,
    }
    for _, call in ipairs(invalid_calls) do
        local succeeded = pcall(call)
        assert(not succeeded)
        assert(producer.edges == nil)
        assert(consumer.aliases.a_input == nil)
        assert(consumer.aliases.resource == nil)
        assert(consumer.aliases.z_existing == other)
    end
    return graph:build()
end }
)", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    EXPECT_FALSE(tasks[0].contains("connect"));
    EXPECT_FALSE(tasks[1].contains("connect"));
    EXPECT_FALSE(tasks[1].get_entry("aliases").contains("a_input"));
}

TEST_F(WBERendGraphScriptVKResourceTest, InputConnectionsPreserveConflictingOwnedHandlesForValidation) {
    EXPECT_THROW((void)RendGraphScriptVK::run("conflicting_input_handles", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function compute_pipeline(p_key)
    return helper_graph:add_compute_pipeline(p_key)
end
return { build = function()
    local first = graph_utils.storage_buffer("same", 16)
    local second = graph_utils.storage_buffer("same", 32)
    local graph = graph_utils.graph()
    local producer = graph:compute("producer", { pipeline = compute_pipeline("producer") })
    graph:compute("consumer", { pipeline = compute_pipeline("consumer") }):inputs_from(producer, { first = first, second = second })
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
}

TEST_F(WBERendGraphScriptVKResourceTest, RejectsTaskHandlesFromAnotherGraph) {
    EXPECT_THROW((void)RendGraphScriptVK::run("foreign_tasks", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
return { build = function()
    local resource = graph_utils.storage_buffer("shared", 16)
    local graph = graph_utils.graph()
    local other = graph_utils.graph()
    local first = graph:add({ id = "first", type = "registry_probe" })
    local foreign = other:add({ id = "second", type = "registry_probe" })
    first:connect(foreign, resource)
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
}

TEST_F(WBERendGraphScriptVKResourceTest, RejectsForgedTaskHandlesWithMatchingIds) {
    EXPECT_THROW((void)RendGraphScriptVK::run("forged_tasks", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
return { build = function()
    local resource = graph_utils.storage_buffer("shared", 16)
    local graph = graph_utils.graph()
    local first = graph:add({ id = "first", type = "registry_probe" })
    local second = graph:add({ id = "second", type = "registry_probe" })
    graph:chain(resource, { first, { id = second.id, graph = graph } })
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
}

TEST_F(WBERendGraphScriptVKResourceTest, RejectsRenamedTaskHandlesAndUnregisteredEdgeTargets) {
    EXPECT_THROW((void)RendGraphScriptVK::run("renamed_task", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = graph_utils.graph()
    local task = graph:add({ id = "original", type = "registry_probe" })
    task.id = "changed"
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
    EXPECT_THROW((void)RendGraphScriptVK::run("unregistered_edge", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = graph_utils.graph()
    graph:add({ id = "first", type = "registry_probe", edges = {
        { to = { id = "missing" }, resource = graph_utils.storage_buffer("shared", 16) },
    } })
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
}

TEST_F(WBERendGraphScriptVKResourceTest, RejectsRepeatedTaskLocalAliasNames) {
    EXPECT_THROW((void)RendGraphScriptVK::run("repeated_alias", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
return { build = function()
    local resource = graph_utils.storage_buffer("shared", 16)
    local graph = graph_utils.graph()
    local task = graph:add({ id = "first", type = "registry_probe", aliases = { input = resource } })
    task:alias("input", resource)
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
}

TEST_F(WBERendGraphScriptVKResourceTest, TypedConstructorsLowerResourceParametersAndKeepPlainRecipeTables) {
    const DataValue recipe = RendGraphScriptVK::run("tests.teal_graph_api", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    ASSERT_EQ(tasks.size(), 11U);
    EXPECT_EQ(tasks[0].get_value<std::string>("type"), "run_compute_pipeline");
    EXPECT_EQ(tasks[0].get_entry("params").get_value<std::string>("pipeline"), "prepare_pipeline");
    EXPECT_TRUE(tasks[0].get_entry("trigger").get_value<bool>("activate"));
    EXPECT_TRUE(tasks[0].get_entry("toggle").get_value<bool>("enabled"));
    EXPECT_EQ(tasks[1].get_value<std::string>("type"), "run_graphics_pipeline");
    EXPECT_EQ(tasks[1].get_entry("aliases").get_value<std::string>("render_target_colors[0]"), "color_image.view");
    EXPECT_EQ(tasks[2].get_value<std::string>("type"), "graphics_draw_mesh");
    EXPECT_EQ(tasks[2].get_entry("params").get_value<std::string>("transform"), "commands");
    EXPECT_EQ(tasks[2].get_entry("params").get_value<std::string>("mesh"), "mesh_key");
    EXPECT_EQ(tasks[3].get_value<std::string>("type"), "run_graphics_pipeline_indirect");
    EXPECT_EQ(tasks[3].get_entry("params").get_value<std::string>("index_buffer"), "commands");
    EXPECT_EQ(tasks[3].get_entry("params").get_value<std::string>("indirect_buffer"), "commands");
    EXPECT_EQ(tasks[4].get_value<std::string>("type"), "run_compute_pipeline_indirect");
    EXPECT_EQ(tasks[4].get_entry("params").get_value<std::string>("indirect_buffer"), "commands");
    EXPECT_EQ(tasks[5].get_value<std::string>("type"), "run_compute_pipelines_sequential");
    const auto& dispatches = tasks[5].get_entry("params").get_entry("dispatches").as_list();
    ASSERT_EQ(dispatches.size(), 3U);
    const DataValue& sequence_aliases = tasks[5].get_entry("aliases");
    const std::string indirect_local = dispatches[1].get_value<std::string>("indirect_buffer");
    EXPECT_EQ(sequence_aliases.get_value<std::string>(indirect_local), "commands");
    const std::string shader_local = dispatches[1].get_entry("aliases").get_value<std::string>("shader_buffer");
    EXPECT_EQ(sequence_aliases.get_value<std::string>(shader_local), "commands");
    EXPECT_EQ(dispatches[2].get_value<std::string>("indirect_buffer"), "commands_local");
    EXPECT_EQ(dispatches[2].get_entry("aliases").get_value<std::string>("shader_buffer"), "commands_local");
    EXPECT_EQ(tasks[6].get_value<std::string>("type"), "generate_mipmaps");
    EXPECT_EQ(tasks[6].get_entry("params").get_value<std::string>("source_view"), "color_image.view");
    EXPECT_EQ(tasks[6].get_entry("params").get_value<std::string>("mip_chain_view"), "color_mips");
    EXPECT_EQ(tasks[7].get_value<std::string>("type"), "cpu_acquire_image");
    EXPECT_EQ(tasks[7].get_entry("params").get_value<std::string>("image_view"), "color_image.view");
    EXPECT_EQ(tasks[8].get_value<std::string>("type"), "full_memory_barrier");
    EXPECT_EQ(tasks[8].get_entry("params").get_value<std::string>("resource"), "commands");
    EXPECT_EQ(tasks[9].get_value<std::string>("type"), "ping_pong");
    EXPECT_TRUE(tasks[10].get_value<bool>("import"));
    EXPECT_FALSE(tasks[10].contains("params"));
    EXPECT_FALSE(tasks[10].contains("type"));
    for (const auto& task : tasks) {
        EXPECT_FALSE(task.contains("uses"));
        EXPECT_FALSE(task.contains("graph"));
        EXPECT_FALSE(task.contains("color_target"));
        EXPECT_FALSE(task.contains("depth_target"));
    }
    // Pipeline and mesh keys and event strings never become resource imports. Parameter handles emit one declaration each.
    ASSERT_EQ(recipe.get_entry("imports").as_list().size(), 1U);
    EXPECT_EQ(recipe.get_entry("imports").as_list()[0].as_string(), "input");
    EXPECT_EQ(recipe.get_entry("resources").as_list().size(), 4U);
    EXPECT_FALSE(tasks[2].contains("connect"));
    EXPECT_FALSE(tasks[5].contains("connect"));
}

TEST_F(WBERendGraphScriptVKResourceTest, DatabaseResourcesLowerWithoutGraphResourcesAndSnapshotTheirValues) {
    const DataValue recipe = RendGraphScriptVK::run("database_keys", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = graph_utils.graph()
    local mesh = graph:add_mesh("static_mesh")
    local graphics = graph:add_graphics_pipeline("shared_name")
    local compute = graph:add_compute_pipeline("shared_name")
    graph:add_mesh("unused_mesh")
    assert(#graph.tasks == 0)
    local draw = graph:draw_mesh("mesh", { mesh = mesh, pipeline = graphics, parts = { "opaque" } })
    graph:compute("compute", { pipeline = compute })
    graph:compute_sequential("sequence", { dispatches = { { pipeline = compute } } })
    graph:ping_pong("ping_pong", { ping_pipeline = compute, pong_pipeline = compute, iterations = 2,
        ping_prefix_pipeline = compute, pong_prefix_pipeline = compute })
    graph:ping_pong("without_prefix", { ping_pipeline = compute, pong_pipeline = compute, iterations = 1 })
    mesh.id = "changed_mesh"
    graphics.id = "changed_graphics"
    compute.id = "changed_compute"
    -- A database key does not reserve a task-local graph alias with the same name.
    draw:alias("static_mesh", graph_utils.storage_buffer("buffer", 16))
    return graph:build()
end }
)", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    ASSERT_EQ(tasks.size(), 5U);
    EXPECT_EQ(tasks[0].get_entry("params").get_value<std::string>("mesh"), "static_mesh");
    EXPECT_EQ(tasks[0].get_entry("params").get_value<std::string>("pipeline"), "shared_name");
    EXPECT_EQ(tasks[1].get_entry("params").get_value<std::string>("pipeline"), "shared_name");
    const auto& dispatch = tasks[2].get_entry("params").get_entry("dispatches").as_list()[0];
    EXPECT_EQ(dispatch.get_value<std::string>("pipeline"), "shared_name");
    for (const char* field : {"ping_pipeline", "pong_pipeline", "ping_prefix_pipeline", "pong_prefix_pipeline"}) {
        EXPECT_EQ(tasks[3].get_entry("params").get_value<std::string>(field), "shared_name");
    }
    EXPECT_FALSE(tasks[4].get_entry("params").contains("ping_prefix_pipeline"));
    EXPECT_FALSE(tasks[4].get_entry("params").contains("pong_prefix_pipeline"));
    for (const auto& task : tasks) {
        EXPECT_FALSE(task.contains("connect"));
        EXPECT_FALSE(task.contains("uses"));
    }
    EXPECT_FALSE(tasks[1].contains("aliases"));
    EXPECT_FALSE(tasks[2].contains("aliases"));
    EXPECT_EQ(tasks[0].get_entry("aliases").get_value<std::string>("static_mesh"), "buffer");
    EXPECT_TRUE(recipe.get_entry("imports").as_list().empty());
    ASSERT_EQ(recipe.get_entry("resources").as_list().size(), 1U);
    EXPECT_EQ(recipe.get_entry("resources").as_list()[0].get_value<std::string>("id"), "buffer");
}

TEST_F(WBERendGraphScriptVKResourceTest, DatabaseResourcesRejectWrongTypesBeforeAddingTasks) {
    const DataValue recipe = RendGraphScriptVK::run("invalid_database_keys", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = graph_utils.graph()
    local graphics = graph:add_graphics_pipeline("graphics")
    local compute = graph:add_compute_pipeline("compute")
    local mesh = graph:add_mesh("mesh")
    local invalid_calls = {
        function() graph:add_mesh("") end,
        function() graph:add_compute_pipeline("") end,
        function() graph:add_graphics_pipeline("") end,
        function() graph:compute("invalid", { pipeline = "compute" }) end,
        function() graph:compute("invalid", { pipeline = graphics }) end,
        function() graph:draw("invalid", { pipeline = compute }) end,
        function() graph:draw_mesh("invalid", { mesh = graphics, pipeline = graphics, parts = { "opaque" } }) end,
        function() graph:draw_mesh("invalid", { mesh = mesh, pipeline = mesh, parts = { "opaque" } }) end,
        function() graph:compute("invalid", { pipeline = graph_utils.import_resource("compute") }) end,
        function() graph:compute("invalid", { pipeline = { id = "", kind = "database", dependencies = {},
            resource_type = compute.resource_type } }) end,
        function() graph:compute("invalid", { pipeline = { id = 42, kind = "database", dependencies = {},
            resource_type = compute.resource_type } }) end,
        function() graph:compute("invalid", { pipeline = { id = "compute", dependencies = {},
            resource_type = compute.resource_type } }) end,
        function() graph:compute("invalid", { pipeline = { id = "compute", kind = "database",
            resource_type = compute.resource_type } }) end,
        function() graph:compute("invalid", { pipeline = { id = "compute", kind = "import", dependencies = {},
            resource_type = compute.resource_type } }) end,
        function() graph:compute("invalid", { pipeline = { id = "compute", kind = "database", dependencies = {},
            resource_type = graphics.resource_type } }) end,
        function() graph:compute("invalid", { pipeline = { id = "compute", kind = "database", dependencies = {} } }) end,
        function() graph:compute_sequential("invalid", { dispatches = { { pipeline = graphics } } }) end,
        function() graph:ping_pong("invalid", { ping_pipeline = compute, pong_pipeline = compute, iterations = 1,
            ping_prefix_pipeline = graphics, pong_prefix_pipeline = compute }) end,
    }
    for _, call in ipairs(invalid_calls) do
        assert(not pcall(call))
        assert(#graph.tasks == 0)
    end
    graph:compute("valid", { pipeline = compute })
    return graph:build()
end }
)", DataValue::make_map());
    ASSERT_EQ(recipe.get_entry("tasks").as_list().size(), 1U);
    EXPECT_EQ(recipe.get_entry("tasks").as_list()[0].get_value<std::string>("id"), "valid");
    EXPECT_TRUE(recipe.get_entry("imports").as_list().empty());
    EXPECT_TRUE(recipe.get_entry("resources").as_list().empty());
}

TEST_F(WBERendGraphScriptVKResourceTest, DatabaseResourcesRejectGraphBindingsBeforeChangingTasks) {
    const DataValue recipe = RendGraphScriptVK::run("database_graph_bindings", R"(
local Graph = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = Graph.new()
    local compute = graph:add_compute_pipeline("compute")
    local graphics = graph:add_graphics_pipeline("graphics")
    local buffer = graph:add_storage_buffer("buffer", 16)
    local producer = graph:compute("producer", { pipeline = compute })
    local consumer = graph:draw("consumer", { pipeline = graphics })
    consumer:alias("valid", buffer)
    producer:connect(consumer, buffer)
    local aliases = consumer.aliases
    local edges = producer.edges
    for _, resource in ipairs({ graph:add_mesh("mesh"), compute, graphics }) do
        local invalid_calls = {
            function() graph:full_memory_barrier("invalid", { resource = resource }) end,
            function() graph:compute_sequential("invalid", { dispatches = {
                { pipeline = compute, indirect_buffer = resource } } }) end,
            function() graph:compute_sequential("invalid", { dispatches = {
                { pipeline = compute, aliases = { invalid = resource } } } }) end,
            function() consumer:alias("invalid", resource) end,
            function() producer:connect(consumer, resource) end,
            function() producer:connect(consumer, { buffer, resource }) end,
            function() producer:connect_all({ consumer }, { buffer, resource }) end,
            function() consumer:inputs_from(producer, { first = buffer, invalid = resource }) end,
            function() graph:chain({ buffer, resource }, { producer, consumer }) end,
            function() consumer:color_target(0, resource) end,
            function() consumer:depth_target(resource) end,
            function() resource:view() end,
            function() graph:add_image_view("invalid", resource) end,
        }
        for _, call in ipairs(invalid_calls) do
            assert(not pcall(call))
            assert(#graph.tasks == 2)
            assert(consumer.aliases == aliases and aliases.valid == buffer)
            local alias_count = 0
            for _ in pairs(aliases) do
                alias_count = alias_count + 1
            end
            assert(alias_count == 1)
            assert(producer.edges == edges and #edges == 1)
        end
    end
    return graph:build()
end }
)", DataValue::make_map());
    ASSERT_EQ(recipe.get_entry("tasks").as_list().size(), 2U);
    const auto& producer = recipe.get_entry("tasks").as_list()[0];
    ASSERT_EQ(producer.get_entry("connect").as_list().size(), 1U);
    EXPECT_EQ(producer.get_entry("connect").as_list()[0].get_value<std::string>("resource"), "buffer");
    const auto& aliases = recipe.get_entry("tasks").as_list()[1].get_entry("aliases").as_map();
    EXPECT_EQ(aliases.size(), 1U);
    EXPECT_TRUE(recipe.get_entry("imports").as_list().empty());
    EXPECT_EQ(recipe.get_entry("resources").as_list().size(), 1U);
}

TEST_F(WBERendGraphScriptVKResourceTest, BuildRejectsDatabaseResourcesInRawGraphBindings) {
    EXPECT_NO_THROW((void)RendGraphScriptVK::run("raw_database_graph_bindings", R"(
local Graph = require("render_graph_utils.render_graph_utils")
return { build = function()
    local invalid_bindings = {
        function(graph, producer, consumer, buffer, resource)
            graph:add({ id = "invalid", type = "registry_probe", aliases = { input = resource } })
        end,
        function(graph, producer, consumer, buffer, resource)
            graph:add({ id = "invalid", type = "registry_probe", uses = { resource } })
        end,
        function(graph, producer, consumer, buffer, resource)
            graph:add({ id = "invalid", subgraph = { tasks = {} }, ports = { input = resource } })
        end,
        function(graph, producer, consumer, buffer, resource)
            graph:add({ id = "invalid", type = "registry_probe", edges = { { to = consumer, resource = resource } } })
        end,
        function(graph, producer, consumer, buffer, resource)
            consumer.aliases = { input = resource }
        end,
        function(graph, producer, consumer, buffer, resource)
            producer.edges = { { to = consumer, resource = resource } }
        end,
        function(graph, producer, consumer, buffer, resource)
            buffer.dependencies = { resource }
            consumer:alias("input", buffer)
        end,
        function(graph, producer, consumer, buffer, resource)
            graph:compute("invalid", { pipeline = graph:add_compute_pipeline("compute") }, { aliases = { input = resource } })
        end,
        function(graph, producer, consumer, buffer, resource)
            graph:compute("invalid", { pipeline = graph:add_compute_pipeline("compute") }, { uses = { resource } })
        end,
    }
    for _, resource_kind in ipairs({ "mesh", "compute", "graphics" }) do
        for _, bind in ipairs(invalid_bindings) do
            local graph = Graph.new()
            local resource = resource_kind == "mesh" and graph:add_mesh("resource") or
                resource_kind == "compute" and graph:add_compute_pipeline("resource") or graph:add_graphics_pipeline("resource")
            local buffer = graph:add_storage_buffer("buffer", 16)
            local producer = graph:add({ id = "producer", type = "registry_probe" })
            local consumer = graph:add({ id = "consumer", type = "registry_probe" })
            bind(graph, producer, consumer, buffer, resource)
            assert(not pcall(function() graph:build() end))
        end
    end
    return {}
end }
)", DataValue::make_map()));
}

TEST_F(WBERendGraphScriptVKResourceTest, ImageMethodsCacheDefaultViewsAndPreserveExplicitRanges) {
    const DataValue recipe = RendGraphScriptVK::run("image_methods", R"(
local Graph = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = Graph.new()
    local image = graph:add_image("image", "image_read_write", "r16g16b16a16_sfloat", { width = 16, height = 8 }, nil, 4)
    local view = image:view()
    assert(image:view() == view)
    local range = image:view("image_range", { base_mip_level = 1, mip_level_count = 3 })
    graph:draw("draw", { pipeline = graph:add_graphics_pipeline("pipeline") }):color_target(0, view)
    graph:cpu_acquire_image("readback", { image_view = range })
    return graph:build()
end }
)", DataValue::make_map());
    const auto& resources = recipe.get_entry("resources").as_list();
    ASSERT_EQ(resources.size(), 3U);
    const auto default_view = std::ranges::find_if(resources, [](const DataValue& p_resource) {
        return p_resource.get_value<std::string>("id") == "image.view";
    });
    ASSERT_NE(default_view, resources.end());
    EXPECT_EQ(default_view->get_entry("image_view").get_value<std::string>("image"), "image");
    const auto range = std::ranges::find_if(resources, [](const DataValue& p_resource) {
        return p_resource.get_value<std::string>("id") == "image_range";
    });
    ASSERT_NE(range, resources.end());
    const DataValue& description = range->get_entry("image_view").get_entry("view");
    EXPECT_EQ(description.get_value<int64_t>("base_mip_level"), 1);
    EXPECT_EQ(description.get_value<int64_t>("mip_level_count"), 3);
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_context(&allocator);
    const auto graph = RendGraphRecipeBuilderVK().build(recipe, RendGraphRecipeImportsVK{}, &render_context);
    ASSERT_EQ(graph.image_views.size(), 2U);
    // The no-argument method preserves the graph's existing default range of one mip and one layer.
    const auto default_description = std::ranges::find_if(graph.image_views, [](const auto& p_view) {
        return p_view.res_id == "image.view";
    });
    ASSERT_NE(default_description, graph.image_views.end());
    const RendImageViewDescVK configuration = default_description->factory(RendGraphContextVK{});
    EXPECT_EQ(configuration.mip_level_count, 1U);
    EXPECT_EQ(configuration.layer_count, 1U);
}

TEST_F(WBERendGraphScriptVKResourceTest, TypedConstructorsDoNotMutateCallerParametersOrSharedOptions) {
    const DataValue recipe = RendGraphScriptVK::run("typed_parameter_copies", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function compute_pipeline(p_key)
    return helper_graph:add_compute_pipeline(p_key)
end
return { build = function()
    local buffer = graph_utils.storage_buffer("commands", 64)
    local other = graph_utils.storage_buffer("other", 64)
    local options = { aliases = { input = buffer } }
    local params = { pipeline = compute_pipeline("pipeline"), indirect_buffer = buffer }
    local graph = graph_utils.graph()
    graph:compute_indirect("first", params, options)
    assert(params.indirect_buffer == buffer)
    assert(options.aliases.input == buffer)
    params.indirect_buffer = other
    options.aliases.input = other
    graph:compute_indirect("second", params, options)
    local dispatch = { pipeline = compute_pipeline("nested"), indirect_buffer = buffer, aliases = { shader_buffer = buffer } }
    graph:compute_sequential("sequence", { dispatches = { dispatch } })
    assert(dispatch.indirect_buffer == buffer)
    assert(dispatch.aliases.shader_buffer == buffer)
    dispatch.indirect_buffer = other
    dispatch.aliases.shader_buffer = other
    return graph:build()
end }
)", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    EXPECT_EQ(tasks[0].get_entry("params").get_value<std::string>("indirect_buffer"), "commands");
    EXPECT_EQ(tasks[0].get_entry("aliases").get_value<std::string>("input"), "commands");
    EXPECT_EQ(tasks[1].get_entry("params").get_value<std::string>("indirect_buffer"), "other");
    EXPECT_EQ(tasks[1].get_entry("aliases").get_value<std::string>("input"), "other");
    const auto& dispatch = tasks[2].get_entry("params").get_entry("dispatches").as_list()[0];
    const auto& aliases = tasks[2].get_entry("aliases");
    EXPECT_EQ(aliases.get_value<std::string>(dispatch.get_value<std::string>("indirect_buffer")), "commands");
    EXPECT_EQ(aliases.get_value<std::string>(dispatch.get_entry("aliases").get_value<std::string>("shader_buffer")), "commands");
}

TEST_F(WBERendGraphScriptVKResourceTest, AttachmentMethodsKeepAliasesTaskScopedAndRejectDuplicateTargets) {
    const DataValue recipe = RendGraphScriptVK::run("attachment_methods", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function graphics_pipeline(p_key)
    return helper_graph:add_graphics_pipeline(p_key)
end
return { build = function()
    local first = graph_utils.import_resource("first")
    local second = graph_utils.import_resource("second")
    local depth = graph_utils.import_resource("depth")
    local graph = graph_utils.graph()
    local draw = graph:draw("first", { pipeline = graphics_pipeline("pipeline") })
    assert(draw:color_target(0, first) == draw)
    assert(draw:depth_target(depth) == draw)
    graph:draw("second", { pipeline = graphics_pipeline("pipeline") }):color_target(0, second)
    return graph:build()
end }
)", DataValue::make_map());
    const auto& tasks = recipe.get_entry("tasks").as_list();
    EXPECT_EQ(tasks[0].get_entry("aliases").get_value<std::string>("render_target_colors[0]"), "first");
    EXPECT_EQ(tasks[1].get_entry("aliases").get_value<std::string>("render_target_colors[0]"), "second");
    EXPECT_EQ(tasks[0].get_entry("aliases").get_value<std::string>("render_target_depth"), "depth");
    EXPECT_THROW((void)RendGraphScriptVK::run("duplicate_attachment", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function graphics_pipeline(p_key)
    return helper_graph:add_graphics_pipeline(p_key)
end
return { build = function()
    local target = graph_utils.import_resource("target")
    local graph = graph_utils.graph()
    graph:draw("draw", { pipeline = graphics_pipeline("pipeline") }):color_target(0, target):color_target(0, target)
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
    EXPECT_THROW((void)RendGraphScriptVK::run("invalid_attachment_index", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function graphics_pipeline(p_key)
    return helper_graph:add_graphics_pipeline(p_key)
end
return { build = function()
    local graph = graph_utils.graph()
    graph:draw("draw", { pipeline = graphics_pipeline("pipeline") }):color_target(-1, graph_utils.import_resource("target"))
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
}

TEST_F(WBERendGraphScriptVKResourceTest, SequentialHandleBindingsPreserveSameNameResourcesAndRejectConflictingAliases) {
    const DataValue recipe = RendGraphScriptVK::run("sequential_identity_bindings", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function compute_pipeline(p_key)
    return helper_graph:add_compute_pipeline(p_key)
end
return { build = function()
    local commands = graph_utils.storage_buffer("commands", 64)
    local same_name = graph_utils.storage_buffer("__render_graph_resource_1", 64)
    local graph = graph_utils.graph()
    graph:compute_sequential("sequence", { dispatches = {
        { pipeline = compute_pipeline("first"), indirect_buffer = commands },
        { pipeline = compute_pipeline("second"), aliases = { input = "__render_graph_resource_1" } },
    } }, { uses = { same_name } })
    return graph:build()
end }
)", DataValue::make_map());
    const auto& task = recipe.get_entry("tasks").as_list()[0];
    EXPECT_EQ(task.get_entry("aliases").get_value<std::string>("commands"), "commands");
    EXPECT_FALSE(task.get_entry("aliases").contains("__render_graph_resource_1"));
    const auto& dispatches = task.get_entry("params").get_entry("dispatches").as_list();
    EXPECT_EQ(dispatches[1].get_entry("aliases").get_value<std::string>("input"), "__render_graph_resource_1");
    EXPECT_THROW((void)RendGraphScriptVK::run("conflicting_sequential_alias", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function compute_pipeline(p_key)
    return helper_graph:add_compute_pipeline(p_key)
end
return { build = function()
    local commands = graph_utils.storage_buffer("commands", 64)
    local other = graph_utils.storage_buffer("other", 64)
    local graph = graph_utils.graph()
    graph:compute_sequential("sequence", { dispatches = {
        { pipeline = compute_pipeline("pipeline"), indirect_buffer = commands },
    } }, { aliases = { commands = other } })
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
}

TEST_F(WBERendGraphScriptVKResourceTest, RejectsViewsOfOwnedNonImageResourcesAndConflictingDefaultViewIds) {
    EXPECT_THROW((void)RendGraphScriptVK::run("buffer_view", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
return { build = function()
    local graph = graph_utils.graph()
    local buffer = graph_utils.storage_buffer("buffer", 16)
    graph:cpu_acquire_image("readback", { image_view = buffer:view() })
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
    EXPECT_THROW((void)RendGraphScriptVK::run("default_view_collision", R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function graphics_pipeline(p_key)
    return helper_graph:add_graphics_pipeline(p_key)
end
return { build = function()
    local image = graph_utils.storage_image("image", "r16g16b16a16_sfloat", 16, 8)
    local graph = graph_utils.graph()
    graph:draw("draw", { pipeline = graphics_pipeline("pipeline") }):color_target(0, image:view())
    graph:full_memory_barrier("barrier", { resource = graph_utils.storage_buffer("image.view", 16) })
    return graph:build()
end }
)", DataValue::make_map()), std::runtime_error);
}

TEST_F(WBERendGraphScriptVKResourceTest, TypedResourceParametersRejectAliasesThatRedirectTheirHandles) {
    constexpr std::string_view REDIRECT_RECIPE = R"(
local graph_utils = require("render_graph_utils.render_graph_utils")
local helper_graph = graph_utils.new()
local function compute_pipeline(p_key)
    return helper_graph:add_compute_pipeline(p_key)
end
return { build = function(context)
    local commands = graph_utils.storage_buffer("commands", 64)
    local other = graph_utils.storage_buffer("other", 64)
    local graph = graph_utils.graph()
    if context.mode == "options" then
        graph:compute_indirect("dispatch", { pipeline = compute_pipeline("pipeline"), indirect_buffer = commands },
            { aliases = { commands = other } })
    else
        local dispatch = graph:compute_indirect("dispatch", { pipeline = compute_pipeline("pipeline"), indirect_buffer = commands })
        if context.mode == "method" then
            dispatch:alias("commands", other)
        elseif context.mode == "mutation" then
            dispatch.aliases = { commands = other }
        else
            dispatch:alias("commands", commands)
        end
    end
    return graph:build()
end }
)";
    for (const char* mode : {"options", "method", "mutation"}) {
        SCOPED_TRACE(mode);
        DataValue context = DataValue::make_map();
        context.set_value("mode", std::string(mode));
        EXPECT_THROW((void)RendGraphScriptVK::run("redirect_handle", REDIRECT_RECIPE, context), std::runtime_error);
    }
    const DataValue recipe = RendGraphScriptVK::run("identity_handle", REDIRECT_RECIPE, DataValue::make_map());
    const auto& dispatch = recipe.get_entry("tasks").as_list()[0];
    EXPECT_EQ(dispatch.get_entry("params").get_value<std::string>("indirect_buffer"), "commands");
    EXPECT_EQ(dispatch.get_entry("aliases").get_value<std::string>("commands"), "commands");
    EXPECT_EQ(recipe.get_entry("resources").as_list().size(), 1U);
}

TEST_F(WBERendGraphScriptVKResourceTest, RunsTheShippedPixelSortingRecipe) {
    RenderAllocator allocator(WBE_KI_B(16));
    RenderContextVK render_data(&allocator);
    render_data.frames_in_flight = 2;
    render_data.swap_chain_format = VK_FORMAT_B8G8R8A8_SRGB;
    DataValue context = RendGraphScriptVK::make_context(render_data, RendGraphContextVK{});
    context.set_value("source_width", 64);
    context.set_value("source_height", 32);
    context.set_value("orientation", std::string("horizontal"));
    const DataValue recipe = RendGraphScriptVK::run("render_graphs.pixel_sorting", context);
    // 3 line passes, the radix sort, 2 image passes, present, and the imported ImGui task.
    const DataValue::List& tasks = recipe.get_entry("tasks").as_list();
    ASSERT_EQ(tasks.size(), 8U);
    EXPECT_EQ(tasks.back().get_value<std::string>("id"), "draw_imgui");
    EXPECT_TRUE(tasks.back().get_value<bool>("import"));
    // The radix sort is one task dispatching the preparation and 5 passes for each of its 4 digits.
    const DataValue& radix_sort = tasks.at(3);
    EXPECT_EQ(radix_sort.get_value<std::string>("id"), "radix_sort");
    EXPECT_EQ(radix_sort.get_value<std::string>("type"), "run_compute_pipelines_sequential");
    EXPECT_EQ(radix_sort.get_entry("params").get_entry("dispatches").as_list().size(), 21U);
    EXPECT_EQ(recipe.get_entry("resources").as_list().size(), 12U);
}

TEST_F(WBERendGraphScriptVKResourceTest, BuildsTheSVO3LeafDeduplicationAsOneSequentialTask) {
    const DataValue recipe = RendGraphScriptVK::run("tests.teal_svo_3", DataValue::make_map());
    // The deferred-list reset, the gathering, and one task clamping the leaf count and dispatching the deduplication.
    const DataValue::List& tasks = recipe.get_entry("tasks").as_list();
    ASSERT_EQ(tasks.size(), 3U);
    EXPECT_EQ(tasks[1].get_value<std::string>("type"), "graphics_draw_mesh");
    EXPECT_EQ(tasks[1].get_entry("aliases").get_value<std::string>("svo_deferred_list"), "svo_3_deferred_list");
    bool gather_connects_deduplication = false;
    for (const DataValue& connection : tasks[1].get_entry("connect").as_list()) {
        gather_connects_deduplication = gather_connects_deduplication ||
            (connection.get_value<std::string>("to") == "svo_3_deduplicate_leaves" &&
                connection.get_value<std::string>("resource") == "svo_3_deferred_list");
    }
    EXPECT_TRUE(gather_connects_deduplication);
    const DataValue& deduplicate = tasks[2];
    EXPECT_EQ(deduplicate.get_value<std::string>("id"), "svo_3_deduplicate_leaves");
    EXPECT_EQ(deduplicate.get_value<std::string>("type"), "run_compute_pipelines_sequential");
    // The clamping, the deduplication's preparation, the radix sort's preparation and 5 passes for each of its 4 digits,
    // the marking, the scan's preparation and 4 passes, and the scatter.
    const DataValue::List& dispatches = deduplicate.get_entry("params").get_entry("dispatches").as_list();
    ASSERT_EQ(dispatches.size(), 30U);
    const DataValue& aliases = deduplicate.get_entry("aliases");
    const auto bound = [&](const DataValue& p_dispatch, std::string_view p_name) {
        return aliases.get_value<std::string>(p_dispatch.get_entry("aliases").get_value<std::string>(p_name));
    };
    EXPECT_EQ(dispatches[0].get_value<std::string>("pipeline"), "svo_3_prepare_leaves_shader");
    EXPECT_EQ(dispatches[1].get_value<std::string>("pipeline"), "dedup_large_prepare_dispatch");
    EXPECT_EQ(dispatches[2].get_value<std::string>("pipeline"), "radix_sort_prepare_dispatch");
    EXPECT_EQ(bound(dispatches[2], "input_buffer"), "svo_3_deferred_list");
    const DataValue& mark = dispatches[23];
    EXPECT_EQ(mark.get_value<std::string>("pipeline"), "dedup_large_mark");
    EXPECT_EQ(aliases.get_value<std::string>(mark.get_value<std::string>("indirect_buffer")),
        "svo_3_deferred_list_dedup_dispatch");
    EXPECT_EQ(bound(mark, "marks"), "svo_3_deferred_list_marks");
    // The scan's blocks pass scans the marks in place, and its sections pass runs the fixed 256 workgroups.
    EXPECT_EQ(dispatches[25].get_value<std::string>("pipeline"), "large_prefix_scan_blocks");
    EXPECT_EQ(bound(dispatches[25], "data_buffer"), "svo_3_deferred_list_marks");
    EXPECT_EQ(aliases.get_value<std::string>(dispatches[25].get_value<std::string>("indirect_buffer")),
        "svo_3_deferred_list_mark_dispatch");
    EXPECT_EQ(dispatches[26].get_value<uint32_t>("group_count_x"), 256U);
    EXPECT_EQ(dispatches[29].get_value<std::string>("pipeline"), "dedup_large_scatter");
    EXPECT_EQ(bound(dispatches[29], "unique_keys"), "svo_3_unique_leaves");
}

TEST_F(WBERendGraphScriptVKResourceTest, RunsTheShippedDeferredRecipeWithItsVoxelAndPixelSortFeatures) {
    RenderAllocator allocator(WBE_KI_B(16));
    RenderContextVK render_data(&allocator);
    render_data.frames_in_flight = 2;
    render_data.swap_chain_format = VK_FORMAT_B8G8R8A8_SRGB;
    auto make_extent = [](uint32_t p_width, uint32_t p_height) {
        DataValue extent = DataValue::make_map();
        extent.set_value("width", p_width);
        extent.set_value("height", p_height);
        return extent;
    };
    auto make_imports = [](std::initializer_list<const char*> p_names) {
        DataValue imports = DataValue::make_list();
        for (const char* name : p_names) {
            imports.push_back(DataValue::make_string(name));
        }
        return imports;
    };
    auto make_octree = [&] {
        DataValue octree = DataValue::make_map();
        octree.set_value("module", std::string("render_graph_utils.features.voxel_octree"));
        octree.set_entry("imports", make_imports({"octree_info", "octree_static", "octree_dynamic", "octree_debug_draw_index_buffer",
            "octree_debug_draw_indirect_buffer", "octree_debug_leaf_masks", "octree_debug_node_draw_indirect_buffer",
            "octree_debug_node_list", "octree_static_levels[0]", "octree_dynamic_levels[0]", "octree_static_levels[1]",
            "octree_dynamic_levels[1]"}));
        octree.set_value("level_count", 2);
        DataValue leaf_groups = DataValue::make_map();
        leaf_groups.set_value("x", 4);
        leaf_groups.set_value("y", 4);
        leaf_groups.set_value("z", 2);
        octree.set_entry("leaf_gather_group_count", leaf_groups);
        octree.set_value("node_gather_group_count", 8);
        octree.set_value("leaf_draw_count", 33);
        return octree;
    };
    auto make_svo = [&] {
        DataValue svo = DataValue::make_map();
        svo.set_value("module", std::string("render_graph_utils.features.voxel_svo"));
        svo.set_value("variant", std::string("pass_published"));
        svo.set_value("shader_prefix", std::string("svo_2"));
        svo.set_value("dynamic_shader_prefix", std::string("svo_2_dynamic"));
        svo.set_entry("imports", make_imports({"svo_info", "svo_node_buffer", "svo_dynamic_leaves", "svo_leaf_buffer", "svo_debug_draw_index_buffer",
            "svo_debug_draw_indirect_buffer", "svo_pre_deferred_debug_draw_indirect_buffer"}));
        svo.set_value("extent", 512);
        svo.set_value("max_depth", 9);
        DataValue buffer_sizes = DataValue::make_map();
        buffer_sizes.set_value("dynamic_nodes", 1024);
        buffer_sizes.set_value("deferred_list", 516);
        buffer_sizes.set_value("pre_deferred_debug_info", 60);
        buffer_sizes.set_value("pre_deferred_debug_list", 512);
        svo.set_entry("buffer_sizes", buffer_sizes);
        for (const char* group_count : {
                 "reset_group_count",
                 "deferred_dispatch_group_count",
                 "dynamic_clear_group_count",
                 "static_publication_group_count",
                 "dynamic_publication_group_count",
             }) {
            svo.set_value(group_count, 4);
        }
        DataValue static_build = DataValue::make_map();
        static_build.set_value("event", std::string("test.voxel_static_build"));
        static_build.set_value("activate", true);
        svo.set_entry("static_build", static_build);
        return svo;
    };
    auto run = [&](bool p_summed_area_table, bool p_post_processing, DataValue p_voxel) {
        DataValue context =
            RendGraphScriptVK::make_context(render_data, RendGraphContextVK{.swap_chain_extent = {.width = 640, .height = 360}});
        context.set_value("summed_area_table", p_summed_area_table);
        context.set_value("spot_light_count", 2);
        context.set_value("max_spot_light_info_count", 4);
        context.set_entry("shadow_extent", make_extent(256, 256));
        context.set_value("shadow_mipmap_level_count", 9);
        context.set_entry("bloom_extent", make_extent(640, 360));
        context.set_value("bloom_blur_pass_count", 4);
        DataValue formats = DataValue::make_map();
        for (const char* role : {
                 "gbuffer_albedo",
                 "gbuffer_normal",
                 "gbuffer_rma",
                 "gbuffer_depth",
                 "shadowmap_depth",
                 "shadow_moments",
                 "rsm_flux",
                 "rsm_normal",
                 "lighting_output",
             }) {
            formats.set_value(role, std::string("r16g16b16a16_sfloat"));
        }
        context.set_entry("formats", formats);
        context.set_value("bloom_acquire_event", std::string("test.bloom"));
        context.set_value("frame_sequence_event", std::string("test.frames"));
        context.set_value("has_mesh_acceleration_structure", false);
        context.set_entry("voxel", p_voxel);
        DataValue transforms = DataValue::make_list();
        transforms.push_back(DataValue::make_string("dynamic_mesh_entity_0.transform"));
        transforms.push_back(DataValue::make_string("dynamic_mesh_entity_1.transform"));
        context.set_entry("dynamic_mesh_transform_ids", transforms);
        DataValue toggles = DataValue::make_map();
        for (const char* group : {"disabled", "enabled", "post_svo_build", "pre_deferred_propagation"}) {
            DataValue toggle = DataValue::make_map();
            toggle.set_value("event", std::string("test.voxel_debug_view.") + group);
            toggle.set_value("enabled", std::string_view(group) == "disabled");
            toggles.set_entry(group, toggle);
        }
        context.set_entry("voxel_debug_view_toggles", toggles);
        if (p_post_processing) {
            DataValue post_processing = DataValue::make_map();
            post_processing.set_value("module", std::string("render_graph_utils.features.pixel_sort"));
            post_processing.set_value("direction", std::string("vertical"));
            post_processing.set_value("scope", std::string("span"));
            post_processing.set_value("color_format", std::string("r16g16b16a16_sfloat"));
            post_processing.set_value("queue_family", std::string("compute_1"));
            post_processing.set_value("contrast_import", std::string("pixel_sort_contrast"));
            post_processing.set_value("toggle_event", std::string("test.pixel_sort"));
            post_processing.set_value("enabled", true);
            context.set_entry("post_processing", post_processing);
        }
        return RendGraphScriptVK::run("render_graphs.deferred", context);
    };
    const auto count_tasks_with = [](const DataValue::List& p_tasks, std::string_view p_field) {
        size_t count = 0;
        for (const DataValue& task : p_tasks) {
            count += task.contains(p_field) ? 1U : 0U;
        }
        return count;
    };
    const auto contains_import = [](const DataValue& p_recipe, std::string_view p_name) {
        const DataValue::List& imports = p_recipe.get_entry("imports").as_list();
        return std::ranges::any_of(imports, [p_name](const DataValue& p_import) {
            return p_import.as_string() == p_name;
        });
    };
    const auto find_by_id = [](const DataValue::List& p_entries, std::string_view p_id) -> const DataValue* {
        for (const DataValue& entry : p_entries) {
            if (entry.get_value<std::string>("id") == p_id) {
                return &entry;
            }
        }
        return nullptr;
    };
    // 2 lights x (4 shadow mesh draws + 1 filter), 4 G-buffer mesh draws, indirect lighting and its debug view, the frame
    // sequence acquisition, ImGui, direct and combine lighting, extract and blur bloom, the bloom acquisition, 2 presents,
    // 2 billboards, and the voxel subgraph.
    constexpr size_t BASE_TASK_COUNT = 10 + 4 + 2 + 1 + 1 + 2 + 2 + 1 + 2 + 2 + 1;
    const DataValue mipmap = run(false, false, make_octree());
    const DataValue::List& mipmap_tasks = mipmap.get_entry("tasks").as_list();
    EXPECT_EQ(mipmap_tasks.size(), BASE_TASK_COUNT);
    EXPECT_EQ(count_tasks_with(mipmap_tasks, "toggle"), 2U);
    EXPECT_EQ(count_tasks_with(mipmap_tasks, "trigger"), 2U);
    // The octree subgraph holds its frame-begin barrier, 6 imported C++ tasks, and 2 gathers and 2 overlays, all toggled,
    // and imports the application's textures and buffers.
    const DataValue* octree = find_by_id(mipmap_tasks, "voxel");
    ASSERT_NE(octree, nullptr);
    const DataValue& octree_recipe = octree->get_entry("subgraph");
    EXPECT_EQ(octree_recipe.get_entry("tasks").as_list().size(), 11U);
    EXPECT_EQ(count_tasks_with(octree_recipe.get_entry("tasks").as_list(), "toggle"), 4U);
    EXPECT_TRUE(contains_import(octree_recipe, "octree_dynamic"));
    EXPECT_EQ(octree->get_entry("ports").get_value<std::string>("depth"), "gbuffer_depth");
    // The pass-published SVO subgraph holds 15 tasks: the 5 static construction tasks are triggered, and the pre-deferred
    // capture and the 2 overlays toggled. The pixel sort subgraph holds 6 tasks, and its one toggle gates them all.
    const DataValue summed_area_table = run(true, true, make_svo());
    const DataValue::List& tasks = summed_area_table.get_entry("tasks").as_list();
    ASSERT_EQ(tasks.size(), BASE_TASK_COUNT + 1);
    EXPECT_EQ(count_tasks_with(tasks, "toggle"), 2U + 1U);
    const DataValue* svo = find_by_id(tasks, "voxel");
    ASSERT_NE(svo, nullptr);
    const DataValue& svo_recipe = svo->get_entry("subgraph");
    const DataValue::List& svo_tasks = svo_recipe.get_entry("tasks").as_list();
    EXPECT_EQ(svo_tasks.size(), 15U);
    EXPECT_EQ(count_tasks_with(svo_tasks, "toggle"), 3U);
    EXPECT_EQ(count_tasks_with(svo_tasks, "trigger"), 5U);
    EXPECT_TRUE(contains_import(svo_recipe, "svo_node_buffer"));
    // The SVO declares its 5 scratch buffers with the sizes it is given.
    const DataValue::List& svo_resources = svo_recipe.get_entry("resources").as_list();
    EXPECT_EQ(svo_resources.size(), 5U);
    const DataValue* deferred_list = find_by_id(svo_resources, "svo_deferred_list_ping");
    ASSERT_NE(deferred_list, nullptr);
    EXPECT_EQ(deferred_list->get_entry("buffer").get_value<int64_t>("size"), 516);
    const DataValue* dynamic_nodes = find_by_id(svo_resources, "svo_dynamic_nodes");
    ASSERT_NE(dynamic_nodes, nullptr);
    EXPECT_EQ(dynamic_nodes->get_entry("buffer").get_value<int64_t>("size"), 1024);
    const DataValue* pixel_sort = find_by_id(tasks, "post_processing");
    ASSERT_NE(pixel_sort, nullptr);
    EXPECT_TRUE(pixel_sort->contains("toggle"));
    const DataValue& pixel_sort_recipe = pixel_sort->get_entry("subgraph");
    EXPECT_EQ(pixel_sort_recipe.get_entry("tasks").as_list().size(), 6U);
    EXPECT_EQ(count_tasks_with(pixel_sort_recipe.get_entry("tasks").as_list(), "toggle"), 0U);
    EXPECT_TRUE(contains_import(pixel_sort_recipe, "pixel_sort_contrast"));
    EXPECT_EQ(pixel_sort->get_entry("ports").get_value<std::string>("color"), "result_image");
}

} // namespace WhiteBirdEngine
