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
#ifndef WBE_FILE_SERIALIZER_TEST_HH
#define WBE_FILE_SERIALIZER_TEST_HH

#include "core/data/data_value.hh"
#include "core/parser/parser_json.hh"
#include "core/parser/parser_toml.hh"
#include "core/reflection/serializable.hh"
#include "core/reflection/serializable_sd.hh"
#include "generated/serializables_sd.gen.hh"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "global/global.hh"
#include "reflection_test_data.hh"
#include "test_global.hh"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace WBE = WhiteBirdEngine;

class WBESerializerTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;
};
template <typename ParserDataType>
inline void serializer_test_default_construct(ParserDataType p_data) {
    WBE::TestSerializable serializable{};
    WBE::SerializableSD<WBE::TestSerializable> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestSerializable>::serialize(p_data, serializable);
    EXPECT_TRUE(p_data.contains("si32_test"));
    EXPECT_TRUE(p_data.contains("si64_test"));
    EXPECT_TRUE(p_data.contains("ui32_test"));
    EXPECT_TRUE(p_data.contains("ui64_test"));
    EXPECT_TRUE(p_data.contains("f32_test"));
    EXPECT_TRUE(p_data.contains("f64_test"));
    EXPECT_TRUE(p_data.contains("vec3_test"));
    EXPECT_TRUE(p_data.contains("vec4_test"));
    EXPECT_TRUE(p_data.contains("str_test"));
    EXPECT_FALSE(p_data.contains("optional_str_test"));
    EXPECT_TRUE(p_data.contains("buffer_test"));
    EXPECT_EQ(p_data.template get_value<int32_t>("si32_test"), 0);
    EXPECT_EQ(p_data.template get_value<int64_t>("si64_test"), 0);
    EXPECT_EQ(p_data.template get_value<uint32_t>("ui32_test"), 0);
    EXPECT_EQ(p_data.template get_value<uint64_t>("ui64_test"), 0);
    EXPECT_FLOAT_EQ(p_data.template get_value<float>("f32_test"), 0.0F);
    EXPECT_DOUBLE_EQ(p_data.template get_value<double>("f64_test"), 0.0);
    EXPECT_EQ(p_data.template get_value<glm::vec3>("vec3_test"), glm::vec3());
    EXPECT_EQ(p_data.template get_value<glm::vec4>("vec4_test"), glm::vec4());
    EXPECT_EQ(p_data.template get_value<std::string>("str_test"), std::string());
}

template <typename ParserDataType>
inline void serializer_test_general(ParserDataType p_data) {
    WBE::TestSerializable serializable;
    // populate with non-default values
    serializable.si32_test = 3;
    serializable.si64_test = -62;
    serializable.ui32_test = 42;
    serializable.ui64_test = 59;
    serializable.f32_test = 3.14F;
    serializable.f64_test = std::numbers::e;
    serializable.vec3_test = glm::vec3(1, -2, 3);
    serializable.vec4_test = glm::vec4(1, -2, 3, -4);
    serializable.str_test = "Hello!";
    serializable.optional_str_test = "Optional hello!";
    strcpy(serializable.buffer_test.buffer, "how are you?");

    WBE::SerializableSD<WBE::TestSerializable> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestSerializable>::serialize(p_data, serializable);

    // verify keys and values
    EXPECT_TRUE(p_data.contains("si32_test"));
    EXPECT_TRUE(p_data.contains("si64_test"));
    EXPECT_TRUE(p_data.contains("ui32_test"));
    EXPECT_TRUE(p_data.contains("ui64_test"));
    EXPECT_TRUE(p_data.contains("f32_test"));
    EXPECT_TRUE(p_data.contains("f64_test"));
    EXPECT_TRUE(p_data.contains("vec3_test"));
    EXPECT_TRUE(p_data.contains("vec4_test"));
    EXPECT_TRUE(p_data.contains("str_test"));
    EXPECT_TRUE(p_data.contains("optional_str_test"));
    EXPECT_TRUE(p_data.contains("buffer_test"));

    EXPECT_EQ(p_data.template get_value<int32_t>("si32_test"), 3);
    EXPECT_EQ(p_data.template get_value<int64_t>("si64_test"), -62);
    EXPECT_EQ(p_data.template get_value<uint32_t>("ui32_test"), 42U);
    EXPECT_EQ(p_data.template get_value<uint64_t>("ui64_test"), 59U);
    EXPECT_FLOAT_EQ(p_data.template get_value<float>("f32_test"), 3.14F);
    EXPECT_DOUBLE_EQ(p_data.template get_value<double>("f64_test"), std::numbers::e);
    EXPECT_EQ(p_data.template get_value<glm::vec3>("vec3_test"), glm::vec3(1, -2, 3));
    EXPECT_EQ(p_data.template get_value<glm::vec4>("vec4_test"), glm::vec4(1, -2, 3, -4));
    EXPECT_EQ(p_data.template get_value<std::string>("str_test"), std::string("Hello!"));
    EXPECT_EQ(p_data.template get_value<std::string>("optional_str_test"), std::string("Optional hello!"));
    EXPECT_EQ(p_data.template get_value<std::string>("buffer_test"), std::string("how are you?"));
}

template <typename ParserDataType>
inline void serializer_test_nesting(ParserDataType p_data) {
    WBE::TestSerializableNesting serializable{};
    // fill nested_test
    serializable.nesting_id = 7;
    serializable.name = "parent";
    strcpy(serializable.small_buffer.buffer, "abc");
    serializable.nested_test.si32_test = 3;
    serializable.nested_test.si64_test = -62;
    serializable.nested_test.ui32_test = 42;
    serializable.nested_test.ui64_test = 59;
    serializable.nested_test.f32_test = 3.14F;
    serializable.nested_test.f64_test = std::numbers::e;
    serializable.nested_test.vec3_test = glm::vec3(1, -2, 3);
    serializable.nested_test.vec4_test = glm::vec4(1, -2, 3, -4);
    serializable.nested_test.str_test = "Hello!";
    strcpy(serializable.nested_test.buffer_test.buffer, "how are you?");

    WBE::SerializableSD<WBE::TestSerializableNesting> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestSerializableNesting>::serialize(p_data, serializable);

    // Top-level checks
    EXPECT_TRUE(p_data.contains("nesting_id"));
    EXPECT_TRUE(p_data.contains("name"));
    EXPECT_TRUE(p_data.contains("small_buffer"));
    EXPECT_TRUE(p_data.contains("nested_test"));

    EXPECT_EQ(p_data.template get_value<int32_t>("nesting_id"), 7);
    EXPECT_EQ(p_data.template get_value<std::string>("name"), std::string("parent"));
    EXPECT_EQ(p_data.template get_value<std::string>("small_buffer"), std::string("abc"));

    // Nested checks
    auto nested = p_data.template get_value<ParserDataType>("nested_test");
    EXPECT_EQ(nested.template get_value<int32_t>("si32_test"), 3);
    EXPECT_EQ(nested.template get_value<int64_t>("si64_test"), -62);
    EXPECT_EQ(nested.template get_value<uint32_t>("ui32_test"), 42U);
    EXPECT_EQ(nested.template get_value<uint64_t>("ui64_test"), 59U);
    EXPECT_FLOAT_EQ(nested.template get_value<float>("f32_test"), 3.14F);
    EXPECT_DOUBLE_EQ(nested.template get_value<double>("f64_test"), std::numbers::e);
    EXPECT_EQ(nested.template get_value<glm::vec3>("vec3_test"), glm::vec3(1, -2, 3));
    EXPECT_EQ(nested.template get_value<glm::vec4>("vec4_test"), glm::vec4(1, -2, 3, -4));
    EXPECT_EQ(nested.template get_value<std::string>("str_test"), std::string("Hello!"));
    EXPECT_EQ(nested.template get_value<std::string>("buffer_test"), std::string("how are you?"));
}

template <typename ParserDataType>
inline void serializer_test_aliases_use_canonical_names(ParserDataType p_data) {
    WBE::TestSerializableAliases serializable{};
    serializable.canonical_name = "canonical";
    serializable.required_value = 41;

    WBE::SerializableSD<WBE::TestSerializableAliases> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestSerializableAliases>::serialize(p_data, serializable);

    EXPECT_TRUE(p_data.contains("canonical_name"));
    EXPECT_TRUE(p_data.contains("required_value"));
    EXPECT_FALSE(p_data.contains("legacy_name"));
    EXPECT_FALSE(p_data.contains("display_name"));
    EXPECT_FALSE(p_data.contains("required_alias"));
    EXPECT_EQ(p_data.template get_value<std::string>("canonical_name"), "canonical");
    EXPECT_EQ(p_data.template get_value<int32_t>("required_value"), 41);
}

template <typename ParserDataType>
inline void serializer_test_deep_nesting(ParserDataType p_data) {
    // Depth-2: TestDepth2 contains WBE::TestSerializable
    WBE::TestDepth2 td2{};
    td2.depth2_id = 123;
    td2.depth2_name = "depth2";
    td2.nested.si32_test = 9;
    td2.nested.str_test = "deep";
    strcpy(td2.nested.buffer_test.buffer, "B2");

    WBE::SerializableSD<WBE::TestDepth2> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestDepth2>::serialize(p_data, td2);

    EXPECT_TRUE(p_data.contains("nested"));
    EXPECT_TRUE(p_data.contains("depth2_id"));
    EXPECT_TRUE(p_data.contains("depth2_name"));

    auto nested = p_data.template get_value<ParserDataType>("nested");
    EXPECT_EQ(nested.template get_value<int32_t>("si32_test"), 9);
    EXPECT_EQ(nested.template get_value<std::string>("str_test"), std::string("deep"));
    EXPECT_EQ(nested.template get_value<std::string>("buffer_test"), std::string("B2"));
}

