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
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/parser/parser_json.hh"
#include "function/render/i_render_object.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/render_resource_list_vk.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_metadata_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/roc_multislots_vk.hh"
#include "function/render_vk/roc_single_vk.hh"
#include "function/render_vk/rt_triggered_vk.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

constexpr std::string_view TEST_LOGICAL_RESOURCE = "logical";
constexpr std::string_view TEST_MISSING_RESOURCE = "missing";
constexpr std::string_view TEST_SHARED_RESOURCE = "shared";

DebugData make_descriptor_row(
    std::string_view p_resource_type, std::string_view p_descriptor_type, std::string_view p_heap, uint32_t p_heap_index) {
    DebugData row;
    row.set_value("resource_type", std::string(p_resource_type));
    row.set_value("descriptor_type", std::string(p_descriptor_type));
    row.set_value("heap", std::string(p_heap));
    row.set_value("heap_index", p_heap_index);
    row.set_value("device_address", nullptr);
    return row;
}

class TestDescriptorResourceVK final : public IRenderObjectVK {
public:
    TestDescriptorResourceVK(const RenderContextVK* p_render_data, DebugDataArray p_rows)
        : IRenderObjectVK(p_render_data), rows(p_render_data->allocator) {
        rows.reserve(p_rows.size());
        for (DebugData& row : p_rows) {
            rows.emplace_back(std::move(row));
        }
    }
    virtual ~TestDescriptorResourceVK() override = default;
    TestDescriptorResourceVK(const TestDescriptorResourceVK&) = delete;
    TestDescriptorResourceVK(TestDescriptorResourceVK&&) noexcept = delete;
    TestDescriptorResourceVK& operator=(const TestDescriptorResourceVK&) = delete;
    TestDescriptorResourceVK& operator=(TestDescriptorResourceVK&&) noexcept = delete;

    virtual TypeID get_type_uuid() const override {
        return static_hash("RenderGraphTestDescriptorResourceVK");
    }

    virtual DebugDataRows descriptor_debug_data() const override {
        return rows;
    }

private:
    DebugDataRows rows;
};

Ref<ROCSingleVK> make_test_container(RenderAllocator* p_allocator, const RenderContextVK* p_render_data) {
    Ref<IRenderObjectVK> resource =
        make_ref<TestDescriptorResourceVK>(p_allocator, p_render_data, DebugDataArray{}).dynamic_cast_ref<IRenderObjectVK>();
    return make_ref<ROCSingleVK>(p_allocator, [resource](uint32_t /*p_slot*/) {
        return resource;
    });
}

class RTGraphResourceProbeVK final : public IRenderTaskVK {
public:
    RTGraphResourceProbeVK(bool p_require_missing_resource, bool p_write_resource)
        : require_missing_resource(p_require_missing_resource), write_resource(p_write_resource) {
    }
    virtual ~RTGraphResourceProbeVK() override = default;
    RTGraphResourceProbeVK(const RTGraphResourceProbeVK&) = delete;
    RTGraphResourceProbeVK(RTGraphResourceProbeVK&&) noexcept = delete;
    RTGraphResourceProbeVK& operator=(const RTGraphResourceProbeVK&) = delete;
    RTGraphResourceProbeVK& operator=(RTGraphResourceProbeVK&&) noexcept = delete;

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void bind_resources(const RendResViewVK& p_resources) override {
        if (require_missing_resource) {
            static_cast<void>(p_resources.get(TEST_MISSING_RESOURCE));
        }
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& /*p_reads*/, Vector<ResAccessVK, RenderAllocator>& p_writes) const override {
        if (write_resource) {
            p_writes.push_back({.res_id = std::string(TEST_LOGICAL_RESOURCE), .usage = RendResUsageVK::STORAGE_BUFFER});
        }
    }

    virtual void record(VkCommandBuffer /*p_command_buffer*/, double /*p_delta*/) override {
    }

private:
    bool require_missing_resource;
    bool write_resource;
};

class RTGraphListAccessProbeVK final : public IRenderTaskVK {
public:
    RTGraphListAccessProbeVK(std::string_view p_resource_id, bool p_write) : resource_id(p_resource_id), write(p_write) {
    }
    virtual ~RTGraphListAccessProbeVK() override = default;
    RTGraphListAccessProbeVK(const RTGraphListAccessProbeVK&) = delete;
    RTGraphListAccessProbeVK(RTGraphListAccessProbeVK&&) noexcept = delete;
    RTGraphListAccessProbeVK& operator=(const RTGraphListAccessProbeVK&) = delete;
    RTGraphListAccessProbeVK& operator=(RTGraphListAccessProbeVK&&) noexcept = delete;

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override {
        if (write) {
            p_writes.push_back({.res_id = resource_id, .usage = RendResUsageVK::STORAGE_IMAGE});
        } else {
            p_reads.push_back({.res_id = resource_id, .usage = RendResUsageVK::SAMPLED_IMAGE});
        }
    }

    virtual void record(VkCommandBuffer /*p_command_buffer*/, double /*p_delta*/) override {
    }

private:
    std::string resource_id;
    bool write;
};

