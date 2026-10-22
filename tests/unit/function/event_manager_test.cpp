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
#include "core/core_utils.hh"
#include "core/engine_core.hh"
#include "function/engine_function.hh"
#include "function/event/event_manager.hh"
#include "function/event/event_registration.hh"
#include "global/global.hh"
#include "test_global.hh"
#include <atomic>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <typeindex>
#include <vector>

namespace WhiteBirdEngine {

namespace {

constexpr std::string_view TEST_EVENT_ID = "test_event";

struct TestEventValue {
    int32_t value = 0;
    std::string label;
};

struct TestEventOther {
    int32_t value = 0;
};

struct TestEventBase {
    int32_t value = 0;
};

struct TestEventDerived : public TestEventBase {};

class WBEEventManagerTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    HeapAllocatorDefault* get_allocator() {
        return global->engine_core->pool_allocator;
    }

    // The test global owns the singleton; every test gets a fresh one with its own global.
    EventManager& get_events() {
        EventManager* events = EventManager::get_singleton();
        EXPECT_NE(events, nullptr);
        return *events;
    }

    std::unique_ptr<Global> global;
};

} // namespace

TEST_F(WBEEventManagerTest, EventKeyCombinesEventIdAndPayloadType) {
    EXPECT_EQ(EventManager::get_event_key<TestEventValue>("resize"), std::string("resize_") + typeid(TestEventValue).name());
    EXPECT_EQ(EventManager::get_event_key<const TestEventValue&>("resize"), EventManager::get_event_key<TestEventValue>("resize"));
    EXPECT_NE(EventManager::get_event_key<TestEventValue>("resize"), EventManager::get_event_key<TestEventOther>("resize"));
    EXPECT_NE(EventManager::get_event_key<TestEventValue>("resize"), EventManager::get_event_key<TestEventValue>("close"));
    EXPECT_NE(EventManager::get_event_key<TestEventBase>("resize"), EventManager::get_event_key<TestEventDerived>("resize"));
    EXPECT_THROW((void)EventManager::get_event_key<TestEventValue>(""), std::runtime_error);
}

TEST_F(WBEEventManagerTest, EventIdsSeparateEventsOfOnePayloadType) {
    EventManager& events = get_events();
    int32_t first_count = 0;
    int32_t second_count = 0;
    const EventRegistration first = events.subscribe<TestEventOther>("first", [&first_count](const TestEventOther&) {
        ++first_count;
    });
    const EventRegistration second = events.subscribe<TestEventOther>("second", [&second_count](const TestEventOther&) {
        ++second_count;
    });

    events.emit("first", TestEventOther{});
    events.emit("first", TestEventOther{});
    events.emit("second", TestEventOther{});
    events.emit("unsubscribed", TestEventOther{});

    EXPECT_EQ(first_count, 2);
    EXPECT_EQ(second_count, 1);
    EXPECT_EQ(events.get_subscriber_count<TestEventOther>("first"), 1U);
    EXPECT_EQ(events.get_subscriber_count<TestEventOther>("unsubscribed"), 0U);
}

TEST_F(WBEEventManagerTest, PayloadTypesSeparateEventsOfOneEventId) {
    EventManager& events = get_events();
    int32_t value_count = 0;
    int32_t other_count = 0;
    const EventRegistration value_registration = events.subscribe<TestEventValue>("shared", [&value_count](const TestEventValue&) {
        ++value_count;
    });
    const EventRegistration other_registration = events.subscribe<TestEventOther>("shared", [&other_count](const TestEventOther&) {
        ++other_count;
    });

    events.emit("shared", TestEventOther{});
    EXPECT_EQ(value_count, 0);
    EXPECT_EQ(other_count, 1);
}

TEST_F(WBEEventManagerTest, RejectsEmptyEventId) {
    EventManager& events = get_events();
    EXPECT_THROW((void)events.subscribe<TestEventOther>("", [](const TestEventOther&) {}), std::runtime_error);
    EXPECT_THROW(events.emit("", TestEventOther{}), std::runtime_error);
    EXPECT_THROW((void)events.get_subscriber_count<TestEventOther>(""), std::runtime_error);
}

// Runs without a test global, so no other EventManager exists.
TEST(WBEEventManagerStandaloneTest, RejectsNullAllocator) {
    EXPECT_THROW(EventManager(nullptr), std::runtime_error);
    EXPECT_EQ(EventManager::get_singleton(), nullptr);
}

TEST_F(WBEEventManagerTest, GlobalOwnsTheSingleton) {
    EventManager* events = EventManager::get_singleton();
    ASSERT_NE(events, nullptr);
    EXPECT_EQ(events, global->engine_function->event_manager.get());
    EXPECT_THROW(EventManager{get_allocator()}, std::runtime_error);
    EXPECT_EQ(EventManager::get_singleton(), events);
}

