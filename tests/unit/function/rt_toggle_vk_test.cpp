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
#include "core/memory/unique.hh"
#include "function/event/event_manager.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/rt_toggle_vk.hh"
#include "global/global.hh"
#include "test_global.hh"
#include "utils/defs.hh"
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {
namespace {

struct ToggleProbeState {
    int recordings = 0;
    int bindings = 0;
    int destructions = 0;
};

class RTToggleProbeVK final : public IRenderTaskVK {
public:
    RTToggleProbeVK() = delete;
    explicit RTToggleProbeVK(ToggleProbeState& p_state) : state(p_state) {
    }
    virtual ~RTToggleProbeVK() override {
        ++state.destructions;
    }
    RTToggleProbeVK(const RTToggleProbeVK&) = delete;
    RTToggleProbeVK(RTToggleProbeVK&&) = delete;
    RTToggleProbeVK& operator=(const RTToggleProbeVK&) = delete;
    RTToggleProbeVK& operator=(RTToggleProbeVK&&) = delete;
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
    virtual void record(VkCommandBuffer, double) override {
        ++state.recordings;
    }

private:
    ToggleProbeState& state;
};

void record_frames(IRenderTaskVK& p_task, int p_frames) {
    for (int frame = 0; frame < p_frames; ++frame) {
        p_task.record(VK_NULL_HANDLE, 0.0);
    }
}

// Event-driven tasks subscribe through the EventManager singleton, which the test global owns.
class WBERTToggleVKEventTest : public ::testing::Test {
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

TEST(WBERTToggleVKTest, StatePersistsUntilChanged) {
    RenderAllocator allocator(WBE_KI_B(64));
    ToggleProbeState state;
    RTToggleVK toggle(make_unique<RTToggleProbeVK>(&allocator, state));
    EXPECT_FALSE(toggle.is_enabled());
    record_frames(toggle, 2);
    EXPECT_EQ(state.recordings, 0);
    // Unlike a trigger, an enabled toggle records on every frame until it is disabled.
    toggle.set_enabled(true);
    record_frames(toggle, 3);
    EXPECT_EQ(state.recordings, 3);
    toggle.set_enabled(false);
    record_frames(toggle, 2);
    EXPECT_EQ(state.recordings, 3);
}

TEST(WBERTToggleVKTest, StartsInTheConstructedState) {
    RenderAllocator allocator(WBE_KI_B(64));
    ToggleProbeState state;
    RTToggleVK toggle(make_unique<RTToggleProbeVK>(&allocator, state), true);
    EXPECT_TRUE(toggle.is_enabled());
    record_frames(toggle, 2);
    EXPECT_EQ(state.recordings, 2);
}

TEST(WBERTToggleVKTest, DisabledWrapperForwardsSetupAndOwnsChild) {
    RenderAllocator allocator(WBE_KI_B(64));
    ToggleProbeState state;
    {
        RTToggleVK toggle(make_unique<RTToggleProbeVK>(&allocator, state));
        EXPECT_EQ(toggle.queue_family(), QueueFamilyVK::COMPUTE_1);
        Vector<ResAccessVK, RenderAllocator> reads(&allocator);
        Vector<ResAccessVK, RenderAllocator> writes(&allocator);
        toggle.append_accesses(reads, writes);
        // Accesses are reported while disabled, so the graph keeps ordering the task.
        EXPECT_EQ(reads.size(), 1U);
        EXPECT_EQ(writes.size(), 1U);
        Map<std::string, Ref<RendResVK>, RenderAllocator> resources(&allocator);
        toggle.bind_resources(RendResViewVK(&resources));
        EXPECT_EQ(state.bindings, 1);
    }
    EXPECT_EQ(state.destructions, 1);
}

TEST(WBERTToggleVKTest, RejectsNullChild) {
    EXPECT_THROW(RTToggleVK(Unique<IRenderTaskVK>()), std::runtime_error);
}

TEST_F(WBERTToggleVKEventTest, CommandsSwitchTheState) {
    RenderAllocator allocator(WBE_KI_B(64));
    ToggleProbeState state;
    RTToggleVK toggle(make_unique<RTToggleProbeVK>(&allocator, state), "probe.toggle");
    record_frames(toggle, 1);
    EXPECT_EQ(state.recordings, 0);
    EventManager::get_singleton()->emit("probe.toggle", RTToggleVK::Command::enable());
    record_frames(toggle, 2);
    EXPECT_EQ(state.recordings, 2);
    EventManager::get_singleton()->emit("probe.toggle", RTToggleVK::Command::disable());
    record_frames(toggle, 2);
    EXPECT_EQ(state.recordings, 2);
}

TEST_F(WBERTToggleVKEventTest, TasksSharingAnIdSwitchTogether) {
    RenderAllocator allocator(WBE_KI_B(64));
    ToggleProbeState first_state;
    ToggleProbeState second_state;
    RTToggleVK first(make_unique<RTToggleProbeVK>(&allocator, first_state), "probe.shared");
    RTToggleVK second(make_unique<RTToggleProbeVK>(&allocator, second_state), "probe.shared", true);
    EventManager::get_singleton()->emit("probe.shared", RTToggleVK::Command::enable());
    EXPECT_TRUE(first.is_enabled());
    EXPECT_TRUE(second.is_enabled());
    EventManager::get_singleton()->emit("probe.shared", RTToggleVK::Command::disable());
    EXPECT_FALSE(first.is_enabled());
    EXPECT_FALSE(second.is_enabled());
}

TEST_F(WBERTToggleVKEventTest, DestroyingTheTaskEndsTheSubscription) {
    RenderAllocator allocator(WBE_KI_B(64));
    ToggleProbeState state;
    {
        RTToggleVK toggle(make_unique<RTToggleProbeVK>(&allocator, state), "probe.destroyed");
        EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTToggleVK::Command>("probe.destroyed"), 1U);
    }
    EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTToggleVK::Command>("probe.destroyed"), 0U);
    EXPECT_NO_THROW(EventManager::get_singleton()->emit("probe.destroyed", RTToggleVK::Command::enable()));
}

TEST_F(WBERTToggleVKEventTest, RejectsAnEmptyOrNullEventId) {
    RenderAllocator allocator(WBE_KI_B(64));
    ToggleProbeState state;
    EXPECT_THROW(RTToggleVK(make_unique<RTToggleProbeVK>(&allocator, state), std::string_view()), std::runtime_error);
    EXPECT_THROW(RTToggleVK(make_unique<RTToggleProbeVK>(&allocator, state), static_cast<const char*>(nullptr)), std::runtime_error);
}

TEST_F(WBERTToggleVKEventTest, StringLiteralSelectsTheEventConstructor) {
    RenderAllocator allocator(WBE_KI_B(64));
    ToggleProbeState state;
    // Without the const char* overload, the literal would convert to the bool p_enabled.
    RTToggleVK toggle(make_unique<RTToggleProbeVK>(&allocator, state), "probe.literal");
    EXPECT_FALSE(toggle.is_enabled());
    EXPECT_EQ(EventManager::get_singleton()->get_subscriber_count<RTToggleVK::Command>("probe.literal"), 1U);
}

TEST_F(WBERTToggleVKEventTest, WrapFactoryCreatesWrappersInTheGivenStateThatFollowEvents) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    ToggleProbeState state;
    const RendTaskFactoryVK factory = RTToggleVK::wrap_factory(
        [&](const RendGraphContextVK&) -> Unique<IRenderTaskVK> {
            return make_unique<RTToggleProbeVK>(&allocator, state);
        },
        "probe.wrapped",
        &render_data,
        true);
    Unique<IRenderTaskVK> task = factory(RendGraphContextVK{});
    auto* toggle = dynamic_cast<RTToggleVK*>(task.get());
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->is_enabled());
    EventManager::get_singleton()->emit("probe.wrapped", RTToggleVK::Command::disable());
    EXPECT_FALSE(toggle->is_enabled());
    // A rebuild evaluates the factory again; the new wrapper starts in the wrapped state, not the last commanded one.
    Unique<IRenderTaskVK> rebuilt = factory(RendGraphContextVK{});
    EXPECT_TRUE(dynamic_cast<RTToggleVK*>(rebuilt.get())->is_enabled());
}

TEST_F(WBERTToggleVKEventTest, WrapFactoryRejectsInvalidInputs) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    ToggleProbeState state;
    const RendTaskFactoryVK inner = [&](const RendGraphContextVK&) -> Unique<IRenderTaskVK> {
        return make_unique<RTToggleProbeVK>(&allocator, state);
    };
    EXPECT_THROW(RTToggleVK::wrap_factory(nullptr, "probe.invalid", &render_data), std::runtime_error);
    EXPECT_THROW(RTToggleVK::wrap_factory(inner, "", &render_data), std::runtime_error);
    EXPECT_THROW(RTToggleVK::wrap_factory(inner, "probe.invalid", nullptr), std::runtime_error);
}

// Runs without a test global, so no EventManager exists.
TEST(WBERTToggleVKStandaloneTest, EventModeRequiresEventManager) {
    RenderAllocator allocator(WBE_KI_B(64));
    ToggleProbeState state;
    ASSERT_EQ(EventManager::get_singleton(), nullptr);
    EXPECT_THROW(RTToggleVK(make_unique<RTToggleProbeVK>(&allocator, state), "probe.no_manager"), std::runtime_error);
}

} // namespace WhiteBirdEngine
