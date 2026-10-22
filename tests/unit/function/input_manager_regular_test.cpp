/* Copyright 2026 OppositeNor

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
#include "function/input_manager/i_input_manager.hh"
#include "function/input_manager/input_manager_regular.hh"
#include "function/input_manager/key_codes.hh"
#include <barrier>
#include <cstddef>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>
#include <vector>

namespace WhiteBirdEngine {

TEST(WBEInputManagerTest, TracksSingletonLifetime) {
    EXPECT_EQ(IInputManager::get_singleton(), nullptr);

    {
        InputManagerRegular input_manager;
        EXPECT_EQ(IInputManager::get_singleton(), &input_manager);
    }

    EXPECT_EQ(IInputManager::get_singleton(), nullptr);
}

TEST(WBEInputManagerTest, RejectedDuplicateDoesNotPoisonSingletonLifetime) {
    {
        InputManagerRegular input_manager;
        EXPECT_THROW({ InputManagerRegular duplicate_input_manager; }, std::runtime_error);
    }

    EXPECT_NO_THROW({ InputManagerRegular recovered_input_manager; });
}

TEST(WBEInputManagerTest, InitializesEveryKeyAsUp) {
    InputManagerRegular input_manager;

    for (int key_code = KEYCODE_0; key_code < TOTAL_KEYCODES; ++key_code) {
        InputStatus status = input_manager.get_input_status(static_cast<KeyCode>(key_code));
        EXPECT_TRUE(status.is_up()) << "Key code " << key_code;
        EXPECT_FALSE(status.is_down()) << "Key code " << key_code;
    }
}

TEST(WBEInputManagerTest, TransitionsEveryKeyBetweenDownAndUp) {
    InputManagerRegular input_manager;

    for (int key_code = KEYCODE_0; key_code < TOTAL_KEYCODES; ++key_code) {
        input_manager.set_keydown(static_cast<KeyCode>(key_code));
    }
    for (int key_code = KEYCODE_0; key_code < TOTAL_KEYCODES; ++key_code) {
        InputStatus status = input_manager.get_input_status(static_cast<KeyCode>(key_code));
        EXPECT_TRUE(status.is_down()) << "Key code " << key_code;
        EXPECT_FALSE(status.is_up()) << "Key code " << key_code;
    }

    for (int key_code = KEYCODE_0; key_code < TOTAL_KEYCODES; ++key_code) {
        input_manager.set_keyup(static_cast<KeyCode>(key_code));
    }
    for (int key_code = KEYCODE_0; key_code < TOTAL_KEYCODES; ++key_code) {
        InputStatus status = input_manager.get_input_status(static_cast<KeyCode>(key_code));
        EXPECT_TRUE(status.is_up()) << "Key code " << key_code;
        EXPECT_FALSE(status.is_down()) << "Key code " << key_code;
    }
}

TEST(WBEInputManagerTest, KeepsNeighboringAndByteBoundaryKeysIndependent) {
    InputManagerRegular input_manager;

    input_manager.set_keydown(KEYCODE_7);
    EXPECT_TRUE(input_manager.get_input_status(KEYCODE_7).is_down());
    EXPECT_TRUE(input_manager.get_input_status(KEYCODE_6).is_up());
    EXPECT_TRUE(input_manager.get_input_status(KEYCODE_8).is_up());

    input_manager.set_keydown(KEYCODE_8);
    input_manager.set_keyup(KEYCODE_7);
    EXPECT_TRUE(input_manager.get_input_status(KEYCODE_7).is_up());
    EXPECT_TRUE(input_manager.get_input_status(KEYCODE_8).is_down());
    EXPECT_TRUE(input_manager.get_input_status(KEYCODE_9).is_up());
}

TEST(WBEInputManagerTest, RepeatedTransitionsAreIdempotent) {
    InputManagerRegular input_manager;

    input_manager.set_keydown(KEYCODE_ESCAPE);
    input_manager.set_keydown(KEYCODE_ESCAPE);
    EXPECT_TRUE(input_manager.get_input_status(KEYCODE_ESCAPE).is_down());

    input_manager.set_keyup(KEYCODE_ESCAPE);
    input_manager.set_keyup(KEYCODE_ESCAPE);
    EXPECT_TRUE(input_manager.get_input_status(KEYCODE_ESCAPE).is_up());
}

TEST(WBEInputManagerTest, ConcurrentlyUpdatesKeysSharingOneStorageByte) {
    InputManagerRegular input_manager;
    constexpr size_t THREAD_COUNT = 8;
    constexpr size_t ITERATION_COUNT = 1000;
    std::barrier sync_point(THREAD_COUNT + 1);
    std::vector<std::thread> threads;
    threads.reserve(THREAD_COUNT);

    for (size_t thread_index = 0; thread_index < THREAD_COUNT; ++thread_index) {
        threads.emplace_back([&input_manager, &sync_point, thread_index]() {
            KeyCode key_code = static_cast<KeyCode>(KEYCODE_0 + thread_index);
            for (size_t iteration = 0; iteration < ITERATION_COUNT; ++iteration) {
                sync_point.arrive_and_wait();
                input_manager.set_keydown(key_code);
                sync_point.arrive_and_wait();
                sync_point.arrive_and_wait();
                input_manager.set_keyup(key_code);
                sync_point.arrive_and_wait();
            }
        });
    }

    for (size_t iteration = 0; iteration < ITERATION_COUNT; ++iteration) {
        sync_point.arrive_and_wait();
        sync_point.arrive_and_wait();
        for (size_t key_index = 0; key_index < THREAD_COUNT; ++key_index) {
            EXPECT_TRUE(input_manager.get_input_status(static_cast<KeyCode>(KEYCODE_0 + key_index)).is_down());
        }

        sync_point.arrive_and_wait();
        sync_point.arrive_and_wait();
        for (size_t key_index = 0; key_index < THREAD_COUNT; ++key_index) {
            EXPECT_TRUE(input_manager.get_input_status(static_cast<KeyCode>(KEYCODE_0 + key_index)).is_up());
        }
    }

    for (std::thread& thread : threads) {
        thread.join();
    }
}

} // namespace WhiteBirdEngine
