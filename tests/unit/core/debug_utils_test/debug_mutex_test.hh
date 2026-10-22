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
#ifndef WBE_FILE_DEBUG_MUTEX_TEST_HH
#define WBE_FILE_DEBUG_MUTEX_TEST_HH

#include "boost/thread/lock_types.hpp"
#include "core/debug_utils/debug_mutex.hh"
#include "global/global.hh"
#include "test_global.hh"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

namespace WBE = WhiteBirdEngine;

#ifdef _DEBUG

class WBEDebugMutexTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;
};

TEST_F(WBEDebugMutexTest, ExclusiveMutexTracksUniqueOwner) {
    WBE::DebugMutex mutex;

    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

    boost::unique_lock lock(mutex);
    EXPECT_TRUE(mutex.is_unique_locked_by_current_thread());
}

TEST_F(WBEDebugMutexTest, ExclusiveMutexTryLockWhenLocked) {
    WBE::DebugMutex mutex;

    boost::unique_lock lock(mutex);
    EXPECT_TRUE(mutex.is_unique_locked_by_current_thread());

    std::atomic<bool> try_lock_result{true};
    std::atomic<bool> is_unique_locked{true};
    std::thread thread([&] {
        try_lock_result = mutex.try_lock();
        is_unique_locked = mutex.is_unique_locked_by_current_thread();
    });
    thread.join();

    EXPECT_FALSE(try_lock_result);
    EXPECT_FALSE(is_unique_locked);
}

