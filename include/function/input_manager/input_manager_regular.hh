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
#ifndef WBE_FILE_INPUT_MANAGER_REGULAR_HH
#define WBE_FILE_INPUT_MANAGER_REGULAR_HH

#include "function/input_manager/i_input_manager.hh"
#include "function/input_manager/key_codes.hh"
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace WhiteBirdEngine {

/**
 * @class InputManagerRegular
 * @brief Input manager that reports the key transitions delivered by the window backend.
 * @details Key statuses are stored in a lock-free atomic bitset, so transitions and queries may run on different threads.
 * Neither copyable nor movable.
 */
class InputManagerRegular : public IInputManager {
public:
    InputManagerRegular() = default;
    virtual ~InputManagerRegular() override = default;
    InputManagerRegular(const InputManagerRegular&) = delete;
    InputManagerRegular(InputManagerRegular&&) noexcept = delete;
    InputManagerRegular& operator=(const InputManagerRegular&) = delete;
    InputManagerRegular& operator=(InputManagerRegular&&) noexcept = delete;

    virtual InputStatus get_input_status(KeyCode p_key_code) const override;

    virtual void set_keydown(KeyCode p_key_code) override;

    virtual void set_keyup(KeyCode p_key_code) override;

private:
    // Use an atomic keyset to store the key statuses.
    std::atomic<uint8_t> key_status_map[KeyCode::TOTAL_KEYCODES % 8 == 0 ? KeyCode::TOTAL_KEYCODES / 8 : KeyCode::TOTAL_KEYCODES / 8 + 1] = {};
    static_assert(std::remove_cvref_t<decltype(key_status_map[0])>::is_always_lock_free, "Key status map must be lock free");
};
} // namespace WhiteBirdEngine

#endif
