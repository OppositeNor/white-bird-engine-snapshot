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
#ifndef WBE_FILE_WORKER_HH
#define WBE_FILE_WORKER_HH

#include "core/job/job_handler.hh"
#include "platform/file_system/path.hh"
#include "utils/interface/i_singleton.hh"
#include "utils/utils.hh"
#include <BS_thread_pool.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class Worker
 * @brief For handling tasks that are expensive, such as an IO task.
 *
 */
class Worker final : public ISingleton<Worker>, public JobHandler {
public:
    Worker(const Worker&) = delete;
    Worker(Worker&&) noexcept = delete;
    Worker& operator=(const Worker&) = delete;
    Worker& operator=(Worker&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * Creates one worker thread. Engine applications should use the configurable constructor.
     */
    Worker();

    /**
     * @brief Construct a worker backed by a thread pool.
     *
     * @param p_thread_count The number of threads in the pool.
     */
    explicit Worker(size_t p_thread_count);

    virtual ~Worker() override;

    virtual std::future<void> add_job(std::function<void()> p_job) override;

    /**
     * @brief Submit a value-producing task to the worker pool.
     * @tparam TFunction Callable type.
     * @param p_task Task to execute.
     * @return A future containing the task result.
     */
    template <typename TFunction>
    auto submit_task(TFunction&& p_task) -> std::future<std::invoke_result_t<TFunction>> {
        return thread_pool.submit_task(std::forward<TFunction>(p_task));
    }

    static Worker* get_singleton() {
        return singleton;
    }

    /**
     * @brief Get the number of worker threads.
     *
     * @return The number of worker threads.
     */
    size_t get_thread_count() const {
        return thread_pool.get_thread_count();
    }

    /**
     * @brief Read data from a file.
     *
     * @param p_path The path to the file to read.
     * @return A future containing the file data.
     */
    std::future<std::vector<char>> read_file(const Path& p_path) {
        return read_binary_file<char>(p_path);
    }

    /**
     * @brief Read a text file.
     *
     * @param p_path The path to the text file.
     * @return A future containing the file text.
     */
    std::future<std::string> read_text_file(const Path& p_path);

    /**
     * @brief Read a binary file.
     *
     * @tparam T The type of the file's data.
     * @param p_path The path to the file.
     * @return A future containing the file data.
     */
    template <typename T = uint8_t>
    std::future<std::vector<T>> read_binary_file(const Path& p_path) {
        return thread_pool.submit_task([path = p_path]() {
            return load_binary_file<T>(path.string().c_str());
        });
    }

    /**
     * @brief Wait for all queued and running jobs to finish.
     */
    void wait();

private:
    inline static Worker* singleton = nullptr;
    BS::thread_pool<> thread_pool;
};

} // namespace WhiteBirdEngine
#endif
