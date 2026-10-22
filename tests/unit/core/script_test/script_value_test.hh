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
#ifndef WBE_FILE_SCRIPT_VALUE_TEST_HH
#define WBE_FILE_SCRIPT_VALUE_TEST_HH

#include "../reflection_test/reflection_test_data.hh"
#include "core/data/data_value.hh"
#include "generated/serializables_sd.gen.hh"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "utils/utils.hh"
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace WBE = WhiteBirdEngine;

TEST(WBEDataValueTest, FactoriesCreateEachKind) {
    EXPECT_TRUE(WBE::DataValue().is_nil());
    EXPECT_EQ(WBE::DataValue::make_boolean(true).as_boolean(), true);
    EXPECT_EQ(WBE::DataValue::make_integer(-7).as_integer(), -7);
    EXPECT_EQ(WBE::DataValue::make_float(2.5).as_float(), 2.5);
    EXPECT_EQ(WBE::DataValue::make_string("text").as_string(), "text");
    EXPECT_EQ(WBE::DataValue::make_list().get_kind(), WBE::DataValueKind::LIST);
    EXPECT_EQ(WBE::DataValue::make_map().get_kind(), WBE::DataValueKind::MAP);
}

TEST(WBEDataValueTest, AccessorsRejectOtherKinds) {
    const WBE::DataValue string_value = WBE::DataValue::make_string("1");
    EXPECT_THROW((void)string_value.as_boolean(), std::runtime_error);
    EXPECT_THROW((void)string_value.as_integer(), std::runtime_error);
    EXPECT_THROW((void)string_value.as_float(), std::runtime_error);
    EXPECT_THROW((void)string_value.as_list(), std::runtime_error);
    EXPECT_THROW((void)string_value.as_map(), std::runtime_error);
    EXPECT_THROW((void)WBE::DataValue::make_integer(1).as_string(), std::runtime_error);
    EXPECT_EQ(WBE::DataValue::make_integer(4).as_float(), 4.0);
}

TEST(WBEDataValueTest, MapKeepsEntriesSortedAndReplacesKeys) {
    WBE::DataValue map;
    map.set_entry("zeta", WBE::DataValue::make_integer(1));
    map.set_entry("alpha", WBE::DataValue::make_integer(2));
    map.set_entry("mid", WBE::DataValue::make_integer(3));
    map.set_entry("alpha", WBE::DataValue::make_integer(4));

    EXPECT_EQ(map.get_all_keys(), (std::vector<std::string>{"alpha", "mid", "zeta"}));
    EXPECT_EQ(map.get_value<int32_t>("alpha"), 4);
    EXPECT_TRUE(map.contains("mid"));
    EXPECT_FALSE(map.contains("missing"));
    EXPECT_THROW((void)map.get_value<int32_t>("missing"), std::runtime_error);
    EXPECT_THROW(map.push_back(WBE::DataValue()), std::runtime_error);
}

TEST(WBEDataValueTest, ListAppendsAndRejectsEntries) {
    WBE::DataValue list;
    list.push_back(WBE::DataValue::make_integer(1));
    list.push_back(WBE::DataValue::make_string("two"));
    ASSERT_EQ(list.as_list().size(), 2U);
    EXPECT_EQ(list.as_list()[1].as_string(), "two");
    EXPECT_FALSE(list.contains("two"));
    EXPECT_TRUE(list.get_all_keys().empty());
    EXPECT_THROW(list.set_entry("key", WBE::DataValue()), std::runtime_error);
}

TEST(WBEDataValueTest, EmptyListAndEmptyMapReadAsEachOther) {
    WBE::DataValue empty_list = WBE::DataValue::make_list();
    EXPECT_FALSE(empty_list.contains("key"));
    empty_list.set_entry("key", WBE::DataValue::make_integer(1));
    EXPECT_EQ(empty_list.get_kind(), WBE::DataValueKind::MAP);

    const WBE::DataValue empty_map = WBE::DataValue::make_map();
    EXPECT_TRUE(empty_map.get<std::vector<WBE::DataValue>>().empty());
    EXPECT_THROW((void)WBE::DataValue::make_map().as_list(), std::runtime_error);
}

TEST(WBEDataValueTest, IntegralReadsAcceptExactFloatsAndCheckRanges) {
    EXPECT_EQ(WBE::DataValue::make_float(512.0).get<int32_t>(), 512);
    EXPECT_THROW((void)WBE::DataValue::make_float(1.5).get<int32_t>(), std::runtime_error);
    EXPECT_THROW((void)WBE::DataValue::make_float(std::numeric_limits<double>::infinity()).get<int64_t>(), std::runtime_error);
    EXPECT_THROW((void)WBE::DataValue::make_float(1.0E30).get<int64_t>(), std::runtime_error);
    EXPECT_THROW((void)WBE::DataValue::make_integer(300).get<uint8_t>(), std::runtime_error);
    EXPECT_THROW((void)WBE::DataValue::make_integer(-1).get<uint32_t>(), std::runtime_error);
    EXPECT_EQ(WBE::DataValue::make_integer(7).get<double>(), 7.0);
    EXPECT_THROW((void)WBE::DataValue::make_string("7").get<int32_t>(), std::runtime_error);
}

