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
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/function_utils.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_resource_list_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/roc_multislots_vk.hh"
#include "function/render_vk/roc_single_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/defs.hh"
#include <array>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/** @brief Minimal render task used to verify that task factories create and destroy tasks. */
class RTDirectDescriptorVK final : public IRenderTaskVK {
public:
    explicit RTDirectDescriptorVK(bool* p_destroyed) : destroyed(p_destroyed) {
    }
    virtual ~RTDirectDescriptorVK() override {
        *destroyed = true;
    }
    RTDirectDescriptorVK(const RTDirectDescriptorVK&) = delete;
    RTDirectDescriptorVK(RTDirectDescriptorVK&&) noexcept = delete;
    RTDirectDescriptorVK& operator=(const RTDirectDescriptorVK&) = delete;
    RTDirectDescriptorVK& operator=(RTDirectDescriptorVK&&) noexcept = delete;

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& /*p_reads*/,
        Vector<ResAccessVK, RenderAllocator>& /*p_writes*/) const override {
    }

    virtual void record(VkCommandBuffer /*p_command_buffer*/, double /*p_delta*/) override {
    }

private:
    bool* destroyed;
};

/** @brief Minimal render object that fills the container used to test container binding. */
class TestContainerResourceVK final : public IRenderObjectVK {
public:
    explicit TestContainerResourceVK(const RenderContextVK* p_render_data) : IRenderObjectVK(p_render_data) {
    }
    virtual ~TestContainerResourceVK() override = default;
    TestContainerResourceVK(const TestContainerResourceVK&) = delete;
    TestContainerResourceVK(TestContainerResourceVK&&) noexcept = delete;
    TestContainerResourceVK& operator=(const TestContainerResourceVK&) = delete;
    TestContainerResourceVK& operator=(TestContainerResourceVK&&) noexcept = delete;

    virtual TypeID get_type_uuid() const override {
        return TypeUUID<ROBufferVK>::UUID;
    }
};

TEST(WBERenderGraphDescriptorVKTest, DescriptorStoresTaskFactory) {
    RenderAllocator allocator(WBE_KI_B(64));
    bool destroyed = false;
    int created_count = 0;
    RendTaskDescVK descriptor(&allocator, "direct_task", [&allocator, &destroyed, &created_count](const RendGraphContextVK& /*p_context*/) {
        ++created_count;
        return make_unique<RTDirectDescriptorVK>(&allocator, &destroyed);
    });
    RendTaskDescVK copied_descriptor = descriptor;

    EXPECT_EQ(copied_descriptor.task_id, "direct_task");
    ASSERT_NE(copied_descriptor.factory, nullptr);
    EXPECT_EQ(created_count, 0);

    Unique<IRenderTaskVK> task = copied_descriptor.factory(RendGraphContextVK{});

    EXPECT_FALSE(task.is_null());
    EXPECT_EQ(created_count, 1);
    EXPECT_FALSE(destroyed);
    task.reset();
    EXPECT_TRUE(destroyed);
}

TEST(WBERenderGraphDescriptorVKTest, TaskFactoryConstructorRejectsNullFactory) {
    RenderAllocator allocator(WBE_KI_B(64));

    EXPECT_THROW(RendTaskDescVK(&allocator, "null_task", nullptr), std::runtime_error);
}

TEST(WBERenderGraphDescriptorVKTest, FluentAliasesAndConnectionsAreStored) {
    RenderAllocator allocator(WBE_KI_B(64));
    RendTaskDescVK descriptor(&allocator);
    descriptor.task_id = "run_lighting";
    descriptor.alias("render_target_colors[0]", "lighting_output").connect("run_bloom", "lighting_output");

    ASSERT_EQ(descriptor.next.size(), 1U);
    EXPECT_EQ(descriptor.next.front().next_task_id, "run_bloom");
    EXPECT_EQ(descriptor.next.front().res_id, "lighting_output");
    EXPECT_EQ(descriptor.resource_map.resolve("render_target_colors[0]"), "lighting_output");
    EXPECT_EQ(descriptor.resource_map.resolve("gbuffer_albedo"), "gbuffer_albedo");
    EXPECT_THROW(descriptor.alias("render_target_colors[0]", "other"), std::runtime_error);
}

