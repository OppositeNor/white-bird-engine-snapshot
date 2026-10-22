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
#ifndef WBE_FILE_ENUM_REFLECTION_TEST_HH
#define WBE_FILE_ENUM_REFLECTION_TEST_HH

#include "core/data/data_value.hh"
#include "core/parser/parser_json.hh"
#include "core/parser/parser_toml.hh"
#include "core/reflection/enum_reflection.hh"
#include "generated/enum_reflection.gen.hh"
#include "generated/serializables_sd.gen.hh"
#include "reflection_test_data.hh"
#include "utils/utils.hh"
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace WBE = WhiteBirdEngine;

namespace {

using SignedReflection = WBE::EnumReflection<WBE::TestEnumSigned>;
using Unsigned64Reflection = WBE::EnumReflection<WBE::TestEnumUnsigned64>;
using NestedReflection = WBE::EnumReflection<WBE::TestEnumHolder::Nested>;

static_assert(WBE::ReflectedEnumConcept<WBE::TestEnumSigned>);
static_assert(WBE::ReflectedEnumConcept<WBE::TestEnumHolder::Nested>);
static_assert(!WBE::ReflectedEnumConcept<WBE::TestEnumUnreflected>);
static_assert(!WBE::ReflectedEnumConcept<int>);
static_assert(std::is_same_v<SignedReflection::UnderlyingType, int8_t>);
static_assert(std::is_same_v<Unsigned64Reflection::UnderlyingType, uint64_t>);
static_assert(SignedReflection::try_string_to_enum("minus") == WBE::TestEnumSigned::NEGATIVE);
static_assert(SignedReflection::try_enum_to_string(WBE::TestEnumSigned::POSITIVE) == "POSITIVE");

WBE::DataValue parse_enum_test_json(std::string_view p_json) {
    WBE::ParserJSON parser;
    parser.parse_from_buffer(p_json);
    return parser.get_data();
}

} // namespace

TEST(WBEEnumReflectionTest, ConvertsNamesAndAliases) {
    EXPECT_EQ(SignedReflection::string_to_enum("NEGATIVE"), WBE::TestEnumSigned::NEGATIVE);
    EXPECT_EQ(SignedReflection::string_to_enum("neg"), WBE::TestEnumSigned::NEGATIVE);
    EXPECT_EQ(SignedReflection::string_to_enum("minus"), WBE::TestEnumSigned::NEGATIVE);
    EXPECT_EQ(SignedReflection::string_to_enum("pos"), WBE::TestEnumSigned::POSITIVE);
    EXPECT_EQ(SignedReflection::enum_to_string(WBE::TestEnumSigned::NEGATIVE), "NEGATIVE");
    EXPECT_EQ(NestedReflection::string_to_enum("b"), WBE::TestEnumHolder::NESTED_B);
    EXPECT_EQ(NestedReflection::enum_to_string(WBE::TestEnumHolder::NESTED_A), "NESTED_A");
    EXPECT_EQ(NestedReflection::TYPE_NAME, "WhiteBirdEngine::TestEnumHolder::Nested");
}

TEST(WBEEnumReflectionTest, RejectsUnknownNamesCaseSensitively) {
    EXPECT_FALSE(SignedReflection::try_string_to_enum("Neg").has_value());
    EXPECT_FALSE(SignedReflection::try_string_to_enum("").has_value());
    EXPECT_THROW((void)SignedReflection::string_to_enum("negative"), std::runtime_error);
}

TEST(WBEEnumReflectionTest, SharedValueUsesFirstDeclaredName) {
    EXPECT_EQ(WBE::TestEnumSigned::SAME_AS_ZERO, WBE::TestEnumSigned::ZERO);
    EXPECT_EQ(SignedReflection::enum_to_string(WBE::TestEnumSigned::SAME_AS_ZERO), "ZERO");
    EXPECT_EQ(SignedReflection::string_to_enum("SAME_AS_ZERO"), WBE::TestEnumSigned::ZERO);
    EXPECT_EQ(SignedReflection::get_enumerators().size(), 4U);
    EXPECT_EQ(SignedReflection::get_names().size(), 7U);
}