class RTGraphEdgeProbeVK final : public IRenderTaskVK {
public:
    RTGraphEdgeProbeVK(RenderAllocator* p_allocator,
        QueueFamilyVK p_queue_family,
        const std::vector<std::string>& p_reads,
        const std::vector<std::string>& p_writes)
        : queue(p_queue_family), reads(p_allocator), writes(p_allocator) {
        for (const std::string& read : p_reads) {
            reads.push_back({.res_id = read, .usage = RendResUsageVK::STORAGE_BUFFER});
        }
        for (const std::string& write : p_writes) {
            writes.push_back({.res_id = write, .usage = RendResUsageVK::STORAGE_BUFFER});
        }
    }
    virtual ~RTGraphEdgeProbeVK() override = default;
    RTGraphEdgeProbeVK(const RTGraphEdgeProbeVK&) = delete;
    RTGraphEdgeProbeVK(RTGraphEdgeProbeVK&&) noexcept = delete;
    RTGraphEdgeProbeVK& operator=(const RTGraphEdgeProbeVK&) = delete;
    RTGraphEdgeProbeVK& operator=(RTGraphEdgeProbeVK&&) noexcept = delete;

    virtual QueueFamilyVK queue_family() const override {
        return queue;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override {
        p_reads.insert(p_reads.end(), reads.begin(), reads.end());
        p_writes.insert(p_writes.end(), writes.begin(), writes.end());
    }

    virtual void record(VkCommandBuffer /*p_command_buffer*/, double /*p_delta*/) override {
    }

private:
    QueueFamilyVK queue;
    Vector<ResAccessVK, RenderAllocator> reads;
    Vector<ResAccessVK, RenderAllocator> writes;
};

// Graph validation succeeded when construction reaches command-buffer allocation, which needs a Vulkan device.
constexpr std::string_view TEST_VALIDATION_PASSED_MESSAGE = "Failed to create command pool";

RendTaskDescVK make_probe_desc(RenderAllocator* p_allocator,
    std::string_view p_task_id,
    QueueFamilyVK p_queue_family,
    std::initializer_list<std::string_view> p_reads,
    std::initializer_list<std::string_view> p_writes) {
    return RendTaskDescVK(p_allocator,
        p_task_id,
        [p_allocator,
            p_queue_family,
            reads = std::vector<std::string>(p_reads.begin(), p_reads.end()),
            writes = std::vector<std::string>(p_writes.begin(), p_writes.end())](const RendGraphContextVK& /*p_context*/) {
            return make_unique<RTGraphEdgeProbeVK>(p_allocator, p_allocator, p_queue_family, reads, writes);
        });
}

// Build p_graph from a callback that returns a copy of p_descriptor.
void build_graph(RenderGraphVK& p_graph, const RendGraphDescVK& p_descriptor) {
    p_graph.build_from_callback([p_descriptor](const RendGraphContextVK& /*p_context*/) {
        return p_descriptor;
    });
}

std::string build_error(RenderGraphVK& p_graph, RendGraphDescVK& p_descriptor) {
    try {
        build_graph(p_graph, p_descriptor);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return {};
}

} // namespace

TEST(WBERenderGraphVKTest, RendResViewDescriptorDebugDataAddsResourceIdsAndSortsDeterministically) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    render_data.frames_in_flight = 2;
    Map<std::string, Ref<RendResVK>, RenderAllocator> resources(&allocator);

    Ref<IRenderObjectVK> single_resource = make_ref<TestDescriptorResourceVK>(
        &allocator, &render_data, DebugDataArray{make_descriptor_row("ROImageViewVK", "sampled_image", "resource", 7)})
                                               .template dynamic_cast_ref<IRenderObjectVK>();
    auto single_container = make_ref<ROCSingleVK>(&allocator, [single_resource](uint32_t /*p_slot*/) {
        return single_resource;
    });
    auto per_frame_container = make_ref<ROCMultislotsVK>(
        &allocator,
        &allocator,
        render_data.frames_in_flight,
        [&allocator, &render_data](uint32_t /*p_slot*/) {
            return make_ref<TestDescriptorResourceVK>(&allocator, &render_data, DebugDataArray{}).template dynamic_cast_ref<IRenderObjectVK>();
        },
        false);
    per_frame_container->set(0,
        make_ref<TestDescriptorResourceVK>(
            &allocator, &render_data, DebugDataArray{make_descriptor_row("ROSamplerVK", "sampler", "sampler", 7)})
            .template dynamic_cast_ref<IRenderObjectVK>());
    per_frame_container->set(1,
        make_ref<TestDescriptorResourceVK>(&allocator,
            &render_data,
            DebugDataArray{make_descriptor_row("ROBufferVK", "uniform_buffer", "resource", 3),
                make_descriptor_row("ROBufferVK", "storage_buffer", "resource", 7)})
            .template dynamic_cast_ref<IRenderObjectVK>());

    auto alpha_resource = make_ref<RendResVK>(&allocator);
    alpha_resource->res_id = "alpha";
    alpha_resource->res_type_id = single_container->get_resource_type_id();
    alpha_resource->container = single_container;
    resources.emplace("alpha", alpha_resource);

    auto beta_resource = make_ref<RendResVK>(&allocator);
    beta_resource->res_id = "beta";
    beta_resource->res_type_id = per_frame_container->get_resource_type_id();
    beta_resource->container = per_frame_container;
    resources.emplace("beta", beta_resource);

    RendResViewVK resource_view(&resources);
    DebugDataRows rows = resource_view.descriptor_debug_data();

