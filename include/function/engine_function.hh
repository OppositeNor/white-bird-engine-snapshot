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
#ifndef WBE_FILE_ENGINE_FUNCTION_HH
#define WBE_FILE_ENGINE_FUNCTION_HH

#include "core/allocator/i_allocator.hh"
#include "core/memory/unique.hh"
#include "function/event/event_manager.hh"
#include "function/game_config/game_config.hh"
#include "platform/file_system/path.hh"
#include "utils/defs.hh"
#include "utils/interface/i_singleton.hh"

namespace WhiteBirdEngine {

/**
 * @class EngineFunction
 * @brief The manager for the function layer.
 *
 */
class EngineFunction final : public ISingleton<EngineFunction> {
public:
    EngineFunction() = delete;
    virtual ~EngineFunction() override;
    EngineFunction(const EngineFunction&) = delete;
    EngineFunction(EngineFunction&&) noexcept = delete;
    EngineFunction& operator=(const EngineFunction&) = delete;
    EngineFunction& operator=(EngineFunction&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * @param p_game_config_file_path The path to the game configurations.
     */
    explicit EngineFunction(const Path& p_game_config_file_path);

    /**
     * @brief Get the singleton instance.
     *
     * @return The singleton instance.
     */
    static EngineFunction* get_singleton() {
        return singleton;
    }

    /**
     * @brief The game configuration.
     */
    Unique<GameConfig> game_config = MEM_NULL;
    /**
     * @brief The engine-wide event manager, also reachable through EventManager::get_singleton().
     */
    Unique<EventManager> event_manager = MEM_NULL;

private:
    inline static EngineFunction* singleton;
};

} // namespace WhiteBirdEngine

#endif
