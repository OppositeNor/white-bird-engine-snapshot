/* Copyright 2026 OppositeNor

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
#include "function/render/shader_debug.hh"
#include "generated/enum_reflection.gen.hh"

#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>

namespace WhiteBirdEngine {

TEST(WBEShaderDebugTest, BufferLayoutReservesHeaderAndEntries) {
    EXPECT_EQ(SHADER_DEBUG_DESCRIPTOR_INDEX, 0U);
    EXPECT_EQ(SHADER_DEBUG_FRAME_ENTRY_CAPACITY, 512U);
    EXPECT_EQ(SHADER_DEBUG_FRAME_WORD_COUNT, 514U);
    EXPECT_EQ(SHADER_DEBUG_FRAME_BYTE_SIZE, 2056U);
}

TEST(WBEShaderDebugTest, ErrorNamesAreStable) {
    EXPECT_EQ(EnumReflection<ShaderDebugError>::enum_to_string(ShaderDebugError::DEBUG_BUFFER_FULL), "DEBUG_BUFFER_FULL");
    EXPECT_EQ(EnumReflection<ShaderDebugError>::enum_to_string(ShaderDebugError::SVO_NODE_INDEX_OVERFLOW), "SVO_NODE_INDEX_OVERFLOW");
    EXPECT_EQ(EnumReflection<ShaderDebugError>::enum_to_string(ShaderDebugError::SVO_LEAF_INDEX_OVERFLOW), "SVO_LEAF_INDEX_OVERFLOW");
    EXPECT_EQ(EnumReflection<ShaderDebugError>::enum_to_string(ShaderDebugError::SVO_DEFERRED_TASK_INDEX_OVERFLOW),
        "SVO_DEFERRED_TASK_INDEX_OVERFLOW");
    EXPECT_EQ(EnumReflection<ShaderDebugError>::enum_to_string(ShaderDebugError::SVO_DEEPER_THAN_EXPECTED), "SVO_DEEPER_THAN_EXPECTED");
    EXPECT_EQ(EnumReflection<ShaderDebugError>::enum_to_string(ShaderDebugError::SVO_SHALLOWER_THAN_EXPECTED), "SVO_SHALLOWER_THAN_EXPECTED");
    EXPECT_EQ(EnumReflection<ShaderDebugError>::enum_to_string(ShaderDebugError::OCTREE_LEAF_LIST_OVERFLOW), "OCTREE_LEAF_LIST_OVERFLOW");
    EXPECT_EQ(EnumReflection<ShaderDebugError>::enum_to_string(ShaderDebugError::OCTREE_DEBUG_NODE_LIST_OVERFLOW),
        "OCTREE_DEBUG_NODE_LIST_OVERFLOW");
    EXPECT_EQ(EnumReflection<ShaderDebugError>::enum_to_string(ShaderDebugError::SVO_VOXEL_DATA_INDEX_OVERFLOW),
        "SVO_VOXEL_DATA_INDEX_OVERFLOW");
    EXPECT_FALSE(EnumReflection<ShaderDebugError>::try_enum_to_string(static_cast<ShaderDebugError>(UINT32_MAX)).has_value());
    EXPECT_THROW((void)EnumReflection<ShaderDebugError>::enum_to_string(static_cast<ShaderDebugError>(UINT32_MAX)), std::runtime_error);
}

} // namespace WhiteBirdEngine
