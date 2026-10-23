
# Rendering executable tests

## Inspect current implementation

Read the header-only shared utility in `tests/executable/function/render/render_test.hh`, the relevant test's `main()` and `render_update()`, and the capture contract in `include/function/render/i_renderer.hh`. Check `git diff` first for updated probes or user changes. Read `tests/executable/function/render/generate.toml` only when adding, removing, or renaming a rendering executable. Do not assume old binaries or screenshots reflect current source.

## Shared test hooks

Every executable test that renders must:

- Include `render_test.hh` and call `WBE::on_exec_test_start()` at the beginning of `main()`, before application/renderer initialization and thread startup. It handles `WBE_WAIT_PROCESS_INJECT` only.
- Call `WBE::on_exec_test_render(renderer, current_frame)` from its render-thread `render_update()`, before any early return. Pass the renderer by reference and its monotonic frame counter, not the rotating frame slot or acquired swapchain image index. It reads the capture variables on every call and returns `true` when it requested a capture this frame (the deferred test hides its ImGui window on such frames).
- Optionally call `WBE::on_exec_test_logic(window)` from the logic thread to support `WBE_EXEC_TEST_RESIZE`.

`render_test.hh` is a header-only helper library; there is no `render_test.cpp` to link. Besides the hooks it provides mesh-resource and pipeline helpers, `alias_mesh_resources`, `alias_render_targets`, `connect_resource_chain`, camera and spot-light GPU structs, and descriptor debug output. Prefer a Teal recipe for new graphs (see the `white-bird-render-graph` skill); `vulkan_draw_cube_unshaded_test.cpp` is the minimal recipe-based executable. When adding a new rendering executable, add only that executable's source pattern to `tests/executable/function/render/generate.toml`. Do not edit `executable_test.gen.cmake`.

The shared test utility owns environment parsing and one-shot trigger state. Keep those policies out of the engine. Use `IRenderer::request_frame_capture()` for capture, never a hard-coded engine screenshot path or environment hook. Renderer access is render-thread confined; requests originating elsewhere must use `IRenderer::add_job()`.

Do not add these hooks to non-rendering unit tests simply because they use renderer mocks.

## Scripted input

Every executable test links the `wbe_executable_test_utilities` static library (`tests/executable/utilities/`, linked through `templates/executable_test.cmake.jinja`). Its `InputManagerMocked` extends `InputManagerRegular` and must be passed as each test application's input manager:

```cpp
ApplicationVulkanDrawCubeTest()
    : WBE::ApplicationGame({}, WBE::make_ref<WBE::InputManagerMocked>(WBE::global_allocator(), WBE::global_allocator())) {}
```

Game code queries keys only through `IInputManager::get_singleton()`. `ApplicationGame` creates an `InputManagerRegular` when no manager is supplied; do not construct a second input manager, because the singleton throws. Keep test-only input policy in the utilities library, not in the engine.

`WBE_EXEC_TEST_INPUT_SEQUENCE` is a JSON array replayed in order, parsed with the engine's `ParserJSON`:

```sh
WBE_EXEC_TEST_INPUT_SEQUENCE='[{"key": "KEYCODE_V", "delay": 3.0, "count": 1}, {"key": "KEYCODE_RIGHT", "duration": 1.0}, {"key": "KEYCODE_F1", "delay": 1.0, "count": 1}]'
```

- `key`: a `KeyCode` enumerator name.
- Exactly one of `count` (positive integer: that many `get_input_status()` queries for the key report down) or `duration` (positive seconds of steady-clock time during which the key reports down).
- Optional `delay`: non-negative seconds before the entry becomes active.

Delays chain from the end of the previous entry and start at the first input query (the first logic tick), so loading time is excluded. Prefer `delay`/`duration` over frame or query counts when timing matters; frame rates are not reliable. Keys outside the active entry, and all keys after the sequence ends, report the real window state; an unset or empty variable replays nothing. Consecutive entries for the same key without a delay form one continuous press, so separate repeated presses with a small `delay`. A malformed sequence or unknown key crashes at startup with an `InputManagerMocked:` message.

