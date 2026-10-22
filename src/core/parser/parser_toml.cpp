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
#include "core/parser/parser_toml.hh"
#include "core/data/data_value.hh"
#include "platform/file_system/path.hh"
#include <cstdint>
#include <format>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <utility>

namespace WhiteBirdEngine {

namespace {

[[noreturn]] void throw_parse_error(const toml::parse_error& p_error, std::string_view p_source) {
    throw std::runtime_error(std::format("Failed to parse TOML from {} at line {}, column {}: {}",
        p_source,
        p_error.source().begin.line,
        p_error.source().begin.column,
        p_error.description()));
}

DataValue from_toml(const toml::node& p_node) {
    if (const toml::table* table = p_node.as_table()) {
        DataValue result = DataValue::make_map();
        for (const auto& [key, element] : *table) {
            result.set_entry(key.str(), from_toml(element));
        }
        return result;
    }
    if (const toml::array* array = p_node.as_array()) {
        DataValue result = DataValue::make_list();
        for (const toml::node& element : *array) {
            result.push_back(from_toml(element));
        }
        return result;
    }
    if (const toml::value<std::string>* string_value = p_node.as_string()) {
        return DataValue::make_string(string_value->get());
    }
    if (const toml::value<int64_t>* integer_value = p_node.as_integer()) {
        return DataValue::make_integer(integer_value->get());
    }
    if (const toml::value<double>* float_value = p_node.as_floating_point()) {
        return DataValue::make_float(float_value->get());
    }
    if (const toml::value<bool>* boolean_value = p_node.as_boolean()) {
        return DataValue::make_boolean(boolean_value->get());
    }
    throw std::runtime_error("Failed to read TOML: dates and times are not supported.");
}

void append_toml(toml::array& p_array, const DataValue& p_data);

toml::table to_toml_table(const DataValue& p_data) {
    toml::table result;
    for (const auto& [key, element] : p_data.as_map()) {
        toml::array holder;
        append_toml(holder, element);
        result.insert_or_assign(key, std::move(holder[0]));
    }
    return result;
}

void append_toml(toml::array& p_array, const DataValue& p_data) {
    switch (p_data.get_kind()) {
    case DataValueKind::NIL:
        throw std::runtime_error("Failed to write TOML: TOML cannot represent nil.");
    case DataValueKind::BOOLEAN:
        p_array.push_back(p_data.as_boolean());
        return;
    case DataValueKind::INTEGER:
        p_array.push_back(p_data.as_integer());
        return;
    case DataValueKind::FLOAT:
        p_array.push_back(p_data.as_float());
        return;
    case DataValueKind::STRING:
        p_array.push_back(p_data.as_string());
        return;
    case DataValueKind::LIST: {
        toml::array list;
        for (const DataValue& element : p_data.as_list()) {
            append_toml(list, element);
        }
        p_array.push_back(std::move(list));
        return;
    }
    case DataValueKind::MAP:
        p_array.push_back(to_toml_table(p_data));
        return;
    }
    throw std::runtime_error("Failed to write TOML: the data value has an unknown kind.");
}

} // namespace

void ParserTOML::parse(const Path& p_path) {
    toml::parse_result result = toml::parse_file(p_path.string());
    if (!result) {
        throw_parse_error(result.error(), p_path.string());
    }
    data = from_toml(result.table());
}

void ParserTOML::parse_from_buffer(std::string_view p_buffer) {
    data = read(p_buffer);
}

DataValue ParserTOML::read(std::string_view p_buffer) {
    toml::parse_result result = toml::parse(p_buffer);
    if (!result) {
        throw_parse_error(result.error(), "buffer");
    }
    return from_toml(result.table());
}

std::string ParserTOML::write(const DataValue& p_data) {
    if (p_data.get_kind() != DataValueKind::MAP) {
        throw std::runtime_error("Failed to write TOML: the document root must be a map.");
    }
    std::ostringstream stream;
    stream << to_toml_table(p_data);
    return stream.str();
}

} // namespace WhiteBirdEngine
