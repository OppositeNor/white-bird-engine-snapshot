/* Copyright 2026 OppositeNor

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
#include "core/data/data_value.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/event/event_manager.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_task_registry_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/rt_triggered_vk.hh"
#include "global/global.hh"
#include "render_task_registry_vk_test_tasks.hh"
#include "test_global.hh"
#include "utils/defs.hh"
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {
namespace {

struct TriggerProbeState {
    int recordings = 0;
    int bindings = 0;
    int destructions = 0;
    double delta = 0.0;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    std::function<void()> on_record;
};

class RTTriggerProbeVK final : public IRenderTaskVK {
public:
    RTTriggerProbeVK() = delete;
    explicit RTTriggerProbeVK(TriggerProbeState& p_state) : state(p_state) {
    }
    virtual ~RTTriggerProbeVK() override {
        ++state.destructions;
    }
    RTTriggerProbeVK(const RTTriggerProbeVK&) = delete;
    RTTriggerProbeVK(RTTriggerProbeVK&&) = delete;
    RTTriggerProbeVK& operator=(const RTTriggerProbeVK&) = delete;
    RTTriggerProbeVK& operator=(RTTriggerProbeVK&&) = delete;
    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::COMPUTE_1;
    }
    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override {
        p_reads.emplace_back("input", RendResUsageVK::STORAGE_BUFFER);
        p_writes.emplace_back("output", RendResUsageVK::STORAGE_IMAGE);
    }
    virtual void bind_resources(const RendResViewVK&) override {
        ++state.bindings;
    }
    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override {
        ++state.recordings;
        state.command_buffer = p_command_buffer;
        state.delta = p_delta;
        if (state.on_record) {
            state.on_record();
        }
    }

private:
    TriggerProbeState& state;
};

// Event-driven tasks subscribe through the EventManager singleton, which the test global owns.
class WBERTTriggeredVKEventTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<Global> global;
};

} // namespace

TEST(WBERTTriggeredVKTest, ConsumesOneActivationAndCanBeRearmed) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState state;
    RTTriggeredVK task(make_unique<RTTriggerProbeVK>(&allocator, state));
    task.record(VK_NULL_HANDLE, 1.0);
    EXPECT_EQ(state.recordings, 0);
    task.trigger();
    task.trigger(); // Requests coalesce.
    task.record(VK_NULL_HANDLE, 2.0);
    EXPECT_EQ(state.recordings, 1);
    EXPECT_DOUBLE_EQ(state.delta, 2.0);
    EXPECT_EQ(state.command_buffer, VK_NULL_HANDLE);
    EXPECT_EQ(task.get_remaining_frames(), 0U);
    task.record(VK_NULL_HANDLE, 3.0);
    EXPECT_EQ(state.recordings, 1);
    task.trigger();
    task.record(VK_NULL_HANDLE, 4.0);
    EXPECT_EQ(state.recordings, 2);
}

TEST(WBERTTriggeredVKTest, ActivationDuringRecordingRemainsPending) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState state;
    RTTriggeredVK task(make_unique<RTTriggerProbeVK>(&allocator, state), true);
    state.on_record = [&task] {
        task.trigger();
    };
    task.record(VK_NULL_HANDLE, 0.0);
    EXPECT_EQ(task.get_remaining_frames(), 1U);
    state.on_record = {};
    task.record(VK_NULL_HANDLE, 0.0);
    EXPECT_EQ(state.recordings, 2);
    EXPECT_EQ(task.get_remaining_frames(), 0U);
}

TEST(WBERTTriggeredVKTest, EnabledForRecordsRequestedFrames) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState state;
    RTTriggeredVK task(make_unique<RTTriggerProbeVK>(&allocator, state));
    task.enabled_for(3);
    EXPECT_EQ(task.get_remaining_frames(), 3U);
    for (int i = 0; i < 5; ++i) {
        task.record(VK_NULL_HANDLE, 0.0);
    }
    EXPECT_EQ(state.recordings, 3);
    EXPECT_EQ(task.get_remaining_frames(), 0U);
}

TEST(WBERTTriggeredVKTest, TriggerDoesNotShortenEnabledPeriod) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState state;
    RTTriggeredVK task(make_unique<RTTriggerProbeVK>(&allocator, state));
    task.enabled_for(3);
    task.trigger();
    EXPECT_EQ(task.get_remaining_frames(), 3U);
    task.record(VK_NULL_HANDLE, 0.0);
    task.trigger();
    EXPECT_EQ(task.get_remaining_frames(), 2U);
}

TEST(WBERTTriggeredVKTest, EnabledForZeroCancelsPendingFrames) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState state;
    RTTriggeredVK task(make_unique<RTTriggerProbeVK>(&allocator, state), true);
    task.enabled_for(0);
    task.record(VK_NULL_HANDLE, 0.0);
    EXPECT_EQ(state.recordings, 0);
}

TEST(WBERTTriggeredVKTest, InactiveWrapperForwardsSetupAndOwnsChild) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState state;
    {
        RTTriggeredVK task(make_unique<RTTriggerProbeVK>(&allocator, state));
        Map<std::string, Ref<RendResVK>, RenderAllocator> resources(&allocator);
        task.bind_resources(RendResViewVK(&resources));
        EXPECT_EQ(state.bindings, 1);
        EXPECT_EQ(task.queue_family(), QueueFamilyVK::COMPUTE_1);
        Vector<ResAccessVK, RenderAllocator> reads(&allocator);
        Vector<ResAccessVK, RenderAllocator> writes(&allocator);
        task.append_accesses(reads, writes);
        ASSERT_EQ(reads.size(), 1U);
        ASSERT_EQ(writes.size(), 1U);
        EXPECT_EQ(reads[0].res_id, "input");
        EXPECT_EQ(reads[0].usage, RendResUsageVK::STORAGE_BUFFER);
        EXPECT_EQ(writes[0].res_id, "output");
        EXPECT_EQ(writes[0].usage, RendResUsageVK::STORAGE_IMAGE);
        EXPECT_EQ(state.recordings, 0);
        EXPECT_EQ(state.destructions, 0);
    }
    EXPECT_EQ(state.destructions, 1);
}

TEST(WBERTTriggeredVKTest, RejectsNullChild) {
    EXPECT_THROW((RTTriggeredVK{Unique<IRenderTaskVK>{}}), std::runtime_error);
}

TEST_F(WBERTTriggeredVKEventTest, TriggerCommandRecordsOnce) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState state;
    RTTriggeredVK task(make_unique<RTTriggerProbeVK>(&allocator, state), "probe.trigger");
    EventManager* events = EventManager::get_singleton();

    task.record(VK_NULL_HANDLE, 0.0);
    EXPECT_EQ(state.recordings, 0);
    events->emit("probe.trigger", RTTriggeredVK::Command::trigger());
    events->emit("probe.trigger", RTTriggeredVK::Command::trigger()); // Requests coalesce.
    task.record(VK_NULL_HANDLE, 0.0);
    task.record(VK_NULL_HANDLE, 0.0);
    EXPECT_EQ(state.recordings, 1);
}

TEST_F(WBERTTriggeredVKEventTest, EnableForCommandRecordsTheRequestedFrames) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState state;
    RTTriggeredVK task(make_unique<RTTriggerProbeVK>(&allocator, state), "probe.sequence");
    EventManager::get_singleton()->emit("probe.sequence", RTTriggeredVK::Command::enable_for(3));
    EXPECT_EQ(task.get_remaining_frames(), 3U);
    for (int i = 0; i < 5; ++i) {
        task.record(VK_NULL_HANDLE, 0.0);
    }
    EXPECT_EQ(state.recordings, 3);

    // Zero disables a pending period.
    EventManager::get_singleton()->emit("probe.sequence", RTTriggeredVK::Command::enable_for(4));
    EventManager::get_singleton()->emit("probe.sequence", RTTriggeredVK::Command::enable_for(0));
    task.record(VK_NULL_HANDLE, 0.0);
    EXPECT_EQ(state.recordings, 3);
}

TEST_F(WBERTTriggeredVKEventTest, TasksSharingAnIdRunOnTheSameFrames) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState present_state;
    TriggerProbeState acquire_state;
    TriggerProbeState other_state;
    RTTriggeredVK present(make_unique<RTTriggerProbeVK>(&allocator, present_state), "probe.shared");
    RTTriggeredVK acquire(make_unique<RTTriggerProbeVK>(&allocator, acquire_state), "probe.shared");
    RTTriggeredVK other(make_unique<RTTriggerProbeVK>(&allocator, other_state), "probe.other");
    EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTTriggeredVK::Command>("probe.shared"), 2U);

    EventManager::get_singleton()->emit("probe.shared", RTTriggeredVK::Command::enable_for(2));
    for (int i = 0; i < 3; ++i) {
        present.record(VK_NULL_HANDLE, 0.0);
        acquire.record(VK_NULL_HANDLE, 0.0);
        other.record(VK_NULL_HANDLE, 0.0);
        EXPECT_EQ(present_state.recordings, acquire_state.recordings);
    }
    EXPECT_EQ(present_state.recordings, 2);
    EXPECT_EQ(other_state.recordings, 0);
}

TEST_F(WBERTTriggeredVKEventTest, DestroyingTheTaskEndsTheSubscription) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState state;
    {
        RTTriggeredVK task(make_unique<RTTriggerProbeVK>(&allocator, state), "probe.lifetime");
        EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTTriggeredVK::Command>("probe.lifetime"), 1U);
    }
    EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTTriggeredVK::Command>("probe.lifetime"), 0U);
    EXPECT_NO_THROW(EventManager::get_singleton()->emit("probe.lifetime", RTTriggeredVK::Command::trigger()));
    EXPECT_EQ(state.destructions, 1);
}

TEST_F(WBERTTriggeredVKEventTest, RejectsAnEmptyOrNullEventId) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState state;
    EXPECT_THROW(RTTriggeredVK(make_unique<RTTriggerProbeVK>(&allocator, state), ""), std::runtime_error);
    EXPECT_THROW(RTTriggeredVK(make_unique<RTTriggerProbeVK>(&allocator, state), std::string_view()), std::runtime_error);
    EXPECT_THROW(RTTriggeredVK(make_unique<RTTriggerProbeVK>(&allocator, state), static_cast<const char*>(nullptr)), std::runtime_error);
}

TEST_F(WBERTTriggeredVKEventTest, StringLiteralSelectsTheEventConstructor) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState state;
    RTTriggeredVK task(make_unique<RTTriggerProbeVK>(&allocator, state), "probe.literal");
    // A literal must subscribe, not convert to the bool that would arm the task.
    EXPECT_EQ(task.get_remaining_frames(), 0U);
    EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTTriggeredVK::Command>("probe.literal"), 1U);
}

TEST_F(WBERTTriggeredVKEventTest, WrapFactoryWrapsEveryCreatedTask) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    TriggerProbeState state;
    int factory_calls = 0;
    VkExtent2D seen_extent{};
    const RendTaskFactoryVK factory = RTTriggeredVK::wrap_factory(
        [&](const RendGraphContextVK& p_context) -> Unique<IRenderTaskVK> {
            ++factory_calls;
            seen_extent = p_context.swap_chain_extent;
            return make_unique<RTTriggerProbeVK>(&allocator, state);
        },
        "probe.wrapped",
        &render_data);

    Unique<IRenderTaskVK> task = factory(RendGraphContextVK{.swap_chain_extent = {.width = 800, .height = 600}});
    EXPECT_EQ(factory_calls, 1);
    EXPECT_EQ(seen_extent.width, 800U);
    auto* triggered = dynamic_cast<RTTriggeredVK*>(task.get());
    ASSERT_NE(triggered, nullptr);
    // The wrapper forwards everything but recording to its child.
    EXPECT_EQ(task->queue_family(), QueueFamilyVK::COMPUTE_1);

    task->record(VK_NULL_HANDLE, 0.0);
    EXPECT_EQ(state.recordings, 0);
    EventManager::get_singleton()->emit("probe.wrapped", RTTriggeredVK::Command::trigger());
    task->record(VK_NULL_HANDLE, 0.0);
    EXPECT_EQ(state.recordings, 1);
}

TEST_F(WBERTTriggeredVKEventTest, WrapFactoryCreatesAFreshWrapperOnEveryEvaluation) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    TriggerProbeState state;
    const RendTaskFactoryVK factory = RTTriggeredVK::wrap_factory(
        [&](const RendGraphContextVK&) -> Unique<IRenderTaskVK> {
            return make_unique<RTTriggerProbeVK>(&allocator, state);
        },
        "probe.recreated",
        &render_data);

    Unique<IRenderTaskVK> first = factory(RendGraphContextVK{});
    EventManager::get_singleton()->emit("probe.recreated", RTTriggeredVK::Command::enable_for(5));
    EXPECT_EQ(dynamic_cast<RTTriggeredVK*>(first.get())->get_remaining_frames(), 5U);
    // A recreated task starts in its initial state, as the graph recreates tasks after a resize.
    first.reset();
    Unique<IRenderTaskVK> second = factory(RendGraphContextVK{});
    EXPECT_EQ(dynamic_cast<RTTriggeredVK*>(second.get())->get_remaining_frames(), 0U);
    EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTTriggeredVK::Command>("probe.recreated"), 1U);
}

TEST_F(WBERTTriggeredVKEventTest, WrapFactoryHonoursActivation) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    TriggerProbeState state;
    const RendTaskFactoryVK factory = RTTriggeredVK::wrap_factory(
        [&](const RendGraphContextVK&) -> Unique<IRenderTaskVK> {
            return make_unique<RTTriggerProbeVK>(&allocator, state);
        },
        "probe.active",
        &render_data,
        true);
    Unique<IRenderTaskVK> task = factory(RendGraphContextVK{});
    task->record(VK_NULL_HANDLE, 0.0);
    task->record(VK_NULL_HANDLE, 0.0);
    EXPECT_EQ(state.recordings, 1);
}

TEST_F(WBERTTriggeredVKEventTest, WrapFactoryRejectsInvalidInputs) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    TriggerProbeState state;
    const RendTaskFactoryVK inner = [&](const RendGraphContextVK&) -> Unique<IRenderTaskVK> {
        return make_unique<RTTriggerProbeVK>(&allocator, state);
    };
    EXPECT_THROW(RTTriggeredVK::wrap_factory(RendTaskFactoryVK(), "probe.invalid", &render_data), std::runtime_error);
    EXPECT_THROW(RTTriggeredVK::wrap_factory(inner, "", &render_data), std::runtime_error);
    EXPECT_THROW(RTTriggeredVK::wrap_factory(inner, "probe.invalid", nullptr), std::runtime_error);
}

TEST_F(WBERTTriggeredVKEventTest, WrapFactoryComposesWithTheTaskRegistry) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    DataValue params;
    params.set_entry("label", DataValue::make_string("triggered"));
    const RendTaskFactoryVK factory = RTTriggeredVK::wrap_factory(
        RenderTaskRegistryVK::create_factory("registry_probe", params, &render_data), "probe.registry", &render_data);
    Unique<IRenderTaskVK> task = factory(RendGraphContextVK{});
    ASSERT_NE(dynamic_cast<RTTriggeredVK*>(task.get()), nullptr);
    EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTTriggeredVK::Command>("probe.registry"), 1U);
}

// Runs without a test global, so no EventManager exists.
TEST(WBERTTriggeredVKStandaloneTest, EventModeRequiresEventManager) {
    RenderAllocator allocator(WBE_KI_B(64));
    TriggerProbeState state;
    ASSERT_EQ(EventManager::get_singleton(), nullptr);
    EXPECT_THROW(RTTriggeredVK(make_unique<RTTriggerProbeVK>(&allocator, state), "probe.no_manager"), std::runtime_error);
}

// Runs without a test global, so no EventManager exists.
TEST(WBERTTriggeredVKStandaloneTest, WrapFactoryRequiresEventManager) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    TriggerProbeState state;
    ASSERT_EQ(EventManager::get_singleton(), nullptr);
    EXPECT_THROW(RTTriggeredVK::wrap_factory(
                     [&](const RendGraphContextVK&) -> Unique<IRenderTaskVK> {
                         return make_unique<RTTriggerProbeVK>(&allocator, state);
                     },
                     "probe.no_manager",
                     &render_data),
        std::runtime_error);
}

} // namespace WhiteBirdEngine
