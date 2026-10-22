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
#include "core/data/data_value.hh"
#include "core/memory/unique.hh"
#include "function/event/event_manager.hh"
#include "function/event/event_registration.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_task_registry_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/rt_cpu_in_flight_buffer_acquire_vk.hh"
#include "function/render_vk/rt_triggered_vk.hh"
#include "global/global.hh"
#include "test_global.hh"
#include "utils/defs.hh"
#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

// The task emits through the EventManager singleton, which the test global owns.
class WBERTCPUInFlightBufferAcquireVKTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    EventRegistration count_acquisitions(std::atomic<int>& p_count) {
        return EventManager::get_singleton()->subscribe<RTCPUInFlightBufferAcquireVK::AcquiredBuffer>(
            "source_buffer", [&p_count](const RTCPUInFlightBufferAcquireVK::AcquiredBuffer& /*p_buffer*/) {
                p_count.fetch_add(1);
            });
    }

    std::unique_ptr<Global> global;
};

} // namespace

TEST_F(WBERTCPUInFlightBufferAcquireVKTest, DirectRecordRequiresBoundSource) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    std::atomic<int> acquisitions = 0;
    const EventRegistration registration = count_acquisitions(acquisitions);
    RTCPUInFlightBufferAcquireVK task(&render_data, "source_buffer");

    ASSERT_EQ(task.queue_family(), QueueFamilyVK::GRAPHICS);
    Vector<ResAccessVK, RenderAllocator> reads(&allocator);
    Vector<ResAccessVK, RenderAllocator> writes(&allocator);
    task.append_accesses(reads, writes);
    ASSERT_EQ(reads.size(), 1U);
    EXPECT_EQ(reads[0].res_id, "source_buffer");
    EXPECT_EQ(reads[0].usage, RendResUsageVK::TRANSFER_SRC);
    EXPECT_TRUE(writes.empty());

    EXPECT_THROW(task.record(VK_NULL_HANDLE, 0.0), std::runtime_error);
    EXPECT_EQ(acquisitions.load(), 0);
}

TEST_F(WBERTCPUInFlightBufferAcquireVKTest, RejectsInvalidInputs) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);

    EXPECT_THROW(RTCPUInFlightBufferAcquireVK(nullptr, "source_buffer"), std::runtime_error);
    EXPECT_THROW(RTCPUInFlightBufferAcquireVK(&render_data, ""), std::runtime_error);
}

TEST_F(WBERTCPUInFlightBufferAcquireVKTest, TriggeredAcquireSkipsUnboundSourceUntilActivated) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    std::atomic<int> acquisitions = 0;
    const EventRegistration registration = count_acquisitions(acquisitions);
    RTTriggeredVK task(make_unique<RTCPUInFlightBufferAcquireVK>(&allocator, &render_data, "source_buffer"));
    EXPECT_NO_THROW(task.record(VK_NULL_HANDLE, 0.0));
    task.trigger();
    EXPECT_THROW(task.record(VK_NULL_HANDLE, 0.0), std::runtime_error);
    EXPECT_EQ(task.get_remaining_frames(), 0U);
    EXPECT_EQ(acquisitions.load(), 0);
}

TEST_F(WBERTCPUInFlightBufferAcquireVKTest, RegistryCreatesTaskFromParameters) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    DataValue params;
    params.set_entry("buffer", DataValue::make_string("source_buffer"));
    for (std::string_view task_type : {"RTCPUInFlightBufferAcquireVK", "cpu_acquire_buffer"}) {
        const Unique<IRenderTaskVK> task =
            RenderTaskRegistryVK::create_factory(task_type, params, &render_data)(RendGraphContextVK{});
        ASSERT_NE(dynamic_cast<const RTCPUInFlightBufferAcquireVK*>(task.get()), nullptr) << task_type;
        Vector<ResAccessVK, RenderAllocator> reads(&allocator);
        Vector<ResAccessVK, RenderAllocator> writes(&allocator);
        task->append_accesses(reads, writes);
        ASSERT_EQ(reads.size(), 1U);
        EXPECT_EQ(reads[0].res_id, "source_buffer");
        EXPECT_EQ(reads[0].usage, RendResUsageVK::TRANSFER_SRC);
    }
    // The resource id is required.
    EXPECT_THROW(RenderTaskRegistryVK::create_factory("cpu_acquire_buffer", DataValue::make_map(), &render_data), std::runtime_error);
}

// Runs without a test global, so no EventManager exists.
TEST(WBERTCPUInFlightBufferAcquireVKStandaloneTest, RequiresEventManager) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    ASSERT_EQ(EventManager::get_singleton(), nullptr);
    EXPECT_THROW(RTCPUInFlightBufferAcquireVK(&render_data, "source_buffer"), std::runtime_error);
}

} // namespace WhiteBirdEngine