template <typename ParserDataType>
inline void serializer_test_vector_primitives(ParserDataType p_data_ints, ParserDataType p_data_strs) {
    // Test serializing vectors of primitives
    std::vector<int> ints = {1, 2, 3, 4, 5};
    WhiteBirdEngine::SerializableSD<std::vector<int, std::allocator<int>>>::serialize(p_data_ints, ints);

    auto out_ints = p_data_ints.template get<std::vector<int>>();
    ASSERT_EQ(out_ints.size(), ints.size());
    for (size_t i = 0; i < ints.size(); ++i) {
        EXPECT_EQ(out_ints[i], ints[i]);
    }

    // Test vector<string>
    std::vector<std::string> strs = {"one", "two", "three"};
    WhiteBirdEngine::SerializableSD<std::vector<std::basic_string<char, std::char_traits<char>, std::allocator<char>>,
        std::allocator<std::basic_string<char, std::char_traits<char>, std::allocator<char>>>>>::serialize(p_data_strs, strs);

    auto out_strs = p_data_strs.template get<std::vector<std::string>>();
    ASSERT_EQ(out_strs.size(), strs.size());
    for (size_t i = 0; i < strs.size(); ++i) {
        EXPECT_EQ(out_strs[i], strs[i]);
    }
}

template <typename ParserDataType>
inline void serializer_test_vector_serializables(ParserDataType p_data) {
    // Test serializing vector of WBE::TestSerializable objects
    std::vector<WBE::TestSerializable> vec;
    WBE::TestSerializable a{};
    a.si32_test = 1;
    a.str_test = "a";
    strcpy(a.buffer_test.buffer, "A");
    WBE::TestSerializable b{};
    b.si32_test = 2;
    b.str_test = "b";
    strcpy(b.buffer_test.buffer, "B");
    vec.push_back(a);
    vec.push_back(b);

    WhiteBirdEngine::SerializableSD<std::vector<WhiteBirdEngine::TestSerializable, std::allocator<WhiteBirdEngine::TestSerializable>>>::serialize(
        p_data, vec);

    // Expect an array of objects
    auto arr = p_data.template get<std::vector<ParserDataType>>();
    ASSERT_EQ(arr.size(), 2);
    EXPECT_EQ(arr[0].template get_value<int32_t>("si32_test"), 1);
    EXPECT_EQ(arr[0].template get_value<std::string>("str_test"), std::string("a"));
    EXPECT_EQ(arr[1].template get_value<int32_t>("si32_test"), 2);
    EXPECT_EQ(arr[1].template get_value<std::string>("str_test"), std::string("b"));
}

template <typename ParserDataType>
inline void serializer_test_struct_with_vector_serializables(ParserDataType p_data) {
    // Build a container with vectors
    WBE::TestVectorContainer container{};
    container.ints = {10, 20, 30};
    container.strs = {"aa", "bb"};

    WBE::TestSerializable a{};
    a.si32_test = 5;
    a.str_test = "aa";
    strcpy(a.buffer_test.buffer, "A");
    WBE::TestSerializable b{};
    b.si32_test = 6;
    b.str_test = "bb";
    strcpy(b.buffer_test.buffer, "B");
    container.children.push_back(a);
    container.children.push_back(b);

    WBE::SerializableSD<WBE::TestVectorContainer> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestVectorContainer>::serialize(p_data, container);

    // Top-level fields
    EXPECT_TRUE(p_data.contains("ints"));
    EXPECT_TRUE(p_data.contains("strs"));
    EXPECT_TRUE(p_data.contains("children"));

    // Inspect ints
    auto ints_out = p_data.template get_value<ParserDataType>("ints").template get<std::vector<int>>();
    ASSERT_EQ(ints_out.size(), 3U);
    EXPECT_EQ(ints_out[0], 10);
    EXPECT_EQ(ints_out[1], 20);
    EXPECT_EQ(ints_out[2], 30);

    // Inspect strs
    auto strs_out = p_data.template get_value<ParserDataType>("strs").template get<std::vector<std::string>>();
    ASSERT_EQ(strs_out.size(), 2U);
    EXPECT_EQ(strs_out[0], std::string("aa"));
    EXPECT_EQ(strs_out[1], std::string("bb"));

    // Inspect children
    auto children_arr = p_data.template get_value<ParserDataType>("children").template get<std::vector<ParserDataType>>();
    ASSERT_EQ(children_arr.size(), 2U);
    EXPECT_EQ(children_arr[0].template get_value<int32_t>("si32_test"), 5);
    EXPECT_EQ(children_arr[0].template get_value<std::string>("str_test"), std::string("aa"));
    EXPECT_EQ(children_arr[1].template get_value<int32_t>("si32_test"), 6);
    EXPECT_EQ(children_arr[1].template get_value<std::string>("str_test"), std::string("bb"));
}

template <typename ParserDataType>
inline void serializer_test_depth_2(ParserDataType p_data) {
    WBE::TestDepth2 d2{};
    d2.nested.si32_test = 11;
    d2.nested.str_test = "deep2";
    strcpy(d2.nested.buffer_test.buffer, "X");
    d2.depth2_id = 77;
    d2.depth2_name = "level2";

    WBE::SerializableSD<WBE::TestDepth2> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestDepth2>::serialize(p_data, d2);

    EXPECT_TRUE(p_data.contains("nested"));
    EXPECT_TRUE(p_data.contains("depth2_id"));
    EXPECT_TRUE(p_data.contains("depth2_name"));

    auto nested = p_data.template get_value<ParserDataType>("nested");
    EXPECT_EQ(nested.template get_value<int32_t>("si32_test"), 11);
    EXPECT_EQ(nested.template get_value<std::string>("str_test"), std::string("deep2"));
}

template <typename ParserDataType>
inline void serializer_test_depth_3(ParserDataType p_data) {
    WBE::TestDepth3 d3{};
    d3.nested2.nested.si32_test = 21;
    d3.nested2.nested.str_test = "deep3";
    d3.depth3_id = 88;
    d3.depth3_name = "level3";

    WBE::SerializableSD<WBE::TestDepth3> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestDepth3>::serialize(p_data, d3);

    EXPECT_TRUE(p_data.contains("nested2"));
    EXPECT_TRUE(p_data.contains("depth3_id"));

    auto n2 = p_data.template get_value<ParserDataType>("nested2");
    auto nested = n2.template get_value<ParserDataType>("nested");
    EXPECT_EQ(nested.template get_value<int32_t>("si32_test"), 21);
}

template <typename ParserDataType>
inline void serializer_vector_with_deep_nesting(ParserDataType p_data) {
    WBE::TestDeepVectorContainer container{};

    WBE::TestDepth2 d2a{};
    d2a.nested.si32_test = 31;
    d2a.depth2_id = 1;
    d2a.depth2_name = "d2a";
    WBE::TestDepth2 d2b{};
    d2b.nested.si32_test = 32;
    d2b.depth2_id = 2;
    d2b.depth2_name = "d2b";
    container.vec2.push_back(d2a);
    container.vec2.push_back(d2b);

    WBE::TestDepth3 d3a{};
    d3a.nested2.nested.si32_test = 41;
    d3a.depth3_id = 3;
    d3a.depth3_name = "d3a";
    WBE::TestDepth3 d3b{};
    d3b.nested2.nested.si32_test = 42;
    d3b.depth3_id = 4;
    d3b.depth3_name = "d3b";
    container.vec3.push_back(d3a);
    container.vec3.push_back(d3b);

    WBE::SerializableSD<WBE::TestDeepVectorContainer> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestDeepVectorContainer>::serialize(p_data, container);

    EXPECT_TRUE(p_data.contains("vec2"));
    EXPECT_TRUE(p_data.contains("vec3"));

    auto out_vec2 = p_data.template get_value<ParserDataType>("vec2").template get<std::vector<ParserDataType>>();
    ASSERT_EQ(out_vec2.size(), 2U);
    EXPECT_EQ(out_vec2[0].template get_value<int32_t>("depth2_id"), 1);
    EXPECT_EQ(out_vec2[1].template get_value<int32_t>("depth2_id"), 2);

    auto out_vec3 = p_data.template get_value<ParserDataType>("vec3").template get<std::vector<ParserDataType>>();
    ASSERT_EQ(out_vec3.size(), 2U);
    EXPECT_EQ(out_vec3[0].template get_value<int32_t>("depth3_id"), 3);
    EXPECT_EQ(out_vec3[1].template get_value<int32_t>("depth3_id"), 4);
}

// Inheritance test functions
template <typename ParserDataType>
inline void serializer_test_inheritance_base(ParserDataType p_data) {
    WBE::TestInheritedBase base{};
    base.base_id = 42;
    base.base_name = "base_test";
    base.base_value = 3.14F;

    WBE::SerializableSD<WBE::TestInheritedBase> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestInheritedBase>::serialize(p_data, base);

    EXPECT_TRUE(p_data.contains("base_id"));
    EXPECT_TRUE(p_data.contains("base_name"));
    EXPECT_TRUE(p_data.contains("base_value"));

    EXPECT_EQ(p_data.template get_value<int32_t>("base_id"), 42);
    EXPECT_EQ(p_data.template get_value<std::string>("base_name"), std::string("base_test"));
    EXPECT_FLOAT_EQ(p_data.template get_value<float>("base_value"), 3.14F);
}

