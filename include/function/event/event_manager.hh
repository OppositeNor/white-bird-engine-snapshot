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
#ifndef WBE_FILE_EVENT_MANAGER_HH
#define WBE_FILE_EVENT_MANAGER_HH

#include "core/allocator/stl_allocator.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/event/event_registration.hh"
#include "utils/interface/i_singleton.hh"
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace WhiteBirdEngine {

/**
 * @class EventManager
 * @brief Thread-safe publish/subscribe table that delivers events to callbacks by event id and payload type.
 *
 * An event is a non-empty string event id plus a payload of any type. The pair selects one entry of the table, keyed
 * `"<event_id>_<payload type>"`, where the payload type is the std::type_index name of the payload's type after removing
 * references and cv-qualifiers (see get_event_key()). subscribe<TPayload>(event_id, ...) registers a callback for one key and
 * returns the EventRegistration that owns it; destroying or resetting the registration unsubscribes. emit(event_id, payload)
 * calls every callback registered for the key, in subscription order, on the emitting thread. The same id with different
 * payload types, the same payload type under different ids, and base and derived payload types are all different events.
 *
 * All methods may be called from any thread, including from inside a callback. emit() calls the callbacks that were
 * subscribed when it started: callbacks subscribed during an emission are not called by it, and callbacks unsubscribed
 * during it are skipped if they have not run yet. Once a registration is destroyed or reset, its callback is not running and
 * will not run again, except for invocations on the destroying thread that are still on its stack (destroying a
 * registration from inside its own callback is allowed). Callbacks can run concurrently when several threads emit, so they
 * must be thread-safe themselves.
 * An exception thrown by a callback propagates out of emit() and skips the remaining callbacks.
 *
 * The allocator must be thread-safe and outlive the manager. The manager is a singleton owned by EngineFunction, which the
 * Global creates; it is neither copyable nor movable, must outlive every emission and every registration it issued, and must
 * not be destroyed while another thread uses it. Destroying it while registrations remain is a debug assertion failure.
 */
class EventManager final : public ISingleton<EventManager> {
public:
    EventManager() = delete;
    /**
     * @brief Construct an empty event table.
     * @param p_allocator Thread-safe allocator for the table and its callbacks; it must outlive the manager.
     * @throws std::runtime_error If another EventManager exists or p_allocator is null.
     */
    explicit EventManager(HeapAllocatorDefault* p_allocator);
    /**
     * @brief Destroy the table. Every registration it issued must already be destroyed or reset.
     */
    virtual ~EventManager() override;
    EventManager(const EventManager&) = delete;
    EventManager(EventManager&&) noexcept = delete;
    EventManager& operator=(const EventManager&) = delete;
    EventManager& operator=(EventManager&&) noexcept = delete;

    /**
     * @brief Get the singleton instance.
     * @return The instance, or null when none exists.
     */
    static EventManager* get_singleton() {
        return singleton;
    }

    /**
     * @brief Get the table key of an event: `"<event_id>_<payload type>"`.
     * @tparam TPayload The payload type; references and cv-qualifiers are ignored. Its part of the key is the
     * implementation-defined std::type_index name, so keys are only meaningful within one running process.
     * @param p_event_id The event id; non-empty.
     * @throws std::runtime_error If p_event_id is empty.
     */
    template <typename TPayload>
    static std::string get_event_key(std::string_view p_event_id) {
        return make_event_key(p_event_id, std::type_index(typeid(std::remove_cvref_t<TPayload>)));
    }

    /**
     * @brief Register a callback for an event id and payload type.
     * @tparam TPayload The payload type; references and cv-qualifiers are ignored.
     * @param p_event_id The event id; non-empty.
     * @param p_callback Callable invoked as `p_callback(const TPayload&)` for every TPayload emitted under p_event_id. It is
     * copied or moved into the manager and may be invoked concurrently from several threads.
     * @return The registration that owns the subscription; the callback stays subscribed until it is destroyed or reset.
     * @throws std::runtime_error If p_event_id is empty.
     */
    template <typename TPayload, typename TCallback>
        requires std::invocable<std::remove_cvref_t<TCallback>&, const std::remove_cvref_t<TPayload>&>
    [[nodiscard]] EventRegistration subscribe(std::string_view p_event_id, TCallback&& p_callback) {
        using PayloadType = std::remove_cvref_t<TPayload>;
        return EventRegistration(this,
            subscribe_erased(get_event_key<PayloadType>(p_event_id), [callback = std::forward<TCallback>(p_callback)](const void* p_payload) mutable {
                callback(*static_cast<const PayloadType*>(p_payload));
            }));
    }

    /**
     * @brief Call every callback subscribed to the event id and the payload's type with the payload.
     * @param p_event_id The event id; non-empty.
     * @param p_payload The payload; its exact type, ignoring references and cv-qualifiers, is part of the event.
     * @throws std::runtime_error If p_event_id is empty.
     * @throws Whatever a callback throws.
     */
    template <typename TPayload>
    void emit(std::string_view p_event_id, const TPayload& p_payload) {
        emit_erased(get_event_key<TPayload>(p_event_id), &p_payload);
    }

    /**
     * @brief Get the number of callbacks currently subscribed to an event id and payload type.
     * @throws std::runtime_error If p_event_id is empty.
     */
    template <typename TPayload>
    size_t get_subscriber_count(std::string_view p_event_id) const {
        return get_subscriber_count_erased(get_event_key<TPayload>(p_event_id));
    }

private:
    friend class EventRegistration;

    using ErasedCallback = std::function<void(const void*)>;

    struct Listener {
        Listener() = delete;
        explicit Listener(HeapAllocatorDefault* p_allocator) : executing_threads(p_allocator) {
        }
        ~Listener() = default;
        Listener(const Listener&) = delete;
        Listener(Listener&&) noexcept = delete;
        Listener& operator=(const Listener&) = delete;
        Listener& operator=(Listener&&) noexcept = delete;

        EventSubscriptionID id = 0;
        ErasedCallback callback;
        // Guards is_active and executing_threads.
        std::mutex state_mutex;
        // Signaled whenever an invocation finishes.
        std::condition_variable invocation_finished;
        // Cleared by unsubscribe() before it waits for running invocations.
        bool is_active = true;
        // One entry per running invocation, naming the thread it runs on.
        Vector<std::thread::id, HeapAllocatorDefault> executing_threads;
    };

    // Records one invocation in its listener for the guard's lifetime, unless the listener is already unsubscribed.
    class InvocationGuard;

    using ListenerRef = Ref<Listener, HeapAllocatorDefault>;
    using ListenerList = Vector<ListenerRef, HeapAllocatorDefault>;
    // Immutable once published; subscribe() and unsubscribe() replace the whole list, so emit() iterates a stable copy.
    using ListenerListRef = Ref<ListenerList, HeapAllocatorDefault>;

    static std::string make_event_key(std::string_view p_event_id, std::type_index p_payload_type);
    EventSubscriptionID subscribe_erased(std::string p_event_key, ErasedCallback p_callback);
    // Removes a subscription and waits for its invocations on other threads; throws if the id is unknown.
    void unsubscribe(EventSubscriptionID p_subscription);
    void emit_erased(const std::string& p_event_key, const void* p_payload) const;
    size_t get_subscriber_count_erased(const std::string& p_event_key) const;

    HeapAllocatorDefault* allocator;
    mutable std::shared_mutex table_mutex;
    HashMap<std::string, ListenerListRef, HeapAllocatorDefault> listeners_by_event;
    HashMap<EventSubscriptionID, std::string, HeapAllocatorDefault> event_by_subscription;
    std::atomic<EventSubscriptionID> next_subscription_id = 1;

    inline static EventManager* singleton = nullptr;
};

} // namespace WhiteBirdEngine

#endif
