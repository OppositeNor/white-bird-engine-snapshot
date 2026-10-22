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
#ifndef WBE_FILE_GAME_CONFIG_HH
#define WBE_FILE_GAME_CONFIG_HH

#include "core/parser/parser_toml.hh"
#include "game_config_options.hh"
#include "generated/serializables_sd.gen.hh"
#include "platform/file_system/path.hh"
#include "utils/interface/i_singleton.hh"

namespace WhiteBirdEngine {

/**
 * @class GameConfig
 * @brief Load and handle the game configs.
 */
class GameConfig : public ISingleton<GameConfig> {
public:
    GameConfig() = delete;
    virtual ~GameConfig() override {
        singleton = nullptr;
    }
    GameConfig(const GameConfig&) = delete;
    GameConfig(GameConfig&&) noexcept = delete;
    GameConfig& operator=(const GameConfig&) = delete;
    GameConfig& operator=(GameConfig&&) noexcept = delete;

    /**
     * @brief Constructor
     *
     * @param p_config_file_path The path to the config file.
     */
    GameConfig(const Path& p_config_file_path) {
        singleton = this;
        auto parser = ParserTOML();
        parser.parse(p_config_file_path);
        SerializableSD<GameConfigOptions>::deserialize(parser.get_data(), config_options);
    }

    /**
     * @brief Get the game configurations.
     *
     * @return The game configurations.
     */
    const GameConfigOptions& get_config_options() const {
        return config_options;
    }

    /**
     * @brief Get the singleton instance.
     *
     * @return The singleton instance.
     */
    static GameConfig* get_singleton() {
        return singleton;
    }

private:
    GameConfigOptions config_options;
    inline static GameConfig* singleton = nullptr;
};

} // namespace WhiteBirdEngine

#endif
