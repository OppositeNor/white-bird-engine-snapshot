# Conventions

`AGENTS.md` at the repository root is the complete, authoritative convention list. This page summarizes the rules that
shape the engine's design.

## Layers

The engine is layered, from upper to lower:

1. `global`: engine-wide primitives.
2. `function`: high-level subsystems (rendering, ECS, events, scripting glue).
3. `resource`: resource units, asset loading, the resource registry.
4. `core`: allocators, memory handles, jobs, parsers, `DataValue`, reflection support, math and utilities.
5. `platform`: OS, windowing, file system, renderer backends.

An upper layer may depend on a lower one; a lower layer must never depend on an upper one. Templates are forbidden in the
exposed API of `global`.

## Errors

- Procedures crash on failure: engine errors are thrown as `std::runtime_error` and are never caught outside tests.
- An explicitly named `try_` alternative exists only where failure is an expected, recoverable outcome (for example
  `allocate()` throws, while `try_allocate()` returns `MEM_NULL`).
- Implementation assumptions guaranteed by callers are expressed with `WBE_DEBUG_ASSERT`.

## Memory

Engine code uses the engine's handles (`Ref`, `RefWeak`, `Unique`, `RefRaw` / `RefRes`) and allocator-aware containers
(`Vector`, `HashMap`, ...) with an explicit allocator, not `std::shared_ptr`, `std::unique_ptr`, or `std::allocator`.
Resource-owning types define or delete all six special member functions explicitly.

## Threading

Render objects and renderer-backend helpers are confined to the rendering thread. Other threads submit work through
`IRenderer::add_job()`.

## Naming

- `snake_case` for files, functions, variables, and members; `PascalCase` for types; `UPPER_SNAKE` for macros.
- Function parameters are prefixed with `p_`.
- Derived implementations carry their interface's abbreviation as a prefix (`ROImageVK`, `RTRunComputePipelineVK`).
- No abbreviations unless universally known. `rma` is the accepted abbreviation and texture role for the roughness,
  metallic, and ambient-occlusion channel pack.
- All engine code lives in the single named namespace `WhiteBirdEngine`.

## Data and build

- Configuration and resource manifests are TOML; ACP emits runtime manifests as JSON.
- Reflection generates `*.gen.*` files and Teal declarations before the ACP runs. Never edit generated files.
- Build only through `python build.py`, and validate with `python validation.py`.