    ASSERT_EQ(rows.size(), 4U);
    EXPECT_EQ(rows[0].get_value<uint32_t>("heap_index"), 3U);
    EXPECT_EQ(rows[0].get_value<std::string>("resource_id"), "beta");
    EXPECT_EQ(rows[0].get_value<uint32_t>("slot_index"), 1U);
    EXPECT_EQ(rows[1].get_value<uint32_t>("heap_index"), 7U);
    EXPECT_EQ(rows[1].get_value<std::string>("heap"), "resource");
    EXPECT_EQ(rows[1].get_value<std::string>("resource_id"), "alpha");
    EXPECT_FALSE(rows[1].contains("slot_index"));
    EXPECT_EQ(rows[2].get_value<uint32_t>("heap_index"), 7U);
    EXPECT_EQ(rows[2].get_value<std::string>("heap"), "resource");
    EXPECT_EQ(rows[2].get_value<std::string>("resource_id"), "beta");
    EXPECT_EQ(rows[2].get_value<uint32_t>("slot_index"), 1U);
    EXPECT_EQ(rows[3].get_value<std::string>("heap"), "sampler");
    EXPECT_EQ(rows[3].get_value<std::string>("resource_id"), "beta");
    EXPECT_EQ(rows[3].get_value<uint32_t>("slot_index"), 0U);
}

TEST(WBERenderGraphVKTest, ListDescriptorsReturnsEmptyJsonForEmptyGraph) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    render_data.frames_in_flight = 1;
    RenderGraphVK graph(&render_data);

    DebugData descriptor_data = graph.list_descriptors();
    DebugDataArray rows = descriptor_data.get<DebugDataArray>();

    EXPECT_TRUE(rows.empty());
}

TEST(WBERenderGraphVKTest, ResourceViewLooksUpGraphGlobalResources) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    Map<std::string, Ref<RendResVK>, RenderAllocator> resources(&allocator);
    Ref<ROCSingleVK> global_container = make_test_container(&allocator, &render_data);

    auto global_resource = make_ref<RendResVK>(&allocator);
    global_resource->res_id = TEST_SHARED_RESOURCE;
    global_resource->res_type_id = global_container->get_resource_type_id();
    global_resource->container = global_container;
    resources.emplace(std::string(TEST_SHARED_RESOURCE), global_resource);

    RendResViewVK resource_view(&resources);
    EXPECT_EQ(resource_view.get(TEST_SHARED_RESOURCE).container, global_container);
    EXPECT_TRUE(resource_view.contains(TEST_SHARED_RESOURCE));
    EXPECT_THROW(resource_view.get(TEST_MISSING_RESOURCE), std::runtime_error);
}

TEST(WBERenderGraphVKTest, RejectsDuplicateGraphResourceRegistration) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    RenderGraphVK graph(&render_data);

    EXPECT_THROW(build_graph(graph, descriptor), std::runtime_error);
}

TEST(WBERenderGraphVKTest, MissingReflectedResourceFailsDuringTaskBinding) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_task("missing", [&allocator](const RendGraphContextVK& /*p_context*/) {
        return make_unique<RTGraphResourceProbeVK>(&allocator, true, false);
    });
    RenderGraphVK graph(&render_data);

    EXPECT_THROW(build_graph(graph, descriptor), std::runtime_error);
}

TEST(WBERenderGraphVKTest, GlobalResourceAccessesConflictAcrossTasks) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    descriptor.add_task("first", [&allocator](const RendGraphContextVK& /*p_context*/) {
        return make_unique<RTGraphResourceProbeVK>(&allocator, false, true);
    });
    descriptor.add_task("second", [&allocator](const RendGraphContextVK& /*p_context*/) {
        return make_unique<RTGraphResourceProbeVK>(&allocator, false, true);
    });
    RenderGraphVK graph(&render_data);

    try {
        build_graph(graph, descriptor);
        FAIL() << "Expected conflicting graph-global resource accesses to fail.";
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("resource 'logical' without a chain of edges on that resource"), std::string::npos);
    }
}

TEST(WBERenderGraphVKTest, InactiveTriggeredWritesStillRequireExplicitOrdering) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    descriptor.add_task("first", [&allocator](const RendGraphContextVK& /*p_context*/) {
        return make_unique<RTTriggeredVK>(&allocator, make_unique<RTGraphResourceProbeVK>(&allocator, false, true));
    });
    descriptor.add_task("second", [&allocator](const RendGraphContextVK& /*p_context*/) {
        return make_unique<RTTriggeredVK>(&allocator, make_unique<RTGraphResourceProbeVK>(&allocator, false, true));
    });
    RenderGraphVK graph(&render_data);

    try {
        build_graph(graph, descriptor);
        FAIL() << "Expected conflicting graph-global resource accesses to fail.";
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("resource 'logical' without a chain of edges on that resource"), std::string::npos);
    }
}

TEST(WBERenderGraphVKTest, ResourceListAccessExpandsToIndexedResourcesForConflictValidation) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    auto resource_list = make_ref<RenderResourceListVK>(&allocator, &allocator);
    resource_list->add_resource(
        make_ref<TestDescriptorResourceVK>(&allocator, &render_data, DebugDataArray{}).dynamic_cast_ref<IRenderObject>());
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource("textures", *resource_list);
    descriptor.add_task("writer", [&allocator](const RendGraphContextVK& /*p_context*/) {
        return make_unique<RTGraphListAccessProbeVK>(&allocator, "textures", true);
    });
    descriptor.add_task("reader", [&allocator](const RendGraphContextVK& /*p_context*/) {
        return make_unique<RTGraphListAccessProbeVK>(&allocator, "textures", false);
    });
    RenderGraphVK graph(&render_data);

    try {
        build_graph(graph, descriptor);
        FAIL() << "Expected conflicting expanded resource-list accesses to fail.";
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("tasks '"), std::string::npos);
        EXPECT_NE(message.find("writer"), std::string::npos);
        EXPECT_NE(message.find("reader"), std::string::npos);
        EXPECT_NE(message.find("resource 'textures[0]' without a chain of edges on that resource"), std::string::npos);
    }
}

