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
#ifndef WBE_FILE_JOB_BUFFER_RING_SPSC_HH
#define WBE_FILE_JOB_BUFFER_RING_SPSC_HH

#include "core/allocator/stl_allocator.hh"
#include "core/core_utils.hh"
#include "job_buffer.hh"
#include "utils/defs.hh"
#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <semaphore>

namespace WhiteBirdEngine {

/**
 * @class JobBufferRingSPSC
 *
 * @tparam JobT The type of the job.
 * @brief Job buffer, spsc ring buffer version.
 *
 */
class JobBufferRingSPSC final : public JobBuffer<JobBufferRingSPSC> {
public:
    virtual ~JobBufferRingSPSC() override = default;
    JobBufferRingSPSC(const JobBufferRingSPSC&) = delete;
    JobBufferRingSPSC(JobBufferRingSPSC&&) noexcept = delete;
    JobBufferRingSPSC& operator=(const JobBufferRingSPSC&) = delete;
    JobBufferRingSPSC& operator=(JobBufferRingSPSC&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * @param p_allocator The allocator this buffer uses.
     * @param p_buffer_size The size of the buffer.
     */
    JobBufferRingSPSC(HeapAllocatorDefault* p_allocator, size_t p_buffer_size);

    std::function<void()> retrieve_job(bool p_block = false);
    std::future<void> add_job(std::function<void()> p_job);

    std::counting_semaphore<>& get_semaphore() {
        return semaphore;
    }

private:
    HeapAllocatorDefault* allocator;
    std::counting_semaphore<> semaphore{0};
    Vector<std::function<void()>, HeapAllocatorDefault> buffer;
    WBE_NO_FALSE_SHARING std::atomic<size_t> head;
    WBE_NO_FALSE_SHARING std::atomic<size_t> tail;
};
} // namespace WhiteBirdEngine

#endif