TEST_F(WBEEventManagerTest, EmitDeliversEventToSubscribersOfItsType) {
    EventManager& events = get_events();
    std::vector<std::string> received;
    int32_t other_count = 0;
    const EventRegistration value_registration = events.subscribe<TestEventValue>(TEST_EVENT_ID, [&received](const TestEventValue& p_event) {
        received.push_back(p_event.label + std::to_string(p_event.value));
    });
    const EventRegistration other_registration = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&other_count](const TestEventOther&) {
        ++other_count;
    });

    events.emit(TEST_EVENT_ID, TestEventValue{.value = 3, .label = "a"});
    const TestEventValue second{.value = 4, .label = "b"};
    events.emit(TEST_EVENT_ID, second);

    EXPECT_EQ(received, (std::vector<std::string>{"a3", "b4"}));
    EXPECT_EQ(other_count, 0);
}

TEST_F(WBEEventManagerTest, EmitWithoutSubscribersDoesNothing) {
    EventManager& events = get_events();
    EXPECT_NO_THROW(events.emit(TEST_EVENT_ID, TestEventValue{}));
    EXPECT_EQ(events.get_subscriber_count<TestEventValue>(TEST_EVENT_ID), 0U);
}

TEST_F(WBEEventManagerTest, BaseAndDerivedTypesAreDifferentEvents) {
    EventManager& events = get_events();
    int32_t base_count = 0;
    const EventRegistration registration = events.subscribe<TestEventBase>(TEST_EVENT_ID, [&base_count](const TestEventBase&) {
        ++base_count;
    });
    events.emit(TEST_EVENT_ID, TestEventDerived{});
    EXPECT_EQ(base_count, 0);
    events.emit(TEST_EVENT_ID, TestEventBase{});
    EXPECT_EQ(base_count, 1);
}

TEST_F(WBEEventManagerTest, CallbacksRunInSubscriptionOrder) {
    EventManager& events = get_events();
    std::vector<int32_t> order;
    std::vector<EventRegistration> registrations;
    for (int32_t i = 0; i < 4; ++i) {
        registrations.push_back(events.subscribe<TestEventOther>(TEST_EVENT_ID, [&order, i](const TestEventOther&) {
            order.push_back(i);
        }));
    }
    events.emit(TEST_EVENT_ID, TestEventOther{});
    EXPECT_EQ(order, (std::vector<int32_t>{0, 1, 2, 3}));
    EXPECT_EQ(events.get_subscriber_count<TestEventOther>(TEST_EVENT_ID), 4U);
}

TEST_F(WBEEventManagerTest, CallbacksKeepTheirOwnState) {
    EventManager& events = get_events();
    int32_t observed = 0;
    const EventRegistration registration =
        events.subscribe<TestEventOther>(TEST_EVENT_ID, [counter = 0, &observed](const TestEventOther& p_event) mutable {
            counter += p_event.value;
            observed = counter;
        });
    events.emit(TEST_EVENT_ID, TestEventOther{.value = 2});
    events.emit(TEST_EVENT_ID, TestEventOther{.value = 5});
    EXPECT_EQ(observed, 7);
}

TEST_F(WBEEventManagerTest, DestroyingRegistrationUnsubscribes) {
    EventManager& events = get_events();
    int32_t scoped_count = 0;
    int32_t kept_count = 0;
    const EventRegistration kept = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&kept_count](const TestEventOther&) {
        ++kept_count;
    });
    {
        const EventRegistration scoped = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&scoped_count](const TestEventOther&) {
            ++scoped_count;
        });
        events.emit(TEST_EVENT_ID, TestEventOther{});
        EXPECT_EQ(events.get_subscriber_count<TestEventOther>(TEST_EVENT_ID), 2U);
    }
    events.emit(TEST_EVENT_ID, TestEventOther{});

    EXPECT_EQ(scoped_count, 1);
    EXPECT_EQ(kept_count, 2);
    EXPECT_EQ(events.get_subscriber_count<TestEventOther>(TEST_EVENT_ID), 1U);
}

TEST_F(WBEEventManagerTest, ResetUnsubscribesOnceAndLeavesRegistrationEmpty) {
    EventManager& events = get_events();
    int32_t count = 0;
    EventRegistration registration = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&count](const TestEventOther&) {
        ++count;
    });
    EXPECT_FALSE(registration.is_empty());
    EXPECT_NE(registration.get_id(), 0U);

    registration.reset();
    EXPECT_TRUE(registration.is_empty());
    EXPECT_EQ(registration.get_id(), 0U);
    EXPECT_NO_THROW(registration.reset());
    events.emit(TEST_EVENT_ID, TestEventOther{});
    EXPECT_EQ(count, 0);
    EXPECT_EQ(events.get_subscriber_count<TestEventOther>(TEST_EVENT_ID), 0U);
}