template <typename ParserDataType>
inline void serializer_test_inheritance_child(ParserDataType p_data) {
    WBE::TestInheritedChild child{};
    // Set inherited fields
    child.base_id = 100;
    child.base_name = "inherited_base";
    child.base_value = 2.71F;
    // Set child fields
    child.child_id = 200;
    child.child_name = "child_test";
    child.child_value = std::numbers::sqrt2;

    WBE::SerializableSD<WBE::TestInheritedChild> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestInheritedChild>::serialize(p_data, child);

    // Check that both base and child fields are serialized
    EXPECT_TRUE(p_data.contains("base_id"));
    EXPECT_TRUE(p_data.contains("base_name"));
    EXPECT_TRUE(p_data.contains("base_value"));
    EXPECT_TRUE(p_data.contains("child_id"));
    EXPECT_TRUE(p_data.contains("child_name"));
    EXPECT_TRUE(p_data.contains("child_value"));

    // Verify inherited fields
    EXPECT_EQ(p_data.template get_value<int32_t>("base_id"), 100);
    EXPECT_EQ(p_data.template get_value<std::string>("base_name"), std::string("inherited_base"));
    EXPECT_FLOAT_EQ(p_data.template get_value<float>("base_value"), 2.71F);

    // Verify child fields
    EXPECT_EQ(p_data.template get_value<int32_t>("child_id"), 200);
    EXPECT_EQ(p_data.template get_value<std::string>("child_name"), std::string("child_test"));
    EXPECT_DOUBLE_EQ(p_data.template get_value<double>("child_value"), std::numbers::sqrt2);
}

template <typename ParserDataType>
inline void serializer_test_inheritance_multilevel(ParserDataType p_data) {
    WBE::TestInheritedGrandchild grandchild{};
    // Set base fields
    grandchild.base_id = 10;
    grandchild.base_name = "grandparent";
    grandchild.base_value = 1.0F;
    // Set child fields
    grandchild.child_id = 20;
    grandchild.child_name = "parent";
    grandchild.child_value = 2.0;
    // Set grandchild fields
    grandchild.grandchild_id = 30;
    grandchild.grandchild_name = "grandchild";
    grandchild.grandchild_vector = glm::vec3(1.0F, 2.0F, 3.0F);

    WBE::SerializableSD<WBE::TestInheritedGrandchild> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestInheritedGrandchild>::serialize(p_data, grandchild);

    // Check all levels of inheritance are serialized
    EXPECT_TRUE(p_data.contains("base_id"));
    EXPECT_TRUE(p_data.contains("base_name"));
    EXPECT_TRUE(p_data.contains("base_value"));
    EXPECT_TRUE(p_data.contains("child_id"));
    EXPECT_TRUE(p_data.contains("child_name"));
    EXPECT_TRUE(p_data.contains("child_value"));
    EXPECT_TRUE(p_data.contains("grandchild_id"));
    EXPECT_TRUE(p_data.contains("grandchild_name"));
    EXPECT_TRUE(p_data.contains("grandchild_vector"));

    // Verify all fields
    EXPECT_EQ(p_data.template get_value<int32_t>("base_id"), 10);
    EXPECT_EQ(p_data.template get_value<std::string>("base_name"), std::string("grandparent"));
    EXPECT_FLOAT_EQ(p_data.template get_value<float>("base_value"), 1.0F);
    EXPECT_EQ(p_data.template get_value<int32_t>("child_id"), 20);
    EXPECT_EQ(p_data.template get_value<std::string>("child_name"), std::string("parent"));
    EXPECT_DOUBLE_EQ(p_data.template get_value<double>("child_value"), 2.0);
    EXPECT_EQ(p_data.template get_value<int32_t>("grandchild_id"), 30);
    EXPECT_EQ(p_data.template get_value<std::string>("grandchild_name"), std::string("grandchild"));
    EXPECT_EQ(p_data.template get_value<glm::vec3>("grandchild_vector"), glm::vec3(1.0F, 2.0F, 3.0F));
}

template <typename ParserDataType>
inline void serializer_test_inheritance_with_nested(ParserDataType p_data) {
    WBE::TestInheritedWithNested obj{};
    // Set inherited base fields
    obj.base_id = 555;
    obj.base_name = "nested_base";
    obj.base_value = 9.99F;
    // Set child fields
    obj.child_numbers = {1, 2, 3, 4, 5};
    obj.nested_object.si32_test = 777;
    obj.nested_object.str_test = "nested_string";
    strcpy(obj.nested_object.buffer_test.buffer, "nested_buf");

    WBE::SerializableSD<WBE::TestInheritedWithNested> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestInheritedWithNested>::serialize(p_data, obj);

    // Check inherited base fields
    EXPECT_TRUE(p_data.contains("base_id"));
    EXPECT_TRUE(p_data.contains("base_name"));
    EXPECT_TRUE(p_data.contains("base_value"));
    EXPECT_EQ(p_data.template get_value<int32_t>("base_id"), 555);
    EXPECT_EQ(p_data.template get_value<std::string>("base_name"), std::string("nested_base"));
    EXPECT_FLOAT_EQ(p_data.template get_value<float>("base_value"), 9.99F);

    // Check child fields
    EXPECT_TRUE(p_data.contains("child_numbers"));
    EXPECT_TRUE(p_data.contains("nested_object"));

    auto numbers = p_data.template get_value<ParserDataType>("child_numbers").template get<std::vector<int>>();
    ASSERT_EQ(numbers.size(), 5U);
    for (size_t i = 0; i < numbers.size(); ++i) {
        EXPECT_EQ(numbers[i], static_cast<int>(i + 1));
    }

    auto nested = p_data.template get_value<ParserDataType>("nested_object");
    EXPECT_EQ(nested.template get_value<int32_t>("si32_test"), 777);
    EXPECT_EQ(nested.template get_value<std::string>("str_test"), std::string("nested_string"));
    EXPECT_EQ(nested.template get_value<std::string>("buffer_test"), std::string("nested_buf"));
}

template <typename ParserDataType>
inline void serializer_test_inheritance_vector(ParserDataType p_data) {
    // Test vector of inherited objects
    std::vector<WBE::TestInheritedChild> children;

    WBE::TestInheritedChild child1{};
    child1.base_id = 1;
    child1.base_name = "base1";
    child1.base_value = 1.1F;
    child1.child_id = 11;
    child1.child_name = "child1";
    child1.child_value = 11.1;

    WBE::TestInheritedChild child2{};
    child2.base_id = 2;
    child2.base_name = "base2";
    child2.base_value = 2.2F;
    child2.child_id = 22;
    child2.child_name = "child2";
    child2.child_value = 22.2;

    children.push_back(child1);
    children.push_back(child2);

    WhiteBirdEngine::SerializableSD<std::vector<WhiteBirdEngine::TestInheritedChild, std::allocator<WhiteBirdEngine::TestInheritedChild>>>::serialize(
        p_data, children);

    auto arr = p_data.template get<std::vector<ParserDataType>>();
    ASSERT_EQ(arr.size(), 2U);

    // Check first child
    EXPECT_EQ(arr[0].template get_value<int32_t>("base_id"), 1);
    EXPECT_EQ(arr[0].template get_value<std::string>("base_name"), std::string("base1"));
    EXPECT_FLOAT_EQ(arr[0].template get_value<float>("base_value"), 1.1F);
    EXPECT_EQ(arr[0].template get_value<int32_t>("child_id"), 11);
    EXPECT_EQ(arr[0].template get_value<std::string>("child_name"), std::string("child1"));
    EXPECT_DOUBLE_EQ(arr[0].template get_value<double>("child_value"), 11.1);

    // Check second child
    EXPECT_EQ(arr[1].template get_value<int32_t>("base_id"), 2);
    EXPECT_EQ(arr[1].template get_value<std::string>("base_name"), std::string("base2"));
    EXPECT_FLOAT_EQ(arr[1].template get_value<float>("base_value"), 2.2F);
    EXPECT_EQ(arr[1].template get_value<int32_t>("child_id"), 22);
    EXPECT_EQ(arr[1].template get_value<std::string>("child_name"), std::string("child2"));
    EXPECT_DOUBLE_EQ(arr[1].template get_value<double>("child_value"), 22.2);
}

// Multiple inheritance test functions
template <typename ParserDataType>
inline void serializer_test_multiple_inheritance(ParserDataType p_data) {
    WBE::TestMultipleInheritanceChild child{};
    // Set fields from first parent (A)
    child.a_id = 100;
    child.a_name = "parent_a";
    child.a_value = 1.23F;
    // Set fields from second parent (B)
    child.b_id = 200;
    child.b_name = "parent_b";
    child.b_value = 4.56;
    // Set child's own fields
    child.child_id = 300;
    child.child_name = "multi_child";
    child.child_vector = glm::vec2(7.7F, 8.8F);

    WBE::SerializableSD<WBE::TestMultipleInheritanceChild> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestMultipleInheritanceChild>::serialize(p_data, child);

    // Check all fields from both parents and child are serialized
    // Fields from parent A
    EXPECT_TRUE(p_data.contains("a_id"));
    EXPECT_TRUE(p_data.contains("a_name"));
    EXPECT_TRUE(p_data.contains("a_value"));
    EXPECT_EQ(p_data.template get_value<int32_t>("a_id"), 100);
    EXPECT_EQ(p_data.template get_value<std::string>("a_name"), std::string("parent_a"));
    EXPECT_FLOAT_EQ(p_data.template get_value<float>("a_value"), 1.23F);

    // Fields from parent B
    EXPECT_TRUE(p_data.contains("b_id"));
    EXPECT_TRUE(p_data.contains("b_name"));
    EXPECT_TRUE(p_data.contains("b_value"));
    EXPECT_EQ(p_data.template get_value<int32_t>("b_id"), 200);
    EXPECT_EQ(p_data.template get_value<std::string>("b_name"), std::string("parent_b"));
    EXPECT_DOUBLE_EQ(p_data.template get_value<double>("b_value"), 4.56);

    // Child's own fields
    EXPECT_TRUE(p_data.contains("child_id"));
    EXPECT_TRUE(p_data.contains("child_name"));
    EXPECT_TRUE(p_data.contains("child_vector"));
    EXPECT_EQ(p_data.template get_value<int32_t>("child_id"), 300);
    EXPECT_EQ(p_data.template get_value<std::string>("child_name"), std::string("multi_child"));
    EXPECT_EQ(p_data.template get_value<glm::vec2>("child_vector"), glm::vec2(7.7F, 8.8F));
}