TEST(WBERenderGraphVKTest, AliasedTaskAccessesResolveToGraphResourcesForValidation) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource("dynamic_nodes", *make_test_container(&allocator, &render_data));
    // Both tasks access the task-local "nodes" identifier, which their descriptors map to the same graph resource.
    RendTaskDescVK writer = make_probe_desc(&allocator, "writer", QueueFamilyVK::GRAPHICS, {}, {"nodes"});
    writer.alias("nodes", "dynamic_nodes");
    RendTaskDescVK reader = make_probe_desc(&allocator, "mapped_reader", QueueFamilyVK::GRAPHICS, {"nodes"}, {});
    reader.alias("nodes", "dynamic_nodes");
    descriptor.add_task(std::move(writer)).add_task(std::move(reader));
    RenderGraphVK graph(&render_data);

    EXPECT_NE(build_error(graph, descriptor).find("resource 'dynamic_nodes' without a chain of edges on that resource"),
        std::string::npos);
}

TEST(WBERenderGraphVKTest, TaskFactoriesReceiveTheContextOfTheRenderData) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    render_data.swap_chain_extent = {.width = 640, .height = 360};
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    VkExtent2D received_extent{};
    descriptor.add_task("task", [&allocator, &received_extent](const RendGraphContextVK& p_context) {
        received_extent = p_context.swap_chain_extent;
        return make_unique<RTGraphEdgeProbeVK>(
            &allocator, &allocator, QueueFamilyVK::GRAPHICS, std::vector<std::string>{}, std::vector<std::string>{std::string(TEST_LOGICAL_RESOURCE)});
    });
    RenderGraphVK graph(&render_data);

    EXPECT_NE(build_error(graph, descriptor).find(TEST_VALIDATION_PASSED_MESSAGE), std::string::npos);
    EXPECT_EQ(received_extent.width, 640U);
    EXPECT_EQ(received_extent.height, 360U);
    const RendGraphContextVK expected_context{.swap_chain_extent = {.width = 640, .height = 360}};
    EXPECT_TRUE(graph.get_context() == expected_context);
}

TEST(WBERenderGraphVKTest, RebuildCallbacksReceiveTheContextOfTheRenderData) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    render_data.swap_chain_extent = {.width = 800, .height = 600};
    Ref<RenderObjectContainerVK> container = make_test_container(&allocator, &render_data);
    VkExtent2D received_extent{};
    uint32_t call_count = 0;
    RenderGraphVK graph(&render_data);
    std::string error;
    try {
        graph.build_from_callback([&](const RendGraphContextVK& p_context) {
            received_extent = p_context.swap_chain_extent;
            ++call_count;
            RendGraphDescVK descriptor(&allocator);
            descriptor.add_resource(TEST_LOGICAL_RESOURCE, *container);
            descriptor.add_task("task", [&allocator](const RendGraphContextVK& /*p_context*/) {
                return make_unique<RTGraphEdgeProbeVK>(
                    &allocator, &allocator, QueueFamilyVK::GRAPHICS, std::vector<std::string>{}, std::vector<std::string>{std::string(TEST_LOGICAL_RESOURCE)});
            });
            return descriptor;
        });
    } catch (const std::runtime_error& p_error) {
        error = p_error.what();
    }
    // The descriptor validated; the build stops at command pool creation without a device.
    EXPECT_NE(error.find(TEST_VALIDATION_PASSED_MESSAGE), std::string::npos) << error;
    EXPECT_EQ(call_count, 1U);
    EXPECT_EQ(received_extent.width, 800U);
    EXPECT_EQ(received_extent.height, 600U);
}

TEST(WBERenderGraphVKTest, RejectsNullRebuildCallbacks) {
    RenderAllocator allocator(WBE_KI_B(16));
    RenderContextVK render_data(&allocator);
    RenderGraphVK graph(&render_data);
    EXPECT_THROW(graph.build_from_callback(nullptr), std::runtime_error);
}

TEST(WBERenderGraphVKTest, RejectsTaskFactoriesThatReturnNull) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_task("task", [](const RendGraphContextVK& /*p_context*/) {
        return Unique<IRenderTaskVK>();
    });
    RenderGraphVK graph(&render_data);

    EXPECT_NE(build_error(graph, descriptor).find("task factory returned null for: task"), std::string::npos);
}

TEST(WBERenderGraphVKTest, RejectsAliasesThatTheTaskNeverUses) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    RendTaskDescVK task = make_probe_desc(&allocator, "task", QueueFamilyVK::GRAPHICS, {"input"}, {});
    task.alias("input", TEST_LOGICAL_RESOURCE).alias("unused", TEST_LOGICAL_RESOURCE);
    descriptor.add_task(std::move(task));
    RenderGraphVK graph(&render_data);

    EXPECT_NE(build_error(graph, descriptor).find("task 'task' maps resource 'unused', which the task never uses"), std::string::npos);
}

