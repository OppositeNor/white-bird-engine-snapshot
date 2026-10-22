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

#include "global/global.hh"
#include "platform/file_system/path.hh"
#include <gtest/gtest.h>
#include <memory>

namespace WBE = WhiteBirdEngine;

// Test objects for lifecycle tracking
struct TrackedObject {
    int value;
    static int instance_count;

    TrackedObject(int p_value = 0) : value(p_value) {
        instance_count++;
    }

    ~TrackedObject() {
        instance_count--;
        value = -999; // Mark as destroyed
    }
};

int TrackedObject::instance_count = 0;

class WBECoreIntegrationTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = std::make_unique<WBE::Global>(0, nullptr, WBE::Path("test_env"));
        TrackedObject::instance_count = 0;
    }

    virtual void TearDown() override {
        global.reset();
        // Verify no memory leaks
        ASSERT_EQ(TrackedObject::instance_count, 0);
    }

    std::unique_ptr<WBE::Global> global;
};
