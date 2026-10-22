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
#include "function/event/event_registration.hh"
#include "function/event/event_manager.hh"
#include <utility>

namespace WhiteBirdEngine {

EventRegistration::~EventRegistration() {
    reset();
}

EventRegistration::EventRegistration(EventRegistration&& p_other) noexcept
    : manager(std::exchange(p_other.manager, nullptr)), id(std::exchange(p_other.id, 0)) {
}

EventRegistration& EventRegistration::operator=(EventRegistration&& p_other) noexcept {
    if (this != &p_other) {
        reset();
        manager = std::exchange(p_other.manager, nullptr);
        id = std::exchange(p_other.id, 0);
    }
    return *this;
}

void EventRegistration::reset() {
    if (manager == nullptr) {
        return;
    }
    // Clear first, so a callback that reaches this registration again while it is being unsubscribed sees it empty.
    EventManager* owning_manager = std::exchange(manager, nullptr);
    const EventSubscriptionID owned_id = std::exchange(id, 0);
    owning_manager->unsubscribe(owned_id);
}

} // namespace WhiteBirdEngine