template <typename ParserDataType>
inline void serializer_test_diamond_inheritance(ParserDataType p_data) {
    WBE::TestDiamondChild diamond{};
    // Set fields from virtual base
    diamond.diamond_base_id = 42;
    diamond.diamond_base_name = "diamond_base";
    // Set fields from left parent
    diamond.left_id = 10;
    diamond.left_value = 1.5F;
    // Set fields from right parent
    diamond.right_id = 20;
    diamond.right_value = 2.5;
    // Set child's own fields
    diamond.diamond_child_id = 30;
    diamond.diamond_child_name = "diamond_child";

    WBE::SerializableSD<WBE::TestDiamondChild> sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestDiamondChild>::serialize(p_data, diamond);

    // Check all fields are serialized
    // Virtual base fields (should appear only once)
    EXPECT_TRUE(p_data.contains("diamond_base_id"));
    EXPECT_TRUE(p_data.contains("diamond_base_name"));
    EXPECT_EQ(p_data.template get_value<int32_t>("diamond_base_id"), 42);
    EXPECT_EQ(p_data.template get_value<std::string>("diamond_base_name"), std::string("diamond_base"));

    // Left parent fields
    EXPECT_TRUE(p_data.contains("left_id"));
    EXPECT_TRUE(p_data.contains("left_value"));
    EXPECT_EQ(p_data.template get_value<int32_t>("left_id"), 10);
    EXPECT_FLOAT_EQ(p_data.template get_value<float>("left_value"), 1.5F);

    // Right parent fields
    EXPECT_TRUE(p_data.contains("right_id"));
    EXPECT_TRUE(p_data.contains("right_value"));
    EXPECT_EQ(p_data.template get_value<int32_t>("right_id"), 20);
    EXPECT_DOUBLE_EQ(p_data.template get_value<double>("right_value"), 2.5);

    // Child's own fields
    EXPECT_TRUE(p_data.contains("diamond_child_id"));
    EXPECT_TRUE(p_data.contains("diamond_child_name"));
    EXPECT_EQ(p_data.template get_value<int32_t>("diamond_child_id"), 30);
    EXPECT_EQ(p_data.template get_value<std::string>("diamond_child_name"), std::string("diamond_child"));
}

template <typename ParserDataType>
inline void serializer_test_multiple_inheritance_vector(ParserDataType p_data) {
    // Test vector of multiple inheritance objects
    std::vector<WBE::TestMultipleInheritanceChild> children;

    WBE::TestMultipleInheritanceChild child1{};
    child1.a_id = 1;
    child1.a_name = "a1";
    child1.a_value = 1.1F;
    child1.b_id = 2;
    child1.b_name = "b1";
    child1.b_value = 2.2;
    child1.child_id = 3;
    child1.child_name = "c1";
    child1.child_vector = glm::vec2(1.0F, 2.0F);

    WBE::TestMultipleInheritanceChild child2{};
    child2.a_id = 11;
    child2.a_name = "a2";
    child2.a_value = 11.1F;
    child2.b_id = 22;
    child2.b_name = "b2";
    child2.b_value = 22.2;
    child2.child_id = 33;
    child2.child_name = "c2";
    child2.child_vector = glm::vec2(3.0F, 4.0F);

    children.push_back(child1);
    children.push_back(child2);

    WhiteBirdEngine::SerializableSD<std::vector<WhiteBirdEngine::TestMultipleInheritanceChild, std::allocator<WhiteBirdEngine::TestMultipleInheritanceChild>>>::serialize(
        p_data, children);

    auto arr = p_data.template get<std::vector<ParserDataType>>();
    ASSERT_EQ(arr.size(), 2U);

    // Check first child - all inheritance levels
    EXPECT_EQ(arr[0].template get_value<int32_t>("a_id"), 1);
    EXPECT_EQ(arr[0].template get_value<std::string>("a_name"), std::string("a1"));
    EXPECT_FLOAT_EQ(arr[0].template get_value<float>("a_value"), 1.1F);
    EXPECT_EQ(arr[0].template get_value<int32_t>("b_id"), 2);
    EXPECT_EQ(arr[0].template get_value<std::string>("b_name"), std::string("b1"));
    EXPECT_DOUBLE_EQ(arr[0].template get_value<double>("b_value"), 2.2);
    EXPECT_EQ(arr[0].template get_value<int32_t>("child_id"), 3);
    EXPECT_EQ(arr[0].template get_value<std::string>("child_name"), std::string("c1"));
    EXPECT_EQ(arr[0].template get_value<glm::vec2>("child_vector"), glm::vec2(1.0F, 2.0F));

    // Check second child
    EXPECT_EQ(arr[1].template get_value<int32_t>("a_id"), 11);
    EXPECT_EQ(arr[1].template get_value<std::string>("a_name"), std::string("a2"));
    EXPECT_FLOAT_EQ(arr[1].template get_value<float>("a_value"), 11.1F);
    EXPECT_EQ(arr[1].template get_value<int32_t>("b_id"), 22);
    EXPECT_EQ(arr[1].template get_value<std::string>("b_name"), std::string("b2"));
    EXPECT_DOUBLE_EQ(arr[1].template get_value<double>("b_value"), 22.2);
    EXPECT_EQ(arr[1].template get_value<int32_t>("child_id"), 33);
    EXPECT_EQ(arr[1].template get_value<std::string>("child_name"), std::string("c2"));
    EXPECT_EQ(arr[1].template get_value<glm::vec2>("child_vector"), glm::vec2(3.0F, 4.0F));
}

// Dynamic serialization test functions (JSON only since dynamic serialization is JSON-specific)
inline void dynamic_serializer_test_basic() {
    WBE::DataValue json_data;
    WBE::TestSerializable serializable{};

    // Set some test values
    serializable.si32_test = 42;
    serializable.si64_test = -1337;
    serializable.ui32_test = 256;
    serializable.ui64_test = 512;
    serializable.f32_test = std::numbers::pi_v<float>;
    serializable.f64_test = std::numbers::e;
    serializable.vec3_test = glm::vec3(1.0F, 2.0F, 3.0F);
    serializable.vec4_test = glm::vec4(4.0F, 5.0F, 6.0F, 7.0F);
    serializable.str_test = "dynamic_test";
    strcpy(serializable.buffer_test.buffer, "dyn_buffer");

    // Use dynamic serialization
    serializable.serialize(json_data);

    // Verify all fields were serialized correctly
    EXPECT_TRUE(json_data.contains("si32_test"));
    EXPECT_TRUE(json_data.contains("si64_test"));
    EXPECT_TRUE(json_data.contains("ui32_test"));
    EXPECT_TRUE(json_data.contains("ui64_test"));
    EXPECT_TRUE(json_data.contains("f32_test"));
    EXPECT_TRUE(json_data.contains("f64_test"));
    EXPECT_TRUE(json_data.contains("vec3_test"));
    EXPECT_TRUE(json_data.contains("vec4_test"));
    EXPECT_TRUE(json_data.contains("str_test"));
    EXPECT_TRUE(json_data.contains("buffer_test"));

    EXPECT_EQ(json_data.get_value<int32_t>("si32_test"), 42);
    EXPECT_EQ(json_data.get_value<int64_t>("si64_test"), -1337);
    EXPECT_EQ(json_data.get_value<uint32_t>("ui32_test"), 256U);
    EXPECT_EQ(json_data.get_value<uint64_t>("ui64_test"), 512U);
    EXPECT_FLOAT_EQ(json_data.get_value<float>("f32_test"), std::numbers::pi_v<float>);
    EXPECT_DOUBLE_EQ(json_data.get_value<double>("f64_test"), std::numbers::e);
    EXPECT_EQ(json_data.get_value<glm::vec3>("vec3_test"), glm::vec3(1.0F, 2.0F, 3.0F));
    EXPECT_EQ(json_data.get_value<glm::vec4>("vec4_test"), glm::vec4(4.0F, 5.0F, 6.0F, 7.0F));
    EXPECT_EQ(json_data.get_value<std::string>("str_test"), std::string("dynamic_test"));
    EXPECT_EQ(json_data.get_value<std::string>("buffer_test"), std::string("dyn_buffer"));
}

inline void dynamic_serializer_test_inheritance() {
    WBE::DataValue json_data;
    WBE::TestInheritedChild child{};

    // Set inherited base fields
    child.base_id = 100;
    child.base_name = "dynamic_base";
    child.base_value = 1.23F;

    // Set child fields
    child.child_id = 200;
    child.child_name = "dynamic_child";
    child.child_value = 4.56;

    // Use dynamic serialization
    child.serialize(json_data);

    // Verify both inherited and child fields were serialized
    EXPECT_TRUE(json_data.contains("base_id"));
    EXPECT_TRUE(json_data.contains("base_name"));
    EXPECT_TRUE(json_data.contains("base_value"));
    EXPECT_TRUE(json_data.contains("child_id"));
    EXPECT_TRUE(json_data.contains("child_name"));
    EXPECT_TRUE(json_data.contains("child_value"));

    EXPECT_EQ(json_data.get_value<int32_t>("base_id"), 100);
    EXPECT_EQ(json_data.get_value<std::string>("base_name"), std::string("dynamic_base"));
    EXPECT_FLOAT_EQ(json_data.get_value<float>("base_value"), 1.23F);
    EXPECT_EQ(json_data.get_value<int32_t>("child_id"), 200);
    EXPECT_EQ(json_data.get_value<std::string>("child_name"), std::string("dynamic_child"));
    EXPECT_DOUBLE_EQ(json_data.get_value<double>("child_value"), 4.56);
}

