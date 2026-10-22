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
#ifndef WBE_FILE_KEY_CODES_HH
#define WBE_FILE_KEY_CODES_HH

#include "core/reflection/reflection_defs.hh"
#include "utils/defs.hh"
#include <cstdint>

namespace WhiteBirdEngine {

// Keycodes white bird engine uses. The corresponding keycode enum name reflects
// the character on a US QWERTY keyboard layout. Reflected; TOTAL_KEYCODES has no reflected name.
enum WBE_META(WBE_REFLECT_ENUM) KeyCode {
    // Numbers.
    KEYCODE_0,
    KEYCODE_1,
    KEYCODE_2,
    KEYCODE_3,
    KEYCODE_4,
    KEYCODE_5,
    KEYCODE_6,
    KEYCODE_7,
    KEYCODE_8,
    KEYCODE_9,

    // Alphabet.
    KEYCODE_A,
    KEYCODE_B,
    KEYCODE_C,
    KEYCODE_D,
    KEYCODE_E,
    KEYCODE_F,
    KEYCODE_G,
    KEYCODE_H,
    KEYCODE_I,
    KEYCODE_J,
    KEYCODE_K,
    KEYCODE_L,
    KEYCODE_M,
    KEYCODE_N,
    KEYCODE_O,
    KEYCODE_P,
    KEYCODE_Q,
    KEYCODE_R,
    KEYCODE_S,
    KEYCODE_T,
    KEYCODE_U,
    KEYCODE_V,
    KEYCODE_W,
    KEYCODE_X,
    KEYCODE_Y,
    KEYCODE_Z,

    // Other common symbols.
    KEYCODE_BACKSPACE,
    KEYCODE_TAB,
    KEYCODE_ENTER,
    KEYCODE_SPACE,
    KEYCODE_DELETE,
    KEYCODE_EQUAL,
    KEYCODE_BACKSLASH,
    KEYCODE_UNDERSCORE,
    KEYCODE_QUOTE,
    KEYCODE_BACKQUOTE,
    KEYCODE_LEFT_BRACKET,
    KEYCODE_RIGHT_BRACKET,
    KEYCODE_SEMICOLON,

    // Control keys.
    KEYCODE_F1,
    KEYCODE_F2,
    KEYCODE_F3,
    KEYCODE_F4,
    KEYCODE_F5,
    KEYCODE_F6,
    KEYCODE_F7,
    KEYCODE_F8,
    KEYCODE_F9,
    KEYCODE_F10,
    KEYCODE_F11,
    KEYCODE_F12,

    KEYCODE_LEFT_SHIFT,
    KEYCODE_LEFT_CONTROL,
    KEYCODE_LEFT_ALT,
    KEYCODE_RIGHT_SHIFT,
    KEYCODE_RIGHT_CONTROL,
    KEYCODE_RIGHT_ALT,
    KEYCODE_CAPSLOCK,

    KEYCODE_HOME,
    KEYCODE_END,
    KEYCODE_PAGE_UP,
    KEYCODE_PAGE_DOWN,
    KEYCODE_INSERT,

    KEYCODE_UP,
    KEYCODE_DOWN,
    KEYCODE_LEFT,
    KEYCODE_RIGHT,
    KEYCODE_ESCAPE,

    // A tracker to track the total number of keycodes.
    TOTAL_KEYCODES WBE_META(WBE_REFLECT_ENUM_IGNORE),
};

static_assert(KEYCODE_1 - KEYCODE_0 == 1, "Keycodes of numbers that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_2 - KEYCODE_1 == 1, "Keycodes of numbers that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_3 - KEYCODE_2 == 1, "Keycodes of numbers that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_4 - KEYCODE_3 == 1, "Keycodes of numbers that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_5 - KEYCODE_4 == 1, "Keycodes of numbers that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_6 - KEYCODE_5 == 1, "Keycodes of numbers that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_7 - KEYCODE_6 == 1, "Keycodes of numbers that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_8 - KEYCODE_7 == 1, "Keycodes of numbers that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_9 - KEYCODE_8 == 1, "Keycodes of numbers that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_B - KEYCODE_A == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_C - KEYCODE_B == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_D - KEYCODE_C == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_E - KEYCODE_D == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_F - KEYCODE_E == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_G - KEYCODE_F == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_H - KEYCODE_G == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_I - KEYCODE_H == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_J - KEYCODE_I == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_K - KEYCODE_J == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_L - KEYCODE_K == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_M - KEYCODE_L == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_N - KEYCODE_M == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_O - KEYCODE_N == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_P - KEYCODE_O == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_Q - KEYCODE_P == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_R - KEYCODE_Q == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_S - KEYCODE_R == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_T - KEYCODE_S == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_U - KEYCODE_T == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_V - KEYCODE_U == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_W - KEYCODE_V == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_X - KEYCODE_W == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_Y - KEYCODE_X == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_Z - KEYCODE_Y == 1, "Keycodes of alphabet that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_F2 - KEYCODE_F1 == 1, "Keycodes of F keys that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_F3 - KEYCODE_F2 == 1, "Keycodes of F keys that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_F4 - KEYCODE_F3 == 1, "Keycodes of F keys that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_F5 - KEYCODE_F4 == 1, "Keycodes of F keys that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_F6 - KEYCODE_F5 == 1, "Keycodes of F keys that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_F7 - KEYCODE_F6 == 1, "Keycodes of F keys that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_F8 - KEYCODE_F7 == 1, "Keycodes of F keys that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_F9 - KEYCODE_F8 == 1, "Keycodes of F keys that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_F10 - KEYCODE_F9 == 1, "Keycodes of F keys that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_F11 - KEYCODE_F10 == 1, "Keycodes of F keys that are differed by 1 must also be differed by 1.");
static_assert(KEYCODE_F12 - KEYCODE_F11 == 1, "Keycodes of F keys that are differed by 1 must also be differed by 1.");

/**
 * @brief Convert a number to a keycode.
 *
 * @param p_number The number to convert from.
 * @return The converted keycode.
 */
inline KeyCode number_to_keycode(uint64_t p_number) {
    WBE_DEBUG_ASSERT(p_number < 10);
    return static_cast<KeyCode>(p_number + KEYCODE_0);
}

/**
 * @brief Convert English alphabet letter to keycode.
 *
 * @param p_letter The letter to convert to keycode.
 * @return The converted keycode.
 */
inline KeyCode alphabet_to_keycode(char p_letter) {
    WBE_DEBUG_ASSERT((p_letter >= 'a' && p_letter <= 'z') || (p_letter >= 'A' && p_letter <= 'Z'));
    return static_cast<KeyCode>((p_letter > 'A' ? p_letter - 'A' : p_letter - 'a') + KEYCODE_A);
}

inline KeyCode f_keycode(uint64_t p_number) {
    WBE_DEBUG_ASSERT(p_number >= 1 && p_number <= 12);
    return static_cast<KeyCode>(p_number - 1 + KEYCODE_F1);
}

} // namespace WhiteBirdEngine

#endif