TEST(WBERenderGraphVKTest, ResourceEdgeChainOrdersConflictingAccesses) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    // Declared out of execution order to exercise topological sorting.
    descriptor.add_task(make_probe_desc(&allocator, "reader", QueueFamilyVK::GRAPHICS, {TEST_LOGICAL_RESOURCE}, {}));
    descriptor.add_task(make_probe_desc(&allocator, "writer", QueueFamilyVK::GRAPHICS, {}, {TEST_LOGICAL_RESOURCE}));
    descriptor.add_task(make_probe_desc(&allocator, "rewriter", QueueFamilyVK::GRAPHICS, {}, {TEST_LOGICAL_RESOURCE}));
    descriptor.get_task("writer").connect("reader", TEST_LOGICAL_RESOURCE);
    descriptor.get_task("reader").connect("rewriter", TEST_LOGICAL_RESOURCE);
    RenderGraphVK graph(&render_data);

    EXPECT_NE(build_error(graph, descriptor).find(TEST_VALIDATION_PASSED_MESSAGE), std::string::npos);
}

TEST(WBERenderGraphVKTest, DumpGraphMetadataIsEmptyBeforeBuild) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RenderGraphVK graph(&render_data);

    RendGraphMetadataVK metadata = graph.dump_graph_metadata();
    EXPECT_TRUE(metadata.tasks.empty());
    EXPECT_TRUE(metadata.edges.empty());
}

TEST(WBERenderGraphVKTest, DumpGraphMetadataListsTasksInExecutionOrderAndEdgeAccesses) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    // Declared out of execution order to check that the metadata follows the topological order.
    descriptor.add_task(make_probe_desc(&allocator, "reader", QueueFamilyVK::GRAPHICS, {TEST_LOGICAL_RESOURCE}, {}));
    descriptor.add_task(make_probe_desc(&allocator, "writer", QueueFamilyVK::GRAPHICS, {}, {TEST_LOGICAL_RESOURCE}));
    descriptor.add_task(make_probe_desc(&allocator, "rewriter", QueueFamilyVK::GRAPHICS, {}, {TEST_LOGICAL_RESOURCE}));
    descriptor.get_task("writer").connect("reader", TEST_LOGICAL_RESOURCE);
    descriptor.get_task("reader").connect("rewriter", TEST_LOGICAL_RESOURCE);
    RenderGraphVK graph(&render_data);
    // The edges are resolved before command-buffer allocation, which fails without a device.
    ASSERT_NE(build_error(graph, descriptor).find(TEST_VALIDATION_PASSED_MESSAGE), std::string::npos);

    RendGraphMetadataVK metadata = graph.dump_graph_metadata();
    ASSERT_EQ(metadata.tasks.size(), 3);
    EXPECT_EQ(metadata.tasks[0].task_id, "writer");
    EXPECT_EQ(metadata.tasks[1].task_id, "reader");
    EXPECT_EQ(metadata.tasks[2].task_id, "rewriter");
    for (uint64_t i = 0; i < metadata.tasks.size(); ++i) {
        EXPECT_EQ(metadata.tasks[i].topological_order, i);
        EXPECT_EQ(metadata.tasks[i].queue_family, QueueFamilyVK::GRAPHICS);
    }

    ASSERT_EQ(metadata.edges.size(), 2);
    auto find_edge = [&metadata](std::string_view p_from, std::string_view p_to) {
        auto edge = std::ranges::find_if(metadata.edges, [p_from, p_to](const RendGraphEdgeMetadataVK& p_edge) {
            return p_edge.from_task_id == p_from && p_edge.to_task_id == p_to;
        });
        return edge == metadata.edges.end() ? nullptr : &*edge;
    };
    const RendGraphEdgeMetadataVK* write_read = find_edge("writer", "reader");
    ASSERT_NE(write_read, nullptr);
    EXPECT_EQ(write_read->resource_id, TEST_LOGICAL_RESOURCE);
    EXPECT_TRUE(write_read->from_task_writes);
    EXPECT_FALSE(write_read->to_task_writes);
    EXPECT_FALSE(write_read->cross_queue);
    const RendGraphEdgeMetadataVK* read_write = find_edge("reader", "rewriter");
    ASSERT_NE(read_write, nullptr);
    EXPECT_EQ(read_write->resource_id, TEST_LOGICAL_RESOURCE);
    EXPECT_FALSE(read_write->from_task_writes);
    EXPECT_TRUE(read_write->to_task_writes);
    EXPECT_FALSE(read_write->cross_queue);
}

TEST(WBERenderGraphVKTest, EdgesOnAnotherResourceDoNotOrderAConflict) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    descriptor.add_resource(TEST_SHARED_RESOURCE, *make_test_container(&allocator, &render_data));
    descriptor.add_task(
        make_probe_desc(&allocator, "writer", QueueFamilyVK::GRAPHICS, {}, {TEST_LOGICAL_RESOURCE, TEST_SHARED_RESOURCE}));
    descriptor.add_task(
        make_probe_desc(&allocator, "reader", QueueFamilyVK::GRAPHICS, {TEST_LOGICAL_RESOURCE, TEST_SHARED_RESOURCE}, {}));
    descriptor.get_task("writer").connect("reader", TEST_SHARED_RESOURCE);
    RenderGraphVK graph(&render_data);

    EXPECT_NE(build_error(graph, descriptor).find("resource 'logical' without a chain of edges on that resource"), std::string::npos);
}

