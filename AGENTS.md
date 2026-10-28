# White Bird Engine Agent Guide

## Project Overview

This is a C++20 engine project.

Follow repository conventions and existing patterns.

---

## General Rules

- Make the smallest change that solves the problem.
- Do not modify unrelated code.
- Do not modify generated files.
- Do not modify dependencies.
- Preserve existing APIs unless explicitly required.
- Use only one named namespace: `WhiteBirdEngine`. Do not introduce other named namespaces in engine or test-support code unless the user explicitly asks for it. Use anonymous namespaces for internal linkage in `.cpp` files instead of `static` free functions, in accordance with clang-tidy.

---

## Agent Workflow

Before implementing a feature or utility, check whether it already exists, whether an equivalent is embedded in another
implementation, or whether existing implementation-specific logic should be abstracted and reused.

For non-trivial implementation tasks:

1. Use repo-explorer when repository context is unclear.
2. Use investigator first for debugging, root cause analysis, and other investigation work.
3. Start implementation only after the investigation findings are clear.

---

## Change Discipline

- Make the smallest change that solves the problem.
- Do not refactor unrelated code.
- Do not rename existing APIs unless explicitly requested.
- Do not modify generated files.
- Do not modify dependencies.
- Before editing multiple files, verify that each file is required.

## Default-Safe Convention

- Procedures must default to crashing on failure. The ordinary API must not silently return a sentinel, invalid object, or
  partial result when it cannot fulfill its contract.
- Provide an explicitly named `try_` alternative only when failure is an expected, recoverable outcome. For example,
  `allocate()` crashes if allocation fails, while `try_allocate()` may return `MEM_NULL` to report an allowed allocation
  failure.
- Private helpers and other implementation details may omit runtime checks for assumptions guaranteed by their callers, such as
  pointer parameters being non-null, when avoiding the check is useful for performance. Express each such precondition with
  `WBE_DEBUG_ASSERT` at the point of use.
- `WBE_DEBUG_ASSERT` is a valid guard under this convention for implementation assumptions. Do not substitute a silent release
  fallback unless failure is explicitly part of the API contract.

## Rendering Threading

- Render objects and renderer-backend helpers are confined to the rendering thread. Their methods are not required to provide
  internal synchronization.
- Code running on any other thread must not access or operate on render objects directly. Submit that work through the
  renderer's job buffer with `IRenderer::add_job()` so it executes on the rendering thread.
- Do not add locks to render objects or renderer-backend helpers solely to support cross-thread access; preserve the
  render-thread confinement contract instead.

## Vulkan Render Graph

- Render graphs are built only from resource dependency edges declared with `RendTaskDescVK::connect(next_task_id, resource_id)`;
  the graph must be a DAG. Edge stages and accesses are derived from the `RendResUsageVK` usages both tasks report for the
  resource, and both tasks must access it. Declared reads and writes do not create edges. Every read-write,
  write-read, and write-write conflict on a resource must be ordered by a chain of edges on that same resource; unordered conflicts
  make graph construction fail. Same-queue edges become pipeline barriers and cross-queue edges use timeline semaphores.
- The graph compiler allocates graph-owned buffers, images, image views, and samplers declared with `RendGraphDescVK::add_buffer()`,
  `add_image()`, `add_image_view()`, and `add_sampler()`. Use `add_resource(id, *bindable)` only to import resources owned outside
  the graph; every import is an `IBindableRenderResourceVK`, and composites register their members under `<id>.<member>`.
- Tasks report every access themselves through `IRenderTaskVK::append_accesses()` (reflected shader resources, render targets, and
  fixed-function buffers); there is no descriptor-level access declaration. Graphics tasks render into `render_target_colors[<index>]`
  and `render_target_depth`. The only way to map task-local identifiers to graph resources is `RendTaskDescVK::alias()`, backed by
  the task's `RenderTaskResourceMap`; tasks do not take resource maps.
- Every graph is registered with `RendererVK::register_render_graph(key, RendGraphRebuildCallbackVK)`; there is no
  descriptor registration. The callback builds the complete descriptor and runs for the first build and on every
  rebuild (context change or `rebuild()`), so keep one-time setup (pipeline compiles, application-owned buffers,
  initial-data snapshots) outside it and only assemble the descriptor inside.
