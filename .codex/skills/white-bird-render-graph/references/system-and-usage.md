# Render Graph System and Usage

## Pipeline map

1. The application calls `RendererVK::register_render_graph(key, RendGraphRebuildCallbackVK)`, which creates the
   `RenderGraphVK`, builds it immediately through `build_from_callback()`, notifies plugins, and returns `Ref<RenderGraphVK>`.
   `register_render_graph(key)` without a callback only adds a reference to an existing graph.
2. A build calls the callback with the current `RendGraphContextVK` (`swap_chain_extent`) and applies the descriptor: it
   diffs resources against the current graph, creates or keeps containers, flattens subgraphs, calls every task factory,
   adds edges, topologically sorts and rejects cycles, validates that edges order every conflict, derives barriers and
   semaphores, and allocates command buffers.
3. `execute()` first calls `update_context(RendGraphContextVK::from_render_data(...))`, which rebuilds when the context
   changed. `rebuild()` forces a rebuild (for example after recompiling pipelines a recipe names).
4. Each frame the renderer acquires the swap-chain image, updates the current slot of the swap-chain view container,
   records and submits nodes in topological order to their queues, and presents.
5. Unregistration defers destroying the graph for the configured frames in flight.

When the graph has a key and a render database, graph-owned objects are registered there as `<graph key>.<resource id>`,
with `[slot]` appended for per-frame copies.

## Core APIs

- `function/render_vk/render_graph_descriptor_vk.hh`: `RendGraphDescVK` (tasks, resources, `add_*`, `add_task`, `get_task`,
  `add_subgraph`, `get_subgraph`, `contains_resource`), `RendTaskDescVK` (`task_id`, `factory`, `connect()`, `alias()`),
  `RendSubgraphDescVK`, `RendSubgraphVK`, and the factory typedefs.
- `function/render_vk/render_graph_vk.hh`: build, `update_context()`, `rebuild()`, `acquire_resource(id)`,
  `dump_graph_metadata()`, `list_descriptors()`.
- `function/render_vk/rend_graph_resource_diff_vk.hh`: rebuild diff planning.
- `function/render_vk/i_render_task_vk.hh`: `QueueFamilyVK`, `RendResUsageVK`, `ResAccessVK`, and the `IRenderTaskVK`
  contract (`queue_family()`, `append_accesses()`, `record()`).
- `function/render_vk/render_task_registry_vk.hh`: registered task types (see the recipes reference).
- `function/render_vk/render_resource_list_vk.hh`: CPU-side aggregate of render objects bound under one key; binding it
  registers `<key>[<index>]` entries, and accesses or edges on the aggregate expand to all entries
  (`MeshResourcesVK::acquire_textures()` uses it).

## Descriptor pattern (C++)

`RendTaskDescVK` holds a `RendTaskFactoryVK` (`Unique<IRenderTaskVK>(const RendGraphContextVK&)`). It is copyable and
movable; copies share the factory's captures. The graph keeps the descriptors and calls each factory on every build, and
throws when a factory is missing or returns null.

```cpp
RendGraphDescVK graph_desc(render_allocator);
graph_desc.add_resource(REND_RES_SWAP_CHAIN, *renderer->acquire_swap_chain_image_view_container())
    .add_image("shadow_depth_image",
        [=](const RendGraphContextVK& /*p_context*/) {
            return RendImageDescVK{
                .usage = RendResPrimaryUsageVK::IMAGE_DEPTH_TARGET,
                .format = VK_FORMAT_D32_SFLOAT,
                .extent = shadow_extent,
            };
        })
    .add_image_view("shadow_depth", "shadow_depth_image");

RendTaskDescVK task_desc(render_allocator);
task_desc.task_id = "run_lighting";
task_desc.alias(RenderTargetsVK::color_id(0), REND_RES_SWAP_CHAIN);
task_desc.factory = [=](const RendGraphContextVK& p_context) {
    return RTRunGraphicsPipelineVK::Builder(render_allocator)
        .set_pipeline(lighting_pipeline)
        .set_render_extent(p_context.swap_chain_extent)
        .create_unique(render_context);
};
graph_desc.get_task("run_shadow").connect("run_lighting", "shadow_depth");
graph_desc.add_task(std::move(task_desc));
```

