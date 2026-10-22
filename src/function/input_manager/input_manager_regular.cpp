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

#include "function/input_manager/input_manager_regular.hh"
#include "function/input_manager/i_input_manager.hh"
#include "function/input_manager/key_codes.hh"
#include <atomic>
#include <cstdint>

#define WBE_SET_ATOMIC_BITSET_TRUE(bitset, index)                                                                           \
    ((bitset)[(index) / 8].fetch_or(static_cast<uint8_t>(0b1 << ((index) % 8)), std::memory_order_release))
#define WBE_SET_ATOMIC_BITSET_FALSE(bitset, index)                                                                          \
    ((bitset)[(index) / 8].fetch_and(static_cast<uint8_t>(~(0b1 << ((index) % 8))), std::memory_order_release))

namespace WhiteBirdEngine {

InputStatus InputManagerRegular::get_input_status(KeyCode p_key_code) const {
    return InputStatus{.status_code = static_cast<uint8_t>(
                           key_status_map[p_key_code / 8].load(std::memory_order_acquire) >> (p_key_code % 8) & 0b1)};
}

void InputManagerRegular::set_keydown(KeyCode p_key_code) {
    WBE_SET_ATOMIC_BITSET_TRUE(key_status_map, p_key_code);
}

void InputManagerRegular::set_keyup(KeyCode p_key_code) {
    WBE_SET_ATOMIC_BITSET_FALSE(key_status_map, p_key_code);
}

} // namespace WhiteBirdEngine
