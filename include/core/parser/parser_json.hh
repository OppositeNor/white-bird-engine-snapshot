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
#ifndef WBE_FILE_PARSER_JSON_HH
#define WBE_FILE_PARSER_JSON_HH

#include "core/data/data_value.hh"
#include "core/parser/parser.hh"
#include "platform/file_system/path.hh"
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class ParserJSON
 * @brief JSON support for DataValue: parses JSON documents into a DataValue and writes a DataValue as JSON.
 *
 * Objects become maps, arrays lists, and null nil; integers become signed 64-bit integers, and an integer outside that range
 * is rejected. Copyable and movable; not thread-safe.
 */
class ParserJSON final : public Parser {
public:
    ParserJSON() = default;
    virtual ~ParserJSON() override = default;
    ParserJSON(const ParserJSON&) = default;
    ParserJSON(ParserJSON&&) noexcept = default;
    ParserJSON& operator=(const ParserJSON&) = default;
    ParserJSON& operator=(ParserJSON&&) noexcept = default;

    virtual void parse(const Path& p_path) override;

    virtual void parse_from_buffer(std::string_view p_buffer) override;

    /**
     * @brief Parse a JSON document into a DataValue.
     * @throws std::runtime_error If the buffer is not valid JSON or holds an integer outside the signed 64-bit range.
     */
    static DataValue read(std::string_view p_buffer);

    /**
     * @brief Write a DataValue as a JSON document.
     * @param p_data The data; nil becomes null.
     * @param p_indent Spaces per indentation level; negative writes the most compact form on one line.
     */
    static std::string write(const DataValue& p_data, int p_indent = -1);
};

} // namespace WhiteBirdEngine

#endif
