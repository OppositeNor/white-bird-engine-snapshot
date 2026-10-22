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
#ifndef WBE_FILE_INPUT_MANAGER_MOCKED_HH
#define WBE_FILE_INPUT_MANAGER_MOCKED_HH

#include "core/allocator/stl_allocator.hh"
#include "core/engine_core.hh"
#include "function/input_manager/i_input_manager.hh"
#include "function/input_manager/input_manager_regular.hh"
#include "function/input_manager/key_codes.hh"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class InputManagerMocked
 * @brief Executable-test input manager that replays a scripted key sequence on top of real window input.
 * @details The sequence is read once at construction from INPUT_SEQUENCE_ENVIRONMENT_VARIABLE, a JSON array of objects:
 * - `"key"`: KeyCode enumerator name, for example `"KEYCODE_R"`.
 * - Exactly one of `"count"` (positive integer: the number of get_input_status() queries for the key that report down) or
 *   `"duration"` (positive seconds: how long queries for the key report down).
 * - Optional `"delay"` (non-negative seconds, default 0): time to wait before the entry becomes active.
 *
 * For example `[{"key": "KEYCODE_V", "delay": 5.0, "count": 1}, {"key": "KEYCODE_W", "duration": 2.0}]` taps V five seconds
 * after the first input query, then holds W for two seconds. Entries run in order. An entry's delay starts when the previous
 * entry finishes, or at the first get_input_status() call for the first entry, so loading time before the first logic tick
 * is not counted. Time is measured with a steady clock. Queries for any key other than the active entry's, and all queries
 * after the sequence ends, report the real window state. An unset or empty variable replays nothing. A malformed sequence
 * throws. Consecutive entries for the same key without a delay are reported as one continuous press. Status queries and
 * window transitions may run on different threads. Neither copyable nor movable.
 */
class InputManagerMocked final : public InputManagerRegular {
public:
    /**
     * @brief Environment variable holding the JSON input sequence.
     */
    static constexpr std::string_view INPUT_SEQUENCE_ENVIRONMENT_VARIABLE = "WBE_EXEC_TEST_INPUT_SEQUENCE";

    InputManagerMocked() = delete;
    /**
     * @brief Reads and validates the input sequence from INPUT_SEQUENCE_ENVIRONMENT_VARIABLE.
     *
     * @param p_allocator Allocator for the parsed sequence. Must outlive this object.
     */
    explicit InputManagerMocked(HeapAllocatorGlobal* p_allocator);
    /**
     * @brief Validates and stores an explicit input sequence, ignoring the environment.
     *
     * @param p_allocator Allocator for the parsed sequence. Must outlive this object.
     * @param p_input_sequence JSON input sequence in the class's documented format. Empty replays nothing.
     */
    InputManagerMocked(HeapAllocatorGlobal* p_allocator, std::string_view p_input_sequence);
    virtual ~InputManagerMocked() override = default;
    InputManagerMocked(const InputManagerMocked&) = delete;
    InputManagerMocked(InputManagerMocked&&) noexcept = delete;
    InputManagerMocked& operator=(const InputManagerMocked&) = delete;
    InputManagerMocked& operator=(InputManagerMocked&&) noexcept = delete;

    /**
     * @brief Reports down for the active sequence entry's key, consuming one count for count entries; otherwise reports real
     * input.
     */
    virtual InputStatus get_input_status(KeyCode p_key_code) const override;

private:
    using Clock = std::chrono::steady_clock;

    struct SequenceEntry {
        KeyCode key_code;
        // Remaining down queries; zero for duration entries.
        uint32_t remaining_count;
        Clock::duration delay;
        // Down time for duration entries; zero for count entries.
        Clock::duration duration;
    };

    // Advances past finished duration entries. Returns whether the current entry is active at p_now.
    bool update_current_entry(Clock::time_point p_now) const;

    mutable std::mutex sequence_mutex;
    mutable Vector<SequenceEntry, HeapAllocatorGlobal> sequence;
    mutable size_t current_entry_index = 0;
    // When the current entry's delay began; unset until the first status query.
    mutable std::optional<Clock::time_point> current_entry_start;
};

} // namespace WhiteBirdEngine

#endif
