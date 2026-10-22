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
#include "core/allocator/heap_allocator_aligned_pool.hh"
#include "core/allocator/heap_allocator_aligned_pool_impl_list.hh"
#include "core/allocator/heap_allocator_atomic_arena_aligned_pool_impl_list.hh"
#include "core/allocator/heap_allocator_atomic_mutex_aligned_pool_impl_list.hh"
#include "core/allocator/heap_allocator_atomic_shared_mutex_aligned_pool.hh"
#include "core/allocator/heap_allocator_atomic_shared_mutex_aligned_pool_impl_list.hh"
#include "core/allocator/heap_allocator_atomic_size_classified.hh"
// #include "core/allocator/heap_allocator_atomic_thread_local_arena_aligned_pool_impl_list.hh"
#include "core/allocator/i_allocator.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <benchmark/benchmark.h>
#include <cstddef>
#include <ranges>
#include <utility>
#include <vector>

namespace WBE = WhiteBirdEngine;

constexpr size_t ALLOC_NUM = 1000;
constexpr size_t POOL_SIZE = WBE_MI_B(128);
constexpr size_t FREE_BATCH = 10000;

// Construct a pool whose allocations of ALLOC_NUM ints get p_pool_size bytes.
template <typename Allocator>
static Allocator make_pool(size_t p_pool_size) {
    return Allocator(p_pool_size);
}

// The benchmark allocations fall in the medium class, which gets the whole pool size; the other classes stay minimal.
template <>
WBE::HeapAllocatorAtomicSizeClassified make_pool<WBE::HeapAllocatorAtomicSizeClassified>(size_t p_pool_size) {
    static_assert(ALLOC_NUM * sizeof(int) > WBE::HeapAllocatorAtomicSizeClassified::DEFAULT_SMALL_MAX_ALLOCATION_SIZE);
    static_assert(ALLOC_NUM * sizeof(int) <= WBE::HeapAllocatorAtomicSizeClassified::DEFAULT_MEDIUM_MAX_ALLOCATION_SIZE);
    return WBE::HeapAllocatorAtomicSizeClassified(
        WBE::HeapAllocatorAtomicSizeClassified::make_default_size_classes(WBE_MI_B(1), p_pool_size, WBE_MI_B(1)));
}

template <typename Allocator>
static void write_allocation(Allocator& p_pool, WBE::MemID p_mem) {
    int* result_ptr = static_cast<int*>(p_pool.get(p_mem));
    for (size_t i = 0; i < ALLOC_NUM; ++i) {
        result_ptr[i] = static_cast<int>(i);
    }
}

template <typename Allocated>
static void shuffle_allocated(std::vector<Allocated>& p_allocated, size_t p_counter) {
    size_t n = p_allocated.size();
    for (size_t k = 0; k < n; ++k) {
        std::swap(p_allocated[WBE::dynam_hash(p_counter + k) % FREE_BATCH], p_allocated[WBE::dynam_hash(p_counter + n + k) % FREE_BATCH]);
    }
}

template <typename Allocator>
static void deallocate_allocated(Allocator& p_pool, std::vector<WBE::MemID>& p_allocated, size_t p_counter) {
    if (p_counter % (2 * FREE_BATCH)) {
        for (auto queued_free : p_allocated) {
            p_pool.deallocate(queued_free);
        }
    } else {
        for (auto queued_free : p_allocated | std::views::reverse) {
            p_pool.deallocate(queued_free);
        }
    }
    p_allocated.clear();
}

template <typename Allocator, bool ShuffleEnabled>
static void run_heap_allocated_pool_benchmark(benchmark::State& p_state, size_t p_pool_size) {
    Allocator pool = make_pool<Allocator>(p_pool_size);
    std::vector<WBE::MemID> allocated;
    allocated.reserve(FREE_BATCH);
    size_t counter = 0;
    for (auto _ : p_state) {
        ++counter;
        WBE::MemID result = pool.allocate(ALLOC_NUM * sizeof(int));
        write_allocation(pool, result);
        allocated.push_back(result);
        if (counter % FREE_BATCH == 0) {
            if constexpr (ShuffleEnabled) {
                if (counter % (3 + FREE_BATCH)) {
                    shuffle_allocated(allocated, counter);
                }
            }
            deallocate_allocated(pool, allocated, counter);
        }
    }
    for (auto queued_free : allocated) {
        pool.deallocate(queued_free);
    }
}