TEST_F(WBEDebugMutexTest, IsCurrentTheadUniqueLocked) {
    WBE::DebugSharedMutex mutex;
    constexpr size_t THREAD_COUNT = 8;
    constexpr size_t ITERATION_COUNT = 1000;
    std::vector<std::thread> threads;
    threads.reserve(THREAD_COUNT);
    for (size_t i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&] {
            for (size_t j = 0; j < ITERATION_COUNT; ++j) {
                // Should not be true when current thread does not hold the unique lock.
                EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
                {
                    // Should be true when current thread holds the unique lock.
                    boost::unique_lock lock(mutex);
                    EXPECT_TRUE(mutex.is_unique_locked_by_current_thread());
                }
                // Should not be true when unique lock is released.
                EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
                {
                    // Should not be true when shared lock is locked.
                    boost::shared_lock lock(mutex);
                    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
                }
                // Should not be true when shared lock is released.
                EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
}

TEST_F(WBEDebugMutexTest, UniqueLockBasicFunctionality) {
    WBE::DebugSharedMutex mutex;

    // Test basic unique lock operations
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

    mutex.lock();
    EXPECT_TRUE(mutex.is_unique_locked_by_current_thread());

    mutex.unlock();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
}

TEST_F(WBEDebugMutexTest, TryLockBasicFunctionality) {
    WBE::DebugSharedMutex mutex;

    // Test try_lock when mutex is available
    EXPECT_TRUE(mutex.try_lock());
    EXPECT_TRUE(mutex.is_unique_locked_by_current_thread());

    mutex.unlock();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
}

TEST_F(WBEDebugMutexTest, TryLockWhenLocked) {
    WBE::DebugSharedMutex mutex;

    // First thread locks the mutex
    mutex.lock();
    EXPECT_TRUE(mutex.is_unique_locked_by_current_thread());

    // Second thread tries to lock - should fail
    std::atomic<bool> try_lock_result{false};
    std::atomic<bool> is_unique_locked{false};
    std::thread t([&] {
        try_lock_result = mutex.try_lock();
        is_unique_locked = mutex.is_unique_locked_by_current_thread();
    });
    t.join();

    EXPECT_FALSE(try_lock_result);
    EXPECT_FALSE(is_unique_locked);

    mutex.unlock();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
}

TEST_F(WBEDebugMutexTest, SharedLockBasicFunctionality) {
    WBE::DebugSharedMutex mutex;

    // Test shared lock - should not affect unique lock detection
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

    mutex.lock_shared();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

    mutex.unlock_shared();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
}

TEST_F(WBEDebugMutexTest, TrySharedLockBasicFunctionality) {
    WBE::DebugSharedMutex mutex;

    // Test try_lock_shared when mutex is available
    EXPECT_TRUE(mutex.try_lock_shared());
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

    mutex.unlock_shared();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
}

TEST_F(WBEDebugMutexTest, MultipleSharedLocks) {
    WBE::DebugSharedMutex mutex;
    constexpr int NUM_READERS = 5;

    std::vector<std::thread> threads;
    std::atomic<int> active_readers{0};
    std::atomic<int> max_concurrent_readers{0};

    threads.reserve(NUM_READERS);
    for (int i = 0; i < NUM_READERS; ++i) {
        threads.emplace_back([&] {
            mutex.lock_shared();

            // Track concurrent readers
            int current_readers = active_readers.fetch_add(1) + 1;
            int expected_max = max_concurrent_readers.load();
            while (current_readers > expected_max && !max_concurrent_readers.compare_exchange_weak(expected_max, current_readers)) {
                expected_max = max_concurrent_readers.load();
            }

            // Should never be true for shared locks
            EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            active_readers.fetch_sub(1);
            mutex.unlock_shared();
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // All readers should have been able to run concurrently
    EXPECT_EQ(max_concurrent_readers.load(), NUM_READERS);
    EXPECT_EQ(active_readers.load(), 0);
}

TEST_F(WBEDebugMutexTest, UniqueBlocksShared) {
    WBE::DebugSharedMutex mutex;
    std::atomic<bool> shared_acquired{false};
    std::atomic<bool> test_complete{false};

    // Main thread acquires unique lock
    mutex.lock();
    EXPECT_TRUE(mutex.is_unique_locked_by_current_thread());

    // Background thread tries to acquire shared lock
    std::thread t([&] {
        mutex.lock_shared();
        shared_acquired = true;
        EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
        mutex.unlock_shared();
        test_complete = true;
    });

    // Give thread time to try to acquire shared lock
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(shared_acquired);

    // Release unique lock
    mutex.unlock();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

    t.join();
    EXPECT_TRUE(shared_acquired);
    EXPECT_TRUE(test_complete);
}

TEST_F(WBEDebugMutexTest, SharedBlocksUnique) {
    WBE::DebugSharedMutex mutex;
    std::atomic<bool> unique_acquired{false};
    std::atomic<bool> test_complete{false};

    // Main thread acquires shared lock
    mutex.lock_shared();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

    // Background thread tries to acquire unique lock
    std::thread t([&] {
        mutex.lock();
        unique_acquired = true;
        EXPECT_TRUE(mutex.is_unique_locked_by_current_thread());
        mutex.unlock();
        test_complete = true;
    });

    // Give thread time to try to acquire unique lock
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(unique_acquired);

    // Release shared lock
    mutex.unlock_shared();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

    t.join();
    EXPECT_TRUE(unique_acquired);
    EXPECT_TRUE(test_complete);
}

TEST_F(WBEDebugMutexTest, TrySharedLockWhenUniqueLocked) {
    WBE::DebugSharedMutex mutex;

    // Main thread acquires unique lock
    mutex.lock();
    EXPECT_TRUE(mutex.is_unique_locked_by_current_thread());

    // Background thread tries try_lock_shared
    std::atomic<bool> try_shared_result{true}; // Initialize to true to detect failure
    std::atomic<bool> is_unique_locked{true};
    std::thread t([&] {
        try_shared_result = mutex.try_lock_shared();
        is_unique_locked = mutex.is_unique_locked_by_current_thread();
        if (try_shared_result) {
            mutex.unlock_shared();
        }
    });
    t.join();

    EXPECT_FALSE(try_shared_result);
    EXPECT_FALSE(is_unique_locked);

    mutex.unlock();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
}

TEST_F(WBEDebugMutexTest, TryUniqueLockWhenSharedLocked) {
    WBE::DebugSharedMutex mutex;

    // Main thread acquires shared lock
    mutex.lock_shared();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

    // Background thread tries try_lock (unique)
    std::atomic<bool> try_unique_result{true}; // Initialize to true to detect failure
    std::atomic<bool> is_unique_locked{false};
    std::thread t([&] {
        try_unique_result = mutex.try_lock();
        is_unique_locked = mutex.is_unique_locked_by_current_thread();
        if (try_unique_result) {
            mutex.unlock();
        }
    });
    t.join();

    EXPECT_FALSE(try_unique_result);
    EXPECT_FALSE(is_unique_locked);

    mutex.unlock_shared();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
}

TEST_F(WBEDebugMutexTest, OwnershipTracking) {
    WBE::DebugSharedMutex mutex;
    std::atomic<std::thread::id> other_thread_id{};
    std::atomic<bool> other_thread_sees_unique{false};

    // Main thread acquires unique lock
    mutex.lock();
    EXPECT_TRUE(mutex.is_unique_locked_by_current_thread());

    // Other thread should not see itself as owner
    std::thread t([&] {
        other_thread_id = std::this_thread::get_id();
        other_thread_sees_unique = mutex.is_unique_locked_by_current_thread();
    });
    t.join();

    // Other thread should not see itself as unique owner
    EXPECT_FALSE(other_thread_sees_unique);
    EXPECT_NE(other_thread_id.load(), std::this_thread::get_id());

    mutex.unlock();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
}

TEST_F(WBEDebugMutexTest, OwnershipAfterUnlock) {
    WBE::DebugSharedMutex mutex;

    // Lock and unlock
    mutex.lock();
    EXPECT_TRUE(mutex.is_unique_locked_by_current_thread());
    mutex.unlock();
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

    // Another thread should be able to acquire and be tracked
    std::atomic<bool> other_thread_unique{false};
    std::thread t([&] {
        mutex.lock();
        other_thread_unique = mutex.is_unique_locked_by_current_thread();
        mutex.unlock();
    });
    t.join();

    EXPECT_TRUE(other_thread_unique);
    EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());
}

TEST_F(WBEDebugMutexTest, StressTestOwnershipTracking) {
    WBE::DebugSharedMutex mutex;
    constexpr int NUM_THREADS = 10;
    constexpr int ITERATIONS = 100;
    std::atomic<int> unique_lock_count{0};
    std::atomic<int> false_positive_count{0};

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < ITERATIONS; ++j) {
                // Test unique lock ownership
                EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

                mutex.lock();
                if (mutex.is_unique_locked_by_current_thread()) {
                    unique_lock_count.fetch_add(1);
                } else {
                    false_positive_count.fetch_add(1);
                }
                mutex.unlock();

                EXPECT_FALSE(mutex.is_unique_locked_by_current_thread());

                // Test shared lock (should never be unique)
                mutex.lock_shared();
                if (mutex.is_unique_locked_by_current_thread()) {
                    false_positive_count.fetch_add(1);
                }
                mutex.unlock_shared();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Every unique lock should have been properly tracked
    EXPECT_EQ(unique_lock_count.load(), NUM_THREADS * ITERATIONS);
    EXPECT_EQ(false_positive_count.load(), 0);
}

#endif
#endif