- Rebuilds diff the new descriptor against the current graph (`RendGraphResourceDiffVK`): a graph-owned resource is kept,
  with its content, only when its id, kind, per-frame layout, and evaluated description match and, for a view, its image
  is kept; changed configurations are recreated in place in their containers, while kind or per-frame changes get a new
  container. Imports are matched by object identity. Keep the description structs' `operator==` complete when adding
  fields, since the diff relies on them.
- The render graph binds the renderer-owned descriptor heaps before recording each task. Pipeline-run tasks push their reflected
  descriptor-index data; do not reintroduce descriptor-set or pipeline-layout objects.

### Render Graph Recipes

- A recipe is a Teal `script` resource whose module exports `build(context)`. It creates a graph with
  `Graph.new()` (`render_graph_utils.render_graph_utils`), declares resources as handles, adds tasks with the typed
  constructors, and returns `graph:build()`, which emits the plain `RendGraphRecipeVK` table (`imports`, `resources`,
  `tasks`; see `rend_graph_recipe_params_vk.hh`). `RendGraphScriptVK` runs it and
  `RendGraphRecipeBuilderVK` turns it into a `RendGraphDescVK`; recipes only build descriptors and never touch render objects.
- `context` holds `RendGraphScriptVK::make_context()` facts (`RendRecipeContextVK`: `frames_in_flight`,
  `swap_chain_format`, `swap_chain_extent`). An application that adds entries declares a `WBE_REFLECT_SCRIPT` struct
  deriving from `RendRecipeContextVK`, fills its base with `get_recipe_context()`, and passes `serialize_context()` to
  `RendGraphScriptVK::run()`. Size swap-chain-dependent resources and dispatches in Lua from `swap_chain_extent`.
- For a recipe graph, the application's rebuild callback runs the script, builds the descriptor, and adds its C++ tasks.
  Never pass script keys to the graph.
- Every resource owned outside the graph must be declared with `graph:import_resource()` and supplied through `RendGraphRecipeImportsVK`; a
  declared-but-missing or supplied-but-undeclared import is an error. Recipes cannot set initial data, so buffers that need
  it are application-owned imports, registered in the render database under their import name.
- Each resource declares exactly one of `buffer`, `image`, `image_view`, or `sampler`; `graph:build()` emits only those
  reachable from tasks. Tasks name a registered task type and pass `params` to it; aliases (`task:alias()`,
  `color_target()`, `depth_target()`) map task-local ids to graph resources, and `uses` only keeps resources reachable.
  Edges come only from explicit connections (`graph:chain()`, `task:connect()`, `connect_all()`, `inputs_from()`),
  emitted as `connect` entries (`{ to = <task id>, resource = <resource id> }`). An optional `trigger` wraps the task with `RTTriggeredVK`, and an
  optional `toggle` (`{ event, enabled }`) wraps it with `RTToggleVK`, outside any trigger. Rebuilds re-run the recipe, so
  pass a toggle's current state through the context.
- Task `pipeline` params are render-database keys; the application compiles those pipelines and keeps them alive.
- Draw meshes with `graphics_draw_mesh` (`RTGraphicsDrawMeshVK`, or `RTGraphicsDrawMeshVK::add_to_graph()` in C++); do not
  assemble `RTRunGraphicsPipelineIndirectVK` mesh draws by hand. Register each compiled mesh in the render database under
  the graph id its draws name (`mesh`), which they import it under, so shared meshes are compiled and imported once.
  `pipeline` maps material pipeline variants to pipelines (`pipeline = { default = ..., foliage = ... }`); draws batch per
  variant, and the task's pipelines must share render targets and one material texture contract.
- A rebuild callback may add to the descriptor `RendGraphRecipeBuilderVK::build()` returns, for parts not yet expressed
  in Lua (the deferred test adds its acceleration structure and dynamic mesh entities that way); keep everything else in
  the recipe.