template <bool ShuffleEnabled>
static void run_heap_allocated_arena_pool_benchmark(benchmark::State& p_state, size_t p_pool_size, size_t p_arena_count) {
    WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList pool(p_pool_size, p_arena_count);
    std::vector<WBE::MemID> allocated;
    allocated.reserve(FREE_BATCH);
    size_t counter = 0;
    for (auto _ : p_state) {
        ++counter;
        WBE::MemID result = pool.allocate(ALLOC_NUM * sizeof(int));
        write_allocation(pool, result);
        allocated.push_back(result);
        if (counter % FREE_BATCH == 0) {
            if constexpr (ShuffleEnabled) {
                if (counter % (3 + FREE_BATCH)) {
                    shuffle_allocated(allocated, counter);
                }
            }
            deallocate_allocated(pool, allocated, counter);
        }
    }
    for (auto queued_free : allocated) {
        pool.deallocate(queued_free);
    }
}

template <typename Allocator>
static void run_async_heap_allocated_pool_benchmark(benchmark::State& p_state) {
    static Allocator pool = make_pool<Allocator>(POOL_SIZE * 8);
    std::vector<WBE::MemID> allocated;
    allocated.reserve(FREE_BATCH);
    size_t counter = 0;
    for (auto _ : p_state) {
        ++counter;
        WBE::MemID result = pool.allocate(ALLOC_NUM * sizeof(int));
        write_allocation(pool, result);
        allocated.push_back(result);
        if (counter % FREE_BATCH == 0) {
            if (counter % (3 + FREE_BATCH)) {
                shuffle_allocated(allocated, counter);
            }
            deallocate_allocated(pool, allocated, counter);
        }
    }
    for (auto queued_free : allocated) {
        pool.deallocate(queued_free);
    }
}

template <size_t ArenaCount>
static void run_async_heap_allocated_arena_pool_benchmark(benchmark::State& p_state) {
    static WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList pool(POOL_SIZE * 8, ArenaCount);
    std::vector<WBE::MemID> allocated;
    allocated.reserve(FREE_BATCH);
    size_t counter = 0;
    for (auto _ : p_state) {
        ++counter;
        WBE::MemID result = pool.allocate(ALLOC_NUM * sizeof(int));
        write_allocation(pool, result);
        allocated.push_back(result);
        if (counter % FREE_BATCH == 0) {
            if (counter % (3 + FREE_BATCH)) {
                shuffle_allocated(allocated, counter);
            }
            deallocate_allocated(pool, allocated, counter);
        }
    }
    for (auto queued_free : allocated) {
        pool.deallocate(queued_free);
    }
}

static void malloc_free_benchmark_with_shuffle(benchmark::State& p_state) {
    std::vector<int*> allocated;
    allocated.reserve(FREE_BATCH);
    size_t counter = 0;
    for (auto _ : p_state) {
        ++counter;
        int* result = static_cast<int*>(malloc(ALLOC_NUM * sizeof(int))); // NOLINT
        for (size_t i = 0; i < ALLOC_NUM; ++i) {
            result[i] = static_cast<int>(i);
        }
        allocated.push_back(result);
        if (counter % FREE_BATCH == 0) {
            if (counter % (3 + FREE_BATCH)) {
                size_t n = allocated.size();
                for (size_t k = 0; k < n; ++k) {
                    std::swap(allocated[WBE::dynam_hash(counter + k) % FREE_BATCH],
                        allocated[WBE::dynam_hash(counter + n + k) % FREE_BATCH]);
                }
            }
            if (counter % (2 * FREE_BATCH)) {
                for (auto* queued_free : allocated) {
                    free(queued_free); // NOLINT
                }
            } else {
                for (auto* queued_free : allocated | std::views::reverse) {
                    free(queued_free); // NOLINT
                }
            }
            allocated.clear();
        }
    }
    for (auto* queued_free : allocated) {
        free(queued_free); // NOLINT
    }
}
BENCHMARK(malloc_free_benchmark_with_shuffle);

static void heap_allocated_aligned_pool_benchmark_with_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAlignedPool, true>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_aligned_pool_benchmark_with_shuffle);

static void heap_allocated_aligned_pool_impl_list_benchmark_with_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAlignedPoolImplicitList, true>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_aligned_pool_impl_list_benchmark_with_shuffle);

static void malloc_free_benchmark_without_shuffle(benchmark::State& p_state) {
    std::vector<int*> allocated;
    allocated.reserve(FREE_BATCH);
    size_t counter = 0;
    for (auto _ : p_state) {
        ++counter;
        int* result = static_cast<int*>(malloc(ALLOC_NUM * sizeof(int))); // NOLINT
        for (size_t i = 0; i < ALLOC_NUM; ++i) {
            result[i] = static_cast<int>(i);
        }
        allocated.push_back(result);
        if (counter % FREE_BATCH == 0) {
            if (counter % (2 * FREE_BATCH)) {
                for (auto* queued_free : allocated) {
                    free(queued_free); // NOLINT
                }
            } else {
                for (auto* queued_free : allocated | std::views::reverse) {
                    free(queued_free); // NOLINT
                }
            }
            allocated.clear();
        }
    }
    for (auto* queued_free : allocated) {
        free(queued_free); // NOLINT
    }
}
BENCHMARK(malloc_free_benchmark_without_shuffle);

