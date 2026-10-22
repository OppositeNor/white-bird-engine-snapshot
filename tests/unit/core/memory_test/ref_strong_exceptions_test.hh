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
#ifndef WBE_FILE_REF_STRONG_EXCEPTIONS_TEST_HH
#define WBE_FILE_REF_STRONG_EXCEPTIONS_TEST_HH

#include "core/allocator/heap_allocator.hh"
#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "global/global.hh"
#include "mock_heap_allocator_aligned.hh"
#include "test_global.hh"
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

namespace WBE = WhiteBirdEngine;

class WBERefStrongExceptionsTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;
};

TEST_F(WBERefStrongExceptionsTest, MemIDOnlyConstructorThrowsOnNonNull) {
    WBE::MockHeapAllocatorAligned allocator(1024);
    // allocate an int to get a non-null MemID
    WBE::MemID id = WBE::create_obj<int>(allocator, 5);
    // constructing Ref<int> with a non-MEM_NULL MemID (without allocator) should throw
    ASSERT_THROW({ WBE::Ref<int> r(id); }, std::runtime_error);
    // cleanup
    WBE::destroy_obj<int>(allocator, id);
}

TEST_F(WBERefStrongExceptionsTest, CompareWithNonNullVoidPointerThrows) {
    WBE::Ref<int> ref; // default null
    void* non_null = reinterpret_cast<void*>(0x1234);
    ASSERT_THROW(
        {
            bool b = (ref == non_null);
            (void)b;
        },
        std::runtime_error);
}

TEST_F(WBERefStrongExceptionsTest, CompareWithNonNullMemIDThrows) {
    WBE::Ref<int> ref; // default null
    WBE::MemID non_null_id(42);
    ASSERT_THROW(
        {
            bool b = (ref == non_null_id);
            (void)b;
        },
        std::runtime_error);
}

#endif