TEST_F(WBEEventManagerTest, DefaultRegistrationIsEmpty) {
    EventRegistration registration;
    EXPECT_TRUE(registration.is_empty());
    EXPECT_EQ(registration.get_id(), 0U);
    EXPECT_NO_THROW(registration.reset());
}

TEST_F(WBEEventManagerTest, MovingTransfersOwnership) {
    EventManager& events = get_events();
    int32_t count = 0;
    EventRegistration source = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&count](const TestEventOther&) {
        ++count;
    });
    const EventSubscriptionID id = source.get_id();

    EventRegistration moved(std::move(source));
    EXPECT_TRUE(source.is_empty()); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(moved.get_id(), id);
    source.reset();
    events.emit(TEST_EVENT_ID, TestEventOther{});
    EXPECT_EQ(count, 1);

    EventRegistration assigned;
    assigned = std::move(moved);
    EXPECT_TRUE(moved.is_empty()); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(assigned.get_id(), id);
    events.emit(TEST_EVENT_ID, TestEventOther{});
    EXPECT_EQ(count, 2);
    EXPECT_EQ(events.get_subscriber_count<TestEventOther>(TEST_EVENT_ID), 1U);
}

TEST_F(WBEEventManagerTest, MoveAssignmentUnsubscribesPreviousSubscription) {
    EventManager& events = get_events();
    int32_t first_count = 0;
    int32_t second_count = 0;
    EventRegistration registration = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&first_count](const TestEventOther&) {
        ++first_count;
    });
    registration = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&second_count](const TestEventOther&) {
        ++second_count;
    });

    events.emit(TEST_EVENT_ID, TestEventOther{});
    EXPECT_EQ(first_count, 0);
    EXPECT_EQ(second_count, 1);
    EXPECT_EQ(events.get_subscriber_count<TestEventOther>(TEST_EVENT_ID), 1U);
}

TEST_F(WBEEventManagerTest, SubscriptionIdsAreUnique) {
    EventManager& events = get_events();
    EventRegistration first = events.subscribe<TestEventOther>(TEST_EVENT_ID, [](const TestEventOther&) {
    });
    const EventRegistration second = events.subscribe<TestEventValue>(TEST_EVENT_ID, [](const TestEventValue&) {
    });
    const EventSubscriptionID first_id = first.get_id();
    first.reset();
    const EventRegistration third = events.subscribe<TestEventOther>(TEST_EVENT_ID, [](const TestEventOther&) {
    });
    EXPECT_NE(first_id, 0U);
    EXPECT_NE(first_id, second.get_id());
    EXPECT_NE(first_id, third.get_id());
    EXPECT_NE(second.get_id(), third.get_id());
}

TEST_F(WBEEventManagerTest, SubscribingDuringEmitTakesEffectOnNextEmit) {
    EventManager& events = get_events();
    int32_t late_count = 0;
    EventRegistration late_registration;
    const EventRegistration registration = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&](const TestEventOther&) {
        if (late_registration.is_empty()) {
            late_registration = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&late_count](const TestEventOther&) {
                ++late_count;
            });
        }
    });

    events.emit(TEST_EVENT_ID, TestEventOther{});
    EXPECT_EQ(late_count, 0);
    events.emit(TEST_EVENT_ID, TestEventOther{});
    EXPECT_EQ(late_count, 1);
}

TEST_F(WBEEventManagerTest, CallbackCanResetItsOwnRegistration) {
    EventManager& events = get_events();
    int32_t self_count = 0;
    int32_t next_count = 0;
    EventRegistration self_registration;
    self_registration = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&](const TestEventOther&) {
        ++self_count;
        self_registration.reset();
    });
    const EventRegistration next_registration = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&next_count](const TestEventOther&) {
        ++next_count;
    });

    events.emit(TEST_EVENT_ID, TestEventOther{});
    events.emit(TEST_EVENT_ID, TestEventOther{});

    EXPECT_EQ(self_count, 1);
    EXPECT_EQ(next_count, 2);
    EXPECT_TRUE(self_registration.is_empty());
}

TEST_F(WBEEventManagerTest, CallbackResetDuringEmitIsSkipped) {
    EventManager& events = get_events();
    int32_t victim_count = 0;
    EventRegistration victim_registration;
    const EventRegistration resetting_registration = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&](const TestEventOther&) {
        victim_registration.reset();
    });
    victim_registration = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&victim_count](const TestEventOther&) {
        ++victim_count;
    });

    events.emit(TEST_EVENT_ID, TestEventOther{});
    EXPECT_EQ(victim_count, 0);
}

