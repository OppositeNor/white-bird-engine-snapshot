---
name: white-bird-testing
description: How to build, run, write, and validate all White Bird Engine tests — validation.py, build.py targets, wbe_unit_test gtest filtering, test layout, Global/test_env fixtures, test_only resources, headless Vulkan unit tests, render-graph recipe and Teal script tests, debug-tsan, benchmarks, valgrind, and rendering executables (WBE_CAPTURE_PNG screenshots, WBE_EXEC_TEST_INPUT_SEQUENCE scripted input, check_deferred.py deferred lighting/voxel checks, capture-free stability runs). Use this whenever you add or change engine code that needs tests, write or fix a gtest, run or interpret test/validation results, add test resources, add or run a rendering test executable, capture or inspect a render screenshot, verify a rendering or crash fix visually, or finish a task in this repo and need to validate it — even if the user only says "test it", "check it renders", "add coverage", or "make sure it passes".
---

# White Bird Engine testing

This skill covers every kind of test in the repo. The body covers unit tests, validation, benchmarks, and leak checks.
Rendering executables (screenshots, scripted input, deferred checks, stability runs) are covered in
[references/render-executables.md](references/render-executables.md). Read that file whenever the task involves
running or changing anything under `tests/executable/`, or verifying rendering output. The section
[Rendering executables](#rendering-executables) below summarizes when you need it.

## Tools at a glance

| Goal | Command (run from repo root) |
|------|------------------------------|
| Final validation: build + full unit tests | `python validation.py` (debug, clang) |
| Validate another target | `python validation.py -t release` / `-t debug-gcc` |
| Validate with a focused run first | `python validation.py -t debug -- --gtest_filter='WBEFooTest*'` |
| Build only | `python build.py -t debug` |
| Iterate on specific tests | `./build/debug/bin/wbe_unit_test --gtest_filter='WBEFooTest.*'` |
| List tests | `./build/debug/bin/wbe_unit_test --gtest_list_tests` |
| Shader utility tests only | `python run_shader_tests.py` (extra args go to pytest, e.g. `-k morton`) |
| Leak check (manual, slow) | `valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=42 ./build/debug/bin/wbe_unit_test` |
| Benchmarks | `./build/<target>/bin/wbe_benchmark_test` (prefer a release target) |
| Capture a render frame | `WBE_WAIT_PROCESS_INJECT=0 WBE_CAPTURE_PNG=/abs/path.png WBE_CAPTURE_FRAME=5 ./build/debug/bin/wbe_render_test_<name>` |
| Automated deferred checks | `python .codex/skills/white-bird-render-testing/scripts/check_deferred.py --svo 2` |

Never invoke `cmake` directly — `build.py` runs the reflection/codegen and ACP resource pipeline before CMake, and
reconfigures CMake on every run. Skipping it produces stale generated code and missing test resources.

Lint (`check_lint.py`) and license checks (`check_license_headers.py`) are manual-only. Do not run them as part of
testing or validation; the user does that.

### validation.py

`validation.py` is the standard gate before reporting work as done. It:

1. Runs `python build.py -t <target>`.
2. If gtest args follow `--`, runs the unit binary with them first.
3. Always runs the full `wbe_unit_test` afterward.
4. Runs the shader tests through `run_shader_tests.run_shader_tests()`.

It prints only `WBEValidator:` lines and stops at the first failed step. Detailed output goes to
`build/validation_logs/<target>_build.log`, `<target>_requested_tests.log`, `<target>_full_unit_tests.log`, and
`<target>_shader_tests.log`.

Valid targets are those with `generate-tests: True` in `build_config.py`: `debug`, `release`, `debug-ninja`,
`release-ninja`, `debug-gcc`, `release-gcc`, `debug-gcc-ninja`, `release-gcc-ninja`, and `debug-tsan` (ThreadSanitizer;
use it for data races in jobs, atomics, and cross-thread renderer handoff). `deploy` builds no tests, which
is why plain `python build.py` (default `deploy`) is not enough for testing.

### Reading results

Logs are large. On failure, read the tail first and then only the relevant section:

```sh
tail -n 40 build/validation_logs/debug_full_unit_tests.log
grep -n -A15 '\[  FAILED  \]\|Failure\|error:' build/validation_logs/debug_build.log | head -80
```

On success, do not read the full log. Report the outcome faithfully: which target, which filter, pass/fail counts, and
the failing test names with the relevant assertion output if anything failed.

### Recommended workflow

1. Iterate with `python build.py -t debug` and a narrow `--gtest_filter` on the binary. This is much faster than the
   full suite.
2. Finish with `python validation.py` (add `-- --gtest_filter=...` to rerun your tests first). If the change touches
   compiler-sensitive code (templates, `constexpr`, atomics, UB-prone memory code), also validate `-t debug-gcc`;
   for optimization-sensitive code (lock-free, timing, aliasing), also `-t release`.
3. If a failure is unrelated to your change, confirm that (e.g., with `git stash` or by checking whether it fails on
   the unchanged tree) before reporting it as pre-existing — do not silently ignore it or edit the test to pass.

## Working directory and test_env

Run test binaries from the **repository root**. Fixtures construct `Global` over the relative path `test_env` through
`tests/unit/test_global.hh`:

```cpp
global = WBE::make_test_global();
```

`test_env/` is build output, not source:

- `test_env/res/` — ACP writes test resources here (both `global`- and `test_only`-scoped chunks).
- `test_env/res/config/` — the test configuration from `res/config/test/`.
- `test_env/metadata.json` — generated reflection metadata.
- `test_env/captures/` — deferred-test `F1` captures and frame sequences.
- `test_env/descriptor_debug/` — descriptor dumps written by rendering executables.

Do not edit files under `test_env/` by hand; they are regenerated by the build. To add test data:

- **Resources**: add them under `res/` and reference them from a TOML manifest under `res/res_chunks/**/*.toml` (or a
  file reachable from one via `include`). Mark test-only data with `scope = "test_only"` so it is excluded from
  deployment builds. Existing examples: `res/res_chunks/test_global_resources.toml`, `reflection_attributes.toml`. ACP
  emits them as `.json` runtime manifests, which is the path tests load.
- **Config overrides**: put them in `res/config/test/`.

Rebuild with `build.py` after adding resources; a stale `test_env` is a common cause of "resource not found" failures.

## Test layout

```
tests/
├── unit/          -> wbe_unit_test (GLOB_RECURSE tests/unit/*.cpp, linked with gtest_main)
│   ├── core/<area>_test/   header-per-topic + one aggregator .cpp
│   ├── function/           mostly one self-contained <subject>_test.cpp per subject
│   ├── global/  platform/  resource/  utils_test/
├── benchmark/     -> wbe_benchmark_test (Google Benchmark, GLOB_RECURSE)
├── executable/    -> wbe_render_test_<name> binaries, registered in function/render/generate.toml
│                     (see references/render-executables.md)
├── shader/        -> pytest + slangpy tests of shader utility functions; kernels in shader/kernels/
└── integration/   not currently wired into CMake; files here are NOT built or run
```

Tests mirror the `src/` layer (`core`, `function`, `global`, `platform`, `resource`). Put a test in the directory of
the layer that owns the code under test.

New `.cpp` files anywhere under `tests/unit/` are picked up automatically on the next `build.py` run. There is no
per-test CMake registration.

### Two file patterns — match the directory you're in

**Aggregator pattern** (`core/*`, `global/`, `platform/`, `resource/`, `utils_test/`): each topic is a guarded
`*_test.hh` containing `TEST`s, and a single `<dir>_test.cpp` includes them all. When adding a new header here, also
add it to the aggregator, otherwise its tests silently never compile or run:

```cpp
// NOLINTBEGIN
#include "required_test.hh"      // IWYU pragma: keep
#include "type_metadata_test.hh" // IWYU pragma: keep
// NOLINTEND
```

Do not create a second `.cpp` that includes the same headers — the tests would be defined twice and fail to link.

**Standalone pattern** (`function/`): one `<subject>_test.cpp` per class/feature, e.g.
`tests/unit/function/device_memory_vk_test.cpp`. No aggregator needed.

## Writing a test

Follow the repo's normal C++ rules (naming, `p_` parameters, license header, 125-column clang-format) in tests too.
Specifics for tests:

- **Suite names**: `WBE<Subject>Test`, e.g. `WBEDeviceMemoryVKTest`, `WBERenderGraphVKTest`. Some older suites lack
  the prefix (`ParserJSONTest`, `StackAllocator`); keep their names but use the prefix for new suites. Consistent
  prefixes make `--gtest_filter='WBE<Subject>*'` reliable.
- **Test names**: `PascalCase` sentences stating the behavior: `RequiresExactlyOneRequirementSource`,
  `ThrowsWhenValueIsMissing`.
- **Namespace**: new standalone files wrap everything in `namespace WhiteBirdEngine { ... }`, with file-local helpers
  and test doubles in an anonymous namespace. Header-pattern files often use the alias `namespace WBE = WhiteBirdEngine;`
  — match whatever the surrounding file uses. Never introduce another named namespace.
- **Header guards** in `*_test.hh`: `WBE_FILE_<UPPER_SNAKE>_HH`.
- **Doxygen** is not required on tests, fixtures, or file-local helpers.

### Testing failure paths

Engine APIs crash by throwing `std::runtime_error` (default-safe convention). Tests are the one place where catching
is expected — assert it with gtest:

```cpp
EXPECT_THROW(DeviceMemoryVK(&render_data, DeviceMemoryVK::Builder().set_domain(DeviceDomain::DEVICE)), std::runtime_error);
```

For every non-`try_` API you add, test both the success path and that each violated contract throws. For a `try_`
variant, test that the recoverable failure returns its sentinel (e.g. `MEM_NULL`) rather than throwing.

`WBE_DEBUG_ASSERT` preconditions are debug-only and are not `std::runtime_error`; don't write tests that depend on them
firing, since they behave differently between `debug` and `release`.

### Fixtures and globals

Use a `::testing::Test` fixture with `SetUp`/`TearDown` when the code needs `Global` (resource registry, config,
logging, metadata). Keep `Global` construction per test so state doesn't leak between tests. The existing fixtures
hold it in a `std::unique_ptr` — that is an accepted test-only exception to the memory-model rules. Engine objects
under test still go through engine allocators and handles (`make_ref`, `make_unique`, `Vector<T, Alloc>`).

Construct it with `WBE::make_test_global()`, never `Global` directly. Engine pools are committed and zeroed up front, and
`test_env`'s `engine_config.toml` is sized for the rendering executables (a 6 GiB resource pool), so a plain `Global`
makes every test slow and memory-heavy. The helper overrides the engine configuration per test through command-line
options (`--<name>=<value>`): `resource_mem_pool_size` 16 MiB and the size-classified global allocator's
`global_mem_small_pool_size` 1 MiB, `global_mem_medium_pool_size` 15 MiB, and `global_mem_large_pool_size` 16 MiB. The
global allocator never spills between size classes, so one allocation over 1 MiB must fit in the large pool. A test that
needs more passes its own configuration:

```cpp
global = WBE::make_test_global({.resource_mem_pool_size = WBE_MI_B(256)});
```

Run unit tests in a single process. Do not launch several test processes in parallel: each one commits its pools, and a
parallel runner has exhausted memory and crashed the machine.

Many tests don't need `Global` at all: allocator, container, parser, and most renderer-helper tests construct only
what they use (e.g. `RenderAllocator allocator(WBE_KI_B(64));`). Prefer that — it's faster and isolates failures.

Existing helpers worth reusing before writing your own:

- `tests/unit/core/allocator_test/test_utilities.hh`
- `tests/unit/core/memory_test/mock_heap_allocator_aligned.hh`
- `tests/unit/core/job_test/job_test_utils.hh`
- `tests/unit/resource/resource_test_utils.hh`
- `tests/unit/core/reflection_test/reflection_test_data.hh` (reflected test types; they are scanned by the metaparser)
- `tests/unit/function/render_task_registry_vk_test_tasks.hh` (registered probe render tasks)

### Vulkan / renderer unit tests

Unit tests under `tests/unit/function/*_vk_test.cpp` run without a GPU or `VkDevice`. They exercise builders,
validation logic, graph compilation, and metadata by constructing `RenderContextVK` over a `RenderAllocator` and
using fake handles (`reinterpret_cast<VkBuffer>(uintptr_t{1})`) or test doubles derived from the real interfaces
(e.g. a local `IRenderObjectVK` subclass in an anonymous namespace; see `render_graph_vk_test.cpp`).

Keep them that way: don't call real Vulkan entry points that need a device from `wbe_unit_test`, because validation
runs headless. Behavior that truly needs a device or window belongs in a rendering executable
(see [Rendering executables](#rendering-executables)).

Render graph tests should cover the DAG rules from `AGENTS.md`: missing edges between conflicting accesses must make
graph construction throw, and cycles must be rejected. Rebuild changes need diff coverage
(`rend_graph_resource_diff_vk_test.cpp`): which resources are kept, recreated, or replaced.

### Scripts, recipes, and Teal

Teal type-checking is a build step: `WBEACPCompilerScript` compiles every `.tl` script against the generated
`build/<target>/teal/wbe.d.tl`, so a type error fails `build.py` before any test runs. Read the end of the build log for
the Teal diagnostic. Runtime behavior is unit-tested:

- `tests/unit/function/rend_graph_script_vk_test.cpp`: runs the test-only `tests.teal_recipe` and the shipped recipes
  (`render_graphs.pixel_sorting`, `render_graphs.deferred` with its features) and checks the resulting tables.
- `rend_graph_recipe_builder_vk_test.cpp`: imports, tasks, subgraphs, triggers, and toggles.
- `render_task_registry_vk_test.cpp` / `render_task_registry_vk_builtin_test.cpp`: registration, params, push constants.
- `teal_declarations_test.cpp`: the generated declarations, read through `WBE_TEAL_DECLARATIONS_DIR`.
- `rt_triggered_vk_test.cpp`, `rt_toggle_vk_test.cpp`, and `tests/unit/core/script_test/` (`WBESBLuaTest`,
  `WBEDataValueTest`).

Test-only scripts go under `res/assets/scripts/teal/tests/` and are declared in `lua_scripts.toml` with
`scope = "test_only"`. A changed recipe should be exercised by `rend_graph_script_vk_test.cpp` and by its rendering
executable.

### Concurrency tests

Job buffers, workers, atomic allocators, and `DebugMutex` have multi-threaded tests. Make them deterministic: join
every thread before asserting, use counts large enough to provoke contention but small enough to keep the suite fast,
and never rely on `sleep` for correctness. If a concurrency test flakes, treat it as a real bug until proven otherwise;
rerun with `--gtest_repeat=200 --gtest_filter=...` to reproduce and validate a fix.

## Rendering executables

`tests/executable/` builds standalone Vulkan apps that need a GPU and a display: `wbe_render_test_` +
`vulkan_draw_triangle_test`, `vulkan_draw_cube_test`, `vulkan_draw_cube_unshaded_test` (minimal recipe),
`vulkan_draw_sponza_test`, `vulkan_draw_sponza_deferred_test` (the deferred recipe with voxel and pixel-sort features),
`vulkan_pixel_sorting_test`, and `vulkan_draw_skinned_mesh_test`. `validation.py` builds
them but does not run them. You need them when:

- a change affects what's drawn (shaders, render graph, materials, lighting) and needs visual confirmation;
- a crash, device loss, or synchronization bug needs reproducing or confirming as fixed;
- you add a new rendering executable or change the shared capture/input hooks.

Read [references/render-executables.md](references/render-executables.md) before doing any of these. It covers:

- the shared hooks in `tests/executable/function/render/render_test.hh`;
- registering executables in `generate.toml`;
- `InputManagerMocked` and the `WBE_EXEC_TEST_INPUT_SEQUENCE` JSON format;
- the capture environment variables;
- clean `SIGTERM` shutdown;
- how to inspect and report PNGs;
- the `check_deferred.py` capture and stability modes.

Key rules even if you skim:

- Open the actual PNG and describe it. A non-empty file alone doesn't count as visual validation.
- Verify stability in a separate run with capture disabled, because readback changes timing. A good screenshot
  doesn't prove a crash is fixed.
- Stop only processes you launched. `SIGTERM` is the clean shutdown; a timeout or `SIGKILL` is not a clean exit.
- Iterate with `build.py` and the relevant executable. Keep `validation.py` for the final check.

## Benchmarks

`tests/benchmark/**/*.cpp` are Google Benchmark files linked with `benchmark_main` into `wbe_benchmark_test`. They are
not run by `validation.py`. Run them on a release target for meaningful numbers, and compare against a baseline run on
the same machine and target:

```sh
python build.py -t release
./build/release/bin/wbe_benchmark_test --benchmark_filter='size_classified'
```

## Shader tests

`tests/shader/` holds pytest scripts that call Slang shader functions directly through slangpy. `build.py` builds
slangpy from `dependencies/slangpy` as a build dependency of every test-generating target. Build dependencies are
built once for all targets, with `build_config.dependency_build_info`, into `build/dependencies` (slangpy's package is
`build/dependencies/slangpy/python`); do not `pip install slangpy`. Build a test-generating target first, then run `python run_shader_tests.py` (extra args go to
pytest). The `device` fixture in `tests/shader/conftest.py` uses the same include paths as the ACP shader compiler: the
shader root, resolved from `build_config.resource_dir`, and its `utils` directory; load modules from that root. When a function needs
groupshared memory or a dispatch, write a test kernel as a `.slang` file under `tests/shader/kernels/` (not as a Python
string) and compile it with the `load_kernel(kernel_file, defines)` fixture, which specializes the kernel through
preprocessor defines. Only shader utility functions are tested for now.

## Leak checking

Run `wbe_unit_test` under valgrind directly (`valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=42
./build/<target>/bin/wbe_unit_test`), using a test-generating target that you built first. It is slow and not part of
normal validation; use it when investigating ownership/lifetime bugs in `Ref`/`Unique`/allocators, and only when asked or clearly warranted.
