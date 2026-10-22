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
#ifndef WBE_FILE_JOB_BUFFER_RING_MPSC_HH
#define WBE_FILE_JOB_BUFFER_RING_MPSC_HH

#include "core/allocator/stl_allocator.hh"
#include "core/core_utils.hh"
#include "core/job/job_buffer.hh"
#include "utils/defs.hh"
#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <semaphore>
namespace WhiteBirdEngine {

/**
 * @class JobBufferRingMPSC
 *
 * @tparam JobT The type of the job.
 * @brief Job buffer, mpsc ring buffer version.
 *
 */
class JobBufferRingMPSC final : public JobBuffer<JobBufferRingMPSC> {
public:
    JobBufferRingMPSC() = delete;
    virtual ~JobBufferRingMPSC() = default;
    JobBufferRingMPSC(const JobBufferRingMPSC&) = delete;
    JobBufferRingMPSC(JobBufferRingMPSC&&) noexcept = delete;
    JobBufferRingMPSC& operator=(const JobBufferRingMPSC&) = delete;
    JobBufferRingMPSC& operator=(JobBufferRingMPSC&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * @param p_allocator The allocator this buffer uses.
     * @param p_buffer_size The size of the buffer.
     */
    JobBufferRingMPSC(HeapAllocatorDefault* p_allocator, size_t p_buffer_size);

    std::function<void()> retrieve_job(bool p_block = false);

    /**
     * @brief Add a job, waiting until the consumer frees capacity when the buffer is full.
     *
     * @param p_job The job to add.
     * @return A future that becomes ready after the job finishes.
     */
    std::future<void> add_job(std::function<void()> p_job);

    /**
     * @brief Get the semaphore.
     *
     * @return The semaphore.
     */
    std::counting_semaphore<>& get_semaphore() {
        return semaphore;
    }

private:
    HeapAllocatorDefault* allocator;
    std::counting_semaphore<> semaphore{0};
    std::counting_semaphore<> available_slot_semaphore{0};
    Vector<std::function<void()>, HeapAllocatorDefault> buffer;
    std::mutex producer_mutex;
    WBE_NO_FALSE_SHARING std::atomic<size_t> head;
    WBE_NO_FALSE_SHARING std::atomic<size_t> tail;
};

} // namespace WhiteBirdEngine
#endif