TEST(WBERenderGraphDescriptorVKTest, GetTaskReturnsStoredDescriptorForLaterConnections) {
    RenderAllocator allocator(WBE_KI_B(64));
    RendGraphDescVK descriptor(&allocator);
    RendTaskDescVK task(&allocator);
    task.task_id = "producer";
    descriptor.add_task(std::move(task));

    descriptor.get_task("producer").connect("consumer", "buffer");

    ASSERT_EQ(descriptor.tasks.front().next.size(), 1U);
    EXPECT_EQ(descriptor.tasks.front().next.front().next_task_id, "consumer");
    EXPECT_EQ(descriptor.tasks.front().next.front().res_id, "buffer");
    EXPECT_THROW(descriptor.get_task("missing"), std::runtime_error);
}

TEST(WBERenderGraphDescriptorVKTest, ResourceListIsStoredAsAnAggregateBinding) {
    RenderAllocator allocator(WBE_KI_B(64));
    RendGraphDescVK descriptor(&allocator);
    auto resource_list = make_ref<RenderResourceListVK>(&allocator, &allocator);

    descriptor.add_resource("textures", *resource_list);

    ASSERT_EQ(descriptor.resources.size(), 1U);
    EXPECT_EQ(descriptor.resources.front().res_id, "textures");
    EXPECT_EQ(descriptor.resources.front().resource_list, resource_list);
    EXPECT_EQ(descriptor.resources.front().container, MEM_NULL);
}

TEST(WBERenderGraphDescriptorVKTest, GraphOwnedResourceDeclarationsAreStoredForCompilation) {
    RenderAllocator allocator(WBE_KI_B(64));
    RendGraphDescVK descriptor(&allocator);

    descriptor
        .add_buffer("camera",
            [=](const RendGraphContextVK& /*p_context*/) {
                return RendBufferDescVK{.usage = RendResPrimaryUsageVK::BUFFER_UNIFORM,
                    .domain = DeviceDomain::HOST_DEVICE,
                    .per_frame = true,
                    .initial_data = {1, 2}};
            })
        .add_image("albedo_image",
            [=](const RendGraphContextVK& /*p_context*/) {
                return RendImageDescVK{.usage = RendResPrimaryUsageVK::IMAGE_RENDER_TARGET,
                    .format = VK_FORMAT_R8G8B8A8_UNORM,
                    .extent = {.width = 8, .height = 8, .depth = 1},
                    .mip_levels = 2};
            })
        .add_image_view("albedo",
            "albedo_image",
            [=](const RendGraphContextVK& /*p_context*/) {
                return RendImageViewDescVK{.mip_level_count = VK_REMAINING_MIP_LEVELS};
            })
        .add_sampler("sampler", [=](const RendGraphContextVK& /*p_context*/) {
            return RendSamplerDescVK{.min_filter = VK_FILTER_NEAREST, .max_anisotropy = 4.0F};
        });

    ASSERT_EQ(descriptor.buffers.size(), 1U);
    EXPECT_EQ(descriptor.buffers.front().res_id, "camera");
    EXPECT_TRUE(descriptor.buffers.front().factory(RendGraphContextVK{}).per_frame);
    EXPECT_EQ(descriptor.buffers.front().factory(RendGraphContextVK{}).initial_data.size(), 2U);
    ASSERT_EQ(descriptor.images.size(), 1U);
    EXPECT_EQ(descriptor.images.front().factory(RendGraphContextVK{}).mip_levels, 2U);
    ASSERT_EQ(descriptor.image_views.size(), 1U);
    EXPECT_EQ(descriptor.image_views.front().image_id, "albedo_image");
    EXPECT_EQ(descriptor.image_views.front().factory(RendGraphContextVK{}).mip_level_count, VK_REMAINING_MIP_LEVELS);
    ASSERT_EQ(descriptor.samplers.size(), 1U);
    EXPECT_EQ(descriptor.samplers.front().factory(RendGraphContextVK{}).min_filter, VK_FILTER_NEAREST);
    EXPECT_EQ(descriptor.samplers.front().factory(RendGraphContextVK{}).mag_filter, VK_FILTER_LINEAR);
    EXPECT_EQ(descriptor.samplers.front().factory(RendGraphContextVK{}).max_anisotropy, 4.0F);
    EXPECT_FALSE(descriptor.samplers.front().factory(RendGraphContextVK{}).compare_op.has_value());
    EXPECT_TRUE(descriptor.resources.empty());
}