TEST_F(WBEEventManagerTest, CallbacksCanEmitOtherEvents) {
    EventManager& events = get_events();
    std::vector<int32_t> received;
    const EventRegistration value_registration = events.subscribe<TestEventValue>(TEST_EVENT_ID, [&](const TestEventValue& p_event) {
        events.emit(TEST_EVENT_ID, TestEventOther{.value = p_event.value * 10});
    });
    const EventRegistration other_registration = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&received](const TestEventOther& p_event) {
        received.push_back(p_event.value);
    });

    events.emit(TEST_EVENT_ID, TestEventValue{.value = 4});
    EXPECT_EQ(received, (std::vector<int32_t>{40}));
}

TEST_F(WBEEventManagerTest, CallbackExceptionPropagatesAndLeavesManagerUsable) {
    EventManager& events = get_events();
    int32_t later_count = 0;
    EventRegistration throwing = events.subscribe<TestEventOther>(TEST_EVENT_ID, [](const TestEventOther&) {
        throw std::runtime_error("callback failure");
    });
    const EventRegistration later = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&later_count](const TestEventOther&) {
        ++later_count;
    });

    EXPECT_THROW(events.emit(TEST_EVENT_ID, TestEventOther{}), std::runtime_error);
    EXPECT_EQ(later_count, 0);
    // The failed invocation is no longer recorded as running, so resetting does not wait for it.
    throwing.reset();
    events.emit(TEST_EVENT_ID, TestEventOther{});
    EXPECT_EQ(later_count, 1);
}

TEST_F(WBEEventManagerTest, ResetWaitsForInvocationOnAnotherThread) {
    EventManager& events = get_events();
    std::binary_semaphore callback_entered(0);
    std::binary_semaphore release_callback(0);
    std::atomic<bool> callback_finished = false;
    std::atomic<bool> callback_finished_before_reset_returned = false;
    EventRegistration registration = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&](const TestEventOther&) {
        callback_entered.release();
        release_callback.acquire();
        callback_finished.store(true);
    });

    std::thread emitter([&events] {
        events.emit(TEST_EVENT_ID, TestEventOther{});
    });
    callback_entered.acquire();
    std::thread resetter([&] {
        registration.reset();
        callback_finished_before_reset_returned.store(callback_finished.load());
    });
    release_callback.release();
    resetter.join();
    emitter.join();

    EXPECT_TRUE(callback_finished_before_reset_returned.load());
    EXPECT_EQ(events.get_subscriber_count<TestEventOther>(TEST_EVENT_ID), 0U);
}

TEST_F(WBEEventManagerTest, ConcurrentEmitSubscribeAndUnsubscribe) {
    EventManager& events = get_events();
    constexpr int32_t EMITTER_COUNT = 4;
    constexpr int32_t EMITS_PER_THREAD = 2000;
    constexpr int32_t CHURN_ITERATIONS = 500;
    std::atomic<int64_t> stable_deliveries = 0;
    std::atomic<int64_t> churn_deliveries = 0;
    const EventRegistration stable = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&stable_deliveries](const TestEventOther& p_event) {
        stable_deliveries.fetch_add(p_event.value, std::memory_order_relaxed);
    });

    std::vector<std::thread> threads;
    threads.reserve(EMITTER_COUNT + 1);
    for (int32_t i = 0; i < EMITTER_COUNT; ++i) {
        threads.emplace_back([&events] {
            for (int32_t emit_index = 0; emit_index < EMITS_PER_THREAD; ++emit_index) {
                events.emit(TEST_EVENT_ID, TestEventOther{.value = 1});
            }
        });
    }
    threads.emplace_back([&events, &churn_deliveries] {
        for (int32_t iteration = 0; iteration < CHURN_ITERATIONS; ++iteration) {
            const EventRegistration churn = events.subscribe<TestEventOther>(TEST_EVENT_ID, [&churn_deliveries](const TestEventOther&) {
                churn_deliveries.fetch_add(1, std::memory_order_relaxed);
            });
        }
    });
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(stable_deliveries.load(), int64_t{EMITTER_COUNT} * EMITS_PER_THREAD);
    EXPECT_LE(churn_deliveries.load(), int64_t{EMITTER_COUNT} * EMITS_PER_THREAD);
    EXPECT_EQ(events.get_subscriber_count<TestEventOther>(TEST_EVENT_ID), 1U);
    // Every churned registration was destroyed; none of their callbacks may run again.
    const int64_t churn_after_join = churn_deliveries.load();
    events.emit(TEST_EVENT_ID, TestEventOther{.value = 1});
    EXPECT_EQ(churn_deliveries.load(), churn_after_join);
}

} // namespace WhiteBirdEngine