- A C++ feature that contributes to a recipe describes itself with a `configure_recipe()` that serializes the
  `WBE_REFLECT_SCRIPT` parameters struct of its Lua feature module (including the `module` name) into the context and supplies its resources and C++ tasks to the
  `RendGraphRecipeImportsVK`; the recipe calls `require(params.module).subgraph(id, params, target)`, which returns the
  feature as a subgraph task. Work that runs once, such as a static
  build, is a `trigger` activated while it is still pending; the application disables the trigger event after the first
  recorded frame, so later rebuilds create it inactive. Task state that must follow runtime input across rebuilds is a
  `toggle` whose event the application emits and whose current state it passes through the context.
- A subgraph groups tasks, graph-owned resources, and internal edges under one id (`RendGraphDescVK::add_subgraph()`;
  in a recipe, a child graph's `graph:as_subgraph(id, ports)`, which emits a task with a nested recipe in `subgraph` and
  its `ports` bound in `ports`). It is flattened when it is added: its task and owned resource ids become
  `<id>.<name>`, and names inside it resolve to a port's bound resource, else its own resource, else the enclosing graph's
  (task resource maps carry this as an unvalidated scope). Imports stay graph-wide; the same resource imported again merges.
  Edges to and from the subgraph id resolve when the graph is built, from the tasks' accesses: an incoming edge on R
  reaches the members that access R with no edge on R from another member, and an outgoing one leaves the members that
  access R with no edge on R to another member, so order a subgraph's accesses to its ports with internal edges. Members
  are ordinary tasks that may be connected directly, and a subgraph task's `trigger` or `toggle` wraps every member.
- Use `full_memory_barrier` (`RTFullMemoryBarrierVK`) to order a frame's mutation of persistent state after the previous
  frame's consumers, and `run_graphics_pipeline_indirect` for non-mesh indexed indirect draws.
- Tasks that need application objects (for example the ImGui overlay) are built in C++ and supplied with
  `RendGraphRecipeImportsVK::add_task()`; the recipe references each with `graph:import_task(id)` (emitted as
  `{ id = <task id>, import = true }`) and connects it like any task. Imported tasks set no `type`, `params`, `aliases`, `trigger`, or `toggle`, and supplied and imported
  tasks must match, like resource imports.
- Recipes and their feature modules are Teal. `templates/teal_types.d.tl.jinja` generates the Teal declarations of the
  `WBE_REFLECT_SCRIPT` classes and enums (the `reflect_script` attribute group; such a class is also a static serializable
  and such an enum a reflected enum) into `build/<target>/teal/wbe.d.tl`; the test executables' types appear only in targets
  that build the tests, the only ones reflection scans them in. The ACP type-checks scripts against them, so reflection
  runs before the ACP. Every script class is an
  interface, a derived one `is` its bases, and a script type naming a reflected type that is not `WBE_REFLECT_SCRIPT`
  fails generation. Declare data C++ hands a recipe as `WBE_REFLECT_SCRIPT` structs rather than Teal records, import the
  declarations as types only (`local type wbe = require("wbe")`), and annotate `build(context: <context type>):
  wbe.RendGraphRecipeVK`. Enums serialize as their canonical names; compare `render_graph_utils.render_graph_utils.enum_name(value)` with the
  canonical upper-case name, which also accepts the lowercase aliases.
  The typed constructors check params against the generated `Graph.RTParams<Name>VK` interfaces; when adding a raw task
  with `graph:add()`, type its `params` with the reflected struct (`local params: wbe.RTParamsRunComputePipelineVK = { ... }`),
  since `RendRecipeTaskVK.params` is untyped. The generated repo-root `tlconfig.lua` points the Teal language server at the
  same paths for the last built target.
- The typed graph constructors (`graph:compute()`, `graph:draw_mesh()`, ...) are generated into `render_graph_utils.render_graph_tasks`
  (`templates/teal_render_graph_tasks.tl.jinja`) from the task classes annotated `WBE_RENDER_TASK(<method>)`; each creates
  the task type of its class's first `WBE_SERIALIZATION_ALIASES` entry, and a bare `WBE_RENDER_TASK` only registers the
  type. Mark params fields that hold resource ids `WBE_RECIPE_RESOURCE(graph)` or `WBE_RECIPE_RESOURCE(task_local)` so the
  constructor accepts resource handles there; do not hand-write constructors or handle-bearing params records.

### Scripts