TEST(WBERenderGraphVKTest, RejectsCyclicResourceEdges) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    descriptor.add_task(make_probe_desc(&allocator, "first", QueueFamilyVK::GRAPHICS, {}, {TEST_LOGICAL_RESOURCE}));
    descriptor.add_task(make_probe_desc(&allocator, "second", QueueFamilyVK::GRAPHICS, {}, {TEST_LOGICAL_RESOURCE}));
    descriptor.get_task("first").connect("second", TEST_LOGICAL_RESOURCE);
    descriptor.get_task("second").connect("first", TEST_LOGICAL_RESOURCE);
    RenderGraphVK graph(&render_data);

    const std::string message = build_error(graph, descriptor);
    EXPECT_NE(message.find("contains a cycle"), std::string::npos);
    EXPECT_NE(message.find("'first'"), std::string::npos);
    EXPECT_NE(message.find("'second'"), std::string::npos);
}

TEST(WBERenderGraphVKTest, RejectsSelfEdgesUnknownTasksAndUndeclaredResources) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    auto make_descriptor = [&]() {
        RendGraphDescVK descriptor(&allocator);
        descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
        descriptor.add_task(make_probe_desc(&allocator, "task", QueueFamilyVK::GRAPHICS, {}, {TEST_LOGICAL_RESOURCE}));
        descriptor.add_task(make_probe_desc(&allocator, "other", QueueFamilyVK::GRAPHICS, {}, {}));
        return descriptor;
    };
    RenderGraphVK graph(&render_data);

    RendGraphDescVK self_edge = make_descriptor();
    self_edge.get_task("task").connect("task", TEST_LOGICAL_RESOURCE);
    EXPECT_NE(build_error(graph, self_edge).find("connects to itself"), std::string::npos);

    RendGraphDescVK unknown_task = make_descriptor();
    unknown_task.get_task("task").connect("missing", TEST_LOGICAL_RESOURCE);
    EXPECT_NE(build_error(graph, unknown_task).find("connects to unknown task 'missing'"), std::string::npos);

    RendGraphDescVK undeclared_resource = make_descriptor();
    undeclared_resource.get_task("task").connect("other", TEST_MISSING_RESOURCE);
    EXPECT_NE(build_error(graph, undeclared_resource).find("uses undeclared resource 'missing'"), std::string::npos);
}

TEST(WBERenderGraphVKTest, RejectsEdgesWhoseTasksDoNotAccessTheResource) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    auto make_descriptor = [&]() {
        RendGraphDescVK descriptor(&allocator);
        descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
        descriptor.add_task(make_probe_desc(&allocator, "accessor", QueueFamilyVK::GRAPHICS, {}, {TEST_LOGICAL_RESOURCE}));
        descriptor.add_task(make_probe_desc(&allocator, "bystander", QueueFamilyVK::GRAPHICS, {}, {}));
        return descriptor;
    };
    RenderGraphVK graph(&render_data);

    RendGraphDescVK unused_by_source = make_descriptor();
    unused_by_source.get_task("bystander").connect("accessor", TEST_LOGICAL_RESOURCE);
    EXPECT_NE(build_error(graph, unused_by_source).find("but task 'bystander' does not access it"), std::string::npos);

    RendGraphDescVK unused_by_destination = make_descriptor();
    unused_by_destination.get_task("accessor").connect("bystander", TEST_LOGICAL_RESOURCE);
    EXPECT_NE(build_error(graph, unused_by_destination).find("but task 'bystander' does not access it"), std::string::npos);
}

TEST(WBERenderGraphVKTest, GraphOwnedResourcesRejectDuplicateIdsBeforeAllocation) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RenderGraphVK graph(&render_data);

    RendGraphDescVK imported_duplicate(&allocator);
    imported_duplicate.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    imported_duplicate.add_buffer(TEST_LOGICAL_RESOURCE, [=](const RendGraphContextVK& /*p_context*/) {
        return RendBufferDescVK{.usage = RendResPrimaryUsageVK::BUFFER_STORAGE, .size = 16};
    });
    EXPECT_NE(build_error(graph, imported_duplicate).find("duplicate resource id: logical"), std::string::npos);

    RendGraphDescVK owned_duplicate(&allocator);
    owned_duplicate.add_image(TEST_SHARED_RESOURCE, [=](const RendGraphContextVK& /*p_context*/) {
        return RendImageDescVK{.format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {.width = 4, .height = 4, .depth = 1}};
    });
    owned_duplicate.add_sampler(TEST_SHARED_RESOURCE);
    EXPECT_NE(build_error(graph, owned_duplicate).find("duplicate resource id: shared"), std::string::npos);
}