## Capture and inspect

For iterative visual checks, build with `python build.py -t debug` (or the requested target) and run the application. Run only relevant unit tests during iteration; reserve `python validation.py` for final validation unless the user requests otherwise. Inspect the end of failure logs first. Shader/resource compilation is part of the Python build pipeline. Do not edit generated shaders to establish a permanent fix, bypass SPIR-V validation, or invoke CMake directly.

From the repository root, choose a fresh absolute PNG path whose parent exists:

```sh
WBE_WAIT_PROCESS_INJECT=0 WBE_CAPTURE_PNG=/tmp/wbe-triangle-check.png WBE_CAPTURE_FRAME=5 ./build/debug/bin/wbe_render_test_vulkan_draw_triangle_test
```

- `WBE_CAPTURE_PNG`: unset or empty disables capture; otherwise requires an absolute path. Choose a new path for each experiment to avoid mistaking stale output for success.
- `WBE_CAPTURE_FRAME`: nonnegative integer, default `5`; requests once when the renderer counter reaches or exceeds it. The actual capture occurs on the next tick that renders; zero means the first eligible frame, not a guaranteed zero-indexed image.
- `WBE_CAPTURE_INTERVAL`: positive integer; captures every N frames from `WBE_CAPTURE_FRAME` on, to `<stem>_frame<N><ext>`. Unset captures once.
- `WBE_WAIT_PROCESS_INJECT`: exactly `0` or `1` (anything else throws). `1` prints the PID and waits for Enter before renderer initialization; use it when attaching Nsight or another injector, and preserve stdin access. Use `0` for unattended captures.
- `WBE_EXEC_TEST_RESIZE=<width>x<height>@<logic frame>`: resizes the window from the logic thread at that frame (tests that call `on_exec_test_logic`), exercising swap-chain rebuilds.
- `WBE_EXEC_TEST_RAY_TRACING` and `WBE_EXEC_TEST_TIMESTAMP_QUERY`: enable those renderer capabilities in the deferred and pixel-sorting tests. Timestamp queries write `gpu_timestamp_report.json`, which `python export_render_graph.py` turns into a Mermaid or SVG graph.

The test continues after capture; it does not automatically exit. Observe the capture log and output file, then stop only the process you launched. `SIGTERM` is a clean shutdown on POSIX: SDL turns it into `SDL_QUIT`, the same path as closing the window. A timeout or `SIGKILL` is not a clean test exit. Respect GUI/GPU execution approvals.

Open the actual PNG with the local image-viewing tool. Report the path, dimensions, selected frame, and visible result: expected scene content, geometry stability, textures, lighting, and any black frame or exploded vertices. A nonempty PNG alone is not visual validation. Compare equivalent camera/frame settings where possible; do not use image generation to manufacture test evidence.

## Interpret results

Vulkan capture requires swapchain transfer-source support and RGBA8/BGRA8 output; unsupported configurations and write failures throw. Inspect console/validation errors as well as the image. Do not silently substitute a desktop screenshot when testing renderer readback.

Readback stalls the GPU and can change timing. Verify crashes and synchronization fixes in separate runs with capture disabled, long enough to exceed the observed failure window. Report visual correctness separately from stability, including duration, exit reason, and outstanding validation errors; a good screenshot does not establish that a segfault is fixed.

## Automated deferred checks

Use [check_deferred.py](../../../../.codex/skills/white-bird-render-testing/scripts/check_deferred.py) for the deferred executable's `R` lighting switch, `V` voxel overlay, and `F1` renderer capture controls. Re-read the handlers in `deferred_test/application_vulkan_deferred_pbr_test.cpp` if they change.

The deferred executable's controls:

