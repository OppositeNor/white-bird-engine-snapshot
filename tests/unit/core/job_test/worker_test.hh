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
#ifndef WBE_FILE_WORKER_TEST_HH
#define WBE_FILE_WORKER_TEST_HH

#include "core/async/worker.hh"
#include "global/global.hh"
#include "job_test_utils.hh"
#include "test_global.hh"
#include <atomic>
#include <future>
#include <gtest/gtest.h>
#include <latch>
#include <memory>

namespace WBE = WhiteBirdEngine;

class WBEWorkerTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;
};

TEST_F(WBEWorkerTest, HandleCompletesWithoutDrainingPool) {
    WBE::Worker worker(2);
    std::latch blocking_started(1);
    std::latch release_blocking(1);
    std::atomic<bool> target_executed{false};

    std::future<void> blocking_handle = worker.add_job([&] {
        blocking_started.count_down();
        release_blocking.wait();
    });

    blocking_started.wait();

    std::future<void> target_handle = worker.add_job([&] {
        target_executed.store(true, std::memory_order_release);
    });

    target_handle.wait();

    EXPECT_TRUE(target_executed.load(std::memory_order_acquire));
    EXPECT_TRUE(is_future_ready(target_handle));
    EXPECT_FALSE(is_future_ready(blocking_handle));

    release_blocking.count_down();
    blocking_handle.wait();
}

TEST_F(WBEWorkerTest, SubmitTaskReturnsValue) {
    WBE::Worker worker;

    std::future<int> result = worker.submit_task([] {
        return 42;
    });

    EXPECT_EQ(result.get(), 42);
}

#endif
