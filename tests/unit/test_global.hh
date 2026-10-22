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
#ifndef WBE_FILE_TEST_GLOBAL_HH
#define WBE_FILE_TEST_GLOBAL_HH

#include "global/global.hh"
#include "platform/file_system/path.hh"
#include "utils/defs.hh"

#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @struct TestGlobalConfig
 * @brief Engine configuration a unit test constructs its Global with.
 *
 * Values override test_env's engine_config.toml through the engine's command-line configuration, so each test only reserves
 * the memory it needs. The shared configuration file stays sized for the rendering executables, which load the full test
 * resources. Engine pools are committed up front, so oversized pools make every test slower and heavier.
 */
struct TestGlobalConfig {
    /**
     * @brief Size of the resource manager's stack allocator in bytes.
     */
    size_t resource_mem_pool_size = WBE_MI_B(16);
    /**
     * @brief Capacity of the global allocator for allocations below 1 KiB, in bytes.
     */
    size_t global_mem_small_pool_size = WBE_MI_B(1);
    /**
     * @brief Capacity of the global allocator for allocations from 1 KiB to 1 MiB, in bytes.
     */
    size_t global_mem_medium_pool_size = WBE_MI_B(15);
    /**
     * @brief Capacity of the global allocator for allocations above 1 MiB, in bytes.
     */
    size_t global_mem_large_pool_size = WBE_MI_B(16);
};

/**
 * @brief Construct the Global of a unit test over test_env with a per-test engine configuration.
 *
 * @param p_config The engine configuration of the test.
 * @return The constructed Global, which the test must destroy before constructing another one.
 */
inline std::unique_ptr<Global> make_test_global(const TestGlobalConfig& p_config = {}) {
    std::vector<std::string> arguments{
        "wbe_unit_test",
        std::format("--resource_mem_pool_size={}", p_config.resource_mem_pool_size),
        std::format("--global_mem_small_pool_size={}", p_config.global_mem_small_pool_size),
        std::format("--global_mem_medium_pool_size={}", p_config.global_mem_medium_pool_size),
        std::format("--global_mem_large_pool_size={}", p_config.global_mem_large_pool_size),
    };
    std::vector<char*> argument_pointers;
    argument_pointers.reserve(arguments.size());
    for (std::string& argument : arguments) {
        argument_pointers.push_back(argument.data());
    }
    return std::make_unique<Global>(static_cast<int>(argument_pointers.size()), argument_pointers.data(), Path("test_env"));
}

} // namespace WhiteBirdEngine

#endif
