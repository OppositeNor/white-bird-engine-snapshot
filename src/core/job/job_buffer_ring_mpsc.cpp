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
#include "core/job/job_buffer_ring_mpsc.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace WhiteBirdEngine {

JobBufferRingMPSC::JobBufferRingMPSC(HeapAllocatorDefault* p_allocator, size_t p_buffer_size)
    : allocator(p_allocator), buffer(p_allocator), head(0), tail(0) {
    if (p_buffer_size <= 1) {
        throw std::runtime_error("Buffer has to be at least size 2.");
    }
    buffer.resize(p_buffer_size);
    available_slot_semaphore.release(static_cast<std::ptrdiff_t>(p_buffer_size - 1));
}

std::future<void> JobBufferRingMPSC::add_job(std::function<void()> p_job) {
    Ref<std::promise<void>> completion_promise = make_ref<std::promise<void>>(allocator);
    std::future<void> completion_future = completion_promise->get_future();
    std::function<void()> wrapped_job = [job = std::move(p_job), completion_promise]() mutable {
        try {
            job();
            completion_promise->set_value();
        } catch (...) {
            // The returned future is backed by this promise, so job failures must be stored here instead of escaping as
            // a broken-promise state with the original exception lost.
            completion_promise->set_exception(std::current_exception());
        }
    };

    // Block if the buffer is full.
    available_slot_semaphore.acquire();
    std::scoped_lock lock(producer_mutex);
    size_t head_l = head.load(std::memory_order_relaxed);
    size_t next = ring_increment(head_l, buffer.size());
    WBE_DEBUG_ASSERT(next != tail.load(std::memory_order_acquire));
    buffer[head_l] = std::move(wrapped_job);
    head.store(next, std::memory_order_relaxed);
    semaphore.release();
    return completion_future;
}

std::function<void()> JobBufferRingMPSC::retrieve_job(bool p_block) {
    if (p_block) {
        semaphore.acquire();
    } else if (!semaphore.try_acquire()) {
        return std::function<void()>();
    }
    size_t tail_l = tail.load(std::memory_order_acquire);

    std::function<void()> result = std::move(buffer[tail_l]);
    buffer[tail_l] = nullptr;
    tail.store(ring_increment(tail_l, buffer.size()), std::memory_order_release);
    available_slot_semaphore.release();
    return result;
}

} // namespace WhiteBirdEngine