static void heap_allocated_aligned_pool_benchmark_without_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAlignedPool, false>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_aligned_pool_benchmark_without_shuffle);

static void heap_allocated_aligned_pool_impl_list_benchmark_without_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAlignedPoolImplicitList, false>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_aligned_pool_impl_list_benchmark_without_shuffle);

static void heap_allocated_atomic_shared_mutex_aligned_pool_benchmark_with_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicSharedMutexAlignedPool, true>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_atomic_shared_mutex_aligned_pool_benchmark_with_shuffle);

static void heap_allocated_atomic_shared_mutex_aligned_pool_impl_list_benchmark_with_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicSharedMutexAlignedPoolImplicitList, true>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_atomic_shared_mutex_aligned_pool_impl_list_benchmark_with_shuffle);

static void heap_allocated_atomic_mutex_aligned_pool_impl_list_benchmark_with_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList, true>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_atomic_mutex_aligned_pool_impl_list_benchmark_with_shuffle);

static void heap_allocated_atomic_arena_aligned_pool_impl_list_benchmark_with_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList, true>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_atomic_arena_aligned_pool_impl_list_benchmark_with_shuffle);

static void heap_allocated_atomic_arena_aligned_pool_impl_list_2_arenas_benchmark_with_shuffle(benchmark::State& p_state) {
    run_heap_allocated_arena_pool_benchmark<true>(p_state, POOL_SIZE, 2);
}
BENCHMARK(heap_allocated_atomic_arena_aligned_pool_impl_list_2_arenas_benchmark_with_shuffle);

static void heap_allocated_atomic_arena_aligned_pool_impl_list_4_arenas_benchmark_with_shuffle(benchmark::State& p_state) {
    run_heap_allocated_arena_pool_benchmark<true>(p_state, POOL_SIZE, 4);
}
BENCHMARK(heap_allocated_atomic_arena_aligned_pool_impl_list_4_arenas_benchmark_with_shuffle);

static void heap_allocated_atomic_size_classified_benchmark_with_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicSizeClassified, true>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_atomic_size_classified_benchmark_with_shuffle);

// static void heap_allocated_atomic_thread_local_arena_aligned_pool_impl_list_benchmark_with_shuffle(benchmark::State& p_state) {
//    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicThreadLocalArenaAlignedPoolImplicitList, true>(p_state, POOL_SIZE);
// }
// BENCHMARK(heap_allocated_atomic_thread_local_arena_aligned_pool_impl_list_benchmark_with_shuffle);

static void heap_allocated_atomic_shared_mutex_aligned_pool_benchmark_without_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicSharedMutexAlignedPool, false>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_atomic_shared_mutex_aligned_pool_benchmark_without_shuffle);

static void heap_allocated_atomic_shared_mutex_aligned_pool_impl_list_benchmark_without_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicSharedMutexAlignedPoolImplicitList, false>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_atomic_shared_mutex_aligned_pool_impl_list_benchmark_without_shuffle);

static void heap_allocated_atomic_mutex_aligned_pool_impl_list_benchmark_without_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList, false>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_atomic_mutex_aligned_pool_impl_list_benchmark_without_shuffle);

static void heap_allocated_atomic_arena_aligned_pool_impl_list_benchmark_without_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList, false>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_atomic_arena_aligned_pool_impl_list_benchmark_without_shuffle);

static void heap_allocated_atomic_arena_aligned_pool_impl_list_2_arenas_benchmark_without_shuffle(benchmark::State& p_state) {
    run_heap_allocated_arena_pool_benchmark<false>(p_state, POOL_SIZE, 2);
}
BENCHMARK(heap_allocated_atomic_arena_aligned_pool_impl_list_2_arenas_benchmark_without_shuffle);

static void heap_allocated_atomic_arena_aligned_pool_impl_list_4_arenas_benchmark_without_shuffle(benchmark::State& p_state) {
    run_heap_allocated_arena_pool_benchmark<false>(p_state, POOL_SIZE, 4);
}
BENCHMARK(heap_allocated_atomic_arena_aligned_pool_impl_list_4_arenas_benchmark_without_shuffle);

static void heap_allocated_atomic_size_classified_benchmark_without_shuffle(benchmark::State& p_state) {
    run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicSizeClassified, false>(p_state, POOL_SIZE);
}
BENCHMARK(heap_allocated_atomic_size_classified_benchmark_without_shuffle);

