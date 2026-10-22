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
#ifndef WBE_FILE_ENGINE_CONFIG_OPTIONS_HH
#define WBE_FILE_ENGINE_CONFIG_OPTIONS_HH

#include "core/reflection/reflection_defs.hh"
#include "core/reflection/serializable.hh"
#include "utils/defs.hh"
#include <cstddef>
#include <cstdint>
#include <string>
namespace WhiteBirdEngine {
/**
 * @class EngineConfigOptions
 * @brief Engine configurations.
 *
 */
struct WBE_META(WBE_CONFIG_OPTION, WBE_CLA_CONFIGURATION) EngineConfigOptions final : public Serializable {

    WBE_DECL_SERIALIZABLE(EngineConfigOptions)

    /**
     * @brief Name of the engine.
     */
    const char* engine_name = "White Bird Engine";
    /**
     * @brief Engine version major.
     */
    static constexpr uint32_t VERSION_MAJOR = 0;
    /**
     * @brief Engine version minor.
     */
    static constexpr uint32_t VERSION_MINOR = 0;
    /**
     * @brief Engine version patch.
     */
    static constexpr uint32_t VERSION_PATCH = 1;

    /**
     * @brief The size of the tick stack.
     */
    WBE_META(WBE_REFLECT)
    size_t per_tick_stack_size = WBE_KI_B(64);
    /**
     * @brief The capacity of the global memory pool for allocations below 1 KiB.
     */
    WBE_META(WBE_REFLECT)
    size_t global_mem_small_pool_size = WBE_MI_B(1);
    /**
     * @brief The capacity of the global memory pool for allocations from 1 KiB to 1 MiB.
     */
    WBE_META(WBE_REFLECT)
    size_t global_mem_medium_pool_size = WBE_MI_B(15);
    /**
     * @brief The capacity of the global memory pool for allocations above 1 MiB.
     */
    WBE_META(WBE_REFLECT)
    size_t global_mem_large_pool_size = WBE_MI_B(512);
    /**
     * @brief The size of the thread memory pool.
     */
    WBE_META(WBE_REFLECT)
    size_t thread_mem_pool_size = WBE_KI_B(16);
    /**
     * @brief The size of the thread memory pool.
     */
    WBE_META(WBE_REFLECT)
    size_t resource_mem_pool_size = WBE_MI_B(64);
    /**
     * @brief The number of worker threads. Zero selects the available CPU thread count after reserving render and logic threads.
     */
    WBE_META(WBE_REFLECT)
    size_t worker_thread_count = 0;

    /**
     * @brief The utility name while running the program.
     */
    WBE_META(WBE_REFLECT)
    std::string utility_name;
};
} // namespace WhiteBirdEngine

#endif
