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
#ifndef WBE_FILE_DESERIALIZER_TEST_TOML_HH
#define WBE_FILE_DESERIALIZER_TEST_TOML_HH

#include "core/data/data_value.hh"
#include "core/parser/parser_toml.hh"
#include "generated/serializables_sd.gen.hh"
#include "global/global.hh"
#include "reflection_test_data.hh"
#include "test_global.hh"
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace WBE = WhiteBirdEngine;

class WBEDeserializerTOMLTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;
    std::string test_serialize_toml_general =
        R"(si32_test = 3
si64_test = -62
ui32_test = 42
ui64_test = 59
f32_test = 3.14
f64_test = 2.718
vec3_test = { x = 1, y = -2, z = 3 }
vec4_test = { x = 1, y = -2, z = 3, w = -4 }
str_test = "Hello!"
buffer_test = "how are you?"
)";
    std::string test_serialize_toml_nesting =
        R"(nesting_id = 7
name = "parent"
small_buffer = "abc"

[nested_test]
si32_test = 3
si64_test = -62
ui32_test = 42
ui64_test = 59
f32_test = 3.14
f64_test = 2.718
vec3_test = { x = 1, y = -2, z = 3 }
vec4_test = { x = 1, y = -2, z = 3, w = -4 }
str_test = "Hello!"
buffer_test = "how are you?"
)";
};

TEST_F(WBEDeserializerTOMLTest, General) {
    WBE::TestSerializable test_obj;
    WBE::ParserTOML parser;
    parser.parse_from_buffer(test_serialize_toml_general);
    WBE::SerializableSD<WBE::TestSerializable>::deserialize(parser.get_data(), test_obj);
    EXPECT_EQ(test_obj.si32_test, 3);
    EXPECT_EQ(test_obj.si64_test, -62);
    EXPECT_EQ(test_obj.ui32_test, 42);
    EXPECT_EQ(test_obj.ui64_test, 59);
    EXPECT_FLOAT_EQ(test_obj.f32_test, 3.14);
    EXPECT_FLOAT_EQ(test_obj.f64_test, 2.718);
    EXPECT_EQ(test_obj.vec3_test, glm::vec3(1, -2, 3));
    EXPECT_EQ(test_obj.vec4_test, glm::vec4(1, -2, 3, -4));
    EXPECT_EQ(test_obj.str_test, "Hello!");
    EXPECT_STREQ(test_obj.buffer_test.buffer, "how are you?");
    EXPECT_FALSE(test_obj.optional_str_test.has_value());
}

TEST_F(WBEDeserializerTOMLTest, NestingGeneral) {
    WBE::TestSerializableNesting test_obj;
    WBE::ParserTOML parser;
    parser.parse_from_buffer(test_serialize_toml_nesting);
    WBE::SerializableSD<WBE::TestSerializableNesting>::deserialize(parser.get_data(), test_obj);
    EXPECT_EQ(test_obj.nesting_id, 7);
    EXPECT_EQ(test_obj.name, "parent");
    EXPECT_STREQ(test_obj.small_buffer.buffer, "abc");
    EXPECT_EQ(test_obj.nested_test.si64_test, -62);
    EXPECT_EQ(test_obj.nested_test.vec4_test, glm::vec4(1, -2, 3, -4));
    EXPECT_STREQ(test_obj.nested_test.buffer_test.buffer, "how are you?");
}

TEST_F(WBEDeserializerTOMLTest, PartialUpdateOnlyOneField) {
    WBE::TestSerializable test_obj;
    test_obj.si64_test = 200;
    test_obj.str_test = "orig";
    WBE::ParserTOML parser;
    parser.parse_from_buffer("si32_test = -7\n");
    WBE::SerializableSD<WBE::TestSerializable>::deserialize(parser.get_data(), test_obj);
    EXPECT_EQ(test_obj.si32_test, -7);
    EXPECT_EQ(test_obj.si64_test, 200);
    EXPECT_EQ(test_obj.str_test, std::string("orig"));
}

TEST_F(WBEDeserializerTOMLTest, SupportsAliasedFieldNames) {
    WBE::TestSerializableAliases test_obj;
    WBE::ParserTOML parser;
    parser.parse_from_buffer("legacy_name = \"alias\"\nrequired_alias = 29\n");
    WBE::SerializableSD<WBE::TestSerializableAliases>::deserialize(parser.get_data(), test_obj);
    EXPECT_EQ(test_obj.canonical_name, "alias");
    EXPECT_EQ(test_obj.required_value, 29);
}

TEST_F(WBEDeserializerTOMLTest, VectorContainer) {
    WBE::TestVectorContainer test_obj;
    WBE::ParserTOML parser;
    parser.parse_from_buffer(R"(ints = [1, 2, 3, -4, 0]
strs = ["hello", "world", "", "test"]

[[children]]
si32_test = 10
str_test = "child1"

[[children]]
si32_test = 20
str_test = "child2"
)");
    WBE::SerializableSD<WBE::TestVectorContainer>::deserialize(parser.get_data(), test_obj);
    ASSERT_EQ(test_obj.ints.size(), 5);
    EXPECT_EQ(test_obj.ints[3], -4);
    ASSERT_EQ(test_obj.strs.size(), 4);
    EXPECT_EQ(test_obj.strs[2], "");
    ASSERT_EQ(test_obj.children.size(), 2);
    EXPECT_EQ(test_obj.children[0].str_test, "child1");
    EXPECT_EQ(test_obj.children[1].si32_test, 20);
}

