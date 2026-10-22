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
#ifndef WBE_FILE_STRICT_DESERIALIZATION_TEST_HH
#define WBE_FILE_STRICT_DESERIALIZATION_TEST_HH

#include "core/data/data_value.hh"
#include "core/parser/parser_json.hh"
#include "core/parser/parser_toml.hh"
#include "core/reflection/serializable_sd.hh"
#include "generated/serializables_sd.gen.hh"
#include "reflection_test_data.hh"
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <string_view>

namespace WBE = WhiteBirdEngine;

namespace {

WBE::DataValue parse_strict_test_json(std::string_view p_json) {
    WBE::ParserJSON parser;
    parser.parse_from_buffer(p_json);
    return parser.get_data();
}

template <typename T>
std::string strict_deserialization_error(const WBE::DataValue& p_data) {
    T result{};
    try {
        WBE::SerializableSD<T>::deserialize(p_data, result);
    } catch (const std::runtime_error& p_error) {
        return p_error.what();
    }
    return {};
}

} // namespace

TEST(WBEStrictDeserializationTest, AcceptsFieldsAndAliases) {
    WBE::TestStrictBase result;
    WBE::SerializableSD<WBE::TestStrictBase>::deserialize(parse_strict_test_json(R"({"base_value": 3, "base_alias": "aliased"})"), result);
    EXPECT_EQ(result.base_value, 3);
    EXPECT_EQ(result.base_name, "aliased");
    EXPECT_NO_THROW(WBE::SerializableSD<WBE::TestStrictBase>::deserialize(parse_strict_test_json("{}"), result));
}

TEST(WBEStrictDeserializationTest, RejectsUnknownKeyNamingKeyAndType) {
    const std::string error = strict_deserialization_error<WBE::TestStrictBase>(parse_strict_test_json(R"({"base_valeu": 3})"));
    EXPECT_NE(error.find("base_valeu"), std::string::npos) << error;
    EXPECT_NE(error.find("TestStrictBase"), std::string::npos) << error;
}

TEST(WBEStrictDeserializationTest, DerivedClassAcceptsBaseFields) {
    WBE::TestStrictChild result;
    WBE::SerializableSD<WBE::TestStrictChild>::deserialize(
        parse_strict_test_json(R"({"base_value": 1, "base_name": "base", "child_value": 2})"), result);
    EXPECT_EQ(result.base_value, 1);
    EXPECT_EQ(result.base_name, "base");
    EXPECT_EQ(result.child_value, 2);
    EXPECT_NE(strict_deserialization_error<WBE::TestStrictChild>(parse_strict_test_json(R"({"child_value": 2, "extra": 1})")), "");
    // Required fields are still enforced.
    EXPECT_NE(strict_deserialization_error<WBE::TestStrictChild>(parse_strict_test_json(R"({"base_value": 1})")), "");
}

TEST(WBEStrictDeserializationTest, StrictBaseAloneRejectsDerivedKeys) {
    EXPECT_NE(strict_deserialization_error<WBE::TestStrictBase>(parse_strict_test_json(R"({"base_value": 1, "child_value": 2})")), "");
}

TEST(WBEStrictDeserializationTest, StrictClassAcceptsLenientBaseFields) {
    WBE::TestStrictOverLenient result;
    WBE::SerializableSD<WBE::TestStrictOverLenient>::deserialize(
        parse_strict_test_json(R"({"lenient_base_value": 4, "strict_value": 5})"), result);
    EXPECT_EQ(result.lenient_base_value, 4);
    EXPECT_EQ(result.strict_value, 5);
    EXPECT_NE(strict_deserialization_error<WBE::TestStrictOverLenient>(parse_strict_test_json(R"({"unknown": 1})")), "");
}

TEST(WBEStrictDeserializationTest, StrictMemberIsCheckedInsideLenientHolder) {
    WBE::TestLenientHolder result;
    // The holder is lenient, so an unknown top-level key is ignored.
    WBE::SerializableSD<WBE::TestLenientHolder>::deserialize(
        parse_strict_test_json(R"({"holder_value": 6, "ignored": true, "strict_member": {"base_value": 7}})"), result);
    EXPECT_EQ(result.holder_value, 6);
    EXPECT_EQ(result.strict_member.base_value, 7);
    // The strict member is checked.
    EXPECT_NE(strict_deserialization_error<WBE::TestLenientHolder>(parse_strict_test_json(R"({"strict_member": {"unknown": 1}})")), "");
}

TEST(WBEStrictDeserializationTest, LenientClassesStillIgnoreUnknownKeys) {
    WBE::TestPartialStruct result;
    EXPECT_NO_THROW(WBE::SerializableSD<WBE::TestPartialStruct>::deserialize(
        parse_strict_test_json(R"({"si32_test": 1, "not_a_field": 2})"), result));
    EXPECT_EQ(result.si32_test, 1);
}

TEST(WBEStrictDeserializationTest, AppliesToTOMLAndScriptValue) {
    WBE::ParserTOML toml;
    toml.parse_from_buffer("base_value = 1\nbase_valeu = 2\n");
    WBE::TestStrictBase toml_result;
    EXPECT_THROW(WBE::SerializableSD<WBE::TestStrictBase>::deserialize(toml.get_data(), toml_result), std::runtime_error);

    WBE::DataValue script;
    script.set_entry("base_value", WBE::DataValue::make_integer(1));
    WBE::TestStrictBase script_result;
    WBE::SerializableSD<WBE::TestStrictBase>::deserialize(script, script_result);
    EXPECT_EQ(script_result.base_value, 1);
    script.set_entry("base_valeu", WBE::DataValue::make_integer(2));
    EXPECT_THROW(WBE::SerializableSD<WBE::TestStrictBase>::deserialize(script, script_result), std::runtime_error);
}

#endif
