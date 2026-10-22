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
#ifndef WBE_FILE_JOB_BUFFER_RING_MPSC_TEST_HH
#define WBE_FILE_JOB_BUFFER_RING_MPSC_TEST_HH

#include "core/core_utils.hh"
#include "core/job/job_buffer_ring_mpsc.hh"
#include "global/global.hh"
#include "job_test_utils.hh"
#include "test_global.hh"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

namespace WBE = WhiteBirdEngine;

class WBEJobBufferRingMPSCTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;

    // Helper to get allocator from global
    WBE::HeapAllocatorDefault* get_allocator() {
        return global->engine_core->pool_allocator;
    }
};

using WBE::JobBufferRingMPSC;

TEST_F(WBEJobBufferRingMPSCTest, ConstructorValidSize) {
    // Should succeed with size >= 2
    EXPECT_NO_THROW({ JobBufferRingMPSC buffer(get_allocator(), 2); });

    EXPECT_NO_THROW({ JobBufferRingMPSC buffer(get_allocator(), 10); });

    EXPECT_NO_THROW({ JobBufferRingMPSC buffer(get_allocator(), 1000); });
}

TEST_F(WBEJobBufferRingMPSCTest, ConstructorInvalidSize) {
    // Should throw with size < 2
    EXPECT_THROW({ JobBufferRingMPSC buffer(get_allocator(), 0); }, std::runtime_error);

    EXPECT_THROW({ JobBufferRingMPSC buffer(get_allocator(), 1); }, std::runtime_error);
}

TEST_F(WBEJobBufferRingMPSCTest, RetrieveFromEmptyBuffer) {
    JobBufferRingMPSC buffer(get_allocator(), 5);

    // Empty buffer should return an empty function.
    std::function<void()> job = buffer.retrieve_job();
    EXPECT_EQ(job, nullptr);
}

TEST_F(WBEJobBufferRingMPSCTest, AddAndRetrieveSingleJob) {
    JobBufferRingMPSC buffer(get_allocator(), 5);

    bool performed = false;
    auto job = std::function<void()>([&performed] {
        performed = true;
    });

    // Add job
    EXPECT_NO_THROW(buffer.add_job(job));

    // Retrieve job
    std::function<void()> retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);

    // Perform the job
    EXPECT_FALSE(performed);
    retrieved();
    EXPECT_TRUE(performed);

    // Buffer should be empty again
    std::function<void()> empty = buffer.retrieve_job();
    EXPECT_EQ(empty, nullptr);
}

TEST_F(WBEJobBufferRingMPSCTest, HandleTracksBufferedJobCompletion) {
    JobBufferRingMPSC buffer(get_allocator(), 5);

    bool performed = false;
    std::future<void> handle = buffer.add_job([&performed] {
        performed = true;
    });

    EXPECT_FALSE(is_future_ready(handle));

    std::function<void()> retrieved = buffer.retrieve_job();
    ASSERT_NE(retrieved, nullptr);
    retrieved();

    EXPECT_TRUE(performed);
    EXPECT_TRUE(is_future_ready(handle));
}

TEST_F(WBEJobBufferRingMPSCTest, AddAndRetrieveMultipleJobs) {
    JobBufferRingMPSC buffer(get_allocator(), 5);

    // Add multiple jobs
    std::vector<bool> performed(3, false);
    for (int i = 0; i < 3; ++i) {
        auto job = std::function<void()>([&performed, i] {
            performed[i] = true;
        });
        buffer.add_job(job);
    }

    // Retrieve jobs (should be in FIFO order)
    for (int i = 0; i < 3; ++i) {
        std::function<void()> retrieved = buffer.retrieve_job();
        EXPECT_NE(retrieved, nullptr);

        EXPECT_FALSE(performed[i]);
        retrieved();
        EXPECT_TRUE(performed[i]);
    }

    // Buffer should be empty
    std::function<void()> empty = buffer.retrieve_job();
    EXPECT_EQ(empty, nullptr);
}

TEST_F(WBEJobBufferRingMPSCTest, JobSubmissionWaitsWhenFull) {
    JobBufferRingMPSC buffer(get_allocator(), 2);
    buffer.add_job([] {
    });

    std::promise<void> submission_started_promise;
    std::future<void> submission_started_future = submission_started_promise.get_future();
    std::promise<void> submission_finished_promise;
    std::future<void> submission_finished_future = submission_finished_promise.get_future();
    std::jthread producer([&] {
        submission_started_promise.set_value();
        buffer.add_job([] {
        });
        submission_finished_promise.set_value();
    });

    submission_started_future.wait();
    EXPECT_EQ(submission_finished_future.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);

    std::function<void()> first_job = buffer.retrieve_job();
    ASSERT_NE(first_job, nullptr);
    first_job();

    EXPECT_EQ(submission_finished_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    std::function<void()> second_job = buffer.retrieve_job();
    ASSERT_NE(second_job, nullptr);
    second_job();
}

TEST_F(WBEJobBufferRingMPSCTest, RingBufferWrapAround) {
    JobBufferRingMPSC buffer(get_allocator(), 4);

    auto job1 = std::function<void()>([] {
    });
    auto job2 = std::function<void()>([] {
    });
    auto job3 = std::function<void()>([] {
    });

    buffer.add_job(job1);
    buffer.add_job(job2);
    buffer.add_job(job3);

    std::function<void()> retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);

    auto job4 = std::function<void()>([] {
    });
    EXPECT_NO_THROW(buffer.add_job(job4));

    retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);

    retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);

    retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);
}

