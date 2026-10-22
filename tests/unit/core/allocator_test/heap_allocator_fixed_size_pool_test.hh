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
#ifndef WBE_FILE_HEAP_ALLOCATOR_FIXED_SIZE_POOL_TEST_HH
#define WBE_FILE_HEAP_ALLOCATOR_FIXED_SIZE_POOL_TEST_HH

#include "core/allocator/heap_allocator_fixed_size_pool.hh"
#include "core/allocator/i_allocator.hh"
#include "global/global.hh"
#include "test_global.hh"
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>

namespace WBE = WhiteBirdEngine;

class WBEAllocFSPTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;
};

TEST_F(WBEAllocFSPTest, Trait) {
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorFixedSizePool>::IS_POOL);
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorFixedSizePool>::IS_LIMITED_SIZE);
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorFixedSizePool>::IS_GURANTEED_CONTINUOUS);
    ASSERT_TRUE(WBE::AllocatorTrait<WBE::HeapAllocatorFixedSizePool>::IS_ALLOC_FIXED_SIZE);
    ASSERT_FALSE(WBE::AllocatorTrait<WBE::HeapAllocatorFixedSizePool>::IS_ATOMIC);
}

static size_t mem_diff(void* p_mem1, void* p_mem2) {
    return reinterpret_cast<size_t>(p_mem1) - reinterpret_cast<size_t>(p_mem2);
}

template <size_t ArrSize>
void test_continuous(WBE::HeapAllocatorFixedSizePool& p_pool, WBE::MemID p_mems[ArrSize]) {
    // One and only one should be at the beginning
    bool found_begin = false;
    WBE::MemID begin_id = p_mems[0];
    WBE::MemID max_addr_id = p_mems[0];
    for (uint32_t i = 0; i < ArrSize; ++i) {
        void* curr_addr = p_pool.get(p_mems[i]);
        if (curr_addr == p_pool.get_mem_start()) {
            if (found_begin) {
                ASSERT_TRUE(false);
            }
            found_begin = true;
            begin_id = p_mems[i];
        }
        if (curr_addr > p_pool.get(max_addr_id)) {
            max_addr_id = p_mems[i];
        }
    }
    ASSERT_TRUE(found_begin);
    // they should be ranged within a size of ArrSize * SIZE
    ASSERT_EQ(mem_diff(p_pool.get(max_addr_id), p_pool.get(begin_id)) + p_pool.get_element_size(), ArrSize * p_pool.get_element_size());
    // for each memid, they should have another id which is SIZE distance apart (except the last one)
    for (uint32_t i = 0; i < ArrSize; ++i) {
        if (p_mems[i] == max_addr_id) {
            continue;
        }
        bool found_next = false;
        for (uint32_t j = 0; j < ArrSize; ++j) {
            if (mem_diff(p_pool.get(p_mems[j]), p_pool.get(p_mems[i])) == p_pool.get_element_size()) {
                found_next = true;
            }
        }
        ASSERT_TRUE(found_next);
    }
}

TEST_F(WBEAllocFSPTest, MemContinuity) {
    WBE::HeapAllocatorFixedSizePool pool(16, 64);
    WBE::MemID mems[4];
    mems[0] = pool.allocate();
    mems[1] = pool.allocate();
    mems[2] = pool.allocate();
    mems[3] = pool.allocate();
    test_continuous<4>(pool, mems);
    pool.deallocate(mems[1]);
    mems[1] = mems[3];
    test_continuous<3>(pool, mems);
    for (uint32_t i = 0; i < 3; ++i) {
        pool.deallocate(mems[i]);
    }
    ASSERT_TRUE(pool.is_empty());
}

#endif
