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
#ifndef WBE_FILE_JOB_HANDLER_HH
#define WBE_FILE_JOB_HANDLER_HH

#include <functional>
#include <future>
namespace WhiteBirdEngine {

inline std::future<void> create_completed_job_future() {
    std::promise<void> promise;
    std::future<void> future = promise.get_future();
    promise.set_value();
    return future;
}

/**
 * @class JobHandler
 * @brief Interface for classes that handles jobs.
 *
 */
class JobHandler {
public:
    JobHandler() = default;
    virtual ~JobHandler() = default;
    JobHandler(const JobHandler&) = delete;
    JobHandler(JobHandler&&) noexcept = delete;
    JobHandler& operator=(const JobHandler&) = delete;
    JobHandler& operator=(JobHandler&&) noexcept = delete;

    /**
     * @brief Add a job to the handler.
     *
     * @param p_job The job to add.
     */
    virtual std::future<void> add_job(std::function<void()> p_job) = 0;
};

} // namespace WhiteBirdEngine

#endif
