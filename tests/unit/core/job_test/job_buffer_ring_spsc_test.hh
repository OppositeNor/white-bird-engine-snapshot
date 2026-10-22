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
#ifndef WBE_FILE_JOB_BUFFER_RING_SPSC_TEST_HH
#define WBE_FILE_JOB_BUFFER_RING_SPSC_TEST_HH

#include "core/core_utils.hh"
#include "core/job/job_buffer_ring_spsc.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_defs.hh"
#include "function/render_dummy/renderer_dummy.hh"
#include "function/render_software/renderer_software.hh"
#include "global/global.hh"
#include "job_test_utils.hh"
#include "test_global.hh"
#include "utils/defs.hh"
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace WBE = WhiteBirdEngine;

class WBEJobBufferRingSPSCTest : public ::testing::Test {
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

using WBE::JobBufferRingSPSC;

TEST_F(WBEJobBufferRingSPSCTest, ConstructorValidSize) {
    // Should succeed with size >= 2
    EXPECT_NO_THROW({ JobBufferRingSPSC buffer(get_allocator(), 2); });

    EXPECT_NO_THROW({ JobBufferRingSPSC buffer(get_allocator(), 10); });

    EXPECT_NO_THROW({ JobBufferRingSPSC buffer(get_allocator(), 1000); });
}

TEST_F(WBEJobBufferRingSPSCTest, ConstructorInvalidSize) {
    // Should throw with size < 2
    EXPECT_THROW({ JobBufferRingSPSC buffer(get_allocator(), 0); }, std::runtime_error);

    EXPECT_THROW({ JobBufferRingSPSC buffer(get_allocator(), 1); }, std::runtime_error);
}

TEST_F(WBEJobBufferRingSPSCTest, RetrieveFromEmptyBuffer) {
    JobBufferRingSPSC buffer(get_allocator(), 5);

    // Empty buffer should return an empty function.
    std::function<void()> job = buffer.retrieve_job();
    EXPECT_EQ(job, nullptr);
}

TEST_F(WBEJobBufferRingSPSCTest, AddAndRetrieveSingleJob) {
    JobBufferRingSPSC buffer(get_allocator(), 5);

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

TEST_F(WBEJobBufferRingSPSCTest, HandleTracksBufferedJobCompletion) {
    JobBufferRingSPSC buffer(get_allocator(), 5);

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

TEST_F(WBEJobBufferRingSPSCTest, HandleWaitBlocksUntilBufferedJobExecutes) {
    JobBufferRingSPSC buffer(get_allocator(), 5);

    std::atomic<bool> performed{false};
    std::latch wait_started(1);
    std::latch allow_execution(1);
    std::latch wait_finished(1);

    std::future<void> handle = buffer.add_job([&performed] {
        performed.store(true, std::memory_order_release);
    });

    std::jthread wait_thread([&] {
        wait_started.count_down();
        handle.wait();
        wait_finished.count_down();
    });

    wait_started.wait();
    EXPECT_FALSE(wait_finished.try_wait());

    std::jthread execute_thread([&] {
        allow_execution.wait();
        std::function<void()> retrieved = buffer.retrieve_job(true);
        ASSERT_NE(retrieved, nullptr);
        retrieved();
    });

    allow_execution.count_down();
    wait_finished.wait();

    EXPECT_TRUE(performed.load(std::memory_order_acquire));
    EXPECT_TRUE(is_future_ready(handle));
}

TEST_F(WBEJobBufferRingSPSCTest, AddAndRetrieveMultipleJobs) {
    JobBufferRingSPSC buffer(get_allocator(), 5);

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

TEST_F(WBEJobBufferRingSPSCTest, BufferOverflow) {
    JobBufferRingSPSC buffer(get_allocator(), 3); // Small buffer

    // Add jobs until buffer is full (size - 1 due to ring buffer implementation)
    auto job1 = std::function<void()>([] {
    });
    auto job2 = std::function<void()>([] {
    });

    EXPECT_NO_THROW(buffer.add_job(job1));
    EXPECT_NO_THROW(buffer.add_job(job2));

    // This should cause overflow
    auto job3 = std::function<void()>([] {
    });
    EXPECT_THROW(buffer.add_job(job3), std::runtime_error);
}

TEST_F(WBEJobBufferRingSPSCTest, RingBufferWrapAround) {
    JobBufferRingSPSC buffer(get_allocator(), 4);

    // Fill buffer
    auto job1 = std::function<void()>([] {
    });
    auto job2 = std::function<void()>([] {
    });
    auto job3 = std::function<void()>([] {
    });

    buffer.add_job(job1);
    buffer.add_job(job2);
    buffer.add_job(job3);

    // Retrieve one job to make space
    std::function<void()> retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);

    // Now we should be able to add another job (wrap around)
    auto job4 = std::function<void()>([] {
    });
    EXPECT_NO_THROW(buffer.add_job(job4));

    // Verify order is maintained
    retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);

    retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);

    retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);
}

