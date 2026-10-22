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
#ifndef WBE_FILE_ENGINE_CORE_HH
#define WBE_FILE_ENGINE_CORE_HH

#include "core/allocator/stack_allocator.hh"
#include "core/clock/clock.hh"
#include "core/core_utils.hh"
#include "core/engine_config/engine_config.hh"
#include "core/logging/log_stream.hh"
#include "core/logging/logging_manager.hh"
#include "platform/file_system/file_system.hh"
#include "platform/file_system/path.hh"
#include "utils/interface/i_singleton.hh"
#include <cstdint>
#include <ostream>

namespace WhiteBirdEngine {

using HeapAllocatorGlobal = HeapAllocatorDefault;

/**
 * @class EngineCore
 * @brief Core singleton of the engine. Contains all singletons of the core
 * layer.
 *
 */
class EngineCore : public ISingleton<EngineCore> {
public:
    EngineCore() = delete;
    virtual ~EngineCore() override;
    EngineCore(const EngineCore&) = delete;
    EngineCore(EngineCore&&) noexcept = delete;
    EngineCore& operator=(const EngineCore&) = delete;
    EngineCore& operator=(EngineCore&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * @param p_argc argc.
     * @param p_argv argv.
     */
    EngineCore(uint32_t p_argc, char* p_argv[]);

    /**
     * @brief Constructor.
     *
     * @param p_argc argc.
     * @param p_argv argv.
     * @param p_root_dir The root directory.
     */
    EngineCore(uint32_t p_argc, char* p_argv[], const Path& p_root_dir);

    /**
     * @brief Get the singleton.
     *
     * @return The singleton of engine core.
     */
    static EngineCore* get_singleton() {
        return singleton;
    }

    /**
     * @brief The global clock starts recording when the engine core is
     * constructed.
     */
    Clock* global_clock = nullptr;

    /**
     * @brief File system.
     */
    FileSystem* file_system = nullptr;
    /**
     * @brief Engine configuration.
     */
    EngineConfig* engine_config = nullptr;
    /**
     * @brief Global pool allocator.
     */
    HeapAllocatorGlobal* pool_allocator = nullptr;
    /**
     * @brief Manager for logs.
     */
    LoggingManager<LogStream, std::ostream>* stdio_logging_manager = nullptr;
    /**
     * @brief Manager for profiling.
     */
    class ProfilingManager* profiling_manager = nullptr;
    /**
     * @brief Manager for the labels.
     */
    class LabelManager* label_manager = nullptr;
    /**
     * @brief Manager for type UUIDs.
     */
    class TypeUUIDManager* type_uuid_manager = nullptr;
    /**
     * @brief Factory for constructing serializable objects by reflected name.
     */
    class SerializableFactory* serializable_factory = nullptr;

private:
    void initialize(uint32_t p_argc, char* p_argv[]);
    inline static EngineCore* singleton;
};

/**
 * @brief Shortcut function to get the global allocator.
 *
 * @return The global allocator;
 */
inline HeapAllocatorGlobal* global_allocator() {
    return EngineCore::get_singleton()->pool_allocator;
}

} // namespace WhiteBirdEngine

#endif