inline void dynamic_serializer_test_multiple_inheritance() {
    WBE::DataValue json_data;
    WBE::TestMultipleInheritanceChild child{};

    // Set fields from first parent (A)
    child.a_id = 111;
    child.a_name = "dynamic_a";
    child.a_value = 1.11F;

    // Set fields from second parent (B)
    child.b_id = 222;
    child.b_name = "dynamic_b";
    child.b_value = 2.22;

    // Set child's own fields
    child.child_id = 333;
    child.child_name = "dynamic_multi_child";
    child.child_vector = glm::vec2(9.9F, 8.8F);

    // Use dynamic serialization
    child.serialize(json_data);

    // Verify all fields from both parents and child are serialized
    EXPECT_TRUE(json_data.contains("a_id"));
    EXPECT_TRUE(json_data.contains("a_name"));
    EXPECT_TRUE(json_data.contains("a_value"));
    EXPECT_TRUE(json_data.contains("b_id"));
    EXPECT_TRUE(json_data.contains("b_name"));
    EXPECT_TRUE(json_data.contains("b_value"));
    EXPECT_TRUE(json_data.contains("child_id"));
    EXPECT_TRUE(json_data.contains("child_name"));
    EXPECT_TRUE(json_data.contains("child_vector"));

    EXPECT_EQ(json_data.get_value<int32_t>("a_id"), 111);
    EXPECT_EQ(json_data.get_value<std::string>("a_name"), std::string("dynamic_a"));
    EXPECT_FLOAT_EQ(json_data.get_value<float>("a_value"), 1.11F);
    EXPECT_EQ(json_data.get_value<int32_t>("b_id"), 222);
    EXPECT_EQ(json_data.get_value<std::string>("b_name"), std::string("dynamic_b"));
    EXPECT_DOUBLE_EQ(json_data.get_value<double>("b_value"), 2.22);
    EXPECT_EQ(json_data.get_value<int32_t>("child_id"), 333);
    EXPECT_EQ(json_data.get_value<std::string>("child_name"), std::string("dynamic_multi_child"));
    EXPECT_EQ(json_data.get_value<glm::vec2>("child_vector"), glm::vec2(9.9F, 8.8F));
}

inline void dynamic_serializer_test_polymorphism() {
    // Test polymorphic serialization through base pointer
    WBE::DataValue json_data_base, json_data_child;

    // Create objects
    WBE::TestInheritedBase base{};
    base.base_id = 42;
    base.base_name = "poly_base";
    base.base_value = 1.0F;

    WBE::TestInheritedChild child{};
    child.base_id = 84;
    child.base_name = "poly_child_base";
    child.base_value = 2.0F;
    child.child_id = 168;
    child.child_name = "poly_child";
    child.child_value = 3.0;

    // Test direct serialization
    base.serialize(json_data_base);
    child.serialize(json_data_child);

    // Verify base object serialization
    EXPECT_TRUE(json_data_base.contains("base_id"));
    EXPECT_TRUE(json_data_base.contains("base_name"));
    EXPECT_TRUE(json_data_base.contains("base_value"));
    EXPECT_FALSE(json_data_base.contains("child_id")); // Should not contain child fields
    EXPECT_FALSE(json_data_base.contains("child_name"));
    EXPECT_FALSE(json_data_base.contains("child_value"));

    // Verify child object serialization includes all fields
    EXPECT_TRUE(json_data_child.contains("base_id"));
    EXPECT_TRUE(json_data_child.contains("base_name"));
    EXPECT_TRUE(json_data_child.contains("base_value"));
    EXPECT_TRUE(json_data_child.contains("child_id"));
    EXPECT_TRUE(json_data_child.contains("child_name"));
    EXPECT_TRUE(json_data_child.contains("child_value"));

    EXPECT_EQ(json_data_child.get_value<int32_t>("base_id"), 84);
    EXPECT_EQ(json_data_child.get_value<std::string>("base_name"), std::string("poly_child_base"));
    EXPECT_FLOAT_EQ(json_data_child.get_value<float>("base_value"), 2.0F);
    EXPECT_EQ(json_data_child.get_value<int32_t>("child_id"), 168);
    EXPECT_EQ(json_data_child.get_value<std::string>("child_name"), std::string("poly_child"));
    EXPECT_DOUBLE_EQ(json_data_child.get_value<double>("child_value"), 3.0);
}

inline void dynamic_serializer_test_nested_objects() {
    WBE::DataValue json_data;
    WBE::TestInheritedWithNested obj{};

    // Set inherited base fields
    obj.base_id = 999;
    obj.base_name = "dynamic_nested_base";
    obj.base_value = 7.77F;

    // Set child fields
    obj.child_numbers = {10, 20, 30, 40};
    obj.nested_object.si32_test = 888;
    obj.nested_object.str_test = "dynamic_nested_string";
    strcpy(obj.nested_object.buffer_test.buffer, "dyn_nest");

    // Use dynamic serialization
    obj.serialize(json_data);

    // Check inherited base fields
    EXPECT_TRUE(json_data.contains("base_id"));
    EXPECT_TRUE(json_data.contains("base_name"));
    EXPECT_TRUE(json_data.contains("base_value"));
    EXPECT_EQ(json_data.get_value<int32_t>("base_id"), 999);
    EXPECT_EQ(json_data.get_value<std::string>("base_name"), std::string("dynamic_nested_base"));
    EXPECT_FLOAT_EQ(json_data.get_value<float>("base_value"), 7.77F);

    // Check child fields
    EXPECT_TRUE(json_data.contains("child_numbers"));
    EXPECT_TRUE(json_data.contains("nested_object"));

    auto numbers = json_data.get_value<WBE::DataValue>("child_numbers").get<std::vector<int>>();
    ASSERT_EQ(numbers.size(), 4U);
    EXPECT_EQ(numbers[0], 10);
    EXPECT_EQ(numbers[1], 20);
    EXPECT_EQ(numbers[2], 30);
    EXPECT_EQ(numbers[3], 40);

    auto nested = json_data.get_value<WBE::DataValue>("nested_object");
    EXPECT_EQ(nested.get_value<int32_t>("si32_test"), 888);
    EXPECT_EQ(nested.get_value<std::string>("str_test"), std::string("dynamic_nested_string"));
    EXPECT_EQ(nested.get_value<std::string>("buffer_test"), std::string("dyn_nest"));
}

// Dynamic dispatch test functions
inline void dynamic_dispatch_test_single_inheritance() {
    WBE::DataValue json_data_base, json_data_child;

    // Create derived object
    auto child = std::make_unique<WBE::TestInheritedChild>();
    child->base_id = 500;
    child->base_name = "dispatch_base";
    child->base_value = 5.5F;
    child->child_id = 600;
    child->child_name = "dispatch_child";
    child->child_value = 6.6;

    // Test dynamic dispatch through base pointer
    WBE::Serializable* base_ptr = child.get();
    base_ptr->serialize(json_data_base);

    // Also test direct child serialization for comparison
    child->serialize(json_data_child);

    // Both should produce identical results (dynamic dispatch should serialize the full derived object)
    EXPECT_TRUE(json_data_base.contains("base_id"));
    EXPECT_TRUE(json_data_base.contains("base_name"));
    EXPECT_TRUE(json_data_base.contains("base_value"));
    EXPECT_TRUE(json_data_base.contains("child_id"));
    EXPECT_TRUE(json_data_base.contains("child_name"));
    EXPECT_TRUE(json_data_base.contains("child_value"));

    // Verify values match
    EXPECT_EQ(json_data_base.get_value<int32_t>("base_id"), 500);
    EXPECT_EQ(json_data_base.get_value<std::string>("base_name"), std::string("dispatch_base"));
    EXPECT_FLOAT_EQ(json_data_base.get_value<float>("base_value"), 5.5F);
    EXPECT_EQ(json_data_base.get_value<int32_t>("child_id"), 600);
    EXPECT_EQ(json_data_base.get_value<std::string>("child_name"), std::string("dispatch_child"));
    EXPECT_DOUBLE_EQ(json_data_base.get_value<double>("child_value"), 6.6);

    // Results should be identical between base pointer and direct access
    EXPECT_EQ(json_data_base.get_value<int32_t>("base_id"), json_data_child.get_value<int32_t>("base_id"));
    EXPECT_EQ(json_data_base.get_value<std::string>("base_name"), json_data_child.get_value<std::string>("base_name"));
    EXPECT_FLOAT_EQ(json_data_base.get_value<float>("base_value"), json_data_child.get_value<float>("base_value"));
    EXPECT_EQ(json_data_base.get_value<int32_t>("child_id"), json_data_child.get_value<int32_t>("child_id"));
    EXPECT_EQ(json_data_base.get_value<std::string>("child_name"), json_data_child.get_value<std::string>("child_name"));
    EXPECT_DOUBLE_EQ(json_data_base.get_value<double>("child_value"), json_data_child.get_value<double>("child_value"));
}

