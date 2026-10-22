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
#ifndef WBE_FILE_EVENT_REGISTRATION_HH
#define WBE_FILE_EVENT_REGISTRATION_HH

#include <cstdint>

namespace WhiteBirdEngine {

class EventManager;

/**
 * @brief Identifier of one subscription, unique within its EventManager. Zero is never issued.
 */
using EventSubscriptionID = uint64_t;

/**
 * @class EventRegistration
 * @brief Sole owner of one EventManager subscription; destroying or resetting it unsubscribes.
 *
 * Obtained only from EventManager::subscribe(). Unsubscribing waits until the callback has finished on every other thread,
 * so after the registration is destroyed or reset the callback is not running and never runs again; objects the callback
 * captures may be destroyed afterwards. Because of that wait, do not destroy or reset a registration while holding a lock
 * its callback acquires. Destroying it from inside its own callback is allowed.
 *
 * The registration is movable but not copyable; a moved-from or default-constructed registration is empty and owns no
 * subscription. It must not outlive the EventManager that issued it. It may be destroyed on any thread, but one
 * registration must not be used by several threads at once.
 */
class EventRegistration final {
public:
    /**
     * @brief Construct an empty registration.
     */
    EventRegistration() = default;
    /**
     * @brief Unsubscribe, unless the registration is empty.
     */
    ~EventRegistration();
    EventRegistration(const EventRegistration&) = delete;
    EventRegistration& operator=(const EventRegistration&) = delete;
    /**
     * @brief Take over another registration's subscription, leaving it empty.
     */
    EventRegistration(EventRegistration&& p_other) noexcept;
    /**
     * @brief Unsubscribe the currently owned subscription, if any, then take over the other's, leaving it empty.
     */
    EventRegistration& operator=(EventRegistration&& p_other) noexcept;

    /**
     * @brief Unsubscribe now and become empty. Does nothing when already empty.
     */
    void reset();

    /**
     * @brief Check whether the registration owns no subscription.
     */
    bool is_empty() const {
        return manager == nullptr;
    }

    /**
     * @brief Get the id of the owned subscription, or 0 when empty.
     */
    EventSubscriptionID get_id() const {
        return id;
    }

private:
    friend class EventManager;

    EventRegistration(EventManager* p_manager, EventSubscriptionID p_id) : manager(p_manager), id(p_id) {
    }

    EventManager* manager = nullptr;
    EventSubscriptionID id = 0;
};

} // namespace WhiteBirdEngine

#endif