TEST(WBERenderGraphVKTest, RejectsInvalidGraphOwnedResourceDeclarations) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RenderGraphVK graph(&render_data);

    RendGraphDescVK image_usage_buffer(&allocator);
    image_usage_buffer.add_buffer("buffer", [=](const RendGraphContextVK& /*p_context*/) {
        return RendBufferDescVK{.usage = RendResPrimaryUsageVK::IMAGE_READ_WRITE, .size = 16};
    });
    EXPECT_NE(build_error(graph, image_usage_buffer).find("buffer 'buffer' has a non-buffer primary usage"), std::string::npos);

    RendGraphDescVK empty_buffer(&allocator);
    empty_buffer.add_buffer("buffer", [=](const RendGraphContextVK& /*p_context*/) {
        return RendBufferDescVK{.usage = RendResPrimaryUsageVK::BUFFER_UNIFORM};
    });
    EXPECT_NE(build_error(graph, empty_buffer).find("buffer 'buffer' has no size"), std::string::npos);

    RendGraphDescVK oversized_data(&allocator);
    oversized_data.add_buffer("buffer", [=](const RendGraphContextVK& /*p_context*/) {
        return RendBufferDescVK{.usage = RendResPrimaryUsageVK::BUFFER_UNIFORM, .size = 2, .initial_data = {1, 2, 3}};
    });
    EXPECT_NE(build_error(graph, oversized_data).find("initial data of buffer 'buffer' exceeds its size"), std::string::npos);

    RendGraphDescVK buffer_usage_image(&allocator);
    buffer_usage_image.add_image("image", [=](const RendGraphContextVK& /*p_context*/) {
        return RendImageDescVK{.usage = RendResPrimaryUsageVK::BUFFER_STORAGE,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {.width = 4, .height = 4, .depth = 1}};
    });
    EXPECT_NE(build_error(graph, buffer_usage_image).find("image 'image' has a non-image primary usage"), std::string::npos);

    RendGraphDescVK undefined_image(&allocator);
    undefined_image.add_image("image", [=](const RendGraphContextVK& /*p_context*/) {
        return RendImageDescVK{.extent = {.width = 4, .height = 4, .depth = 1}};
    });
    EXPECT_NE(build_error(graph, undefined_image).find("image 'image' has an undefined format"), std::string::npos);
}

TEST(WBERenderGraphVKTest, ImageViewsMustViewDeclaredImages) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RenderGraphVK graph(&render_data);

    RendGraphDescVK missing_image(&allocator);
    missing_image.add_image_view("view", "missing_image");
    EXPECT_NE(build_error(graph, missing_image).find("image view 'view' views 'missing_image', which is not a declared image resource"),
        std::string::npos);

    RendGraphDescVK non_image(&allocator);
    non_image.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    non_image.add_image_view("view", TEST_LOGICAL_RESOURCE);
    EXPECT_NE(build_error(graph, non_image).find("which is not a declared image resource"), std::string::npos);
}

namespace {

// A subgraph whose `first` writes its `data` port and `second` then rewrites it.
RendSubgraphDescVK make_rewriting_subgraph(RenderAllocator* p_allocator) {
    RendSubgraphDescVK subgraph(p_allocator);
    subgraph.add_port("data");
    subgraph.graph.add_task(make_probe_desc(p_allocator, "first", QueueFamilyVK::GRAPHICS, {}, {"data"}));
    subgraph.graph.add_task(make_probe_desc(p_allocator, "second", QueueFamilyVK::GRAPHICS, {"data"}, {"data"}));
    subgraph.graph.get_task("first").connect("second", "data");
    return subgraph;
}

const std::array<RendSubgraphPortBindingVK, 1> LOGICAL_DATA_PORT{{{.port = "data", .resource = TEST_LOGICAL_RESOURCE}}};

bool has_edge(const RendGraphMetadataVK& p_metadata, std::string_view p_from, std::string_view p_to, std::string_view p_resource_id) {
    return std::ranges::any_of(p_metadata.edges, [&](const RendGraphEdgeMetadataVK& p_edge) {
        return p_edge.from_task_id == p_from && p_edge.to_task_id == p_to && p_edge.resource_id == p_resource_id;
    });
}

} // namespace

TEST(WBERenderGraphVKTest, SubgraphEdgesConnectToTheEntriesAndLeaveFromTheExits) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    descriptor.add_task(make_probe_desc(&allocator, "producer", QueueFamilyVK::GRAPHICS, {}, {TEST_LOGICAL_RESOURCE}));
    descriptor.add_task(make_probe_desc(&allocator, "consumer", QueueFamilyVK::GRAPHICS, {TEST_LOGICAL_RESOURCE}, {}));
    descriptor.add_subgraph("sort", make_rewriting_subgraph(&allocator), LOGICAL_DATA_PORT).connect("consumer", TEST_LOGICAL_RESOURCE);
    descriptor.get_task("producer").connect("sort", TEST_LOGICAL_RESOURCE);
    RenderGraphVK graph(&render_data);
    // The chain through the subgraph orders every conflicting access.
    ASSERT_NE(build_error(graph, descriptor).find(TEST_VALIDATION_PASSED_MESSAGE), std::string::npos);

    const RendGraphMetadataVK metadata = graph.dump_graph_metadata();
    ASSERT_EQ(metadata.edges.size(), 3U);
    EXPECT_TRUE(has_edge(metadata, "producer", "sort.first", TEST_LOGICAL_RESOURCE));
    EXPECT_TRUE(has_edge(metadata, "sort.first", "sort.second", TEST_LOGICAL_RESOURCE));
    EXPECT_TRUE(has_edge(metadata, "sort.second", "consumer", TEST_LOGICAL_RESOURCE));
}

TEST(WBERenderGraphVKTest, UnconnectedSubgraphAccessesStillConflict) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    descriptor.add_task(make_probe_desc(&allocator, "producer", QueueFamilyVK::GRAPHICS, {}, {TEST_LOGICAL_RESOURCE}));
    descriptor.add_subgraph("sort", make_rewriting_subgraph(&allocator), LOGICAL_DATA_PORT);
    RenderGraphVK graph(&render_data);

    EXPECT_NE(build_error(graph, descriptor).find("without a chain of edges on that resource"), std::string::npos);
}