inline void dynamic_dispatch_test_multilevel_inheritance() {
    WBE::DataValue json_data_base, json_data_child, json_data_grandchild;

    // Create grandchild object
    auto grandchild = std::make_unique<WBE::TestInheritedGrandchild>();
    grandchild->base_id = 100;
    grandchild->base_name = "dispatch_grandparent";
    grandchild->base_value = 1.0F;
    grandchild->child_id = 200;
    grandchild->child_name = "dispatch_parent";
    grandchild->child_value = 2.0;
    grandchild->grandchild_id = 300;
    grandchild->grandchild_name = "dispatch_grandchild";
    grandchild->grandchild_vector = glm::vec3(7.0F, 8.0F, 9.0F);

    // Test dynamic dispatch through different inheritance levels
    WBE::Serializable* base_ptr = grandchild.get();
    WBE::TestInheritedBase* intermediate_ptr = grandchild.get();
    WBE::TestInheritedChild* child_ptr = grandchild.get();

    base_ptr->serialize(json_data_base);
    intermediate_ptr->serialize(json_data_child);
    child_ptr->serialize(json_data_grandchild);

    // All should serialize the complete grandchild object due to dynamic dispatch
    auto verify_complete_serialization = [&](const WBE::DataValue& p_data, std::string_view p_test_name) {
        EXPECT_TRUE(p_data.contains("base_id")) << "Missing base_id in " << p_test_name;
        EXPECT_TRUE(p_data.contains("base_name")) << "Missing base_name in " << p_test_name;
        EXPECT_TRUE(p_data.contains("base_value")) << "Missing base_value in " << p_test_name;
        EXPECT_TRUE(p_data.contains("child_id")) << "Missing child_id in " << p_test_name;
        EXPECT_TRUE(p_data.contains("child_name")) << "Missing child_name in " << p_test_name;
        EXPECT_TRUE(p_data.contains("child_value")) << "Missing child_value in " << p_test_name;
        EXPECT_TRUE(p_data.contains("grandchild_id")) << "Missing grandchild_id in " << p_test_name;
        EXPECT_TRUE(p_data.contains("grandchild_name")) << "Missing grandchild_name in " << p_test_name;
        EXPECT_TRUE(p_data.contains("grandchild_vector")) << "Missing grandchild_vector in " << p_test_name;

        EXPECT_EQ(p_data.get_value<int32_t>("base_id"), 100);
        EXPECT_EQ(p_data.get_value<std::string>("base_name"), std::string("dispatch_grandparent"));
        EXPECT_FLOAT_EQ(p_data.get_value<float>("base_value"), 1.0F);
        EXPECT_EQ(p_data.get_value<int32_t>("child_id"), 200);
        EXPECT_EQ(p_data.get_value<std::string>("child_name"), std::string("dispatch_parent"));
        EXPECT_DOUBLE_EQ(p_data.get_value<double>("child_value"), 2.0);
        EXPECT_EQ(p_data.get_value<int32_t>("grandchild_id"), 300);
        EXPECT_EQ(p_data.get_value<std::string>("grandchild_name"), std::string("dispatch_grandchild"));
        EXPECT_EQ(p_data.get_value<glm::vec3>("grandchild_vector"), glm::vec3(7.0F, 8.0F, 9.0F));
    };

    verify_complete_serialization(json_data_base, "base_ptr");
    verify_complete_serialization(json_data_child, "intermediate_ptr");
    verify_complete_serialization(json_data_grandchild, "child_ptr");
}

inline void dynamic_dispatch_test_multiple_inheritance() {
    WBE::DataValue json_data_a, json_data_b;

    // Create multiple inheritance child
    auto child = std::make_unique<WBE::TestMultipleInheritanceChild>();
    child->a_id = 777;
    child->a_name = "dispatch_a";
    child->a_value = 7.77F;
    child->b_id = 888;
    child->b_name = "dispatch_b";
    child->b_value = 8.88;
    child->child_id = 999;
    child->child_name = "dispatch_multi_child";
    child->child_vector = glm::vec2(10.0F, 11.0F);

    // Test dynamic dispatch through different parent pointers
    WBE::TestMultipleInheritanceA* a_ptr = child.get();
    WBE::TestMultipleInheritanceB* b_ptr = child.get();

    a_ptr->serialize(json_data_a);
    b_ptr->serialize(json_data_b);

    // All should serialize the complete child object
    auto verify_complete_multi_serialization = [&](const WBE::DataValue& p_data, std::string_view p_test_name) {
        EXPECT_TRUE(p_data.contains("a_id")) << "Missing a_id in " << p_test_name;
        EXPECT_TRUE(p_data.contains("a_name")) << "Missing a_name in " << p_test_name;
        EXPECT_TRUE(p_data.contains("a_value")) << "Missing a_value in " << p_test_name;
        EXPECT_TRUE(p_data.contains("b_id")) << "Missing b_id in " << p_test_name;
        EXPECT_TRUE(p_data.contains("b_name")) << "Missing b_name in " << p_test_name;
        EXPECT_TRUE(p_data.contains("b_value")) << "Missing b_value in " << p_test_name;
        EXPECT_TRUE(p_data.contains("child_id")) << "Missing child_id in " << p_test_name;
        EXPECT_TRUE(p_data.contains("child_name")) << "Missing child_name in " << p_test_name;
        EXPECT_TRUE(p_data.contains("child_vector")) << "Missing child_vector in " << p_test_name;

        EXPECT_EQ(p_data.get_value<int32_t>("a_id"), 777);
        EXPECT_EQ(p_data.get_value<std::string>("a_name"), std::string("dispatch_a"));
        EXPECT_FLOAT_EQ(p_data.get_value<float>("a_value"), 7.77F);
        EXPECT_EQ(p_data.get_value<int32_t>("b_id"), 888);
        EXPECT_EQ(p_data.get_value<std::string>("b_name"), std::string("dispatch_b"));
        EXPECT_DOUBLE_EQ(p_data.get_value<double>("b_value"), 8.88);
        EXPECT_EQ(p_data.get_value<int32_t>("child_id"), 999);
        EXPECT_EQ(p_data.get_value<std::string>("child_name"), std::string("dispatch_multi_child"));
        EXPECT_EQ(p_data.get_value<glm::vec2>("child_vector"), glm::vec2(10.0F, 11.0F));
    };

    verify_complete_multi_serialization(json_data_a, "a_ptr");
    verify_complete_multi_serialization(json_data_b, "b_ptr");
}

inline void dynamic_dispatch_test_polymorphic_container() {
    // Test dynamic dispatch with polymorphic containers
    std::vector<std::unique_ptr<WBE::Serializable>> objects;

    // Create different types of objects
    auto base = std::make_unique<WBE::TestInheritedBase>();
    base->base_id = 1;
    base->base_name = "container_base";
    base->base_value = 1.1F;

    auto child = std::make_unique<WBE::TestInheritedChild>();
    child->base_id = 2;
    child->base_name = "container_child_base";
    child->base_value = 2.2F;
    child->child_id = 22;
    child->child_name = "container_child";
    child->child_value = 2.22;

    auto grandchild = std::make_unique<WBE::TestInheritedGrandchild>();
    grandchild->base_id = 3;
    grandchild->base_name = "container_grandchild_base";
    grandchild->base_value = 3.3F;
    grandchild->child_id = 33;
    grandchild->child_name = "container_grandchild_child";
    grandchild->child_value = 3.33;
    grandchild->grandchild_id = 333;
    grandchild->grandchild_name = "container_grandchild";
    grandchild->grandchild_vector = glm::vec3(3.0F, 3.0F, 3.0F);

    // Store in polymorphic container
    objects.push_back(std::move(base));
    objects.push_back(std::move(child));
    objects.push_back(std::move(grandchild));

    // Serialize each object through base pointer
    std::vector<WBE::DataValue> serialized_data(3);
    for (size_t i = 0; i < objects.size(); ++i) {
        objects[i]->serialize(serialized_data[i]);
    }

    // Verify first object (base) - should only have base fields
    EXPECT_TRUE(serialized_data[0].contains("base_id"));
    EXPECT_TRUE(serialized_data[0].contains("base_name"));
    EXPECT_TRUE(serialized_data[0].contains("base_value"));
    EXPECT_FALSE(serialized_data[0].contains("child_id"));
    EXPECT_FALSE(serialized_data[0].contains("grandchild_id"));

    // Verify second object (child) - should have base + child fields
    EXPECT_TRUE(serialized_data[1].contains("base_id"));
    EXPECT_TRUE(serialized_data[1].contains("base_name"));
    EXPECT_TRUE(serialized_data[1].contains("base_value"));
    EXPECT_TRUE(serialized_data[1].contains("child_id"));
    EXPECT_TRUE(serialized_data[1].contains("child_name"));
    EXPECT_TRUE(serialized_data[1].contains("child_value"));
    EXPECT_FALSE(serialized_data[1].contains("grandchild_id"));

    // Verify third object (grandchild) - should have all fields
    EXPECT_TRUE(serialized_data[2].contains("base_id"));
    EXPECT_TRUE(serialized_data[2].contains("base_name"));
    EXPECT_TRUE(serialized_data[2].contains("base_value"));
    EXPECT_TRUE(serialized_data[2].contains("child_id"));
    EXPECT_TRUE(serialized_data[2].contains("child_name"));
    EXPECT_TRUE(serialized_data[2].contains("child_value"));
    EXPECT_TRUE(serialized_data[2].contains("grandchild_id"));
    EXPECT_TRUE(serialized_data[2].contains("grandchild_name"));
    EXPECT_TRUE(serialized_data[2].contains("grandchild_vector"));

    // Verify values
    EXPECT_EQ(serialized_data[0].get_value<int32_t>("base_id"), 1);
    EXPECT_EQ(serialized_data[1].get_value<int32_t>("child_id"), 22);
    EXPECT_EQ(serialized_data[2].get_value<int32_t>("grandchild_id"), 333);
}

inline void dynamic_dispatch_test_reference_semantics() {
    WBE::DataValue json_data;

    // Test dynamic dispatch through references
    WBE::TestInheritedChild child{};
    child.base_id = 42;
    child.base_name = "ref_base";
    child.base_value = 4.2F;
    child.child_id = 84;
    child.child_name = "ref_child";
    child.child_value = 8.4;

    // Serialize through base reference
    WBE::Serializable& base_ref = child;
    base_ref.serialize(json_data);

    // Should serialize the complete child object
    EXPECT_TRUE(json_data.contains("base_id"));
    EXPECT_TRUE(json_data.contains("base_name"));
    EXPECT_TRUE(json_data.contains("base_value"));
    EXPECT_TRUE(json_data.contains("child_id"));
    EXPECT_TRUE(json_data.contains("child_name"));
    EXPECT_TRUE(json_data.contains("child_value"));

    EXPECT_EQ(json_data.get_value<int32_t>("base_id"), 42);
    EXPECT_EQ(json_data.get_value<std::string>("base_name"), std::string("ref_base"));
    EXPECT_FLOAT_EQ(json_data.get_value<float>("base_value"), 4.2F);
    EXPECT_EQ(json_data.get_value<int32_t>("child_id"), 84);
    EXPECT_EQ(json_data.get_value<std::string>("child_name"), std::string("ref_child"));
    EXPECT_DOUBLE_EQ(json_data.get_value<double>("child_value"), 8.4);
}