TEST(WBERenderGraphDescriptorVKTest, GraphOwnedResourceDeclarationsRejectEmptyIds) {
    RenderAllocator allocator(WBE_KI_B(64));
    RendGraphDescVK descriptor(&allocator);

    EXPECT_THROW(descriptor.add_buffer("",
                     [=](const RendGraphContextVK& /*p_context*/) {
                         return RendBufferDescVK{};
                     }),
        std::runtime_error);
    EXPECT_THROW(descriptor.add_image("",
                     [=](const RendGraphContextVK& /*p_context*/) {
                         return RendImageDescVK{};
                     }),
        std::runtime_error);
    EXPECT_THROW(descriptor.add_image_view("view", ""), std::runtime_error);
    EXPECT_THROW(descriptor.add_sampler(""), std::runtime_error);
}

TEST(WBERenderGraphDescriptorVKTest, GraphOwnedResourceDeclarationsRejectNullFactories) {
    RenderAllocator allocator(WBE_KI_B(64));
    RendGraphDescVK descriptor(&allocator);

    EXPECT_THROW(descriptor.add_buffer("buffer", nullptr), std::runtime_error);
    EXPECT_THROW(descriptor.add_image("image", nullptr), std::runtime_error);
}

TEST(WBERenderGraphDescriptorVKTest, GraphOwnedResourceFactoriesReceiveTheContext) {
    RenderAllocator allocator(WBE_KI_B(64));
    RendGraphDescVK descriptor(&allocator);

    descriptor.add_image("image", [](const RendGraphContextVK& p_context) {
        return RendImageDescVK{.format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {.width = p_context.swap_chain_extent.width, .height = p_context.swap_chain_extent.height, .depth = 1}};
    });
    descriptor.add_image_view("view", "image");
    descriptor.add_sampler("sampler");

    const RendImageDescVK image_desc =
        descriptor.images.front().factory(RendGraphContextVK{.swap_chain_extent = {.width = 640, .height = 360}});
    EXPECT_EQ(image_desc.extent.width, 640U);
    EXPECT_EQ(image_desc.extent.height, 360U);
    EXPECT_TRUE(descriptor.image_views.front().factory(RendGraphContextVK{}) == RendImageViewDescVK{});
    EXPECT_TRUE(descriptor.samplers.front().factory(RendGraphContextVK{}) == RendSamplerDescVK{});
}

TEST(WBERenderGraphDescriptorVKTest, ComposesMemberResourceIds) {
    EXPECT_EQ(IBindableRenderResourceVK::make_member_resource_id("sponza", MeshResourcesVK::NODES), "sponza.nodes");
}

TEST(WBERenderGraphDescriptorVKTest, IncompleteMeshResourcesRefuseToBind) {
    RenderAllocator allocator(WBE_KI_B(64));
    RendGraphDescVK descriptor(&allocator);
    RenderContextVK render_data(&allocator);
    MeshResourcesVK static_mesh(&render_data);

    EXPECT_THROW(descriptor.add_resource("static_mesh", static_mesh), std::runtime_error);
    EXPECT_TRUE(descriptor.resources.empty());
}

TEST(WBERenderGraphDescriptorVKTest, ContainerNotOwnedByRefRefusesToBindItself) {
    RenderAllocator allocator(WBE_KI_B(64));
    RendGraphDescVK descriptor(&allocator);
    RenderContextVK render_data(&allocator);
    ROCMultislotsVK container(&allocator, 1, [&allocator, &render_data](uint32_t /*p_slot*/) {
        return make_ref<TestContainerResourceVK>(&allocator, &render_data).dynamic_cast_ref<IRenderObjectVK>();
    });

    EXPECT_THROW(descriptor.add_resource("container", container), std::runtime_error);
    EXPECT_TRUE(descriptor.resources.empty());
}

