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
#ifndef WBE_FILE_ENGINE_CONFIG_HH
#define WBE_FILE_ENGINE_CONFIG_HH

#include "core/engine_config/engine_config_options.hh"
#include "platform/file_system/path.hh"
#include "utils/interface/i_singleton.hh"
#include <cstdint>
#include <string>

namespace WhiteBirdEngine {

/**
 * @class EngineConfig
 * @brief Engine configuration class.
 *
 */
class EngineConfig final : public ISingleton<EngineConfig> {
public:
    /**
     * @brief Constructor.
     *
     * @param p_config_file_path The path to the config file.
     * @param p_argc argc
     * @param p_argv argv
     */
    EngineConfig(const Path& p_config_file_path, uint32_t p_argc, char* p_argv[]) {
        singleton = this;
        parse_config_file(p_config_file_path);
        if (p_argc > 0) {
            parse_cla(p_argc, p_argv);
        }
    }
    virtual ~EngineConfig() {
        singleton = nullptr;
    }
    EngineConfig(const EngineConfig&) = delete;
    EngineConfig(EngineConfig&&) = delete;
    EngineConfig& operator=(const EngineConfig&) = delete;
    EngineConfig& operator=(EngineConfig&&) = delete;

    /**
     * @brief Get the configuration options.
     *
     * @return The configuration options.
     */
    static EngineConfig* get_singleton() {
        return singleton;
    }

    /**
     * @brief Get the configuration options.
     *
     * @return The configuration options.
     */
    const EngineConfigOptions& get_config_options() {
        return config_options;
    }

private:
    EngineConfigOptions config_options;
    inline static EngineConfig* singleton = nullptr;

    // Defined in the source file, so this header does not include generated code. engine_core.hh includes it, and generated
    // headers include headers from every layer.
    void parse_cla(uint32_t p_argc, char* p_argv[]);
    void parse_config_file(const Path& p_path);
};
} // namespace WhiteBirdEngine

#endif