- `R`: switches between the mipmap and summed-area-table graphs (`activate_render_graph`), logging `Deferred shadow filtering:`.
- `V`: cycles the voxel debug view (disabled, post SVO build, pre deferred propagation) through `RTToggleVK` events passed to the recipe as `VoxelDebugViewToggles`.
- `1`: toggles pixel-sort post-processing (`pixel_sort.toggle`). `B`: triggers bloom acquisition (`RTTriggeredVK`).
- `F1`: captures to `test_env/captures/deferred_<ms>_frame_<n>.png`. `T`: selects the next spot light.
- Arrows and `WASDQE` move the camera; `I J K L U O Y P H ;` move and aim the selected spot light.
- An ImGui "Deferred Rendering" window mirrors these and records fixed-delta frame sequences to `test_env/captures/deferred_sequence_<ms>/`.

The static voxel build is an `RTTriggeredVK` disabled after the first recorded frame, so it runs once unless `WBE_REAL_TIME_FULL_SCENE_RECONSTRUCT=1`. This helper does not build, run unit tests, or judge visual correctness. It needs a POSIX platform and a display the renderer can open, but no windowing-system automation tools; request GUI execution permission when the environment requires it.

From the repository root:

```sh
python .codex/skills/white-bird-render-testing/scripts/check_deferred.py --svo 2
python .codex/skills/white-bird-render-testing/scripts/check_deferred.py --svo 1
python .codex/skills/white-bird-render-testing/scripts/check_deferred.py --svo 2 --mode stability --duration 25
python .codex/skills/white-bird-render-testing/scripts/check_deferred.py --svo 1 --full-scene-reconstruct
python .codex/skills/white-bird-render-testing/scripts/check_deferred.py --svo octree
```

`--full-scene-reconstruct` sets `WBE_REAL_TIME_FULL_SCENE_RECONSTRUCT=1`, which rebuilds the static SVO every frame
instead of once. Use it for SVO construction races, which otherwise run only on the first build. Without it the helper
forces the variable to `0`.

`--svo` sets `WBE_SVO_IMPLEMENTATION`: `1` (atomic SVO), `2` (pass-published SVO, the default), or `octree` (mipmapped 3D
texture octree). For the octree, the first `V` view draws its leaves and bounds, and the second adds its occupied nodes
colored by depth; both read the octree textures, and the helper captures only the first.

Run configurations sequentially. Each invocation creates a fresh directory under `build/render_checks/` containing `application.log`, `result.json`, and (in capture mode) five PNGs: mipmap lighting, two summed-area-table frames, and two voxel-overlay frames. `result.json` records the input sequence, actual captured frame numbers, paths, CPU affinity, elapsed time, and exit status. Open the PNGs to assess rotation, instance visibility, occlusion and occupancy; the helper's success does not establish visual correctness.

In capture mode the helper passes all key presses up front through `WBE_EXEC_TEST_INPUT_SEQUENCE`, timed with `delay`: after `--settle` seconds (default 5) from the first logic tick it presses `R`, then `F1` twice, then `V`, then `F1` twice. The first frame is captured through `WBE_CAPTURE_FRAME=15`. It then waits for all five captures, checks that the lighting switch was logged after the initial capture, and fails asking for a larger `--settle` otherwise. Stability mode sends no input and removes inherited capture and input settings. The helper does not clear an inherited `WBE_CAPTURE_INTERVAL`; unset it before capture-mode runs.

The helper limits the owned process to four available CPUs by default (`--cpus` overrides) and sends `SIGTERM` to its own child for normal SDL shutdown. Timeouts or forced cleanup (`SIGKILL`) are recorded as failures and never called clean exits.

The demo always renders two animated instances of the cube mesh. Use `--target`, `--repo`, and `--output-dir` for another build or artifact location. Read `--help` for timing controls.

On device loss or shader overflow, inspect that run's log before continuing the matrix. Do not silently increase SVO capacity or alter shaders merely to obtain a capture. Report failed configurations separately from successful captures and capture-free stability runs.