namespace {

RendTaskDescVK make_empty_task(RenderAllocator* p_allocator, std::string_view p_task_id) {
    return RendTaskDescVK(p_allocator, p_task_id, [p_allocator](const RendGraphContextVK& /*p_context*/) {
        static bool destroyed = false;
        return make_unique<RTDirectDescriptorVK>(p_allocator, &destroyed);
    });
}

Ref<ROCSingleVK> make_import_container(RenderAllocator* p_allocator, const RenderContextVK* p_render_data) {
    return make_ref<ROCSingleVK>(p_allocator, [p_allocator, p_render_data](uint32_t /*p_slot*/) {
        return make_ref<TestContainerResourceVK>(p_allocator, p_render_data).dynamic_cast_ref<IRenderObjectVK>();
    });
}

// A subgraph with a port, an owned buffer, an image and its view, and two connected tasks using all three.
RendSubgraphDescVK make_sort_subgraph(RenderAllocator* p_allocator) {
    RendSubgraphDescVK subgraph(p_allocator);
    subgraph.add_port("color");
    subgraph.graph.add_buffer("keys", [](const RendGraphContextVK& /*p_context*/) {
        return RendBufferDescVK{};
    });
    subgraph.graph.add_image("mask_image", [](const RendGraphContextVK& /*p_context*/) {
        return RendImageDescVK{};
    });
    subgraph.graph.add_image_view("mask", "mask_image");
    subgraph.graph.add_image_view("color_copy", "color");
    subgraph.graph.add_task(make_empty_task(p_allocator, "register"));
    subgraph.graph.add_task(make_empty_task(p_allocator, "write"));
    subgraph.graph.get_task("register").alias("input", "color").connect("write", "keys").connect("write", "color");
    return subgraph;
}

} // namespace

TEST(WBERenderGraphDescriptorVKTest, ResourceMapScopesApplyAfterTheTaskBindingsWithoutBeingValidated) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderTaskResourceMap map(&allocator);
    map.bind("render_target_colors[0]", "color");
    const std::array<RenderTaskResourceMap::ScopeBinding, 2> inner{{{.source = "color", .target = "result_image"}, {.source = "keys", .target = "sort.keys"}}};
    map.enclose(inner);
    // An enclosing scope sees the inner scope's results: the inner `sort.keys` becomes `deferred.sort.keys`.
    const std::array<RenderTaskResourceMap::ScopeBinding, 2> outer{
        {{.source = "sort.keys", .target = "deferred.sort.keys"}, {.source = "result_image", .target = "deferred.result"}}};
    map.enclose(outer);

    EXPECT_EQ(map.resolve("render_target_colors[0]"), "deferred.result");
    EXPECT_EQ(map.resolve("keys"), "deferred.sort.keys");
    EXPECT_EQ(map.resolve("result_image"), "deferred.result");
    EXPECT_EQ(map.resolve("swap_chain"), "swap_chain");
    // Only the task's own binding has to be used.
    EXPECT_NO_THROW(map.validate_all_used("task"));
    const std::array<RenderTaskResourceMap::ScopeBinding, 2> repeated{{{.source = "a", .target = "b"}, {.source = "a", .target = "c"}}};
    EXPECT_THROW(map.enclose(repeated), std::runtime_error);
}

