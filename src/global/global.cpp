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
#include "global/global.hh"
#include "core/engine_core.hh"
#include "core/memory/unique.hh"
#include "function/engine_function.hh"
#include "platform/file_system/path.hh"
#include "resource/engine_resource.hh"
#include <cstdint>

namespace WhiteBirdEngine {

Global::Global(int p_argc, char* p_argv[]) {
    singleton = this;
    engine_core = new EngineCore(static_cast<uint32_t>(p_argc), p_argv);
    auto* file_system = Global::get_singleton()->engine_core->file_system;
    engine_resource = make_unique<EngineResource>(global_allocator());
    engine_function = make_unique<EngineFunction>(global_allocator(), (file_system->get_config_directory() / "game_config.toml"));
}

Global::Global(int p_argc, char* p_argv[], const Path& p_root_dir) {
    singleton = this;
    engine_core = new EngineCore(static_cast<uint32_t>(p_argc), p_argv, p_root_dir);
    auto* file_system = Global::get_singleton()->engine_core->file_system;
    engine_resource = make_unique<EngineResource>(global_allocator());
    engine_function = make_unique<EngineFunction>(global_allocator(), (file_system->get_config_directory() / "game_config.toml"));
}

Global::~Global() {
    engine_resource.reset();
    engine_function.reset();
    delete engine_core;
    singleton = nullptr;
}
} // namespace WhiteBirdEngine
