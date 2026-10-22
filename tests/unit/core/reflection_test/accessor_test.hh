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
#ifndef WBE_FILE_ACCESSOR_TEST_HH
#define WBE_FILE_ACCESSOR_TEST_HH

#include "generated/serializables_access.gen.hh"
#include "global/global.hh"
#include "reflection_test_data.hh"
#include "test_global.hh"

#include <cstdint>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>

namespace WBE = WhiteBirdEngine;

class WBEDeserializerTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;
};

TEST_F(WBEDeserializerTest, General) {
    WBE::TestSerializable serializable;
    serializable.si32_test = 1;
    serializable.si64_test = -2;
    serializable.ui32_test = 42;
    serializable.ui64_test = 365;
    serializable.f32_test = 3.14F;
    serializable.f64_test = std::numbers::e;
    serializable.vec3_test = glm::vec3(3.0F, 2.0F, 1.0F);
    serializable.vec4_test = glm::vec4(1.0F, 2.0F, 3.0F, 4.0F);
    serializable.str_test = "Hello, world!";
    WBE::SerializableAccess<WBE::TestSerializable> accessor;
    EXPECT_EQ(accessor.get_field<int32_t>(serializable, "si32_test"), 1);
    EXPECT_EQ(accessor.get_field<int64_t>(serializable, "si64_test"), -2);
    EXPECT_EQ(accessor.get_field<uint32_t>(serializable, "ui32_test"), 42);
    EXPECT_EQ(accessor.get_field<uint64_t>(serializable, "ui64_test"), 365);
    ASSERT_THROW(accessor.get_field<uint32_t>(serializable, "some_field"), std::runtime_error);

    auto si32_member_ptr = accessor.get_pointer_to_member<int32_t>("si32_test");
    auto str_member_ptr = accessor.get_pointer_to_member<std::string>("str_test");
    EXPECT_EQ(serializable.*si32_member_ptr, 1);
    EXPECT_EQ(serializable.*str_member_ptr, "Hello, world!");
    serializable.*si32_member_ptr = 77;
    EXPECT_EQ(accessor.get_field<int32_t>(serializable, "si32_test"), 77);
    ASSERT_THROW(accessor.get_pointer_to_member<uint32_t>("some_field"), std::runtime_error);
    ASSERT_THROW(accessor.get_pointer_to_member<uint32_t>("si32_test"), std::runtime_error);
}

#endif
