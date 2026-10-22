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
#include "function/engine_function.hh"
#include "core/engine_core.hh"
#include "core/memory/unique.hh"
#include "function/event/event_manager.hh"
#include "function/game_config/game_config.hh"
#include "platform/file_system/path.hh"

namespace WhiteBirdEngine {

EngineFunction::EngineFunction(const Path& p_game_config_file_path) {
    singleton = this;
    game_config = make_unique<GameConfig>(EngineCore::get_singleton()->pool_allocator, p_game_config_file_path);
    event_manager = make_unique<EventManager>(EngineCore::get_singleton()->pool_allocator, EngineCore::get_singleton()->pool_allocator);
}

EngineFunction::~EngineFunction() {
    event_manager.reset();
    singleton = nullptr;
}
} // namespace WhiteBirdEngine