TEST_F(WBESerializerTest, DefaultConstruct) {
    WBE::DataValue json_data;
    serializer_test_default_construct(json_data);
    WBE::DataValue toml_data;
    serializer_test_default_construct(toml_data);
}

TEST_F(WBESerializerTest, General) {
    WBE::DataValue json_data;
    serializer_test_general(json_data);
    WBE::DataValue toml_data;
    serializer_test_general(toml_data);
}

TEST_F(WBESerializerTest, AliasFieldsSerializeCanonically) {
    WBE::DataValue json_data;
    serializer_test_aliases_use_canonical_names(json_data);
    WBE::DataValue toml_data;
    serializer_test_aliases_use_canonical_names(toml_data);
}

TEST_F(WBESerializerTest, NestingSerialize) {
    WBE::DataValue json_data;
    serializer_test_nesting(json_data);
    WBE::DataValue toml_data;
    serializer_test_nesting(toml_data);
}

TEST_F(WBESerializerTest, DeepNestingSerialize) {
    WBE::DataValue json_data;
    serializer_test_deep_nesting(json_data);
    WBE::DataValue toml_data;
    serializer_test_deep_nesting(toml_data);
}

TEST_F(WBESerializerTest, VectorPrimitivesSerialize) {
    WBE::DataValue json_data_int, json_data_strs;
    serializer_test_vector_primitives(json_data_int, json_data_strs);
    WBE::DataValue toml_data_int, toml_data_strs;
    serializer_test_vector_primitives(toml_data_int, toml_data_strs);
}

TEST_F(WBESerializerTest, VectorOfSerializableSerialize) {
    WBE::DataValue json_data;
    serializer_test_vector_serializables(json_data);
    WBE::DataValue toml_data;
    serializer_test_vector_serializables(toml_data);
}

TEST_F(WBESerializerTest, StructWithVectorFieldSerialize) {
    WBE::DataValue json_data;
    serializer_test_struct_with_vector_serializables(json_data);
    WBE::DataValue toml_data;
    serializer_test_struct_with_vector_serializables(toml_data);
}

TEST_F(WBESerializerTest, Depth2Serialize) {
    WBE::DataValue json_data;
    serializer_test_depth_2(json_data);
    WBE::DataValue toml_data;
    serializer_test_depth_2(toml_data);
}

TEST_F(WBESerializerTest, Depth3Serialize) {
    WBE::DataValue json_data;
    serializer_test_depth_3(json_data);
    WBE::DataValue toml_data;
    serializer_test_depth_3(toml_data);
}

TEST_F(WBESerializerTest, VectorOfDeepNestingSerialize) {
    WBE::DataValue json_data;
    serializer_vector_with_deep_nesting(json_data);
    WBE::DataValue toml_data;
    serializer_vector_with_deep_nesting(toml_data);
}

// Inheritance serialization tests
TEST_F(WBESerializerTest, InheritanceBaseSerialize) {
    WBE::DataValue json_data;
    serializer_test_inheritance_base(json_data);
    WBE::DataValue toml_data;
    serializer_test_inheritance_base(toml_data);
}

TEST_F(WBESerializerTest, InheritanceChildSerialize) {
    WBE::DataValue json_data;
    serializer_test_inheritance_child(json_data);
    WBE::DataValue toml_data;
    serializer_test_inheritance_child(toml_data);
}

TEST_F(WBESerializerTest, InheritanceMultilevelSerialize) {
    WBE::DataValue json_data;
    serializer_test_inheritance_multilevel(json_data);
    WBE::DataValue toml_data;
    serializer_test_inheritance_multilevel(toml_data);
}

TEST_F(WBESerializerTest, InheritanceWithNestedSerialize) {
    WBE::DataValue json_data;
    serializer_test_inheritance_with_nested(json_data);
    WBE::DataValue toml_data;
    serializer_test_inheritance_with_nested(toml_data);
}

TEST_F(WBESerializerTest, InheritanceVectorSerialize) {
    WBE::DataValue json_data;
    serializer_test_inheritance_vector(json_data);
    WBE::DataValue toml_data;
    serializer_test_inheritance_vector(toml_data);
}

// Multiple inheritance serialization tests
TEST_F(WBESerializerTest, MultipleInheritanceSerialize) {
    WBE::DataValue json_data;
    serializer_test_multiple_inheritance(json_data);
    WBE::DataValue toml_data;
    serializer_test_multiple_inheritance(toml_data);
}

TEST_F(WBESerializerTest, DiamondInheritanceSerialize) {
    WBE::DataValue json_data;
    serializer_test_diamond_inheritance(json_data);
    WBE::DataValue toml_data;
    serializer_test_diamond_inheritance(toml_data);
}

TEST_F(WBESerializerTest, MultipleInheritanceVectorSerialize) {
    WBE::DataValue json_data;
    serializer_test_multiple_inheritance_vector(json_data);
    WBE::DataValue toml_data;
    serializer_test_multiple_inheritance_vector(toml_data);
}

// Dynamic serialization tests (JSON only)
TEST_F(WBESerializerTest, DynamicSerializationBasic) {
    dynamic_serializer_test_basic();
}

TEST_F(WBESerializerTest, DynamicSerializationInheritance) {
    dynamic_serializer_test_inheritance();
}

TEST_F(WBESerializerTest, DynamicSerializationMultipleInheritance) {
    dynamic_serializer_test_multiple_inheritance();
}

TEST_F(WBESerializerTest, DynamicSerializationPolymorphism) {
    dynamic_serializer_test_polymorphism();
}

TEST_F(WBESerializerTest, DynamicSerializationNestedObjects) {
    dynamic_serializer_test_nested_objects();
}

// Dynamic dispatch tests
TEST_F(WBESerializerTest, DynamicDispatchSingleInheritance) {
    dynamic_dispatch_test_single_inheritance();
}

TEST_F(WBESerializerTest, DynamicDispatchMultilevelInheritance) {
    dynamic_dispatch_test_multilevel_inheritance();
}

TEST_F(WBESerializerTest, DynamicDispatchMultipleInheritance) {
    dynamic_dispatch_test_multiple_inheritance();
}

TEST_F(WBESerializerTest, DynamicDispatchPolymorphicContainer) {
    dynamic_dispatch_test_polymorphic_container();
}

TEST_F(WBESerializerTest, DynamicDispatchReferenceSemantics) {
    dynamic_dispatch_test_reference_semantics();
}

// Serialization tests for new required fields structures
template <typename ParserDataType>
inline void test_required_fields_serialization(ParserDataType p_data) {
    WBE::TestRequiredFieldsBase base_obj;
    base_obj.required_id = 123;
    base_obj.required_name = "test_base";
    base_obj.optional_value = 2.5F;

    WBE::SerializableSD<WBE::TestRequiredFieldsBase> base_sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestRequiredFieldsBase>::serialize(p_data, base_obj);

    EXPECT_TRUE(p_data.contains("required_id"));
    EXPECT_TRUE(p_data.contains("required_name"));
    EXPECT_TRUE(p_data.contains("optional_value"));
    EXPECT_EQ(p_data.template get_value<int32_t>("required_id"), 123);
    EXPECT_EQ(p_data.template get_value<std::string>("required_name"), "test_base");
    EXPECT_FLOAT_EQ(p_data.template get_value<float>("optional_value"), 2.5F);
}

template <typename ParserDataType>
inline void test_required_fields_child_serialization(ParserDataType p_data) {
    WBE::TestRequiredFieldsChild child_obj;
    child_obj.required_id = 456;
    child_obj.required_name = "test_child";
    child_obj.optional_value = 3.14F;
    child_obj.required_child_field = "child_data";
    child_obj.optional_child_value = 5.5;
    child_obj.required_vector = glm::vec3(1.0F, 2.0F, 3.0F);

    WBE::SerializableSD<WBE::TestRequiredFieldsChild> child_sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestRequiredFieldsChild>::serialize(p_data, child_obj);

    // Base class fields
    EXPECT_TRUE(p_data.contains("required_id"));
    EXPECT_TRUE(p_data.contains("required_name"));
    EXPECT_TRUE(p_data.contains("optional_value"));

    // Child class fields
    EXPECT_TRUE(p_data.contains("required_child_field"));
    EXPECT_TRUE(p_data.contains("optional_child_value"));
    EXPECT_TRUE(p_data.contains("required_vector"));

    EXPECT_EQ(p_data.template get_value<int32_t>("required_id"), 456);
    EXPECT_EQ(p_data.template get_value<std::string>("required_name"), "test_child");
    EXPECT_EQ(p_data.template get_value<std::string>("required_child_field"), "child_data");
    EXPECT_DOUBLE_EQ(p_data.template get_value<double>("optional_child_value"), 5.5);
}

template <typename ParserDataType>
inline void test_multiple_required_inheritance_serialization(ParserDataType p_data) {
    WBE::TestMultipleRequiredChild multi_obj;
    multi_obj.required_a_id = 100;
    multi_obj.optional_a_name = "a_name";
    multi_obj.required_b_value = 2.71F;
    multi_obj.optional_b_desc = "b_description";
    multi_obj.required_child_info = "child_info";
    multi_obj.optional_child_count = 42;

    WBE::SerializableSD<WBE::TestMultipleRequiredChild> multi_sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestMultipleRequiredChild>::serialize(p_data, multi_obj);

    EXPECT_TRUE(p_data.contains("required_a_id"));
    EXPECT_TRUE(p_data.contains("optional_a_name"));
    EXPECT_TRUE(p_data.contains("required_b_value"));
    EXPECT_TRUE(p_data.contains("optional_b_desc"));
    EXPECT_TRUE(p_data.contains("required_child_info"));
    EXPECT_TRUE(p_data.contains("optional_child_count"));

    EXPECT_EQ(p_data.template get_value<int32_t>("required_a_id"), 100);
    EXPECT_EQ(p_data.template get_value<std::string>("optional_a_name"), "a_name");
    EXPECT_FLOAT_EQ(p_data.template get_value<float>("required_b_value"), 2.71F);
    EXPECT_EQ(p_data.template get_value<std::string>("optional_b_desc"), "b_description");
    EXPECT_EQ(p_data.template get_value<std::string>("required_child_info"), "child_info");
    EXPECT_EQ(p_data.template get_value<int32_t>("optional_child_count"), 42);
}