- Script sources live under `res/assets/scripts/teal/` and are `script` resources declared in
  `res/assets/scripts/teal/lua_scripts.toml`: recipes in `render_graphs/`, reusable recipe parts (modules whose
  `subgraph(id, params, target)` returns a subgraph task) in `render_graph_utils/features/`, graph helpers in
  `render_graph_utils/`, and general script helpers in `utils/`.
- A script's resource id is its module name: its path under `scripts/teal/` without its extension, with dots for slashes
  (`render_graph_utils/render_graph_utils.tl` is `render_graph_utils.render_graph_utils`). Keep the two in sync, so that
  `require` resolves the same module at runtime (through `SMRResourceManager`) and in the editor.
- Scripts may be written in Lua (`.lua`) or Teal (`.tl`). `WBEACPCompilerScript` copies Lua sources and type-checks and
  compiles Teal sources to `<name>.lua` with the vendored compiler in `dependencies/tl`, run in-process through `lupa`;
  a type error fails the build. Teal modules resolve `require` against `res/assets/scripts/teal/` at compile time, so a
  Teal module can require a Lua module only through a `.d.tl` declaration next to it.
- Modules that reflection generates (`render_graph_utils.render_graph` and `render_graph_utils.render_graph_tasks`) are
  written to `build/<target>/teal/`, which `require`
  also resolves against. Their `lua_scripts.toml` entries give `source_file` relative to that directory, where the ACP
  looks first, and set `path` under `assets/scripts/teal/`.
- `require` is the only import mechanism. It loads each module once per backend and shares its export table, and cyclic
  requires fail. Implement new import sources as an `IScriptModuleResolver`, whose `try_resolve()` must not throw.

## Layered Architecture

Layers, from upper to lower. Upper layers may depend on lower layers; the reverse is **forbidden**.

| # | Layer      | Role                                                                |
|---|------------|---------------------------------------------------------------------|
| 1 | `global`   | Engine-wide primitives         |
| 2 | `function` | High-level engine subsystems (e.g. ECS, gameplay glue).             |
| 3 | `resource` | Resource units, asset loading, resource registry.                   |
| 4 | `core`     | Allocators, STL-allocator wrappers, Memory model (`Ref`, `Unique`, ...), jobs, math/util building blocks. |
| 5 | `platform` | OS / windowing / file system / renderer backends.                   |

Rules:
- Templates are **forbidden in the exposed APIs** of `global` — its public headers, member functions, free functions, and types must not be templated. Internal helper functions / implementation details (e.g. file-local `static` helpers in a `.cpp`, anonymous-namespace utilities) **may** use templates. Templates are unrestricted in `function`, `resource`, `core`, and `platform`.
- Do not catch exceptions outside tests. All engine errors are thrown as `std::runtime_error` and must crash the program.

## Validation

Use the validation wrapper for normal build and unit-test validation. It prints only short `WBEValidator:` status lines, writes detailed logs under `build/validation_logs/`, and stops at the first failed step.

```sh
python validation.py                         # debug build + full unit tests
python validation.py -t release              # release build + full unit tests
python validation.py -t debug-gcc            # GCC debug build + full unit tests
python validation.py -t debug -- --gtest_filter='WBESomeTest*'
```

When gtest arguments are forwarded after `--`, the requested gtest run is executed first. If it passes, validation still runs the full unit test binary afterward.

Do not include lint in validation. The user performs lint checks manually. Do not make lint or formatting fixes unless the
user explicitly asks for them.

## Build

Always use the Python build script — never invoke `cmake` directly.

```sh
python build.py                 # default: deploy
python build.py -t debug        # debug build  (clang)
python build.py -t release      # release build (clang)
python build.py -t deploy       # deploy build  (clang)
python build.py -t debug-gcc    # debug build with GCC
python build.py -t release-gcc  # release with GCC
```

Build outputs land in `build/<target>/`. Test binaries: `build/<target>/bin/`, e.g. `build/debug/bin/wbe_unit_test`.

## Build/Test Logs

Build and test outputs can be large. Always check the end of the previous output first using `tail`.

- Success: do not read the full log.
- Failure: inspect only the relevant sections needed to diagnose the issue.
- Do not dump or scan entire logs unless necessary.

