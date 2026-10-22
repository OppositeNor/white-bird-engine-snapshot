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
#include "core/data/data_value.hh"
#include "utils/defs.hh"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

namespace {

constexpr std::array<std::string_view, 4> VECTOR_COMPONENT_KEYS = {"x", "y", "z", "w"};

// The smallest double above every int64_t value: 2^63.
constexpr double INT64_UPPER_BOUND = 9223372036854775808.0;

} // namespace

std::string_view DataValue::get_kind_name(DataValueKind p_kind) {
    switch (p_kind) {
    case DataValueKind::NIL:
        return "nil";
    case DataValueKind::BOOLEAN:
        return "boolean";
    case DataValueKind::INTEGER:
        return "integer";
    case DataValueKind::FLOAT:
        return "float";
    case DataValueKind::STRING:
        return "string";
    case DataValueKind::LIST:
        return "list";
    case DataValueKind::MAP:
        return "map";
    }
    throw std::runtime_error("Unknown data value kind.");
}

DataValue DataValue::make_boolean(bool p_value) {
    DataValue result;
    result.value = p_value;
    return result;
}

DataValue DataValue::make_integer(int64_t p_value) {
    DataValue result;
    result.value = p_value;
    return result;
}

DataValue DataValue::make_float(double p_value) {
    DataValue result;
    result.value = p_value;
    return result;
}

DataValue DataValue::make_string(std::string_view p_value) {
    DataValue result;
    result.value = std::string(p_value);
    return result;
}

DataValue DataValue::make_list() {
    DataValue result;
    result.value = List();
    return result;
}

DataValue DataValue::make_map() {
    DataValue result;
    result.value = Map();
    return result;
}

bool DataValue::as_boolean() const {
    if (get_kind() != DataValueKind::BOOLEAN) {
        throw_kind_mismatch("boolean");
    }
    return std::get<bool>(value);
}

int64_t DataValue::as_integer() const {
    if (get_kind() != DataValueKind::INTEGER) {
        throw_kind_mismatch("integer");
    }
    return std::get<int64_t>(value);
}

double DataValue::as_float() const {
    if (get_kind() == DataValueKind::INTEGER) {
        return static_cast<double>(std::get<int64_t>(value));
    }
    if (get_kind() != DataValueKind::FLOAT) {
        throw_kind_mismatch("float");
    }
    return std::get<double>(value);
}

const std::string& DataValue::as_string() const {
    if (get_kind() != DataValueKind::STRING) {
        throw_kind_mismatch("string");
    }
    return std::get<std::string>(value);
}

const DataValue::List& DataValue::as_list() const {
    if (get_kind() != DataValueKind::LIST) {
        throw_kind_mismatch("list");
    }
    return std::get<List>(value);
}

const DataValue::Map& DataValue::as_map() const {
    if (get_kind() != DataValueKind::MAP) {
        throw_kind_mismatch("map");
    }
    return std::get<Map>(value);
}

void DataValue::push_back(DataValue p_element) {
    if (is_nil()) {
        value = List();
    }
    if (get_kind() != DataValueKind::LIST) {
        throw_kind_mismatch("list");
    }
    std::get<List>(value).push_back(std::move(p_element));
}

void DataValue::set_entry(std::string_view p_key, DataValue p_entry) {
    if (is_nil() || (get_kind() == DataValueKind::LIST && std::get<List>(value).empty())) {
        value = Map();
    }
    if (get_kind() != DataValueKind::MAP) {
        throw_kind_mismatch("map");
    }
    Map& map = std::get<Map>(value);
    auto position = std::ranges::lower_bound(map, p_key, std::less<>(), &MapEntry::first);
    if (position != map.end() && position->first == p_key) {
        position->second = std::move(p_entry);
        return;
    }
    map.emplace(position, std::string(p_key), std::move(p_entry));
}

const DataValue* DataValue::find(std::string_view p_key) const {
    if (get_kind() != DataValueKind::MAP) {
        return nullptr;
    }
    const Map& map = std::get<Map>(value);
    auto position = std::ranges::lower_bound(map, p_key, std::less<>(), &MapEntry::first);
    if (position == map.end() || position->first != p_key) {
        return nullptr;
    }
    return &position->second;
}

const DataValue& DataValue::get_entry(std::string_view p_key) const {
    const DataValue* entry = find(p_key);
    if (entry == nullptr) {
        if (get_kind() != DataValueKind::MAP) {
            throw std::runtime_error(std::format(
                "Failed to get data value entry '{}': the value is a {}, not a map.", p_key, get_kind_name(get_kind())));
        }
        throw std::runtime_error(std::format("Failed to get data value: key '{}' is missing.", p_key));
    }
    return *entry;
}

bool DataValue::operator==(const DataValue& p_other) const {
    return value == p_other.value;
}

std::vector<std::string> DataValue::get_all_keys() const {
    std::vector<std::string> keys;
    if (get_kind() != DataValueKind::MAP) {
        return keys;
    }
    const Map& map = std::get<Map>(value);
    keys.reserve(map.size());
    for (const MapEntry& entry : map) {
        keys.push_back(entry.first);
    }
    return keys;
}

int64_t DataValue::float_to_exact_integer(double p_value) {
    if (!std::isfinite(p_value)) {
        throw std::runtime_error(std::format("Failed to get data value: float {} is not finite.", p_value));
    }
    if (std::trunc(p_value) != p_value) {
        throw std::runtime_error(std::format("Failed to get data value: float {} has a fractional part.", p_value));
    }
    if (p_value < -INT64_UPPER_BOUND || p_value >= INT64_UPPER_BOUND) {
        throw std::runtime_error(std::format("Failed to get data value: float {} exceeds the signed 64-bit range.", p_value));
    }
    return static_cast<int64_t>(p_value);
}

void DataValue::set_vector_components(const float* p_components, size_t p_count) {
    WBE_DEBUG_ASSERT(p_components != nullptr && p_count <= VECTOR_COMPONENT_KEYS.size());
    Map map;
    map.reserve(p_count);
    value = std::move(map);
    for (size_t i = 0; i < p_count; ++i) {
        set_entry(VECTOR_COMPONENT_KEYS[i], make_float(p_components[i]));
    }
}

const DataValue::List& DataValue::as_list_or_empty() const {
    static const List empty_list;
    if (get_kind() == DataValueKind::MAP && std::get<Map>(value).empty()) {
        return empty_list;
    }
    return as_list();
}

void DataValue::throw_kind_mismatch(std::string_view p_expected) const {
    throw std::runtime_error(std::format("Data value kind mismatch: expected {} but got {}.", p_expected, get_kind_name(get_kind())));
}

} // namespace WhiteBirdEngine