TEST(WBEDataValueTest, SetConvertsCppValues) {
    WBE::DataValue value;
    value.set(true);
    EXPECT_EQ(value.get_kind(), WBE::DataValueKind::BOOLEAN);
    value.set(uint32_t{42});
    EXPECT_EQ(value.as_integer(), 42);
    value.set(1.25F);
    EXPECT_EQ(value.as_float(), 1.25);
    value.set("literal");
    EXPECT_EQ(value.as_string(), "literal");
    value.set(std::string("string"));
    EXPECT_EQ(value.as_string(), "string");
    EXPECT_THROW(value.set(std::numeric_limits<uint64_t>::max()), std::runtime_error);
}

TEST(WBEDataValueTest, GlmValuesRoundTrip) {
    WBE::DataValue vector_value;
    vector_value.set(glm::vec3(1.0F, -2.0F, 3.0F));
    EXPECT_EQ(vector_value.get_all_keys(), (std::vector<std::string>{"x", "y", "z"}));
    EXPECT_EQ(vector_value.get<glm::vec3>(), glm::vec3(1.0F, -2.0F, 3.0F));

    WBE::DataValue uv_value;
    uv_value.set_entry("u", WBE::DataValue::make_float(0.25));
    uv_value.set_entry("v", WBE::DataValue::make_integer(1));
    EXPECT_EQ(uv_value.get<glm::vec2>(), glm::vec2(0.25F, 1.0F));

    glm::mat4 matrix(1.0F);
    matrix[3] = glm::vec4(4.0F, 5.0F, 6.0F, 1.0F);
    WBE::DataValue matrix_value;
    matrix_value.set(matrix);
    ASSERT_EQ(matrix_value.as_list().size(), 16U);
    EXPECT_EQ(matrix_value.as_list()[12].as_float(), 4.0);
    EXPECT_EQ(matrix_value.get<glm::mat4>(), matrix);
    EXPECT_THROW((void)WBE::DataValue::make_list().get<glm::mat4>(), std::runtime_error);
}

TEST(WBEDataValueTest, BufferRejectsStringsLongerThanCapacity) {
    WBE::DataValue value = WBE::DataValue::make_string("1234567");
    WBE::Buffer<8> buffer{};
    value.get(buffer);
    EXPECT_EQ(std::string(buffer.buffer), "1234567");
    value = WBE::DataValue::make_string("12345678");
    EXPECT_THROW(value.get(buffer), std::runtime_error);
}

TEST(WBEDataValueTest, MoveLeavesSourceNil) {
    WBE::DataValue source = WBE::DataValue::make_string("moved");
    WBE::DataValue target = std::move(source);
    EXPECT_EQ(target.as_string(), "moved");
    EXPECT_TRUE(source.is_nil()); // NOLINT(bugprone-use-after-move)
}

TEST(WBEDataValueTest, ReflectedStructRoundTripsThroughSerializableSD) {
    WBE::TestSerializable source;
    source.si32_test = -3;
    source.si64_test = 1LL << 40;
    source.ui32_test = 7;
    source.ui64_test = 9;
    source.f32_test = 1.5F;
    source.f64_test = -0.25;
    source.vec3_test = glm::vec3(1.0F, 2.0F, 3.0F);
    source.vec4_test = glm::vec4(-1.0F, 0.0F, 1.0F, 2.0F);
    source.str_test = "hello";
    source.optional_str_test = "optional";
    WBE::DataValue value;
    WBE::SerializableSD<WBE::TestSerializable>::serialize(value, source);

    EXPECT_EQ(value.get_kind(), WBE::DataValueKind::MAP);
    EXPECT_EQ(value.get_value<std::string>("str_test"), "hello");
    WBE::TestSerializable result;
    WBE::SerializableSD<WBE::TestSerializable>::deserialize(value, result);
    EXPECT_EQ(result.si32_test, source.si32_test);
    EXPECT_EQ(result.si64_test, source.si64_test);
    EXPECT_EQ(result.ui32_test, source.ui32_test);
    EXPECT_EQ(result.ui64_test, source.ui64_test);
    EXPECT_EQ(result.f32_test, source.f32_test);
    EXPECT_EQ(result.f64_test, source.f64_test);
    EXPECT_EQ(result.vec3_test, source.vec3_test);
    EXPECT_EQ(result.vec4_test, source.vec4_test);
    EXPECT_EQ(result.str_test, source.str_test);
    ASSERT_TRUE(result.optional_str_test.has_value());
    EXPECT_EQ(result.optional_str_test.value(), "optional");
}

TEST(WBEDataValueTest, NestedAndVectorStructsRoundTrip) {
    WBE::TestVectorContainer source;
    source.ints = {1, 2, 3};
    source.strs = {"a", "b"};
    source.children.resize(2);
    source.children[1].str_test = "child";
    WBE::DataValue value;
    WBE::SerializableSD<WBE::TestVectorContainer>::serialize(value, source);

    WBE::TestVectorContainer result;
    WBE::SerializableSD<WBE::TestVectorContainer>::deserialize(value, result);
    EXPECT_EQ(result.ints, source.ints);
    EXPECT_EQ(result.strs, source.strs);
    ASSERT_EQ(result.children.size(), 2U);
    EXPECT_EQ(result.children[1].str_test, "child");
}

TEST(WBEDataValueTest, DeserializingRequiredFieldFromEmptyValueThrows) {
    WBE::TestSerializableAliases result;
    EXPECT_THROW(WBE::SerializableSD<WBE::TestSerializableAliases>::deserialize(WBE::DataValue::make_map(), result), std::runtime_error);
}

#endif