Manual-only checks: `python check_lint.py` for lint/format and `python check_license_headers.py` for license headers. Do not run these automatically as part of validation.

## Tests

Tests mirror the src layer structure.

Run:

./build/debug/bin/wbe_unit_test

Use:
--gtest_filter='Pattern*'

Per-directory CMakeLists.txt use `file(GLOB *.cpp)`; new test files are picked up automatically.

## File / Directory Layout

- `include/<layer>/...` — public headers (mirrors `src/`).
- `src/<layer>/...` — implementation (`.cpp`).
- `templates/` — Jinja templates consumed by `build_script/reflection/`.
- `res/` — source assets and manifests; ACP writes deployment resources to `build/<target>/bin/res` and test resources to `test_env/res`.
- `dependencies/` — vendored third-party (do not modify).
- `todos/` — per-layer todo lists.

Generated files:
- src/per_target/<target>/generated/
- include/per_target/<target>/generated/

Never edit generated files.

Header / source naming: `snake_case.hh` / `snake_case.cpp`. Header guards: `WBE_FILE_<UPPER_SNAKE>_HH`. One primary class per file; file name matches the class in `snake_case`.

## Resource Pipeline / ACP

- ACP source manifests are TOML (`.toml`), parsed with Python's `tomllib`. ACP output stays JSON: source `<path>.toml` is
  emitted as runtime `<path>.json`, and runtime code refers to the `.json` path.
- ACP root manifest discovery starts only from `res/res_chunks/**/*.toml`.
- Do not make ACP scan every TOML file under a resource tree.
- Manifest files outside `res_chunks` are processed only when reachable from a root chunk manifest through `include`.
- `include` paths are resource-root-relative `.toml` manifest paths. Included files must contain a `resources` array.
- `scope` defaults to `global`. `global` resources compile for both deployment and tests; `test_only` resources compile only with tests enabled. Scope may be set on a manifest to skip its entire include tree, or on an individual resource. ACP validates scopes, filters before compiling, and removes scope from runtime manifests.
- `wbe-utils-model-compiler` owns final mesh and material resource shaping. Keep `acp_compiler_model.py` limited to ACP path/schema
  adaptation; do not normalize compiled material textures in the ACP adapter.
- Mesh SDFs (`sdf_config` on `model`, `static_geometry`, and `mesh`) are emitted by the model compiler's `MeshSDFBuilder`
  for both mesh compilers; only the injected host `SDFCompiler` (`SDFCompilerMeshToSDF`, mesh-to-sdf) samples distances. Do
  not sample SDFs anywhere else.

### Material Texture Contract

- Materials do not have a `material_type`. Their graphics pipeline and its reflected shader metadata define the rendering
  contract. A material may set a free-string `pipeline_variant` (`default` when absent); draw tasks map variants to compiled
  pipelines, so materials never name pipelines or passes.
- Every material texture slot has a non-empty, shader-defined `texture_role`; do not introduce a closed engine-side texture-role
  enum or whitelist.
- Shader texture roles are declared on integer fields of the global `MaterialParams` struct with `WBE_TEXTURE` in `utils/utils.slang`.
  - `[WBE_TEXTURE("albedo")] int albedo_index;` selects the explicit `albedo` role.
  - `[WBE_TEXTURE] int detail_index;` uses `detail_index` as the role.
- `MaterialTextureContractMetadata.material_sampler` and `MaterialTextureContractMetadata.textures` are fixed metadata-only
  members; `MaterialParamsBuffer.materials` is the fixed runtime material-table member. Applications may configure only the
  `material_textures` and `material_params` base variable names and the `default_material` resource key.
- Static-mesh compilation resolves textures to global sampled-image descriptor-heap indices and stores those indices in the reflected
  material-parameter table. Keep engine propagation generic; do not branch on concrete roles such as `albedo`, `normal`, or
  `rma`.