TEST(WBERenderGraphVKTest, SubgraphsConnectToSubgraphsAndExposeTheirMembers) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    descriptor.add_task(make_probe_desc(&allocator, "reader", QueueFamilyVK::GRAPHICS, {TEST_LOGICAL_RESOURCE}, {}));
    descriptor.add_subgraph("upstream", make_rewriting_subgraph(&allocator), LOGICAL_DATA_PORT).connect("downstream", TEST_LOGICAL_RESOURCE);
    descriptor.add_subgraph("downstream", make_rewriting_subgraph(&allocator), LOGICAL_DATA_PORT);
    // A member is an ordinary task, which other tasks may connect to directly.
    descriptor.get_task("downstream.second").connect("reader", TEST_LOGICAL_RESOURCE);
    RenderGraphVK graph(&render_data);
    ASSERT_NE(build_error(graph, descriptor).find(TEST_VALIDATION_PASSED_MESSAGE), std::string::npos);

    const RendGraphMetadataVK metadata = graph.dump_graph_metadata();
    EXPECT_TRUE(has_edge(metadata, "upstream.second", "downstream.first", TEST_LOGICAL_RESOURCE));
    EXPECT_TRUE(has_edge(metadata, "downstream.second", "reader", TEST_LOGICAL_RESOURCE));
}

TEST(WBERenderGraphVKTest, NestedSubgraphsResolveToTheirInnermostTasks) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    // `post` runs the nested `sort` on its port, then `finish` reads the result.
    RendSubgraphDescVK post(&allocator);
    post.add_port("target");
    post.graph.add_task(make_probe_desc(&allocator, "finish", QueueFamilyVK::GRAPHICS, {"target"}, {}));
    const std::array<RendSubgraphPortBindingVK, 1> sort_ports{{{.port = "data", .resource = "target"}}};
    post.graph.add_subgraph("sort", make_rewriting_subgraph(&allocator), sort_ports).connect("finish", "target");
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    descriptor.add_task(make_probe_desc(&allocator, "producer", QueueFamilyVK::GRAPHICS, {}, {TEST_LOGICAL_RESOURCE}));
    const std::array<RendSubgraphPortBindingVK, 1> post_ports{{{.port = "target", .resource = TEST_LOGICAL_RESOURCE}}};
    descriptor.add_subgraph("post", post, post_ports);
    descriptor.get_task("producer").connect("post", TEST_LOGICAL_RESOURCE);
    RenderGraphVK graph(&render_data);
    ASSERT_NE(build_error(graph, descriptor).find(TEST_VALIDATION_PASSED_MESSAGE), std::string::npos);

    const RendGraphMetadataVK metadata = graph.dump_graph_metadata();
    ASSERT_EQ(metadata.edges.size(), 3U);
    EXPECT_TRUE(has_edge(metadata, "producer", "post.sort.first", TEST_LOGICAL_RESOURCE));
    EXPECT_TRUE(has_edge(metadata, "post.sort.first", "post.sort.second", TEST_LOGICAL_RESOURCE));
    EXPECT_TRUE(has_edge(metadata, "post.sort.second", "post.finish", TEST_LOGICAL_RESOURCE));
    // Each task names its innermost subgraph, and each subgraph its parent, parents first.
    const auto subgraph_of = [&metadata](std::string_view p_task_id) {
        return std::ranges::find_if(metadata.tasks, [p_task_id](const RendGraphTaskMetadataVK& p_task) {
            return p_task.task_id == p_task_id;
        })->subgraph_id;
    };
    EXPECT_EQ(subgraph_of("producer"), "");
    EXPECT_EQ(subgraph_of("post.finish"), "post");
    EXPECT_EQ(subgraph_of("post.sort.first"), "post.sort");
    ASSERT_EQ(metadata.subgraphs.size(), 2U);
    EXPECT_EQ(metadata.subgraphs[0].subgraph_id, "post");
    EXPECT_EQ(metadata.subgraphs[0].parent_subgraph_id, "");
    EXPECT_EQ(metadata.subgraphs[1].subgraph_id, "post.sort");
    EXPECT_EQ(metadata.subgraphs[1].parent_subgraph_id, "post");
}

TEST(WBERenderGraphVKTest, RejectsSubgraphEdgesOnResourcesNoMemberAccesses) {
    RenderAllocator allocator(WBE_KI_B(128));
    RenderContextVK render_data(&allocator);
    RendGraphDescVK descriptor(&allocator);
    descriptor.add_resource(TEST_LOGICAL_RESOURCE, *make_test_container(&allocator, &render_data));
    descriptor.add_resource("other", *make_test_container(&allocator, &render_data));
    descriptor.add_task(make_probe_desc(&allocator, "consumer", QueueFamilyVK::GRAPHICS, {"other"}, {}));
    descriptor.add_subgraph("sort", make_rewriting_subgraph(&allocator), LOGICAL_DATA_PORT).connect("consumer", "other");
    RenderGraphVK graph(&render_data);

    EXPECT_NE(build_error(graph, descriptor).find("subgraph 'sort' is connected on resource 'other', but none of its tasks accesses it"),
        std::string::npos);
}

} // namespace WhiteBirdEngine