TEST(WBEEnumReflectionTest, ConvertsDeclaredUnderlyingValuesOnly) {
    EXPECT_EQ(SignedReflection::from_underlying(-3), WBE::TestEnumSigned::NEGATIVE);
    EXPECT_EQ(SignedReflection::to_underlying(WBE::TestEnumSigned::POSITIVE), 100);
    EXPECT_FALSE(SignedReflection::try_from_underlying(5).has_value());
    EXPECT_THROW((void)SignedReflection::from_underlying(5), std::runtime_error);
    EXPECT_THROW((void)SignedReflection::enum_to_string(static_cast<WBE::TestEnumSigned>(7)), std::runtime_error);
    EXPECT_EQ(Unsigned64Reflection::from_underlying(0xFFFF'FFFF'FFFF'FFF0ULL), WBE::TestEnumUnsigned64::LARGE);
}

TEST(WBEEnumReflectionTest, IgnoredEnumeratorIsNotReflected) {
    EXPECT_EQ(Unsigned64Reflection::get_enumerators().size(), 2U);
    EXPECT_FALSE(Unsigned64Reflection::try_string_to_enum("IGNORED_SENTINEL").has_value());
    EXPECT_FALSE(Unsigned64Reflection::try_enum_to_string(WBE::TestEnumUnsigned64::IGNORED_SENTINEL).has_value());
    EXPECT_FALSE(Unsigned64Reflection::try_from_underlying(0xFFFF'FFFF'FFFF'FFF1ULL).has_value());
}

TEST(WBEEnumReflectionTest, EngineEnumsAcceptLegacyNames) {
    EXPECT_EQ(WBE::EnumReflection<WBE::ColorSpace>::string_to_enum("srgb"), WBE::ColorSpace::SRGB);
    EXPECT_EQ(WBE::EnumReflection<WBE::EncodeType>::string_to_enum("utf16be"), WBE::EncodeType::UTF16BE);
}

TEST(WBEEnumReflectionTest, JSONSerializesCanonicalNames) {
    WBE::TestEnumFields source;
    source.signed_value = WBE::TestEnumSigned::NEGATIVE;
    source.unsigned_value = WBE::TestEnumUnsigned64::LARGE;
    source.optional_value = WBE::TestEnumSigned::SAME_AS_ZERO;
    source.nested_values = {WBE::TestEnumHolder::NESTED_B, WBE::TestEnumHolder::NESTED_A};
    WBE::DataValue data;
    WBE::SerializableSD<WBE::TestEnumFields>::serialize(data, source);
    EXPECT_EQ(data.get_value<std::string>("signed_value"), "NEGATIVE");
    EXPECT_EQ(data.get_value<std::string>("unsigned_value"), "LARGE");
    EXPECT_EQ(data.get_value<std::string>("optional_value"), "ZERO");

    WBE::TestEnumFields result;
    WBE::SerializableSD<WBE::TestEnumFields>::deserialize(data, result);
    EXPECT_EQ(result.signed_value, source.signed_value);
    EXPECT_EQ(result.unsigned_value, source.unsigned_value);
    EXPECT_EQ(result.optional_value, std::optional<WBE::TestEnumSigned>(WBE::TestEnumSigned::ZERO));
    EXPECT_EQ(result.nested_values, source.nested_values);
}

TEST(WBEEnumReflectionTest, JSONDeserializesAliasesAndIntegers) {
    WBE::TestEnumFields result;
    WBE::SerializableSD<WBE::TestEnumFields>::deserialize(
        parse_enum_test_json(R"({"signed_value": -3, "unsigned_value": "LARGE", "nested_values": ["b", 0]})"), result);
    EXPECT_EQ(result.signed_value, WBE::TestEnumSigned::NEGATIVE);
    EXPECT_EQ(result.unsigned_value, WBE::TestEnumUnsigned64::LARGE);
    EXPECT_EQ(result.nested_values,
        (std::vector<WBE::TestEnumHolder::Nested>{WBE::TestEnumHolder::NESTED_B, WBE::TestEnumHolder::NESTED_A}));

    WBE::SerializableSD<WBE::TestEnumFields>::deserialize(parse_enum_test_json(R"({"signed_value": "pos"})"), result);
    EXPECT_EQ(result.signed_value, WBE::TestEnumSigned::POSITIVE);
}

TEST(WBEEnumReflectionTest, JSONRejectsUndeclaredOrOutOfRangeIntegers) {
    WBE::TestEnumFields result;
    // 300 does not fit int8_t; it must not wrap around to 44.
    EXPECT_THROW(WBE::SerializableSD<WBE::TestEnumFields>::deserialize(parse_enum_test_json(R"({"signed_value": 300})"), result),
        std::runtime_error);
    EXPECT_THROW(WBE::SerializableSD<WBE::TestEnumFields>::deserialize(parse_enum_test_json(R"({"signed_value": 5})"), result),
        std::runtime_error);
    EXPECT_THROW(WBE::SerializableSD<WBE::TestEnumFields>::deserialize(parse_enum_test_json(R"({"signed_value": "missing"})"), result),
        std::runtime_error);
    EXPECT_THROW(WBE::SerializableSD<WBE::TestEnumFields>::deserialize(parse_enum_test_json(R"({"nested_values": [-1]})"), result),
        std::runtime_error);
}

TEST(WBEEnumReflectionTest, TOMLAndScriptValueDeserialize) {
    WBE::ParserTOML toml;
    toml.parse_from_buffer("signed_value = \"neg\"\nunsigned_value = 1\nnested_values = [\"NESTED_B\"]\n");
    WBE::TestEnumFields toml_result;
    WBE::SerializableSD<WBE::TestEnumFields>::deserialize(toml.get_data(), toml_result);
    EXPECT_EQ(toml_result.signed_value, WBE::TestEnumSigned::NEGATIVE);
    EXPECT_EQ(toml_result.unsigned_value, WBE::TestEnumUnsigned64::SMALL);
    EXPECT_EQ(toml_result.nested_values, (std::vector<WBE::TestEnumHolder::Nested>{WBE::TestEnumHolder::NESTED_B}));

    WBE::DataValue script;
    script.set_entry("signed_value", WBE::DataValue::make_integer(100));
    script.set_entry("optional_value", WBE::DataValue::make_string("minus"));
    WBE::TestEnumFields script_result;
    WBE::SerializableSD<WBE::TestEnumFields>::deserialize(script, script_result);
    EXPECT_EQ(script_result.signed_value, WBE::TestEnumSigned::POSITIVE);
    EXPECT_EQ(script_result.optional_value, std::optional<WBE::TestEnumSigned>(WBE::TestEnumSigned::NEGATIVE));

    WBE::DataValue serialized;
    WBE::SerializableSD<WBE::TestEnumFields>::serialize(serialized, script_result);
    EXPECT_EQ(serialized.get_value<std::string>("signed_value"), "POSITIVE");
}

#endif
