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
#ifndef WBE_FILE_I_INPUT_MANAGER_HH
#define WBE_FILE_I_INPUT_MANAGER_HH

#include "function/input_manager/key_codes.hh"
#include "utils/interface/i_singleton.hh"
#include <cstdint>

namespace WhiteBirdEngine {

using KeyStatus = uint8_t;

// The key is released.
constexpr KeyStatus KEY_UP = 0b00;
// The key is pressed.
constexpr KeyStatus KEY_DOWN = 0b01;

/**
 * @class InputStatus
 * @brief The input status.
 *
 */
struct InputStatus {
    /**
     * @brief The key's status code.
     */
    KeyStatus status_code;

    /**
     * @brief Is key up.
     *
     * @return True if the key is up. False otherwise.
     */
    bool is_up() const {
        return status_code == KEY_UP;
    }

    /**
     * @brief Is key down.
     *
     * @return True if the key is down. False otherwise.
     */
    bool is_down() const {
        return status_code & KEY_DOWN;
    }
};

/**
 * @class IInputManager
 * @brief Input manager interface. The window backend reports key transitions; game code queries key statuses.
 * @details Only one input manager may exist at a time; constructing a second one throws. Neither copyable nor movable.
 * Implementations must allow key transitions and status queries from different threads.
 */
class IInputManager : public ISingleton<IInputManager> {
public:
    IInputManager() {
        singleton = this;
    }
    virtual ~IInputManager() override {
        singleton = nullptr;
    }
    IInputManager(const IInputManager&) = delete;
    IInputManager(IInputManager&&) noexcept = delete;
    IInputManager& operator=(const IInputManager&) = delete;
    IInputManager& operator=(IInputManager&&) noexcept = delete;

    /**
     * @brief Get the singleton instance.
     *
     * @return The singleton instance, or nullptr when no input manager exists.
     */
    static IInputManager* get_singleton() {
        return singleton;
    }

    /**
     * @brief Get the input status of a key.
     *
     * @param p_key_code The key code.
     * @return The input status.
     */
    virtual InputStatus get_input_status(KeyCode p_key_code) const = 0;

    /**
     * @brief Set the keycode to key down.
     */
    virtual void set_keydown(KeyCode p_key_code) = 0;

    /**
     * @brief Set the keycode to key up.
     */
    virtual void set_keyup(KeyCode p_key_code) = 0;

private:
    inline static IInputManager* singleton = nullptr;
};

} // namespace WhiteBirdEngine

#endif
