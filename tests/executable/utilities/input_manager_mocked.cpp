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
#include "input_manager_mocked.hh"
#include "core/data/data_value.hh"
#include "core/engine_core.hh"
#include "core/parser/parser_json.hh"
#include "function/input_manager/i_input_manager.hh"
#include "function/input_manager/input_manager_regular.hh"
#include "function/input_manager/key_codes.hh"
#include "generated/enum_reflection.gen.hh"
#include "utils/defs.hh"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

namespace {

static_assert(EnumReflection<KeyCode>::get_enumerators().size() == TOTAL_KEYCODES, "Every key code must have a name for input sequences.");

KeyCode key_code_from_name(std::string_view p_name) {
    const std::optional<KeyCode> key_code = EnumReflection<KeyCode>::try_string_to_enum(p_name);
    if (!key_code.has_value()) {
        throw std::runtime_error(std::format("InputManagerMocked: unknown key \"{}\" in input sequence.", p_name));
    }
    return *key_code;
}

std::chrono::steady_clock::duration read_seconds(const DataValue& p_entry, std::string_view p_field, bool p_allow_zero) {
    double seconds = p_entry.get_value<double>(p_field);
    // Also rejects NaN; the upper bound keeps the tick conversion in range.
    if (!(seconds >= 0.0 && seconds <= 1.0e6) || (!p_allow_zero && seconds == 0.0)) {
        throw std::runtime_error(std::format(
            "InputManagerMocked: input sequence \"{}\" must be in {}, 1e6] seconds, got {}.", p_field, p_allow_zero ? "[0" : "(0", seconds));
    }
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
}

std::string_view read_input_sequence_environment() {
    const char* value = std::getenv(InputManagerMocked::INPUT_SEQUENCE_ENVIRONMENT_VARIABLE.data());
    return value == nullptr ? std::string_view() : std::string_view(value);
}

} // namespace

InputManagerMocked::InputManagerMocked(HeapAllocatorGlobal* p_allocator)
    : InputManagerMocked(p_allocator, read_input_sequence_environment()) {
}

InputManagerMocked::InputManagerMocked(HeapAllocatorGlobal* p_allocator, std::string_view p_input_sequence)
    : sequence(p_allocator) {
    if (p_input_sequence.empty()) {
        return;
    }
    // Wrongly typed values fail inside the parser and crash like any other malformed sequence.
    ParserJSON parser;
    parser.parse_from_buffer(p_input_sequence);
    std::vector<DataValue> entries = parser.get<std::vector<DataValue>>();
    sequence.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const DataValue& entry = entries[i];
        if (!entry.contains("key") || entry.contains("count") == entry.contains("duration")) {
            throw std::runtime_error(std::format("InputManagerMocked: input sequence entry {} must be an object with a \"key\" and "
                                                 "exactly one of \"count\" or \"duration\".",
                i));
        }
        SequenceEntry sequence_entry{
            .key_code = key_code_from_name(entry.get_value<std::string>("key")),
            .remaining_count = 0,
            .delay = entry.contains("delay") ? read_seconds(entry, "delay", true) : Clock::duration::zero(),
            .duration = Clock::duration::zero(),
        };
        if (entry.contains("count")) {
            int64_t count = entry.get_value<int64_t>("count");
            if (count < 1 || std::cmp_greater(count, std::numeric_limits<uint32_t>::max())) {
                throw std::runtime_error(std::format("InputManagerMocked: input sequence \"count\" must be in [1, {}], got {}.",
                    std::numeric_limits<uint32_t>::max(),
                    count));
            }
            sequence_entry.remaining_count = static_cast<uint32_t>(count);
        } else {
            sequence_entry.duration = read_seconds(entry, "duration", false);
        }
        sequence.push_back(sequence_entry);
    }
}

bool InputManagerMocked::update_current_entry(Clock::time_point p_now) const {
    WBE_DEBUG_ASSERT(current_entry_start.has_value());
    while (current_entry_index < sequence.size()) {
        const SequenceEntry& entry = sequence[current_entry_index];
        const Clock::time_point active_start = *current_entry_start + entry.delay;
        if (p_now < active_start) {
            return false;
        }
        if (entry.duration == Clock::duration::zero() || p_now < active_start + entry.duration) {
            return true;
        }
        // Chain from the scheduled end so later entries do not drift with query timing.
        current_entry_start = active_start + entry.duration;
        ++current_entry_index;
    }
    return false;
}

InputStatus InputManagerMocked::get_input_status(KeyCode p_key_code) const {
    {
        std::scoped_lock lock(sequence_mutex);
        const Clock::time_point now = Clock::now();
        if (!current_entry_start.has_value()) {
            current_entry_start = now;
        }
        if (update_current_entry(now) && sequence[current_entry_index].key_code == p_key_code) {
            SequenceEntry& entry = sequence[current_entry_index];
            if (entry.remaining_count != 0 && --entry.remaining_count == 0) {
                current_entry_start = now;
                ++current_entry_index;
            }
            return InputStatus{.status_code = KEY_DOWN};
        }
    }
    return InputManagerRegular::get_input_status(p_key_code);
}

} // namespace WhiteBirdEngine
