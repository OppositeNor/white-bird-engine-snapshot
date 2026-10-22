---
name: white-bird-allocator-memory
description: Use White Bird Engine's allocator and memory modules when choosing an allocator, sizing engine memory pools, constructing allocator-owned objects, using Ref/RefWeak/Unique/RefRaw/RefRes handles, or creating allocator-aware containers. Apply to C++ engine code that allocates or transfers ownership; do not use for unrelated memory profiling or GPU resource allocation.
---

# White Bird Allocator and Memory

Use the engine's ownership types and pass the allocator explicitly. A handle or container never owns its allocator: keep the
allocator alive until every object, container, `Ref` control block, and `RefWeak` observer using it is gone. A `MemID` comes
from one allocator and must be returned to that allocator. `MEM_NULL` is the null ID.

## Choose ownership before allocating

| Need | API | Lifetime |
| --- | --- | --- |
| One owner | `Unique<T>` via `make_unique<T>(allocator, args...)` | Move-only; destruction or `reset()` destroys the object and frees its storage. `make_unique` always returns `Unique<T, HeapAllocator>`, whatever the allocator's concrete type. |
| Shared ownership | `Ref<T, AllocType>` via `make_ref<T>(allocator, args...)` | The last strong handle destroys the object; the control block remains until weak handles are gone. |
| Observe a shared object | `RefWeak<T, AllocType>` from a valid `Ref` | `lock()` and check the returned `Ref` before access; it does not extend lifetime. |
| Use an object without owning it | `RefRaw<T, AllocType, false>`; resources use `RefRes<T>` (`RefRaw<T, IAllocator, false>`, in `resource/resource_unit/resource_unit.hh`) | Copyable view; its destructor and `delete_ref()` never destroy the object. |
| Explicit manual ownership | free `new_ref<T>(allocator, args...)` with `delete_ref(std::move(handle))`; `new_ref_stack` / `delete_ref_stack` for a `StackAllocator` | The free factories default to `AllowDestruct=true`. Copies alias; destroying one leaves the others dangling. Prefer `Unique` or `Ref`. |

Include the matching header under `include/core/memory/`. Do not use `std::shared_ptr`, `std::weak_ptr`, `std::unique_ptr`,
`std::make_*`, or `std::allocator` in engine code, except as short-lived locals required by a third-party API. A `RefRaw` made
from a `Unique` is a view, and the `Unique` must outlive it. The class template `RefRaw<T>` defaults `AllowDestruct=false`, so
`RefRaw<T>::delete_ref()` does not destroy; use the free `new_ref` / `delete_ref` pair for manual ownership.

## Choose the allocator

- `HeapAllocatorDefault` (`core/core_utils.hh`) is `HeapAllocatorAtomicSizeClassified`, a thread-safe allocator with
  separate small (< 1 KiB), medium (1 KiB to 1 MiB), and large (> 1 MiB) classes. `global_allocator()` (`core/engine_core.hh`)
  returns the engine's instance, sized by `global_mem_small_pool_size`, `global_mem_medium_pool_size`, and
  `global_mem_large_pool_size` in the engine config. Classes never spill into each other, and one allocation must fit in one
  arena of its class, so a large allocation is bounded by the large pool (16 MiB under `make_test_global()`). Do not let users
  of `global_allocator()` outlive engine shutdown.
- Render code uses `RenderAllocator` (`function/render/render_defs.hh`, `HeapAllocatorAlignedPoolImplicitList`); preserve that
  choice in renderer code.
- Use `HeapAllocatorRAM` for a simple unlimited heap, including focused unit tests. Choose a pool or arena only when its
  capacity, alignment, and threading properties fit the caller.
- Use `StackAllocator` for bounded LIFO work: `create_stack_obj<T>()`, then `pop_stack_obj<T>()` in reverse order; `clear()`
  invalidates outstanding IDs.
- Use `Vector`, `Set`, `HashSet`, `HashMap`, `Map` (flat map), `Deque`, and `String` from `core/allocator/stl_allocator.hh`
  with an explicit allocator type and instance. `STLAllocator` requires `AllocatorTrait<AllocType>::WILL_ADDR_MOVE == false`,
  and its allocator must outlive the container.
- For manual construction, pair `create_obj<T>` with `destroy_obj<T>` and `create_array<T>` with `destroy_array<T>`
  (`core/allocator/heap_allocator.hh`). These run destructors; raw `deallocate` only releases storage.

Use `allocate()` for required allocations and `try_allocate()` only where failure is an expected, recoverable result.

Read [references/allocator-selection.md](references/allocator-selection.md) when selecting a concrete pool, aligned, atomic,
arena, or size-classified allocator, or when working with `MemoryChunk` subranges. Verify the type's current `AllocatorTrait`
and constructor in its header.

## Work and validate

Before changing allocation code, trace who owns each object, who owns the allocator, and whether an ID can move. Document
copy and move behavior for allocator-aware resource types (Rule of 6). Test destruction order, moved-from and null handles,
allocator emptiness, and expected allocation failure where the contract depends on them. Suites live under
`tests/unit/core/allocator_test/` and `tests/unit/core/memory_test/`; allocator benchmarks under `tests/benchmark/core/allocator/`.

When a test exhausts memory, shrink its allocations or pass a larger pool to `make_test_global()`; never run test processes in
parallel. Validate with `python validation.py -t debug -- --gtest_filter='RelevantSuite*'`; the wrapper then runs the full
suite. Do not include lint.
