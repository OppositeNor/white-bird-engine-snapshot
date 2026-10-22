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
#ifndef WBE_FILE_SINGLETON_HH
#define WBE_FILE_SINGLETON_HH

#include <atomic>
#include <cstdint>
#include <stdexcept>
namespace WhiteBirdEngine {

/**
 * @class ISingleton
 * @brief Singleton interface. This interface ensures a class that inherits it
 * has only one instance.
 *
 * @tparam T The type of the singleton.
 */
template <typename T>
class ISingleton {
public:
    ISingleton() {
        uint8_t expected_count = 0;
        if (!instance_count.compare_exchange_strong(expected_count, 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
            throw std::runtime_error("Failed to construct object: trying to construct multiple singletons.");
        }
    }

    virtual ~ISingleton() {
        instance_count.fetch_sub(1, std::memory_order_acq_rel);
    }

    ISingleton(const ISingleton&) = delete;
    ISingleton(ISingleton&&) noexcept = delete;
    ISingleton& operator=(const ISingleton&) = delete;
    ISingleton& operator=(ISingleton&&) noexcept = delete;

private:
    inline static std::atomic<uint8_t> instance_count = 0;
};

} // namespace WhiteBirdEngine

#endif