template <typename ParserDataType>
inline void test_static_serializable_serialization(ParserDataType p_data) {
    WBE::TestStaticSerializable static_obj;
    static_obj.static_id = 789;
    static_obj.static_name = "static_test";
    static_obj.static_value = 9.87F;

    WBE::SerializableSD<WBE::TestStaticSerializable> static_sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestStaticSerializable>::serialize(p_data, static_obj);

    EXPECT_TRUE(p_data.contains("static_id"));
    EXPECT_TRUE(p_data.contains("static_name"));
    EXPECT_TRUE(p_data.contains("static_value"));

    EXPECT_EQ(p_data.template get_value<int32_t>("static_id"), 789);
    EXPECT_EQ(p_data.template get_value<std::string>("static_name"), "static_test");
    EXPECT_FLOAT_EQ(p_data.template get_value<float>("static_value"), 9.87F);
}

template <typename ParserDataType>
inline void test_static_with_required_serialization(ParserDataType p_data) {
    WBE::TestStaticWithRequired static_req_obj;
    static_req_obj.required_static_field = "static_required";
    static_req_obj.optional_static_number = 100;
    static_req_obj.required_static_vector = glm::vec3(7.0F, 8.0F, 9.0F);

    WBE::SerializableSD<WBE::TestStaticWithRequired> static_req_sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestStaticWithRequired>::serialize(p_data, static_req_obj);

    EXPECT_TRUE(p_data.contains("required_static_field"));
    EXPECT_TRUE(p_data.contains("optional_static_number"));
    EXPECT_TRUE(p_data.contains("required_static_vector"));

    EXPECT_EQ(p_data.template get_value<std::string>("required_static_field"), "static_required");
    EXPECT_EQ(p_data.template get_value<int32_t>("optional_static_number"), 100);
}

template <typename ParserDataType>
inline void test_static_complex_serialization(ParserDataType p_data) {
    WBE::TestStaticComplex static_complex_obj;
    static_complex_obj.static_numbers = {1, 2, 3, 4, 5};
    static_complex_obj.static_strings = {"static1", "static2"};
    strcpy(static_complex_obj.required_static_buffer.buffer, "static_buffer_data");
    static_complex_obj.nested_static.static_id = 555;
    static_complex_obj.nested_static.static_name = "nested_static";
    static_complex_obj.nested_static.static_value = 1.23F;

    WBE::SerializableSD<WBE::TestStaticComplex> static_complex_sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestStaticComplex>::serialize(p_data, static_complex_obj);

    EXPECT_TRUE(p_data.contains("static_numbers"));
    EXPECT_TRUE(p_data.contains("static_strings"));
    EXPECT_TRUE(p_data.contains("required_static_buffer"));
    EXPECT_TRUE(p_data.contains("nested_static"));

    auto numbers_array = p_data.template get_value<std::vector<ParserDataType>>("static_numbers");
    EXPECT_EQ(numbers_array.size(), 5);
    EXPECT_EQ(numbers_array[0].template get<int>(), 1);
    EXPECT_EQ(numbers_array[4].template get<int>(), 5);

    auto strings_array = p_data.template get_value<std::vector<ParserDataType>>("static_strings");
    EXPECT_EQ(strings_array.size(), 2);
    EXPECT_EQ(strings_array[0].template get<std::string>(), "static1");
    EXPECT_EQ(strings_array[1].template get<std::string>(), "static2");

    EXPECT_EQ(p_data.template get_value<std::string>("required_static_buffer"), "static_buffer_data");

    auto nested_static = p_data.template get_value<ParserDataType>("nested_static");
    EXPECT_TRUE(nested_static.contains("static_id"));
    EXPECT_TRUE(nested_static.contains("static_name"));
    EXPECT_TRUE(nested_static.contains("static_value"));
}

template <typename ParserDataType>
inline void test_mixed_container_serialization(ParserDataType p_data) {
    WBE::TestMixedContainer mixed_obj;

    // Add required objects
    WBE::TestRequiredFieldsBase req_obj1;
    req_obj1.required_id = 1;
    req_obj1.required_name = "obj1";
    req_obj1.optional_value = 1.1F;

    WBE::TestRequiredFieldsBase req_obj2;
    req_obj2.required_id = 2;
    req_obj2.required_name = "obj2";
    req_obj2.optional_value = 1.0F; // default

    mixed_obj.required_objects = {req_obj1, req_obj2};

    // Add static objects
    WBE::TestStaticSerializable static_obj;
    static_obj.static_id = 10;
    static_obj.static_name = "static1";
    static_obj.static_value = 2.2F;

    mixed_obj.static_objects = {static_obj};

    // Set required child
    mixed_obj.required_child.required_id = 999;
    mixed_obj.required_child.required_name = "child_test";
    mixed_obj.required_child.required_child_field = "child_value";
    mixed_obj.required_child.required_vector = glm::vec3(1.0F, 2.0F, 3.0F);

    // Set optional static
    mixed_obj.optional_static.static_numbers = {100, 200};
    strcpy(mixed_obj.optional_static.required_static_buffer.buffer, "mixed_buffer");

    WBE::SerializableSD<WBE::TestMixedContainer> mixed_sd;
    WhiteBirdEngine::SerializableSD<WhiteBirdEngine::TestMixedContainer>::serialize(p_data, mixed_obj);

    EXPECT_TRUE(p_data.contains("required_objects"));
    EXPECT_TRUE(p_data.contains("static_objects"));
    EXPECT_TRUE(p_data.contains("required_child"));
    EXPECT_TRUE(p_data.contains("optional_static"));

    auto req_objects_array = p_data.template get_value<std::vector<ParserDataType>>("required_objects");
    EXPECT_EQ(req_objects_array.size(), 2);
    auto req_obj_0 = req_objects_array[0].template get<ParserDataType>();
    EXPECT_EQ(req_obj_0.template get_value<int32_t>("required_id"), 1);
    EXPECT_EQ(req_obj_0.template get_value<std::string>("required_name"), "obj1");

    auto static_objects_array = p_data.template get_value<std::vector<ParserDataType>>("static_objects");
    EXPECT_EQ(static_objects_array.size(), 1);
    auto static_obj_0 = static_objects_array[0].template get<ParserDataType>();
    EXPECT_EQ(static_obj_0.template get_value<int32_t>("static_id"), 10);

    auto required_child = p_data.template get_value<ParserDataType>("required_child");
    EXPECT_EQ(required_child.template get_value<int32_t>("required_id"), 999);
    EXPECT_EQ(required_child.template get_value<std::string>("required_name"), "child_test");

    auto optional_static = p_data.template get_value<ParserDataType>("optional_static");
    EXPECT_TRUE(optional_static.contains("static_numbers"));
    EXPECT_TRUE(optional_static.contains("required_static_buffer"));
}

TEST_F(WBESerializerTest, RequiredFieldsBaseSerialization) {
    WBE::ParserJSON json_parser;
    const auto& json_data = json_parser.get_data();
    test_required_fields_serialization(json_data);

    WBE::ParserTOML toml_parser;
    const auto& toml_data = toml_parser.get_data();
    test_required_fields_serialization(toml_data);
}

TEST_F(WBESerializerTest, RequiredFieldsChildSerialization) {
    WBE::ParserJSON json_parser;
    const auto& json_data = json_parser.get_data();
    test_required_fields_child_serialization(json_data);

    WBE::ParserTOML toml_parser;
    const auto& toml_data = toml_parser.get_data();
    test_required_fields_child_serialization(toml_data);
}

TEST_F(WBESerializerTest, MultipleRequiredInheritanceSerialization) {
    WBE::ParserJSON json_parser;
    const auto& json_data = json_parser.get_data();
    test_multiple_required_inheritance_serialization(json_data);

    WBE::ParserTOML toml_parser;
    const auto& toml_data = toml_parser.get_data();
    test_multiple_required_inheritance_serialization(toml_data);
}

TEST_F(WBESerializerTest, StaticSerializableSerialization) {
    WBE::ParserJSON json_parser;
    const auto& json_data = json_parser.get_data();
    test_static_serializable_serialization(json_data);

    WBE::ParserTOML toml_parser;
    const auto& toml_data = toml_parser.get_data();
    test_static_serializable_serialization(toml_data);
}

TEST_F(WBESerializerTest, StaticWithRequiredSerialization) {
    WBE::ParserJSON json_parser;
    const auto& json_data = json_parser.get_data();
    test_static_with_required_serialization(json_data);

    WBE::ParserTOML toml_parser;
    const auto& toml_data = toml_parser.get_data();
    test_static_with_required_serialization(toml_data);
}

TEST_F(WBESerializerTest, StaticComplexSerialization) {
    WBE::ParserJSON json_parser;
    const auto& json_data = json_parser.get_data();
    test_static_complex_serialization(json_data);

    WBE::ParserTOML toml_parser;
    const auto& toml_data = toml_parser.get_data();
    test_static_complex_serialization(toml_data);
}

TEST_F(WBESerializerTest, MixedContainerSerialization) {
    WBE::ParserJSON json_parser;
    const auto& json_data = json_parser.get_data();
    test_mixed_container_serialization(json_data);

    WBE::ParserTOML toml_parser;
    const auto& toml_data = toml_parser.get_data();
    test_mixed_container_serialization(toml_data);
}

#endif
