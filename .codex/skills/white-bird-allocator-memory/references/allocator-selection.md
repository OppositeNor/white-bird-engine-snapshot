# Allocator selection details

Use this guide when a task needs a concrete allocator beyond the default, or needs to partition backing storage. Check the
header for constructor arguments and current traits before editing code. `HeapAllocator` and `HeapAllocatorAligned` are the
abstract bases.

| Need | Type and header | Contract that affects usage |
| --- | --- | --- |
| Engine default, thread-safe | `HeapAllocatorAtomicSizeClassified` in `core/allocator/heap_allocator_atomic_size_classified.hh` | Up to 8 size classes, each its own `HeapAllocatorAtomicArenaAlignedPoolImplicitList`; `make_default_size_classes()` builds small (< 1 KiB, 4 arenas), medium (1 KiB to 1 MiB, 4 arenas), and large (> 1 MiB, 1 arena). No spill between classes: a full class makes `allocate()` throw and `try_allocate()` return `MEM_NULL`. Sizes only, no `MemoryChunk` constructor. Neither copyable nor movable. |
| General standalone heap | `HeapAllocatorRAM` in `core/allocator/heap_allocator_ram.hh` | malloc-backed, unlimited, tracks outstanding IDs. Useful in isolated tests. |
| Aligned standalone heap | `HeapAllocatorAlignedRAM` in `core/allocator/heap_allocator_aligned_ram.hh` | malloc-backed, unlimited; alignment must satisfy its API. |
| Bounded LIFO | `StackAllocator` in `core/allocator/stack_allocator.hh` | `MemoryChunk`-backed, finite, not thread-safe. Pop in reverse order. |
| Finite variable-size pool | `HeapAllocatorPool` in `core/allocator/heap_allocator_pool.hh` | `MemoryChunk`-backed; free blocks coalesce. |
| Finite fixed-size pool | `HeapAllocatorFixedSizePool` in `core/allocator/heap_allocator_fixed_size_pool.hh` | The only allocator whose IDs can move (`WILL_ADDR_MOVE=true`); never an `STLAllocator` backend, and an ID is not an address. |
| Finite aligned pool | `HeapAllocatorAlignedPool` or `HeapAllocatorAlignedPoolImplicitList` | `get()` asserts pool membership in debug builds; `try_get()` skips the check. `RenderAllocator` is the implicit-list variant. |
| Shared concurrent pool | `HeapAllocatorAtomicMutexAlignedPoolImplicitList`, `HeapAllocatorAtomicSharedMutexAlignedPoolImplicitList`, `HeapAllocatorAtomicSharedMutexAlignedPool` | Allocator operations are synchronized; object access and lifetime still follow the caller's threading contract. |
| Arena allocation | `HeapAllocatorAtomicArenaAlignedPoolImplicitList`, `HeapAllocatorAtomicThreadLocalArenaAlignedPoolImplicitList` | One backing region split into fixed arenas; use `allocate_in_arena` only with a valid arena index. Sizes only. |

`IS_ATOMIC` is true only for the atomic mutex, shared-mutex, arena, thread-local arena, and size-classified allocators.

`MemoryChunk` (`core/allocator/memory_chunk.hh`) is a reference-counted handle to contiguous backing storage. The pool,
aligned-pool, atomic mutex/shared-mutex, fixed-size-pool, and stack constructors accept a checked
`(chunk, start_offset, size)` subrange. Keep the backing storage alive for the allocator and its users, and do not overlap
live ranges unintentionally.

`IAllocator::get()` resolves a valid ID; `try_get()` probes without debug membership checks. Neither makes a dangling ID
safe.