TEST_F(WBEJobBufferRingSPSCTest, FIFOOrdering) {
    JobBufferRingSPSC buffer(get_allocator(), 10);

    // Add jobs
    std::vector<bool> performed(5, false);
    for (int i = 0; i < 5; ++i) {
        auto job = std::function<void()>([&performed, i] {
            performed[i] = true;
        });
        buffer.add_job(job);
    }

    // Retrieve jobs and verify FIFO order
    for (int i = 0; i < 5; ++i) {
        std::function<void()> retrieved = buffer.retrieve_job();
        EXPECT_NE(retrieved, nullptr);

        EXPECT_FALSE(performed[i]);
        retrieved();
        EXPECT_TRUE(performed[i]);
    }
}

TEST_F(WBEJobBufferRingSPSCTest, MixedAddRetrieveOperations) {
    JobBufferRingSPSC buffer(get_allocator(), 5);

    // Add a job
    bool performed1 = false;
    auto job1 = std::function<void()>([&performed1] {
        performed1 = true;
    });
    buffer.add_job(job1);

    // Retrieve it
    std::function<void()> retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);
    retrieved();
    EXPECT_TRUE(performed1);

    // Add more jobs
    bool performed2 = false, performed3 = false, performed4 = false;
    auto job2 = std::function<void()>([&performed2] {
        performed2 = true;
    });
    auto job3 = std::function<void()>([&performed3] {
        performed3 = true;
    });
    buffer.add_job(job2);
    buffer.add_job(job3);

    // Retrieve one
    retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);
    retrieved();
    EXPECT_TRUE(performed2);

    // Add another
    auto job4 = std::function<void()>([&performed4] {
        performed4 = true;
    });
    buffer.add_job(job4);

    // Retrieve remaining
    retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);
    retrieved();
    EXPECT_TRUE(performed3);

    retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);
    retrieved();
    EXPECT_TRUE(performed4);

    // Should be empty
    EXPECT_EQ(buffer.retrieve_job(), nullptr);
}

TEST_F(WBEJobBufferRingSPSCTest, JobExecution) {
    JobBufferRingSPSC buffer(get_allocator(), 5);

    bool performed = false;
    auto job = std::function<void()>([&performed] {
        performed = true;
    });
    buffer.add_job(job);

    std::function<void()> retrieved = buffer.retrieve_job();
    EXPECT_NE(retrieved, nullptr);

    EXPECT_FALSE(performed);

    // Execute the job
    retrieved();
    EXPECT_TRUE(performed);
}