- Vulkan shader resources are declared as aligned `uint2` fields in push data with `WBE_DESCRIPTOR("TYPE", "resource_id", "ACCESS")`,
  where `TYPE` is `uniform_buffer`, `storage_buffer`, `sampled_image`, `storage_image`, `sampler`, or `acceleration_structure`, and
  the optional `ACCESS` is `read`, `write`, or `read_write`. Storage buffers and images default to `read_write`; annotate read-only
  ones with `read`. Runtime shaders access the renderer-owned global heaps through those indices; metadata-only resource
  declarations belong under `WBE_REFLECTION_METADATA`.
- `rma` is the canonical project abbreviation and texture role for the roughness, metallic, and ambient-occlusion channel pack.
  Use `rma` in project-owned identifiers instead of a spelled-out equivalent.

## Configuration Files

- Engine and game configuration files (`res/config/**/*.toml`) are TOML, loaded with `ParserTOML` from
  `core/parser/parser_toml.hh` (toml++). The engine has no YAML parser.

## Memory Model

**Do not use `std::shared_ptr`, `std::weak_ptr`, `std::unique_ptr`, `std::make_shared`, `std::make_unique`, `std::allocator`** in engine code. Allowed only as short-lived locals inside a function body when interfacing with third-party APIs that require them.

- Resource handles and allocator-aware objects must document whether they are copyable, movable, or neither.

Engine equivalents (in `core/memory/`):

| STL                  | Engine                                | Notes                                                       |
|----------------------|---------------------------------------|-------------------------------------------------------------|
| `std::shared_ptr<T>` | `Ref<T, AllocType>`                   | Strong owning ref-counted handle.                           |
| `std::weak_ptr<T>`   | `RefWeak<T, AllocType>`               | Non-owning observer of a `Ref`.                             |
| `std::unique_ptr<T>` | `Unique<T>`                           | Sole-owner handle.                                          |
| (raw `T*` view of a `Unique`) | `RefRaw<T, AllocType, AllowDestruct>`       | **Use-only** view; must NOT create or destroy the resource. |

There are other aliases: `RefRes` is an alias of `RefRaw`, with `AllowDestruct` specified as false.

Construct via factories, passing the allocator:

```cpp
Ref<Foo> r = make_ref<Foo>(allocator, args...);
Unique<Foo> u = make_unique<Foo>(allocator, args...);
```

Containers: use `Vector<T, <HeapAllocatorType>>` etc. from `core/allocator/stl_allocator.hh` rather than raw `std::vector`. Custom allocator types follow standard `allocator_traits` member names: `rebind::other`, `propagate_on_container_copy_assignment`, `propagate_on_container_move_assignment`, `propagate_on_container_swap`.

## Special Member Functions (Rule of 6)

Classes that manage resources, ownership, or non-trivial state must explicitly follow the Rule of 6:

- Default constructor
- Destructor
- Copy constructor
- Copy assignment operator
- Move constructor
- Move assignment operator

Rules:
- If a class owns a resource, define or delete copy/move operations explicitly.
- Do not rely on compiler-generated copy/move operations for resource-owning types unless the behavior is verified to be correct.
- Prefer move semantics for ownership transfer.
- Copy operations must preserve ownership invariants and perform deep copies when required.
- Move operations must leave the moved-from object in a valid destructible state.
- Use `= delete` for unsupported operations instead of leaving accidental behavior.
- Use `= default` only when the generated implementation matches the intended ownership semantics.

## Naming

- `snake_case` for files, variables, functions, members. `PascalCase` for types. `UPPER_SNAKE` for macros and constants of macro-like nature.
- Function parameters: prefix `p_` (e.g. `p_allocator`, `p_buffer_size`). Local variables: no prefix. Member fields: no prefix.
- **No abbreviations** unless they are universally well-known. Examples:
  - Forbidden: `ci` (use `create_info`), `mgr` (use `manager`), `tex` (use `texture`), `cfg` (use `config` only if widely understood, else `configuration`).
  - Allowed well-known: `MPSC` (multiple producers, single consumer), `SPSC` (single producer, single consumer), `i`/`j`/`k` as for-loop indices, `id`, `uuid`, `gpu`, `cpu`, `os`, `vk` (Vulkan), `rma` (roughness, metallic, and ambient-occlusion texture channel pack).
- If a name is too long, each word may be shortened to its first **≥4** letters. Examples: `initiate` → `init`, `information` → `info`, `allocator` → `alloc`; use shortening only when it remains unambiguous (`init`, `info`, `config`, `descr`).
- Preserve domain spellings already used in the codebase (e.g. test labels) even if unusual.

