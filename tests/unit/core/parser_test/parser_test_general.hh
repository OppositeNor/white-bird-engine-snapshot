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
#ifndef WBE_FILE_PARSER_TEST_GENERAL_HH
#define WBE_FILE_PARSER_TEST_GENERAL_HH

#include "core/data/data_value.hh"
#include "core/parser/parser.hh"
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

namespace WBE = WhiteBirdEngine;

inline void test_parser_general(
    WBE::Parser& p_parser, std::string_view p_test_buff1, std::string_view p_test_buff2, std::string_view p_test_buff3) {
    p_parser.parse_from_buffer(p_test_buff1);
    ASSERT_STREQ(p_parser.template get_value<std::string>("fruit").c_str(), "apple");
    ASSERT_STREQ(p_parser.template get_value<std::string>("vegetable").c_str(), "lettice");
    ASSERT_STREQ(p_parser.get_data().template get_value<std::string>("fruit").c_str(), "apple");
    ASSERT_STREQ(p_parser.get_data().template get_value<std::string>("vegetable").c_str(), "lettice");
    ASSERT_EQ(p_parser.template get_value<int32_t>("number"), 123);
    ASSERT_EQ(p_parser.get_data().template get_value<int32_t>("number"), 123);
    p_parser.parse_from_buffer(p_test_buff2);
    ASSERT_STREQ(p_parser.template get_value<std::string>("vegetable").c_str(), "lettice");
    ASSERT_STREQ(p_parser.get_data().template get_value<std::string>("vegetable").c_str(), "lettice");
    auto numbers = p_parser.template get_value<std::vector<int32_t>>("numbers");
    ASSERT_EQ(numbers[0], 123);
    ASSERT_EQ(numbers[1], -200);
    ASSERT_EQ(numbers[2], 5);
    ASSERT_EQ(numbers[3], 60);
    ASSERT_EQ(numbers[4], -123);
    numbers = p_parser.get_data().template get_value<std::vector<int32_t>>("numbers");
    ASSERT_EQ(numbers[0], 123);
    ASSERT_EQ(numbers[1], -200);
    ASSERT_EQ(numbers[2], 5);
    ASSERT_EQ(numbers[3], 60);
    ASSERT_EQ(numbers[4], -123);
    p_parser.parse_from_buffer(p_test_buff3);
    ASSERT_STREQ(p_parser.template get_value<WBE::DataValue>("test_pair").template get_value<std::string>("test_key1").c_str(), "test_val1");
    ASSERT_EQ(p_parser.template get_value<WBE::DataValue>("test_pair").template get_value<int32_t>("test_key2"), 123);
    ASSERT_DOUBLE_EQ(p_parser.template get_value<WBE::DataValue>("test_pair").template get_value<double>("test_key3"), 3.14);
    ASSERT_STREQ(
        p_parser.get_data().template get_value<WBE::DataValue>("test_pair").template get_value<std::string>("test_key1").c_str(), "test_val1");
    ASSERT_EQ(p_parser.get_data().template get_value<WBE::DataValue>("test_pair").template get_value<int32_t>("test_key2"), 123);
    ASSERT_DOUBLE_EQ(p_parser.get_data().template get_value<WBE::DataValue>("test_pair").template get_value<double>("test_key3"), 3.14);
}

#endif
