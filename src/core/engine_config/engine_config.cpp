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
#include "core/engine_config/engine_config.hh"
#include "core/engine_config/engine_config_options.hh"
#include "core/parser/parser_toml.hh"
#include "generated/cla_configuration.gen.hh"
#include "generated/serializables_sd.gen.hh"
#include "platform/file_system/path.hh"
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <cstdint>

namespace WhiteBirdEngine {

void EngineConfig::parse_cla(uint32_t p_argc, char* p_argv[]) {
    if (p_argc == 0 || p_argv == nullptr) {
        return;
    }

    if (p_argv[0] != nullptr) {
        config_options.utility_name = p_argv[0];
    }

    boost::program_options::options_description options("White Bird Engine options");
    CLAConfiguration<EngineConfigOptions>::add_options(options);

    const auto parsed =
        boost::program_options::command_line_parser(static_cast<int>(p_argc), p_argv).options(options).allow_unregistered().run();
    boost::program_options::variables_map arguments;
    boost::program_options::store(parsed, arguments);
    boost::program_options::notify(arguments);

    CLAConfiguration<EngineConfigOptions>::apply(arguments, config_options);
}

void EngineConfig::parse_config_file(const Path& p_path) {
    auto parser = ParserTOML();
    parser.parse(p_path);
    SerializableSD<EngineConfigOptions>::deserialize(parser.get_data(), config_options);
}

} // namespace WhiteBirdEngine