// SPSC (Single Producer Single Consumer) concurrent tests
TEST_F(WBEJobBufferRingSPSCTest, ConcurrentProducerConsumer) {
    constexpr int BUFFER_SIZE = 100;
    constexpr int NUM_JOBS = 1000;

    JobBufferRingSPSC buffer(get_allocator(), BUFFER_SIZE);
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<int> perform_count{0};
    std::atomic<bool> producer_done{false};

    // Producer thread
    std::thread producer([&] {
        for (int i = 0; i < NUM_JOBS; ++i) {
            auto job = std::function<void()>([&perform_count] {
                perform_count.fetch_add(1);
            });

            // Keep trying until we can add the job (buffer might be full)
            while (true) {
                try {
                    buffer.add_job(job);
                    produced.fetch_add(1);
                    break;
                } catch (const std::runtime_error&) {
                    // Buffer full, yield and try again
                    std::this_thread::yield();
                }
            }
        }
        producer_done = true;
    });

    // Consumer thread
    std::thread consumer([&] {
        while (!producer_done || consumed.load() < produced.load()) {
            std::function<void()> job = buffer.retrieve_job();
            if (job != nullptr) {
                job();
                consumed.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(produced.load(), NUM_JOBS);
    EXPECT_EQ(consumed.load(), NUM_JOBS);
    EXPECT_EQ(perform_count.load(), NUM_JOBS);
}

TEST_F(WBEJobBufferRingSPSCTest, ProducerConsumerWithSmallBuffer) {
    constexpr int BUFFER_SIZE = 5; // Very small buffer
    constexpr int NUM_JOBS = 100;

    JobBufferRingSPSC buffer(get_allocator(), BUFFER_SIZE);
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<int> perform_count{0};
    std::atomic<bool> producer_done{false};

    // Producer thread
    std::thread producer([&] {
        for (int i = 0; i < NUM_JOBS; ++i) {
            auto job = std::function<void()>([&perform_count] {
                perform_count.fetch_add(1);
            });

            while (true) {
                try {
                    buffer.add_job(job);
                    produced.fetch_add(1);
                    break;
                } catch (const std::runtime_error&) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
        }
        producer_done = true;
    });

    // Consumer thread
    std::thread consumer([&] {
        while (!producer_done || consumed.load() < NUM_JOBS) {
            std::function<void()> job = buffer.retrieve_job();
            if (job != nullptr) {
                job();
                consumed.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(produced.load(), NUM_JOBS);
    EXPECT_EQ(consumed.load(), NUM_JOBS);
    EXPECT_EQ(perform_count.load(), NUM_JOBS);
}

TEST_F(WBEJobBufferRingSPSCTest, StressTestManyOperations) {
    constexpr int BUFFER_SIZE = 50;
    constexpr int NUM_JOBS = 10000;

    JobBufferRingSPSC buffer(get_allocator(), BUFFER_SIZE);
    std::atomic<int> perform_count{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> producer_done{false};

    // Producer thread
    std::thread producer([&] {
        for (int i = 0; i < NUM_JOBS; ++i) {
            auto job = std::function<void()>([&perform_count] {
                perform_count.fetch_add(1);
            });

            while (true) {
                try {
                    buffer.add_job(job);
                    break;
                } catch (const std::runtime_error&) {
                    std::this_thread::yield();
                }
            }
        }
        producer_done = true;
    });

    // Consumer thread
    std::thread consumer([&] {
        while (!producer_done || consumed.load() < NUM_JOBS) {
            std::function<void()> job = buffer.retrieve_job();
            if (job != nullptr) {
                job();
                consumed.fetch_add(1);
            }
        }
    });

    producer.join();
    consumer.join();

    // Verify all jobs were consumed
    EXPECT_EQ(perform_count.load(), NUM_JOBS);
    EXPECT_EQ(consumed.load(), NUM_JOBS);
}

TEST_F(WBEJobBufferRingSPSCTest, NoOpHandlersReturnReadyHandles) {
    {
        WBE::RendererCapabilities capabilities{.ray_tracing = true};
        WBE::RenderAllocator render_allocator(WBE_MI_B(1));
        WBE::RendererDummy dummy_renderer(&render_allocator, capabilities);

        std::future<void> dummy_handle = dummy_renderer.add_job([] {
        });
        EXPECT_TRUE(is_future_ready(dummy_handle));
    }

    WBE::RendererCapabilities software_capabilities{.ray_tracing = true};
    auto software_allocator = WBE::make_ref<WBE::RenderAllocator>(get_allocator(), WBE_MI_B(1));
    WBE::RendererSoftware software_renderer(software_allocator, software_capabilities);

    std::future<void> software_handle = software_renderer.add_job([] {
    });
    EXPECT_TRUE(is_future_ready(software_handle));
}

#endif