TEST_F(WBEJobBufferRingMPSCTest, ConcurrentMultiProducerSingleConsumer) {
    constexpr int BUFFER_SIZE = 100;
    constexpr int NUM_PRODUCERS = 4;
    constexpr int JOBS_PER_PRODUCER = 250;
    constexpr int TOTAL_JOBS = NUM_PRODUCERS * JOBS_PER_PRODUCER;

    JobBufferRingMPSC buffer(get_allocator(), BUFFER_SIZE);
    std::atomic<int> produced_count{0};
    std::atomic<int> perform_count{0};
    std::vector<std::thread> producers;
    std::mutex consumed_mutex;
    std::vector<int> consumed_job_ids;
    consumed_job_ids.reserve(TOTAL_JOBS);

    producers.reserve(NUM_PRODUCERS);
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        producers.emplace_back([&, producer_id = i] {
            for (int j = 0; j < JOBS_PER_PRODUCER; ++j) {
                // Unique job ID for each job across all producers
                int job_id = producer_id * JOBS_PER_PRODUCER + j;
                auto job = std::function<void()>([job_id, &consumed_job_ids, &perform_count, &consumed_mutex] {
                    std::scoped_lock lock(consumed_mutex);
                    consumed_job_ids.push_back(job_id);
                    perform_count.fetch_add(1);
                });

                while (true) {
                    try {
                        buffer.add_job(job);
                        produced_count.fetch_add(1);
                        break;
                    } catch (const std::runtime_error&) {
                        std::this_thread::yield();
                    }
                }
            }
        });
    }

    int consumed_count = 0;

    // Consumer logic
    while (consumed_count < TOTAL_JOBS) {
        std::function<void()> job = buffer.retrieve_job();
        if (job != nullptr) {
            job();
            consumed_count++;
        } else {
            // If all producers are done and buffer is empty, break
            // A simple check, in a real scenario might need a more robust signal
            if ((produced_count.load() == TOTAL_JOBS) && (buffer.retrieve_job() == nullptr))
            // to avoid busy-waiting
            {
                break;
            }

            std::this_thread::yield();
        }
    }

    for (auto& p : producers) {
        p.join();
    }

    EXPECT_EQ(produced_count.load(), TOTAL_JOBS);
    EXPECT_EQ(consumed_count, TOTAL_JOBS);
    EXPECT_EQ(perform_count.load(), TOTAL_JOBS);

    // Verify that all jobs were consumed, regardless of order
    std::sort(consumed_job_ids.begin(), consumed_job_ids.end());
    std::vector<int> expected_ids(TOTAL_JOBS);
    std::iota(expected_ids.begin(), expected_ids.end(), 0);

    EXPECT_EQ(consumed_job_ids, expected_ids);
}

TEST_F(WBEJobBufferRingMPSCTest, StressTestManyProducers) {
    constexpr int BUFFER_SIZE = 200;
    constexpr int NUM_PRODUCERS = 10;
    constexpr int JOBS_PER_PRODUCER = 2000;
    constexpr int TOTAL_JOBS = NUM_PRODUCERS * JOBS_PER_PRODUCER;

    JobBufferRingMPSC buffer(get_allocator(), BUFFER_SIZE);
    std::atomic<int> produced_count{0};
    std::atomic<int> perform_count{0};
    std::vector<std::thread> producers;
    std::mutex consumed_mutex;
    std::vector<int> consumed_job_ids;
    consumed_job_ids.reserve(TOTAL_JOBS);

    producers.reserve(NUM_PRODUCERS);
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        producers.emplace_back([&, producer_id = i] {
            for (int j = 0; j < JOBS_PER_PRODUCER; ++j) {
                int job_id = producer_id * JOBS_PER_PRODUCER + j;
                auto job = std::function<void()>([job_id, &consumed_job_ids, &perform_count, &consumed_mutex] {
                    std::scoped_lock lock(consumed_mutex);
                    consumed_job_ids.push_back(job_id);
                    perform_count.fetch_add(1);
                });

                while (true) {
                    try {
                        buffer.add_job(job);
                        produced_count.fetch_add(1);
                        break;
                    } catch (const std::runtime_error&) {
                        std::this_thread::yield();
                    }
                }
            }
        });
    }

    int consumed_count = 0;

    while (consumed_count < TOTAL_JOBS) {
        std::function<void()> job = buffer.retrieve_job();
        if (job != nullptr) {
            job();
            consumed_count++;
        } else if (produced_count.load() == TOTAL_JOBS) {
            // All jobs produced, but buffer might be temporarily empty.
            // One final check to ensure we don't exit prematurely.
            job = buffer.retrieve_job();
            if (job == nullptr) {
                break;
            }
            job();
            consumed_count++;
        } else {
            std::this_thread::yield();
        }
    }

    for (auto& p : producers) {
        p.join();
    }

    EXPECT_EQ(produced_count.load(), TOTAL_JOBS);
    EXPECT_EQ(consumed_count, TOTAL_JOBS);
    EXPECT_EQ(perform_count.load(), TOTAL_JOBS);

    // Verify that all jobs were consumed
    std::sort(consumed_job_ids.begin(), consumed_job_ids.end());
    std::vector<int> expected_ids(TOTAL_JOBS);
    std::iota(expected_ids.begin(), expected_ids.end(), 0);

    EXPECT_EQ(consumed_job_ids, expected_ids);
}

#endif