Resource factories return `RendBufferDescVK`, `RendImageDescVK`, `RendImageViewDescVK`, or `RendSamplerDescVK`; view and
sampler factories may be null to take defaults. The primary usage (`RendResPrimaryUsageVK`) selects Vulkan usage flags
(`additional_usage` adds more). Images are device-local; buffers take a `DeviceDomain`, `per_frame`, and optional initial
data. Unset view fields follow the viewed image, and depth formats default to the depth aspect. Size swap-chain-dependent
resources from the context. Ids are unique lower-snake-case strings across imports and graph-owned resources.

Every import is an `IBindableRenderResourceVK`; null containers are rejected. When an import varies by frame, bind one
stable container (`ROCMultislotsVK`) and mutate the objects inside it. Swap-chain-style containers use `p_flush = false`. The
renderer replaces the swap-chain container when `frames_in_flight` changes, so import it inside the callback.

Update graph-owned buffers through `acquire_resource(id)` on the rendering thread, and re-acquire after a rebuild that
replaced the container.

## Rebuild diffing

Each resource has a `RendResSignatureVK`: kind, evaluated description, `image_id`, `per_frame`, and import identity.
`RendGraphResourceDiffVK::plan()` returns one action per id:

- `CREATE`: new id.
- `KEEP`: same kind, per-frame layout, and description; imports bind the same object; views view the same, kept image.
  Content is preserved.
- `RECREATE`: the description changed; objects are recreated in place in the same container, losing content.
- `REPLACE`: kind, per-frame layout, or import identity changed; a new container is created.

Views of recreated images are recreated, ids missing from the new descriptor are released, and every task is recreated.
Rebuilding frees existing command buffers after `vkDeviceWaitIdle`.

## Subgraphs

`RendGraphDescVK::add_subgraph(id, RendSubgraphDescVK, port bindings)` groups tasks, owned resources, and internal edges under
one id and returns a `RendSubgraphVK&` to `connect` from. It is flattened on add: member tasks and owned resources become
`<id>.<name>`; names resolve to a port's bound resource, else the subgraph's own resource, else the enclosing graph's.
Imports stay graph-wide, and importing the same object again merges. Edges to and from the subgraph id resolve at build from
accesses: an incoming edge on R reaches members that access R without an edge on R from another member, and an outgoing one
leaves members that access R without an edge on R to another member. Order a subgraph's port accesses with internal edges.

## Resource dependency model

- Edge source: the union of the earlier task's usage stages; source access is its writes only.
- Edge destination: the union of the later task's usage stages, reads, and writes.
- A task whose barrier covers everything reports `RendResUsageVK::ALL_COMMANDS`.

Construction fails on cycles, self edges, unknown tasks, undeclared resources, edges whose tasks do not both access the
resource, graphics-only usages on compute tasks, and conflicts not ordered by a same-resource edge chain. Views of one image
are separate ids; connect the id each task actually accesses.

Same-queue edges become one pipeline barrier per resource before the later task: buffer barriers for buffers, whole-image
barriers in the unified `GENERAL` layout for image views, global memory barriers otherwise. Cross-queue edges signal the
producer's timeline semaphore at merged source stages and wait at merged destination stages. Layout transitions out of
`UNDEFINED` / `PRESENT_SRC_KHR` are automatic for written images; queue-family ownership transfers are not emitted.

## Queue behavior

`GRAPHICS` selects the graphics queue. `COMPUTE_1` and `COMPUTE_2` select two queues of the compute family, sharing its
command pool. Logical queues get their own `VkQueue` while the family has unused queues, and otherwise share one (edges on a
shared `VkQueue` become barriers). A compute-capable graphics family may serve all logical queues.

`ROImageVK` and `ROBufferVK` default to all renderer-selected queue families with `VK_SHARING_MODE_CONCURRENT`. An explicit
exclusive override needs ownership transfers the graph does not generate.

## Presentation and frame completion

Acquire waits attach to the first graphics submission; the last graphics node transitions the swap-chain image to
`PRESENT_SRC_KHR` and carries the terminal semaphore and in-flight fence. Every task on a non-graphics `VkQueue` must reach
a graphics task through edges (skipped when the graph has no graphics task).

## Built-in task types

All in `include/function/render_vk/`. Registered types are usable from recipes by alias.

