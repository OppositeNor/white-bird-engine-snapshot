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
#ifndef WBE_FILE_REQUIRED_TEST_HH
#define WBE_FILE_REQUIRED_TEST_HH

#include "core/core_utils.hh"
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

namespace WBE = WhiteBirdEngine;

TEST(WBERequiredTest, ReturnsMutableReference) {
    WBE::Required<std::string> value = "before";

    std::string& required_value = WBE_REQUIRED(value);
    required_value = "after";

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "after");
    EXPECT_EQ(&required_value, &value.value());
}

TEST(WBERequiredTest, ReturnsConstReference) {
    const WBE::Required<std::string> value = "main";

    const std::string& required_value = WBE_REQUIRED(value);

    EXPECT_EQ(required_value, "main");
    EXPECT_EQ(&required_value, &value.value());
}

TEST(WBERequiredTest, ThrowsWhenValueIsMissing) {
    WBE::Required<int> value;

    EXPECT_THROW((void)WBE_REQUIRED(value), std::runtime_error);
}

#endif
