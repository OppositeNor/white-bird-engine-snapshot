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
#ifndef WBE_FILE_PARSER_TOML_HH
#define WBE_FILE_PARSER_TOML_HH

#include "core/data/data_value.hh"
#include "core/parser/parser.hh"
#include "platform/file_system/path.hh"
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class ParserTOML
 * @brief TOML support for DataValue: parses TOML documents into a DataValue and writes a DataValue as TOML.
 *
 * Tables become maps and arrays lists. Dates and times have no DataValue kind and are rejected. A written document's root
 * must be a map, and TOML cannot represent nil. Copyable and movable; not thread-safe.
 */
class ParserTOML final : public Parser {
public:
    ParserTOML() = default;
    virtual ~ParserTOML() override = default;
    ParserTOML(const ParserTOML&) = default;
    ParserTOML(ParserTOML&&) noexcept = default;
    ParserTOML& operator=(const ParserTOML&) = default;
    ParserTOML& operator=(ParserTOML&&) noexcept = default;

    virtual void parse(const Path& p_path) override;

    virtual void parse_from_buffer(std::string_view p_buffer) override;

    /**
     * @brief Parse a TOML document into a DataValue map.
     * @throws std::runtime_error If the buffer is not valid TOML or holds a date or time.
     */
    static DataValue read(std::string_view p_buffer);

    /**
     * @brief Write a DataValue map as a TOML document.
     * @throws std::runtime_error If p_data is not a map, or holds nil anywhere.
     */
    static std::string write(const DataValue& p_data);
};

} // namespace WhiteBirdEngine

#endif