## Style

- C++20. `.clang-format` is authoritative; key rules:
  - `PointerAlignment: Left`, `ReferenceAlignment: Left` (`T* p`, `T& r`).
  - `ColumnLimit: 125`.
  - No short single-line forms: functions, lambdas, blocks, `if`, loops, `case`, `enum` all break to next line.
  - `AlwaysBreakTemplateDeclarations: Yes`.
  - `BreakStringLiterals: false` — long error-message string literals stay on one line.
- Declare every overriding member function, destructors included, with both `virtual` and `override`
  (`virtual void record(...) override;`, `virtual ~Foo() override = default;`). Never write `override` without `virtual`.
- Use `WBE_NO_FALSE_SHARING` to pad hot atomics that risk contention.
- Use the engine's `Path` alias from `platform/file_system/path.hh` for filesystem paths in engine APIs. It is `std::filesystem::path`; use standard path operations directly.
- For unbounded counting semaphores, use `std::counting_semaphore<>` (max = `PTRDIFF_MAX`); do not pick a small `LeastMaxValue` unless you can prove the bound.

## Header / Source Separation

Non-trivial method implementations must be placed in `.cpp` files.

Allowed inline definitions in headers:
- Simple getters/setters.
- Trivial methods with a short, obvious implementation.
- Functions that require header definitions (e.g. templates).

Avoid placing complex logic, control flow, or dependency-heavy code in headers. Public headers should expose interfaces and declarations rather than implementation details.

## API Design

- **Prefer exposing classes and interfaces over free-function wrappers.** When a feature has internal state, a constructable context, or a small set of related operations, declare the class in the public header and expose its members. Do not hide it behind a file-local impl class wrapped by free functions in the .cpp. Internal helpers, anonymous-namespace utilities, and detail types stay in the `.cpp`.
- **Name derived implementations with the interface (or its abbreviation) as a prefix.** For example, `RenderObject` → `ROImage`, `ROGraphicsPipelineVK`; `Renderer` → `RendererVK`; `RenderTask` → `RTRunGraphicsPipelineVK`. Pick a short, consistent abbreviation per interface and use it for all derived types.

## Documentation

Use Doxygen for C++ and Slang.

Place Doxygen commands in multiline comment blocks, with every command on its own `*` line. Do not use compact command comments
such as `/** @brief ... */` or combine commands such as `@class` and `@brief` on one line.

```cpp
/**
 * @class Example
 * @brief Describes the example.
 */
```

Required:
- Public/protected classes, structs, fields, functions.

Not required:
- Private members.
- Overrides unless behavior differs.

Document contracts, ownership, threading, and errors; do not restate signatures.

## Reflection / Codegen

- All C++ header code are scanned by `build_script/reflection/metaparser.py`.
- A `WBE_META` attribute is a bare name (`WBE_REFLECT`) or a name with parameters (`WBE_RENDER_TASK(compute)`), which
  metadata keeps as trimmed text in `attribute_parameters`; there is no assignment syntax. Declare serialization aliases as
  `WBE_SERIALIZATION_ALIASES(name, ...)`, each a name of letters, digits, and underscores.
- Generated artifacts live in `*.gen.*` files; regenerated automatically each build. Do not edit by hand.
- Templates for codegen live in `templates/*.jinja`.
- Each `[[generate]]` entry of a `generate.toml` supports `${name}` replacement in `output_name`, `template`, `out_dir`, and nested string values under `data`. Per-entry `params` override built-ins; built-ins include `build_target`, `build_dir`, `binary_dir`, `root_dir`, `include_dir`, and `source_dir`.

## Python Scripts

- All function parameters and return values must have type annotations. Use `-> None` explicitly for procedures.
- Prefer concrete generics (`list[str]`, `dict[str, Any]`) over bare `list` / `dict`. Use `from typing import Any, Callable` etc. when needed.
- Annotate non-trivial local variables whose type cannot be obviously inferred (e.g. empty containers: `result: list[str] = []`).
- Use PEP 604 unions (`X | None`) rather than `Optional[X]`.