TEST(WBERenderGraphDescriptorVKTest, SubgraphsArePrefixedAndResolveTheirPortsAndResources) {
    RenderAllocator allocator(WBE_KI_B(128));
    RendGraphDescVK descriptor(&allocator);
    const std::array<RendSubgraphPortBindingVK, 1> ports{{{.port = "color", .resource = "result_image"}}};
    RendSubgraphVK& subgraph = descriptor.add_subgraph("sort", make_sort_subgraph(&allocator), ports);
    subgraph.connect("present", "result_image");

    ASSERT_EQ(descriptor.tasks.size(), 2U);
    EXPECT_EQ(descriptor.tasks[0].task_id, "sort.register");
    EXPECT_EQ(descriptor.tasks[1].task_id, "sort.write");
    const RendTaskDescVK& registering = descriptor.tasks[0];
    ASSERT_EQ(registering.next.size(), 2U);
    EXPECT_EQ(registering.next[0].next_task_id, "sort.write");
    EXPECT_EQ(registering.next[0].res_id, "sort.keys");
    EXPECT_EQ(registering.next[1].res_id, "result_image");
    // Ports, then declared resources, then the enclosing graph's resources.
    EXPECT_EQ(registering.resource_map.resolve("input"), "result_image");
    EXPECT_EQ(registering.resource_map.resolve("color"), "result_image");
    EXPECT_EQ(registering.resource_map.resolve("keys"), "sort.keys");
    EXPECT_EQ(registering.resource_map.resolve("mask"), "sort.mask");
    EXPECT_EQ(registering.resource_map.resolve("swap_chain"), "swap_chain");
    ASSERT_EQ(descriptor.buffers.size(), 1U);
    EXPECT_EQ(descriptor.buffers[0].res_id, "sort.keys");
    ASSERT_EQ(descriptor.image_views.size(), 2U);
    EXPECT_EQ(descriptor.image_views[0].res_id, "sort.mask");
    EXPECT_EQ(descriptor.image_views[0].image_id, "sort.mask_image");
    EXPECT_EQ(descriptor.image_views[1].image_id, "result_image");
    ASSERT_EQ(descriptor.subgraphs.size(), 1U);
    const RendSubgraphVK& record = descriptor.get_subgraph("sort");
    ASSERT_EQ(record.members.size(), 2U);
    EXPECT_EQ(record.members[1], "sort.write");
    ASSERT_EQ(record.next.size(), 1U);
    EXPECT_EQ(record.next[0].next_task_id, "present");
    EXPECT_THROW(descriptor.get_subgraph("missing"), std::runtime_error);
}

TEST(WBERenderGraphDescriptorVKTest, NestedSubgraphsArePrefixedAgain) {
    RenderAllocator allocator(WBE_KI_B(128));
    RendSubgraphDescVK outer(&allocator);
    outer.add_port("target");
    outer.graph.add_task(make_empty_task(&allocator, "begin"));
    const std::array<RendSubgraphPortBindingVK, 1> inner_ports{{{.port = "color", .resource = "target"}}};
    outer.graph.add_subgraph("sort", make_sort_subgraph(&allocator), inner_ports).connect("begin", "target");
    RendGraphDescVK descriptor(&allocator);
    const std::array<RendSubgraphPortBindingVK, 1> outer_ports{{{.port = "target", .resource = "result_image"}}};
    descriptor.add_subgraph("post", outer, outer_ports);

    ASSERT_EQ(descriptor.subgraphs.size(), 2U);
    const RendSubgraphVK& inner = descriptor.get_subgraph("post.sort");
    EXPECT_EQ(inner.members[0], "post.sort.register");
    ASSERT_EQ(inner.next.size(), 1U);
    EXPECT_EQ(inner.next[0].next_task_id, "post.begin");
    EXPECT_EQ(inner.next[0].res_id, "result_image");
    EXPECT_EQ(descriptor.get_subgraph("post").members.size(), 3U);
    const RendTaskDescVK& registering = descriptor.get_task("post.sort.register");
    EXPECT_EQ(registering.resource_map.resolve("input"), "result_image");
    EXPECT_EQ(registering.resource_map.resolve("keys"), "post.sort.keys");
    EXPECT_EQ(descriptor.buffers[0].res_id, "post.sort.keys");
}

