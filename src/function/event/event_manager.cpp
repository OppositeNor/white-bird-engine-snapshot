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
#include "function/event/event_manager.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "utils/defs.hh"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <typeindex>
#include <utility>

namespace WhiteBirdEngine {

class EventManager::InvocationGuard final {
public:
    InvocationGuard() = delete;
    explicit InvocationGuard(Listener& p_listener) : listener(p_listener) {
        std::scoped_lock lock(listener.state_mutex);
        if (listener.is_active) {
            listener.executing_threads.push_back(std::this_thread::get_id());
            is_entered = true;
        }
    }
    ~InvocationGuard() {
        if (!is_entered) {
            return;
        }
        {
            std::scoped_lock lock(listener.state_mutex);
            auto entry = std::ranges::find(listener.executing_threads, std::this_thread::get_id());
            WBE_DEBUG_ASSERT(entry != listener.executing_threads.end());
            listener.executing_threads.erase(entry);
        }
        listener.invocation_finished.notify_all();
    }
    InvocationGuard(const InvocationGuard&) = delete;
    InvocationGuard(InvocationGuard&&) noexcept = delete;
    InvocationGuard& operator=(const InvocationGuard&) = delete;
    InvocationGuard& operator=(InvocationGuard&&) noexcept = delete;

    /**
     * @brief Whether the invocation was recorded and the callback may run.
     */
    bool get_is_entered() const {
        return is_entered;
    }

private:
    Listener& listener;
    bool is_entered = false;
};

EventManager::EventManager(HeapAllocatorDefault* p_allocator)
    : allocator(p_allocator), listeners_by_event(p_allocator), event_by_subscription(p_allocator) {
    if (allocator == nullptr) {
        throw std::runtime_error("Failed to construct event manager: the allocator is null.");
    }
    singleton = this;
}

EventManager::~EventManager() {
    WBE_DEBUG_ASSERT(event_by_subscription.empty() && "Every EventRegistration must be destroyed before its EventManager.");
    singleton = nullptr;
}

std::string EventManager::make_event_key(std::string_view p_event_id, std::type_index p_payload_type) {
    if (p_event_id.empty()) {
        throw std::runtime_error("Failed to build event key: the event id is empty.");
    }
    return std::format("{}_{}", p_event_id, p_payload_type.name());
}

EventSubscriptionID EventManager::subscribe_erased(std::string p_event_key, ErasedCallback p_callback) {
    const EventSubscriptionID subscription_id = next_subscription_id.fetch_add(1, std::memory_order_relaxed);
    ListenerRef listener = make_ref<Listener>(allocator, allocator);
    listener->id = subscription_id;
    listener->callback = std::move(p_callback);

    std::unique_lock lock(table_mutex);
    ListenerListRef updated_listeners = make_ref<ListenerList>(allocator, allocator);
    auto existing = listeners_by_event.find(p_event_key);
    if (existing != listeners_by_event.end()) {
        updated_listeners->reserve(existing->second->size() + 1);
        updated_listeners->assign(existing->second->begin(), existing->second->end());
    }
    updated_listeners->push_back(std::move(listener));
    listeners_by_event.insert_or_assign(p_event_key, std::move(updated_listeners));
    event_by_subscription.emplace(subscription_id, std::move(p_event_key));
    return subscription_id;
}

void EventManager::unsubscribe(EventSubscriptionID p_subscription) {
    ListenerRef removed_listener;
    {
        std::unique_lock lock(table_mutex);
        auto subscription = event_by_subscription.find(p_subscription);
        if (subscription == event_by_subscription.end()) {
            throw std::runtime_error(
                std::format("Failed to unsubscribe event subscription {}: no subscription has this id.", p_subscription));
        }
        auto listeners = listeners_by_event.find(subscription->second);
        event_by_subscription.erase(subscription);
        WBE_DEBUG_ASSERT(listeners != listeners_by_event.end());
        ListenerListRef remaining_listeners = make_ref<ListenerList>(allocator, allocator);
        remaining_listeners->reserve(listeners->second->size());
        for (const ListenerRef& listener : *listeners->second) {
            if (listener->id == p_subscription) {
                removed_listener = listener;
            } else {
                remaining_listeners->push_back(listener);
            }
        }
        if (remaining_listeners->empty()) {
            listeners_by_event.erase(listeners);
        } else {
            listeners->second = std::move(remaining_listeners);
        }
    }
    WBE_DEBUG_ASSERT(!removed_listener.is_null());

    // Emissions that already copied the old list skip the listener from now on. Wait for the invocations already running,
    // except those on this thread: they are this call's own callers and finish only after it returns.
    std::unique_lock lock(removed_listener->state_mutex);
    removed_listener->is_active = false;
    const std::thread::id this_thread = std::this_thread::get_id();
    removed_listener->invocation_finished.wait(lock, [&removed_listener, this_thread] {
        return std::ranges::all_of(removed_listener->executing_threads, [this_thread](std::thread::id p_thread) {
            return p_thread == this_thread;
        });
    });
}

void EventManager::emit_erased(const std::string& p_event_key, const void* p_payload) const {
    ListenerListRef listeners;
    {
        std::shared_lock lock(table_mutex);
        auto entry = listeners_by_event.find(p_event_key);
        if (entry == listeners_by_event.end()) {
            return;
        }
        listeners = entry->second;
    }
    // The copied list keeps every listener alive even if it is unsubscribed meanwhile.
    for (ListenerRef& listener : *listeners) {
        InvocationGuard guard(*listener);
        if (!guard.get_is_entered()) {
            continue;
        }
        listener->callback(p_payload);
    }
}

size_t EventManager::get_subscriber_count_erased(const std::string& p_event_key) const {
    std::shared_lock lock(table_mutex);
    auto entry = listeners_by_event.find(p_event_key);
    return entry == listeners_by_event.end() ? 0 : entry->second->size();
}

} // namespace WhiteBirdEngine
