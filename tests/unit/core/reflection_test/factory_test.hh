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
#ifndef WBE_FILE_SERIALIZABLE_FACTORY_TEST_HH
#define WBE_FILE_SERIALIZABLE_FACTORY_TEST_HH

#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/parser/parser_json.hh"
#include "core/reflection/serializable.hh"
#include "generated/serializable_factory.gen.hh"
#include "generated/type_uuid.gen.hh"
#include "global/global.hh"
#include "reflection_test_data.hh"
#include "test_global.hh"

#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace WBE = WhiteBirdEngine;

class WBESerializableFactoryTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;
};

TEST_F(WBESerializableFactoryTest, CreatesSerializableObjectByName) {
    WBE::ParserJSON parser;
    parser.parse_from_buffer(R"({"si32_test":3,"str_test":"Hello!","optional_str_test":"Optional hello!"})");

    WBE::Unique<WBE::Serializable> created =
        WBE::SerializableFactory::create_serializable_object_unique("TestSerializable", parser.get_data());
    auto* serializable = dynamic_cast<WBE::TestSerializable*>(created.get());

    ASSERT_NE(serializable, nullptr);
    EXPECT_EQ(serializable->si32_test, 3);
    EXPECT_EQ(serializable->str_test, "Hello!");
    ASSERT_TRUE(serializable->optional_str_test.has_value());
    EXPECT_EQ(serializable->optional_str_test.value(), "Optional hello!");
}

TEST_F(WBESerializableFactoryTest, CreatesSerializableRefByName) {
    WBE::ParserJSON parser;
    parser.parse_from_buffer(R"({"si32_test":7,"str_test":"Hello ref!"})");

    WBE::Ref<WBE::Serializable> created =
        WBE::SerializableFactory::create_serializable_object_ref("TestSerializable", parser.get_data());
    auto* serializable = dynamic_cast<WBE::TestSerializable*>(created.get());

    ASSERT_NE(serializable, nullptr);
    EXPECT_EQ(serializable->si32_test, 7);
    EXPECT_EQ(serializable->str_test, "Hello ref!");
}

TEST_F(WBESerializableFactoryTest, CreatesSerializableByAliasName) {
    WBE::ParserJSON parser;
    parser.parse_from_buffer(R"({"display_name":"aliased","required_alias":55})");

    WBE::Unique<WBE::Serializable> created =
        WBE::SerializableFactory::create_serializable_object_unique("TestSerializableAlias", parser.get_data());
    auto* serializable = dynamic_cast<WBE::TestSerializableAliases*>(created.get());

    ASSERT_NE(serializable, nullptr);
    EXPECT_EQ(serializable->canonical_name, "aliased");
    EXPECT_EQ(serializable->required_value, 55);
}

TEST_F(WBESerializableFactoryTest, SupportsInPlaceConstruction) {
    WBE::ParserJSON parser;
    parser.parse_from_buffer(R"({"si32_test":9,"ui32_test":42,"str_test":"In place"})");

    alignas(WBE::TestSerializable) std::byte storage[sizeof(WBE::TestSerializable)];
    WBE::Serializable* created = WBE::SerializableFactory::create_serializable_object("TestSerializable", parser.get_data(), storage);
    auto* serializable = dynamic_cast<WBE::TestSerializable*>(created);

    ASSERT_EQ(created, static_cast<void*>(storage));
    EXPECT_EQ(serializable->si32_test, 9);
    EXPECT_EQ(serializable->ui32_test, 42U);
    EXPECT_EQ(serializable->str_test, "In place");

    serializable->~TestSerializable();
}

TEST_F(WBESerializableFactoryTest, ThrowsForUnknownSerializableName) {
    WBE::ParserJSON parser;
    parser.parse_from_buffer(R"({})");

    EXPECT_THROW(WBE::SerializableFactory::create_serializable_object_unique("MissingSerializable", parser.get_data()), std::runtime_error);
}

#endif
