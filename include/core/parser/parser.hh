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
#ifndef WBE_FILE_PARSER_HH
#define WBE_FILE_PARSER_HH

#include "core/data/data_value.hh"
#include "platform/file_system/path.hh"
#include <string>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class Parser
 * @brief Base of the text-format parsers: reads a document into a DataValue tree, which the accessors then read.
 *
 * A parser only supports a text format; the parsed data, and every conversion to and from C++ types, is the format-neutral
 * DataValue. A parse replaces the held data. Copyable and movable; not thread-safe.
 */
class Parser {
public:
    Parser() = default;
    virtual ~Parser() = default;
    Parser(const Parser&) = default;
    Parser(Parser&&) noexcept = default;
    Parser& operator=(const Parser&) = default;
    Parser& operator=(Parser&&) noexcept = default;

    /**
     * @brief Parse a file.
     * @throws std::runtime_error If the file cannot be read or is not a valid document of the format.
     */
    virtual void parse(const Path& p_path) = 0;

    /**
     * @brief Parse a document held in memory.
     * @throws std::runtime_error If the buffer is not a valid document of the format.
     */
    virtual void parse_from_buffer(std::string_view p_buffer) = 0;

    /**
     * @brief Get the parsed data.
     */
    const DataValue& get_data() const {
        return data;
    }

    /**
     * @brief Get the parsed data.
     */
    DataValue& get_data() {
        return data;
    }

    /**
     * @brief Convert the entry under a key of the parsed data; see DataValue::get_value().
     */
    template <typename T>
    T get_value(std::string_view p_key) const {
        return data.get_value<T>(p_key);
    }

    /**
     * @brief Convert the entry under a key of the parsed data into p_value; see DataValue::get_value().
     */
    template <typename T>
    void get_value(std::string_view p_key, T& p_value) const {
        data.get_value<T>(p_key, p_value);
    }

    /**
     * @brief Convert the parsed data; see DataValue::get().
     */
    template <typename T>
    T get() const {
        return data.get<T>();
    }

    /**
     * @brief Convert the parsed data into p_value; see DataValue::get().
     */
    template <typename T>
    void get(T& p_value) const {
        data.get<T>(p_value);
    }

    /**
     * @brief Get the keys of the parsed data; see DataValue::get_all_keys().
     */
    std::vector<std::string> get_all_keys() const {
        return data.get_all_keys();
    }

    /**
     * @brief Check whether the parsed data has an entry for a key; see DataValue::contains().
     */
    bool contains(std::string_view p_key) const {
        return data.contains(p_key);
    }

protected:
    DataValue data;
};

} // namespace WhiteBirdEngine

#endif