| Class | Recipe type | Purpose |
| --- | --- | --- |
| `RTRunComputePipelineVK` | `run_compute_pipeline` | One compute dispatch; queue configurable, default graphics |
| `RTRunComputePipelineIndirectVK` | `run_compute_pipeline_indirect` | Dispatch with counts read from a buffer |
| `RTRunComputePipelinesSequentialVK` | `run_compute_pipelines_sequential` | Ordered compute passes in one command buffer |
| `RTPingPongVK` | `ping_pong` | Alternates two compute pipelines for N iterations |
| `RTMaxDownsampleVK` | `max_downsample` | Mip chain by 2x2 max, one view per mip |
| `RTRunGraphicsPipelineVK` | `run_graphics_pipeline` | One graphics pipeline draw (`Builder`) |
| `RTRunGraphicsPipelineIndirectVK` | `run_graphics_pipeline_indirect` | Indexed indirect draws for non-mesh geometry |
| `RTGraphicsDrawMeshVK` | `graphics_draw_mesh` | Draws parts of a compiled mesh; imports and aliases it |
| `RTGenerateMipmapsVK` | `generate_mipmaps` | Color mip chain with filtered blits |
| `RTFullMemoryBarrierVK` | `full_memory_barrier` | Orders a frame's persistent-state mutation after the previous frame's consumers |
| `RTCPUInFlightAcquireVK` | `cpu_acquire_image` | Async image readback, emitted through `EventManager` |
| `RTCPUInFlightBufferAcquireVK` | `cpu_acquire_buffer` | Async buffer readback, emitted through `EventManager` |
| `RTRunGraphicsPipelinesSequentialVK` | — | Ordered graphics passes in one command buffer |
| `RTBuildAccelerationStructuresVK` | — | Records a prepared acceleration-structure build |
| `RTTriggeredVK` | recipe `trigger` | Records its child on triggered or enabled frames |
| `RTToggleVK` | recipe `toggle` | Records its child while enabled |

Compute tasks default to `GRAPHICS`. Graphics tasks always run on graphics. Prefer an existing type before adding one.

## Debug output

- `RenderGraphVK::dump_graph_metadata()` returns `RendGraphMetadataVK` (tasks with queue, order, and subgraph; edges with
  stages, accesses, and `cross_queue`; subgraphs).
- `TimestampQueryPluginVK` (enabled by `RendererCapabilities::timestamp_query`) writes `gpu_timestamp_report.json`;
  `python export_render_graph.py` turns it into Mermaid Markdown or Graphviz SVG.
- `write_descriptor_debug_output(name, graph->list_descriptors())` dumps descriptor rows (to `test_env/descriptor_debug/` in
  tests).

## Failure checklist

- Every id a task accesses is declared, imported, or aliased to one, and every import is supplied with a non-null container.
- Every conflict has a same-resource edge chain, and both ends of each edge access the resource.
- Storage resources that are only read are annotated `read`; otherwise they count as writes.
- Compute tasks on another queue reach a graphics task.
- The rebuild callback does not compile pipelines or snapshot initial data, and re-imports renderer-owned containers.
- After a rebuild, re-acquire containers that were `REPLACE`d.
- Concurrent sharing is preserved for resources crossing queue families.

## Canonical examples

- `tests/executable/function/render/vulkan_draw_triangle_test.cpp`: compact C++ graph.
- `vulkan_draw_cube_test.cpp`, `vulkan_draw_sponza_test.cpp`, `vulkan_draw_skinned_mesh_test.cpp`: larger C++ graphs with
  factory resources and initial data.
- `vulkan_draw_cube_unshaded_test.cpp`, `vulkan_pixel_sorting_test.cpp`, `deferred_test/deferred_render_graph.cpp`:
  recipe-based graphs (see the recipes reference).
- `src/function/render_vk/render_graph_vk.cpp`: authoritative build, diff, validation, barrier, and submission behavior.
- `src/function/render_vk/renderer_vk.cpp`: registration, swap-chain container, execution, presentation, delayed
  destruction.
- `tests/unit/function/render_graph_vk_test.cpp`, `render_graph_descriptor_vk_test.cpp`,
  `rend_graph_resource_diff_vk_test.cpp`, `render_resource_list_vk_test.cpp`.