TEST_F(WBEDeserializerTOMLTest, VectorContainerEmpty) {
    WBE::TestVectorContainer test_obj;
    test_obj.ints = {1};
    WBE::ParserTOML parser;
    parser.parse_from_buffer("ints = []\nstrs = []\nchildren = []\n");
    WBE::SerializableSD<WBE::TestVectorContainer>::deserialize(parser.get_data(), test_obj);
    EXPECT_TRUE(test_obj.ints.empty());
    EXPECT_TRUE(test_obj.strs.empty());
    EXPECT_TRUE(test_obj.children.empty());
}

TEST_F(WBEDeserializerTOMLTest, TypeMismatchThrows) {
    WBE::TestSerializable test_obj;
    WBE::ParserTOML parser;
    parser.parse_from_buffer("si32_test = \"three\"\n");
    EXPECT_THROW(WBE::SerializableSD<WBE::TestSerializable>::deserialize(parser.get_data(), test_obj), std::runtime_error);
    parser.parse_from_buffer("str_test = 3\n");
    EXPECT_THROW(WBE::SerializableSD<WBE::TestSerializable>::deserialize(parser.get_data(), test_obj), std::runtime_error);
}

TEST_F(WBEDeserializerTOMLTest, OutOfRangeIntegerThrows) {
    WBE::TestSerializable test_obj;
    WBE::ParserTOML parser;
    parser.parse_from_buffer("ui32_test = -1\n");
    EXPECT_THROW(WBE::SerializableSD<WBE::TestSerializable>::deserialize(parser.get_data(), test_obj), std::runtime_error);
    parser.parse_from_buffer("si32_test = 4294967296\n");
    EXPECT_THROW(WBE::SerializableSD<WBE::TestSerializable>::deserialize(parser.get_data(), test_obj), std::runtime_error);
}

TEST_F(WBEDeserializerTOMLTest, BufferTooLongThrows) {
    WBE::TestSerializable test_obj;
    WBE::ParserTOML parser;
    parser.parse_from_buffer("buffer_test = \"" + std::string(32, 'A') + "\"\n");
    EXPECT_THROW(WBE::SerializableSD<WBE::TestSerializable>::deserialize(parser.get_data(), test_obj), std::runtime_error);
}

TEST_F(WBEDeserializerTOMLTest, MissingRequiredThrows) {
    WBE::TestSerializableAliases test_obj;
    WBE::ParserTOML parser;
    parser.parse_from_buffer("canonical_name = \"primary\"\n");
    EXPECT_THROW(WBE::SerializableSD<WBE::TestSerializableAliases>::deserialize(parser.get_data(), test_obj), std::runtime_error);
}

TEST_F(WBEDeserializerTOMLTest, SerializeRoundTrip) {
    WBE::TestSerializableNesting source;
    source.nesting_id = 11;
    source.name = "round";
    strncpy(source.small_buffer.buffer, "trip", sizeof(source.small_buffer.buffer));
    source.nested_test.ui64_test = 123456789012ULL;
    source.nested_test.f32_test = 1.5F;
    source.nested_test.vec3_test = glm::vec3(1.0F, 2.0F, 3.0F);
    source.nested_test.str_test = "nested";
    source.nested_test.optional_str_test = "present";
    strncpy(source.nested_test.buffer_test.buffer, "buffer", sizeof(source.nested_test.buffer_test.buffer));

    WBE::DataValue data;
    WBE::SerializableSD<WBE::TestSerializableNesting>::serialize(data, source);

    // Reparse the emitted document to check it is valid TOML.
    WBE::ParserTOML parser;
    parser.parse_from_buffer(WBE::ParserTOML::write(data));

    WBE::TestSerializableNesting result;
    WBE::SerializableSD<WBE::TestSerializableNesting>::deserialize(parser.get_data(), result);
    EXPECT_EQ(result.nesting_id, 11);
    EXPECT_EQ(result.name, "round");
    EXPECT_STREQ(result.small_buffer.buffer, "trip");
    EXPECT_EQ(result.nested_test.ui64_test, 123456789012ULL);
    EXPECT_FLOAT_EQ(result.nested_test.f32_test, 1.5F);
    EXPECT_EQ(result.nested_test.vec3_test, glm::vec3(1.0F, 2.0F, 3.0F));
    EXPECT_EQ(result.nested_test.str_test, "nested");
    ASSERT_TRUE(result.nested_test.optional_str_test.has_value());
    EXPECT_EQ(result.nested_test.optional_str_test.value(), "present");
    EXPECT_STREQ(result.nested_test.buffer_test.buffer, "buffer");
}

#endif