TEST(WBERenderGraphDescriptorVKTest, SubgraphImportsMergeIntoTheGraph) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    Ref<ROCSingleVK> mesh = make_import_container(&allocator, &render_data);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(RendResVK{.res_id = "mesh", .container = mesh});
    RendSubgraphDescVK subgraph(&allocator);
    subgraph.graph.add_resource(RendResVK{.res_id = "mesh", .container = mesh});
    subgraph.graph.add_resource(RendResVK{.res_id = "camera", .container = make_import_container(&allocator, &render_data)});
    subgraph.graph.add_task(make_empty_task(&allocator, "draw"));
    descriptor.add_subgraph("first", subgraph, {});

    ASSERT_EQ(descriptor.resources.size(), 2U);
    EXPECT_EQ(descriptor.resources[1].res_id, "camera");
    // Each import of a render object wraps it in a container of its own; the same object still merges.
    const Ref<IRenderObjectVK> camera = descriptor.resources[1].container->get().dynamic_cast_ref<IRenderObjectVK>();
    RendSubgraphDescVK rewrapping(&allocator);
    rewrapping.graph.add_resource(RendResVK{.res_id = "camera", .container = make_ref<ROCSingleVK>(&allocator, [camera](uint32_t /*p_slot*/) {
        return camera;
    })});
    descriptor.add_subgraph("rewrapping", rewrapping, {});
    EXPECT_EQ(descriptor.resources.size(), 2U);
    // A second subgraph importing another resource under an imported id conflicts.
    RendSubgraphDescVK conflicting(&allocator);
    conflicting.graph.add_resource(RendResVK{.res_id = "mesh", .container = make_import_container(&allocator, &render_data)});
    EXPECT_THROW(descriptor.add_subgraph("second", conflicting, {}), std::runtime_error);
}

TEST(WBERenderGraphDescriptorVKTest, SubgraphsRejectInvalidPortsIdsAndEdges) {
    RenderAllocator allocator(WBE_KI_B(128));
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_task(make_empty_task(&allocator, "present"));
    const RendSubgraphDescVK subgraph = make_sort_subgraph(&allocator);
    const std::array<RendSubgraphPortBindingVK, 1> ports{{{.port = "color", .resource = "result_image"}}};
    const std::array<RendSubgraphPortBindingVK, 2> twice{{{.port = "color", .resource = "a"}, {.port = "color", .resource = "b"}}};
    const std::array<RendSubgraphPortBindingVK, 2> unknown{{{.port = "color", .resource = "a"}, {.port = "depth", .resource = "b"}}};
    const std::array<RendSubgraphPortBindingVK, 1> empty{{{.port = "color", .resource = ""}}};

    EXPECT_THROW(descriptor.add_subgraph("sort", subgraph, {}), std::runtime_error);
    EXPECT_THROW(descriptor.add_subgraph("sort", subgraph, twice), std::runtime_error);
    EXPECT_THROW(descriptor.add_subgraph("sort", subgraph, unknown), std::runtime_error);
    EXPECT_THROW(descriptor.add_subgraph("sort", subgraph, empty), std::runtime_error);
    EXPECT_THROW(descriptor.add_subgraph("", subgraph, ports), std::runtime_error);
    EXPECT_THROW(descriptor.add_subgraph("present", subgraph, ports), std::runtime_error);
    descriptor.add_subgraph("sort", subgraph, ports);
    EXPECT_THROW(descriptor.add_subgraph("sort", subgraph, ports), std::runtime_error);

    RendSubgraphDescVK shadowing(&allocator);
    shadowing.add_port("keys");
    shadowing.graph.add_buffer("keys", [](const RendGraphContextVK& /*p_context*/) {
        return RendBufferDescVK{};
    });
    const std::array<RendSubgraphPortBindingVK, 1> keys{{{.port = "keys", .resource = "result_image"}}};
    EXPECT_THROW(descriptor.add_subgraph("shadowing", shadowing, keys), std::runtime_error);

    RendSubgraphDescVK leaving(&allocator);
    leaving.graph.add_task(make_empty_task(&allocator, "inside"));
    leaving.graph.get_task("inside").connect("present", "result_image");
    EXPECT_THROW(descriptor.add_subgraph("leaving", leaving, {}), std::runtime_error);
    EXPECT_THROW(RendSubgraphDescVK(&allocator).add_port("a").add_port("a"), std::runtime_error);
}

} // namespace WhiteBirdEngine
