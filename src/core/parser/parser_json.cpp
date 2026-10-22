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
#include "core/parser/parser_json.hh"
#include "core/data/data_value.hh"
#include "platform/file_system/path.hh"
#include <cstdint>
#include <exception>
#include <format>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace WhiteBirdEngine {

namespace {

using Json = nlohmann::json;

DataValue from_json(const Json& p_json) {
    switch (p_json.type()) {
    case Json::value_t::null:
    case Json::value_t::discarded:
        return DataValue();
    case Json::value_t::boolean:
        return DataValue::make_boolean(p_json.get<bool>());
    case Json::value_t::number_integer:
        return DataValue::make_integer(p_json.get<int64_t>());
    case Json::value_t::number_unsigned: {
        const uint64_t integer = p_json.get<uint64_t>();
        if (integer > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            throw std::runtime_error(std::format("Failed to read JSON: integer {} exceeds the signed 64-bit range.", integer));
        }
        return DataValue::make_integer(static_cast<int64_t>(integer));
    }
    case Json::value_t::number_float:
        return DataValue::make_float(p_json.get<double>());
    case Json::value_t::string:
        return DataValue::make_string(p_json.get_ref<const std::string&>());
    case Json::value_t::array: {
        DataValue result = DataValue::make_list();
        for (const Json& element : p_json) {
            result.push_back(from_json(element));
        }
        return result;
    }
    case Json::value_t::object: {
        DataValue result = DataValue::make_map();
        for (const auto& [key, element] : p_json.items()) {
            result.set_entry(key, from_json(element));
        }
        return result;
    }
    case Json::value_t::binary:
        throw std::runtime_error("Failed to read JSON: binary values are not supported.");
    }
    throw std::runtime_error("Failed to read JSON: the value has an unknown type.");
}

Json to_json(const DataValue& p_data) {
    switch (p_data.get_kind()) {
    case DataValueKind::NIL:
        return nullptr;
    case DataValueKind::BOOLEAN:
        return p_data.as_boolean();
    case DataValueKind::INTEGER:
        return p_data.as_integer();
    case DataValueKind::FLOAT:
        return p_data.as_float();
    case DataValueKind::STRING:
        return p_data.as_string();
    case DataValueKind::LIST: {
        Json result = Json::array();
        for (const DataValue& element : p_data.as_list()) {
            result.push_back(to_json(element));
        }
        return result;
    }
    case DataValueKind::MAP: {
        Json result = Json::object();
        for (const auto& [key, element] : p_data.as_map()) {
            result[key] = to_json(element);
        }
        return result;
    }
    }
    throw std::runtime_error("Failed to write JSON: the data value has an unknown kind.");
}

} // namespace

void ParserJSON::parse(const Path& p_path) {
    std::ifstream file(p_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file at path: " + p_path.string());
    }
    try {
        data = from_json(Json::parse(file));
    } catch (const std::exception& p_error) {
        throw std::runtime_error("Failed to parse JSON file at path: " + p_path.string() + ". Error: " + p_error.what());
    }
}

void ParserJSON::parse_from_buffer(std::string_view p_buffer) {
    data = read(p_buffer);
}

DataValue ParserJSON::read(std::string_view p_buffer) {
    Json json;
    try {
        json = Json::parse(p_buffer);
    } catch (const std::exception& p_error) {
        throw std::runtime_error("Failed to parse JSON from buffer. Error: " + std::string(p_error.what()));
    }
    return from_json(json);
}

std::string ParserJSON::write(const DataValue& p_data, int p_indent) {
    return to_json(p_data).dump(p_indent);
}

} // namespace WhiteBirdEngine
