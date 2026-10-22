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
#ifndef WBE_FILE_PARSE_INTEGER_TEST_HH
#define WBE_FILE_PARSE_INTEGER_TEST_HH
#include "utils/utils.hh"
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace WBE = WhiteBirdEngine;

TEST(WBEParseIntegerTest, ParsesWholeIntegers) {
    EXPECT_EQ(WBE::try_parse_integer<uint64_t>("30"), std::optional<uint64_t>(30));
    EXPECT_EQ(WBE::try_parse_integer<int32_t>("-12"), std::optional<int32_t>(-12));
    EXPECT_EQ(WBE::try_parse_integer<uint32_t>("ff", 16), std::optional<uint32_t>(255));
    EXPECT_EQ(WBE::parse_integer<uint16_t>("65535"), 65535);
}

TEST(WBEParseIntegerTest, ParsesViewsIntoLargerStrings) {
    // The view is not NUL-terminated; parsing stops at its end, not at the end of the buffer.
    constexpr std::string_view RESIZE = "1280x720@60";
    EXPECT_EQ(WBE::try_parse_integer<uint32_t>(RESIZE.substr(0, 4)), std::optional<uint32_t>(1280));
    EXPECT_EQ(WBE::try_parse_integer<uint32_t>(RESIZE.substr(5, 3)), std::optional<uint32_t>(720));
}

TEST(WBEParseIntegerTest, RejectsAnythingButOneIntegerThatFits) {
    for (const std::string_view text : {"", " 30", "30 ", "+30", "30x", "x30", "3.0"}) {
        EXPECT_EQ(WBE::try_parse_integer<uint64_t>(text), std::nullopt) << '"' << text << '"';
    }
    EXPECT_EQ(WBE::try_parse_integer<uint32_t>("-1"), std::nullopt);
    EXPECT_EQ(WBE::try_parse_integer<uint8_t>("256"), std::nullopt);
    EXPECT_EQ(WBE::try_parse_integer<int8_t>("-129"), std::nullopt);
    EXPECT_THROW((void)WBE::parse_integer<uint64_t>("30x"), std::runtime_error);
}

#endif
