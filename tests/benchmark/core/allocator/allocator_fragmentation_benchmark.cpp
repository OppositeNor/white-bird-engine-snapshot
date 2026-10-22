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
#include "core/allocator/heap_allocator_atomic_arena_aligned_pool_impl_list.hh"
#include "core/allocator/heap_allocator_atomic_size_classified.hh"
#include "core/allocator/i_allocator.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <array>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <vector>

// Fragmentation benchmarks. Each allocator gets the same total capacity, the default size-classified capacities, and
// runs a deterministic workload. Fragmentation is reported through counters rather than time:
// - *_failures: allocations that failed although the allocator as a whole had enough free bytes for them.
// - largest_allocation_mib: the largest single allocation that still succeeds after the workload.
// - free_mib: the total free bytes after the workload.
// - external_fragmentation: 1 - largest_allocation / free, so 0 means the free space is one contiguous block.

namespace WBE = WhiteBirdEngine;

namespace {

constexpr size_t TOTAL_CAPACITY = WBE::HeapAllocatorAtomicSizeClassified::DEFAULT_SMALL_CAPACITY +
                                  WBE::HeapAllocatorAtomicSizeClassified::DEFAULT_MEDIUM_CAPACITY +
                                  WBE::HeapAllocatorAtomicSizeClassified::DEFAULT_LARGE_CAPACITY;
constexpr size_t ARENA_COUNT = 4;
static_assert(TOTAL_CAPACITY % ARENA_COUNT == 0);

constexpr size_t CHURN_LIVE_SLOTS = 1024;
constexpr size_t CHURN_OPERATIONS = 200000;
constexpr size_t PINNING_SMALL_SIZE = 64;
constexpr size_t PINNING_MEDIUM_SIZE = WBE_KI_B(64);
constexpr size_t PINNING_LARGE_SIZE = WBE_MI_B(2);

enum class SizeKind : size_t {
    SMALL,
    MEDIUM,
    LARGE,
};

struct Allocation {
    WBE::MemID mem = WBE::MEM_NULL;
    size_t size = 0;
};

struct FragmentationStats {
    std::array<size_t, 3> failures{};
    std::array<size_t, 3> attempts{};
};

// 85% small (16 B to 1 KiB), 14% medium (1 KiB to 128 KiB), 1% large (2 MiB to 16 MiB).
Allocation pick_allocation(size_t p_operation, SizeKind& p_kind) {
    const size_t kind_roll = WBE::dynam_hash(p_operation * 3) % 100;
    const size_t size_roll = WBE::dynam_hash(p_operation * 3 + 1);
    if (kind_roll < 85) {
        p_kind = SizeKind::SMALL;
        return Allocation{.size = 16 + size_roll % (WBE_KI_B(1) - 16)};
    }
    if (kind_roll < 99) {
        p_kind = SizeKind::MEDIUM;
        return Allocation{.size = (WBE_KI_B(1) << (size_roll % 8)) + size_roll % WBE_KI_B(1)};
    }
    p_kind = SizeKind::LARGE;
    return Allocation{.size = WBE_MI_B(2) + size_roll % WBE_MI_B(14)};
}

// Binary search the largest single allocation that still succeeds.
template <typename Allocator>
size_t probe_largest_allocation(Allocator& p_allocator) {
    size_t low = 0;
    size_t high = TOTAL_CAPACITY;
    while (low < high) {
        const size_t middle = low + (high - low + 1) / 2;
        WBE::MemID mem = p_allocator.try_allocate(middle);
        if (mem != WBE::MEM_NULL) {
            p_allocator.deallocate(mem);
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

template <typename Allocator>
void report(benchmark::State& p_state, Allocator& p_allocator, const FragmentationStats& p_stats) {
    const size_t largest = probe_largest_allocation(p_allocator);
    const size_t free_size = p_allocator.get_remain_size();
    p_state.counters["small_failures"] = static_cast<double>(p_stats.failures[0]);
    p_state.counters["medium_failures"] = static_cast<double>(p_stats.failures[1]);
    p_state.counters["large_failures"] = static_cast<double>(p_stats.failures[2]);
    p_state.counters["large_attempts"] = static_cast<double>(p_stats.attempts[2]);
    p_state.counters["largest_allocation_mib"] = static_cast<double>(largest) / static_cast<double>(WBE_MI_B(1));
    p_state.counters["free_mib"] = static_cast<double>(free_size) / static_cast<double>(WBE_MI_B(1));
    p_state.counters["external_fragmentation"] =
        free_size == 0 ? 0.0 : 1.0 - (static_cast<double>(largest) / static_cast<double>(free_size));
}

template <typename Allocator, typename... Args>
void run_mixed_churn_benchmark(benchmark::State& p_state, Args... p_args) {
    Allocator allocator(p_args...);
    std::vector<Allocation> live(CHURN_LIVE_SLOTS);
    FragmentationStats stats;
    size_t operation = 0;
    for (auto _ : p_state) {
        Allocation& slot = live[WBE::dynam_hash(operation * 3 + 2) % CHURN_LIVE_SLOTS];
        if (slot.mem != WBE::MEM_NULL) {
            allocator.deallocate(slot.mem);
            slot = Allocation{};
        }
        SizeKind kind = SizeKind::SMALL;
        Allocation allocation = pick_allocation(operation, kind);
        ++stats.attempts[static_cast<size_t>(kind)];
        allocation.mem = allocator.try_allocate(allocation.size);
        if (allocation.mem == WBE::MEM_NULL) {
            ++stats.failures[static_cast<size_t>(kind)];
        } else {
            slot = allocation;
        }
        ++operation;
    }
    report(p_state, allocator, stats);
    for (const Allocation& allocation : live) {
        allocator.deallocate(allocation.mem);
    }
}

template <typename Allocator, typename... Args>
void run_small_pinning_benchmark(benchmark::State& p_state, Args... p_args) {
    Allocator allocator(p_args...);
    std::vector<WBE::MemID> small_allocations;
    std::vector<WBE::MemID> medium_allocations;
    std::vector<WBE::MemID> large_allocations;
    FragmentationStats stats;
    for (auto _ : p_state) {
        // Fill the allocator with interleaved long-lived small and medium allocations, then free the medium ones,
        // leaving holes pinned by the small allocations.
        while (true) {
            WBE::MemID small = allocator.try_allocate(PINNING_SMALL_SIZE);
            WBE::MemID medium = allocator.try_allocate(PINNING_MEDIUM_SIZE);
            if (small == WBE::MEM_NULL || medium == WBE::MEM_NULL) {
                allocator.deallocate(small);
                allocator.deallocate(medium);
                break;
            }
            small_allocations.push_back(small);
            medium_allocations.push_back(medium);
        }
        const size_t freed_size = medium_allocations.size() * PINNING_MEDIUM_SIZE;
        for (WBE::MemID mem : medium_allocations) {
            allocator.deallocate(mem);
        }
        medium_allocations.clear();
        // Then try to reuse as many bytes as were freed with large allocations.
        for (size_t i = 0; i < freed_size / PINNING_LARGE_SIZE; ++i) {
            ++stats.attempts[static_cast<size_t>(SizeKind::LARGE)];
            WBE::MemID mem = allocator.try_allocate(PINNING_LARGE_SIZE);
            if (mem == WBE::MEM_NULL) {
                ++stats.failures[static_cast<size_t>(SizeKind::LARGE)];
            } else {
                large_allocations.push_back(mem);
            }
        }
    }
    for (WBE::MemID mem : large_allocations) {
        allocator.deallocate(mem);
    }
    report(p_state, allocator, stats);
    p_state.counters["pinning_small_allocations"] = static_cast<double>(small_allocations.size());
    for (WBE::MemID mem : small_allocations) {
        allocator.deallocate(mem);
    }
}

void fragmentation_mixed_churn_size_classified(benchmark::State& p_state) {
    run_mixed_churn_benchmark<WBE::HeapAllocatorAtomicSizeClassified>(p_state);
}

void fragmentation_mixed_churn_arena_4(benchmark::State& p_state) {
    run_mixed_churn_benchmark<WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList>(p_state, TOTAL_CAPACITY, ARENA_COUNT);
}

void fragmentation_small_pinning_size_classified(benchmark::State& p_state) {
    run_small_pinning_benchmark<WBE::HeapAllocatorAtomicSizeClassified>(p_state);
}

void fragmentation_small_pinning_arena_4(benchmark::State& p_state) {
    run_small_pinning_benchmark<WBE::HeapAllocatorAtomicArenaAlignedPoolImplicitList>(p_state, TOTAL_CAPACITY, ARENA_COUNT);
}

} // namespace

// Fixed iterations keep the workloads, and therefore the counters, deterministic.
BENCHMARK(fragmentation_mixed_churn_size_classified)->Iterations(CHURN_OPERATIONS)->Unit(benchmark::kMillisecond);
BENCHMARK(fragmentation_mixed_churn_arena_4)->Iterations(CHURN_OPERATIONS)->Unit(benchmark::kMillisecond);
BENCHMARK(fragmentation_small_pinning_size_classified)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(fragmentation_small_pinning_arena_4)->Iterations(1)->Unit(benchmark::kMillisecond);
