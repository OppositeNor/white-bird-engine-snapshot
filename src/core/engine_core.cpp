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
#include "core/engine_core.hh"
#include "core/clock/clock.hh"
#include "core/engine_config/engine_config.hh"
#include "core/logging/log_stream.hh"
#include "core/logging/logging_manager.hh"
#include "core/profiling/profiling_manager.hh"
#include "generated/label_manager.gen.hh"
#include "generated/serializable_factory.gen.hh"
#include "generated/type_uuid.gen.hh"
#include "platform/file_system/file_system.hh"
#include "platform/file_system/path.hh"
#include <cstdint>
#include <iostream>

namespace WhiteBirdEngine {

EngineCore::~EngineCore() {
    delete serializable_factory;
    delete type_uuid_manager;
    delete label_manager;
    delete profiling_manager;
    delete file_system;
    delete pool_allocator;
    delete stdio_logging_manager;
    delete engine_config;
    delete global_clock;
    singleton = nullptr;
}

EngineCore::EngineCore(uint32_t p_argc, char* p_argv[]) {
    global_clock = new Clock();
    file_system = new FileSystem();
    initialize(p_argc, p_argv);
}

EngineCore::EngineCore(uint32_t p_argc, char* p_argv[], const Path& p_root_dir) {
    global_clock = new Clock();
    file_system = new FileSystem(p_root_dir);
    initialize(p_argc, p_argv);
}

void EngineCore::initialize(uint32_t p_argc, char* p_argv[]) {
    engine_config = new EngineConfig((file_system->get_config_directory() / "engine_config.toml"), p_argc, p_argv);
    const EngineConfigOptions& config_options = engine_config->get_config_options();
    pool_allocator = new HeapAllocatorGlobal(HeapAllocatorGlobal::make_default_size_classes(
        config_options.global_mem_small_pool_size, config_options.global_mem_medium_pool_size, config_options.global_mem_large_pool_size));
    stdio_logging_manager = new LoggingManager<LogStream, std::ostream>(std::cout);
    profiling_manager = new ProfilingManager();
    label_manager = new LabelManager();
    type_uuid_manager = new TypeUUIDManager();
    singleton = this;
    serializable_factory = new SerializableFactory();
}

} // namespace WhiteBirdEngine