// static void heap_allocated_atomic_thread_local_arena_aligned_pool_impl_list_benchmark_without_shuffle(benchmark::State& p_state) {
//     run_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicThreadLocalArenaAlignedPoolImplicitList, false>(p_state, POOL_SIZE);
// }
// BENCHMARK(heap_allocated_atomic_thread_local_arena_aligned_pool_impl_list_benchmark_without_shuffle);

static void async_malloc_free_benchmark(benchmark::State& p_state) {
    std::vector<int*> allocated;
    allocated.reserve(FREE_BATCH);
    size_t counter = 0;
    for (auto _ : p_state) {
        ++counter;
        int* result = static_cast<int*>(malloc(ALLOC_NUM * sizeof(int))); // NOLINT
        for (size_t i = 0; i < ALLOC_NUM; ++i) {
            result[i] = static_cast<int>(i);
        }
        allocated.push_back(result);
        if (counter % FREE_BATCH == 0) {
            if (counter % (3 + FREE_BATCH)) {
                size_t n = allocated.size();
                for (size_t k = 0; k < n; ++k) {
                    std::swap(allocated[WBE::dynam_hash(counter + k) % FREE_BATCH],
                        allocated[WBE::dynam_hash(counter + n + k) % FREE_BATCH]);
                }
            }
            if (counter % (2 * FREE_BATCH)) {
                for (auto* queued_free : allocated) {
                    free(queued_free); // NOLINT
                }
            } else {
                for (auto* queued_free : allocated | std::views::reverse) {
                    free(queued_free); // NOLINT
                }
            }
            allocated.clear();
        }
    }
    for (auto* queued_free : allocated) {
        free(queued_free); // NOLINT
    }
}
BENCHMARK(async_malloc_free_benchmark)->Threads(2)->Threads(4)->Threads(8);

static void async_heap_allocated_atomic_shared_mutex_aligned_pool_benchmark(benchmark::State& p_state) {
    run_async_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicSharedMutexAlignedPool>(p_state);
}
BENCHMARK(async_heap_allocated_atomic_shared_mutex_aligned_pool_benchmark)->Threads(2)->Threads(4)->Threads(8);

static void async_heap_allocated_atomic_shared_mutex_aligned_pool_impl_list_benchmark(benchmark::State& p_state) {
    run_async_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicSharedMutexAlignedPoolImplicitList>(p_state);
}
BENCHMARK(async_heap_allocated_atomic_shared_mutex_aligned_pool_impl_list_benchmark)->Threads(2)->Threads(4)->Threads(8);

static void async_heap_allocated_atomic_mutex_aligned_pool_impl_list_benchmark(benchmark::State& p_state) {
    run_async_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicMutexAlignedPoolImplicitList>(p_state);
}
BENCHMARK(async_heap_allocated_atomic_mutex_aligned_pool_impl_list_benchmark)->Threads(2)->Threads(4)->Threads(8);

static void async_heap_allocated_atomic_arena_aligned_pool_impl_list_benchmark(benchmark::State& p_state) {
    run_async_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList>(p_state);
}
BENCHMARK(async_heap_allocated_atomic_arena_aligned_pool_impl_list_benchmark)->Threads(2)->Threads(4)->Threads(8);

static void async_heap_allocated_atomic_arena_aligned_pool_impl_list_2_arenas_benchmark(benchmark::State& p_state) {
    run_async_heap_allocated_arena_pool_benchmark<2>(p_state);
}
BENCHMARK(async_heap_allocated_atomic_arena_aligned_pool_impl_list_2_arenas_benchmark)->Threads(2)->Threads(4)->Threads(8);

static void async_heap_allocated_atomic_arena_aligned_pool_impl_list_4_arenas_benchmark(benchmark::State& p_state) {
    run_async_heap_allocated_arena_pool_benchmark<4>(p_state);
}
BENCHMARK(async_heap_allocated_atomic_arena_aligned_pool_impl_list_4_arenas_benchmark)->Threads(2)->Threads(4)->Threads(8);

static void async_heap_allocated_atomic_size_classified_benchmark(benchmark::State& p_state) {
    run_async_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicSizeClassified>(p_state);
}
BENCHMARK(async_heap_allocated_atomic_size_classified_benchmark)->Threads(2)->Threads(4)->Threads(8);

// static void async_heap_allocated_atomic_thread_local_arena_aligned_pool_impl_list_benchmark(benchmark::State& p_state) {
//     run_async_heap_allocated_pool_benchmark<WBE::HeapAllocatorAtomicThreadLocalArenaAlignedPoolImplicitList>(p_state);
// }
// BENCHMARK(async_heap_allocated_atomic_thread_local_arena_aligned_pool_impl_list_benchmark)->Threads(2)->Threads(4)->Threads(8);

BENCHMARK_MAIN();
