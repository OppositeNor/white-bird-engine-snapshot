# Render Graphs

A render graph is a DAG of `IRenderTaskVK` nodes connected by resource dependency edges. The application does not build it
directly: it registers a rebuild callback that returns a complete `RendGraphDescVK`, usually produced by a Lua/Teal recipe.

## Registration and Rebuilds

```cpp
Ref<RenderGraphVK> graph = renderer->register_render_graph("scene",
    [this, renderer](const RendGraphContextVK& p_context) {
        RendGraphRecipeImportsVK imports;
        imports.add(REND_RES_SWAP_CHAIN, *renderer->acquire_swap_chain_image_view_container())
            .add("camera_info", *camera_info_ubo);
        const DataValue recipe = RendGraphScriptVK::run("render_graphs.scene",
            RendGraphScriptVK::make_context(*render_context, p_context));
        return RendGraphRecipeBuilderVK().build(recipe, imports, render_context);
    });
renderer->activate_render_graph("scene");
```

The callback runs for the first build (at registration), whenever the context changes (the swap-chain extent, checked when
the graph executes), and on `RenderGraphVK::rebuild()`. Keep one-time setup outside it: pipeline compiles,
application-owned buffers, and initial-data snapshots. Inside it, only assemble the descriptor. Import renderer-owned
containers such as the swap chain inside the callback, since the renderer replaces them when the number of frames in flight
changes. Unregistering defers destroying the graph until no frame in flight can use it.

A rebuild diffs the new descriptor against the current graph (`RendGraphResourceDiffVK`):

- A graph-owned resource is **kept**, with its content, when its id, kind, per-frame layout, and evaluated description match
  and, for a view, its image is kept. Imports are kept when they bind the same object.
- A changed description is **recreated** in place in its container, losing its content.
- A changed kind, per-frame layout, or import identity is **replaced** with a new container; re-acquire it with
  `acquire_resource()`.
- Every task is recreated from its factory, so task state that must survive a rebuild lives outside the task.

The diff relies on complete `operator==` on the description structs.

## Descriptors

`RendGraphDescVK` holds tasks, resources, and subgraphs.

- **Graph-owned resources** are declared with `add_buffer()`, `add_image()`, `add_image_view()`, and `add_sampler()`, each
  taking a factory of the context that returns `RendBufferDescVK`, `RendImageDescVK`, `RendImageViewDescVK`, or
  `RendSamplerDescVK`. The primary usage (`RendResPrimaryUsageVK`) selects Vulkan usage flags. Buffers take a memory domain,
  optional initial data, and `per_frame`. Per-frame resources live in an `ROCMultislotsVK` with one slot per frame in flight,
  and others in an `ROCSingleVK`.
- **Imports** are resources owned outside the graph: `add_resource(id, *bindable)` with any `IBindableRenderResourceVK`.
  Composites such as compiled meshes register their members as `<id>.<member>`. A `RenderResourceListVK` registers
  `<id>[<index>]` entries, and an access or edge on the list expands to every entry.
- **Tasks** are `RendTaskDescVK` entries holding a `task_id`, a factory
  (`Unique<IRenderTaskVK>(const RendGraphContextVK&)`), edges added with `connect()`, and aliases added with `alias()`.
  Descriptors are copyable; copies share the factory's captures.

Frame-local data is held in graph resources and updated in place, never rebound:

```cpp
Ref<ROCMultislotsVK> camera_buffers = graph->acquire_resource("camera_info").dynamic_cast_ref<ROCMultislotsVK>();
camera_buffers->publish_buffer_update(&camera_info, 0, sizeof(camera_info));
```

Each slot applies the newest bytes when it is next used. Publish on the rendering thread; other threads use
`IRenderer::add_job()`.

## Edges and Synchronization

Edges are the only ordering mechanism. `connect(next_task_id, resource_id)` on the earlier task adds one edge carrying one
resource. Both tasks must access the resource, and the edge's stages and accesses are derived from the `RendResUsageVK`
usages both report. Every read-write, write-read, and write-write conflict on a resource must be ordered by a chain of edges
on that same resource; read-read pairs do not conflict. Construction fails on cycles, self edges, unknown tasks, undeclared
resources, unordered conflicts, and graphics-only usages on compute tasks.

Tasks report every access themselves through `IRenderTaskVK::append_accesses()`: reflected shader resources, render targets,
and fixed-function buffers. Accesses never create edges. The only way to map task-local identifiers to graph resources is
`alias()`; the graph rejects aliases the task never uses.

Same-queue edges become pipeline barriers recorded before the later task: buffer barriers for buffers, whole-image barriers in
the unified `GENERAL` layout for image views, and global memory barriers otherwise. Cross-queue edges use one timeline
semaphore per producer task.

### Queues

`QueueFamilyVK::GRAPHICS` selects the graphics queue. `COMPUTE_1` and `COMPUTE_2` select two queues of the compute family,
which share one `VkQueue` when the family has too few queues. Compute tasks default to `GRAPHICS`; choose a compute queue to
overlap work. Every task on a non-graphics queue must reach a graphics task through edges, so the frame fence covers it.
Images and buffers default to concurrent sharing across queue families; the graph emits no queue-family ownership transfers.

Acquire waits attach to the first graphics submission. The last graphics submission transitions the swap-chain image for
presentation and signals the terminal semaphore and in-flight fence.

## Subgraphs

`add_subgraph(id, RendSubgraphDescVK, port bindings)` groups tasks, graph-owned resources, and internal edges under one id. It is
flattened when added: member tasks and owned resources become `<id>.<name>`, and names inside it resolve to a port's bound
resource, else the subgraph's own resource, else the enclosing graph's. Imports stay graph-wide, and importing the same object
again merges. Edges to and from the subgraph id are resolved at build time from the tasks' accesses. An incoming edge on R
reaches the members that access R with no edge on R from another member; an outgoing edge leaves the members that access R
with no edge on R to another member. Order a subgraph's accesses to its ports with internal edges.

## Recipes

A recipe is a Teal (or Lua) `script` resource whose module exports `build(context)` and returns a `RendGraphRecipeVK`
(`rend_graph_recipe_params_vk.hh`). `RendGraphScriptVK::run()` executes it in a fresh sandbox, and
`RendGraphRecipeBuilderVK::build()` turns the result into a descriptor. Recipes only build descriptors; they never touch render
objects.

Recipes are written against the graph API of `render_graph_utils.render_graph_utils`: create a graph, declare resources as
handles, add tasks with typed constructors, connect them, and return `graph:build()`.

```lua
local type wbe = require("wbe")
local Graph = require("render_graph_utils.render_graph_utils")
local exports = {}

function exports.build(_context: wbe.RendRecipeContextVK): wbe.RendGraphRecipeVK
    local graph = Graph.new()
    local swap_chain = graph:import_resource("swap_chain")
    local camera_info = graph:import_resource("camera_info")
    graph:draw_mesh("run_static_mesh", {
        mesh = graph:add_mesh("model_cube_static.mesh"),
        pipeline = graph:add_graphics_pipeline("draw_cube_unshaded_pipeline"),
        parts = { "opaque" },
    }, { uses = { camera_info } }):color_target(0, swap_chain)
    return graph:build()
end

return exports
```

### Resource handles

Every resource method returns a `Graph.Resource` handle:

- Graph-owned resources: `:add_buffer(id, usage, size)`, `:add_storage_buffer(id, size)`,
  `:add_image(id, usage, format, extent?, additional_usage?, mip_levels?)`, `:add_storage_image(id, format, width, height,
  additional_usage?)`, `:add_image_view(id, image, view?)`, and `:add_clamp_to_edge_sampler(id, sampler?)`. An image with
  no extent follows the swap chain.
- `image:view()` returns a cached default view with id `<image id>.view`, whose type, format, and range follow the image.
  `image:view(id, view?)` names a view or customizes its mip, layer, or aspect range; a customized view needs an id. A view
  keeps its image reachable.
- `:import_resource(id)` names a resource C++ supplies through `RendGraphRecipeImportsVK`; `:reference(id)` names an
  enclosing resource or a composite member such as `<mesh>.<member>` without declaring another import; `:port(id)` names a
  port of a graph that becomes a subgraph. Views may be taken of all three; C++ validates the actual kind.
- `:add_mesh(key)`, `:add_compute_pipeline(key)`, and `:add_graphics_pipeline(key)` name entries the application registered
  in the render database. They are only valid in task params, never as edges, aliases, or attachments, and the task checks
  their type.

Nothing is registered with the graph: `graph:build()` emits the resources reachable from task params, aliases, edges,
`uses`, and ports, each once. Unused handles are omitted, and two different owned handles with the same id fail.

```lua
local albedo = graph:add_image("gbuffer_albedo_image", "image_render_target", formats.gbuffer_albedo):view("gbuffer_albedo")
draw_gbuffer:color_target(0, albedo)
lighting:alias("gbuffer_albedo", albedo)
graph:chain(albedo, { draw_gbuffer, lighting })
```

### Tasks

The typed constructors are `:compute`, `:compute_indirect`, `:compute_sequential`, `:ping_pong`, `:draw`,
`:draw_indirect`, `:draw_mesh`, `:generate_mipmaps`, `:full_memory_barrier`, and `:cpu_acquire_image`, generated from the
task types that name them (see [Task types](#task-types)). Each is `graph:<method>(id, params, options?)` and returns a
task handle. `options` holds `aliases`, `uses`, `trigger`, and `toggle`. Params type-check against
`Graph.RTParams<Name>VK`, which accepts handles in resource fields:

```lua
local lighting = graph:compute("lighting", {
    pipeline = graph:add_compute_pipeline("lighting_shader"),
    group_count_x = groups_x,
    group_count_y = groups_y,
    queue_family = "compute_1",
}, { aliases = { albedo = albedo } })

graph:generate_mipmaps("shadow_mipmaps", {
    source_view = moments,
    mip_chain_view = sampled_moments,
    format = formats.shadow_moments,
})
```

Constructors copy params and options, so one table can serve several tasks.

- `task:alias(local_name, resource)` maps a task-local id to a graph resource. A name is mapped once per task, and tasks may
  reuse each other's names. An alias cannot redirect an id that the task's params already name.
- `task:color_target(index, resource)` and `task:depth_target(resource)` alias the render attachments
  `render_target_colors[<index>]` and `render_target_depth`.
- `uses = { handle, ... }` keeps reachable a resource that the task accesses only through a same-name shader binding, for
  example a camera buffer or a scratch buffer used by one task. It declares no access and adds no edge.
- `graph:import_task(id)` references a task built in C++ (see [Imports and C++ tasks](#imports-and-c-tasks)).
- `graph:add(task)` adds a raw description: a type without a constructor, or a subgraph task. Its params use string ids
  and are untyped, so annotate them with the reflected struct (`local params: wbe.RTParams<Name>VK`).

Task methods return the task, so calls chain. Task ids are the names C++ and diagnostics see; `graph:get(id)` looks one up,
but keeping the handles is simpler.

### Edges

Edges stay explicit and per resource. Each method takes task handles of the same graph and one resource handle, a
non-empty list of them, or a bundle from `graph:resource_bundle({ ... })`, and adds one edge per resource:

```lua
graph:chain({ color, depth }, { first, second, third })   -- first -> second -> third on both resources
gbuffer:connect(lighting, { albedo, normal, depth })      -- a two-task chain
composition:connect_all({ extract_bloom, present }, result_image)  -- fan-out; the consumers stay unordered
lighting:inputs_from(gbuffer, { gbuffer_albedo = albedo, gbuffer_depth = depth })  -- aliases plus one edge each
local targets = graph:resource_bundle({ albedo, normal, depth })
graph:chain(targets, { geometry, lighting })
```

Every adjacent pair of a chain must access every listed resource. A bundle is only a list for edges; it declares nothing.

### Imports and C++ tasks

- Every import is supplied through `RendGraphRecipeImportsVK`; a declared-but-missing or supplied-but-undeclared import is
  an error. Recipes cannot set initial data, so buffers that need it are application-owned imports registered in the render
  database under their import name. When C++ supplies imports that only an imported task binds, pass the full list to
  `graph:build(imports)`.
- Tasks that need application objects (for example the ImGui overlay) are built in C++, supplied with
  `RendGraphRecipeImportsVK::add_task()`, referenced with `graph:import_task(id)`, and connected like any task.
- The callback may add to the descriptor the builder returns for parts not yet expressible in Lua. The deferred test adds
  its acceleration structure and dynamic mesh entities this way.

### Triggers and toggles

- `trigger = { event, activate }` wraps the task with `RTTriggeredVK`, which records only on triggered or enabled frames.
  Work that runs once is a trigger activated while pending, which the application disables after the first recorded frame.
- `toggle = { event, enabled }` wraps it with `RTToggleVK`, outside any trigger. Rebuilds re-run the recipe, so pass a
  toggle's current state through the context.

Both are driven by commands emitted through `EventManager`. On a subgraph task they wrap every member.

### Subgraphs and feature modules

A child graph becomes a subgraph task with `child:as_subgraph(id, { port = parent_resource, ... }, imports?)`, which the
parent adds with `parent:add()` and connects like any task. The port map declares every port, including unused ones, and
binds it to a parent resource; the child's owned resources stay scoped to it.

A reusable recipe part lives in `render_graph_utils/features/` and exports `subgraph(id, params, target)`, returning such a
task. Its C++ side has a `configure_recipe()` that serializes the feature's `WBE_REFLECT_SCRIPT` parameter struct (including
the Lua `module` name) into the context and supplies the feature's resources and C++ tasks to the imports. The recipe calls
`require(params.module).subgraph(id, params, target)`. The deferred test's pixel-sort, SVO, and octree features follow this
pattern.

### Emitted recipe

`graph:build()` lowers handles and methods to plain tables; nothing else crosses the Lua/C++ boundary:

- `imports`: the import ids.
- `resources`: each declares exactly one of `buffer`, `image`, `image_view`, or `sampler`.
- `tasks`: a registered task `type` with strictly deserialized `params`, `aliases`, `connect` edges
  (`{ to = <task id>, resource = <resource id> }`), and optional `trigger`, `toggle`, `subgraph`, and `ports`. Imported tasks
  carry only `id`, `import = true`, and `connect`.

### Context

`context` holds `RendRecipeContextVK` facts: `frames_in_flight`, `swap_chain_format`, and `swap_chain_extent`
(`RendGraphScriptVK::make_context()`). An application that adds entries declares a `WBE_REFLECT_SCRIPT` struct deriving from
`RendRecipeContextVK`, fills its base with `get_recipe_context()`, and passes `serialize_context()` to `run()`. Size
swap-chain-dependent resources and dispatches in Lua from `swap_chain_extent`.

### Task types

Recipes refer to task types by name. A type is registered by annotating its class
`WBE_META(WBE_RENDER_TASK(<method>), WBE_SERIALIZATION_ALIASES(<name>))` and declaring a reflected static
`create(const RenderContextVK*, const <Params>&, const RendGraphContextVK&)`. The params struct is
`WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT`. An optional static `prepare_graph()` lets the type add
imports and aliases when it is added (`RTGraphicsDrawMeshVK` uses it to import meshes). A bare `WBE_RENDER_TASK` registers
the type without a constructor.

Reflection generates the `RenderTaskRegistryVK` implementation, the declarations in `render_graph_utils.render_graph`
(including a `Graph.RTParams<Name>VK` interface for each params struct with resource fields), and, in
`render_graph_utils.render_graph_tasks`, the installer of each `graph:<method>()` constructor.

Annotate the params fields that hold ids, so the constructor accepts handles there and lowers them to ids:

- `WBE_RECIPE_RESOURCE(graph)` for graph resource ids;
- `WBE_RECIPE_RESOURCE(task_local)` for ids the task's aliases map;
- `WBE_RECIPE_RESOURCE(database, RenderObjectType)` for render-database keys, where `RenderObjectType` is a
  `WBE_RENDER_OBJECT` class that the handle's type is checked against.

The field holds a `std::string`, optional or in a vector, or a `DataValue` map of ids, directly or in nested params.

| Type | Constructor | Task | Purpose |
| --- | --- | --- | --- |
| `run_compute_pipeline` | `:compute` | `RTRunComputePipelineVK` | One compute dispatch |
| `run_compute_pipeline_indirect` | `:compute_indirect` | `RTRunComputePipelineIndirectVK` | Dispatch with counts read from a buffer |
| `run_compute_pipelines_sequential` | `:compute_sequential` | `RTRunComputePipelinesSequentialVK` | Ordered compute passes in one command buffer |
| `ping_pong` | `:ping_pong` | `RTPingPongVK` | Alternates two compute pipelines for N iterations |
| `max_downsample` | — | `RTMaxDownsampleVK` | Mip chain by 2x2 maximum |
| `run_graphics_pipeline` | `:draw` | `RTRunGraphicsPipelineVK` | One graphics pipeline draw |
| `run_graphics_pipeline_indirect` | `:draw_indirect` | `RTRunGraphicsPipelineIndirectVK` | Non-mesh indexed indirect draws |
| `graphics_draw_mesh` | `:draw_mesh` | `RTGraphicsDrawMeshVK` | Draws parts of a compiled mesh |
| `generate_mipmaps` | `:generate_mipmaps` | `RTGenerateMipmapsVK` | Color mip chain with filtered blits |
| `full_memory_barrier` | `:full_memory_barrier` | `RTFullMemoryBarrierVK` | Orders a frame's mutation of persistent state after the previous frame's consumers |
| `cpu_acquire_image` / `cpu_acquire_buffer` | `:cpu_acquire_image` / — | `RTCPUInFlightAcquireVK` / `RTCPUInFlightBufferAcquireVK` | Asynchronous CPU readback, delivered through `EventManager` |

Compute types take a `queue_family` param and a `push_constants` table.

### Scripts

Recipes and their modules are Teal sources under `res/assets/scripts/teal/` (see
[resource_standard.md](../resource/resource_standard.md#script-resources)). They import the generated declarations of the
`WBE_REFLECT_SCRIPT` types as types only (`local type wbe = require("wbe")`) and annotate `build(context: <context type>):
wbe.RendGraphRecipeVK`. Enums serialize as their canonical names; compare `Graph.enum_name(value)` with the canonical
upper-case name, which also accepts the lowercase aliases.

Importing `render_graph_utils.render_graph_utils` also brings the types `Graph.Graph`, `Graph.Resource`, `Graph.Task`, and
`Graph.RTParams<Name>VK`. For types only, import the generated declarations with
`local type render_graph = require("render_graph_utils.render_graph")`.

Other graph helpers live next to it: `render_graph_utils.sort` provides `radix_sort_buffers(names, max_count, graph?)` and
`add_radix_sort_task(graph, id, buffers, queue_family?)`; `render_graph_utils.large_prefix_scan` and
`render_graph_utils.dedup_large` provide dispatch sequences for a `run_compute_pipelines_sequential` task; and
`render_graph_utils.svo_3` gathers and deduplicates the leaves of the third sparse voxel octree construction.

## C++-only Graphs

Small tests may build the descriptor in C++ inside the callback; see
`tests/executable/function/render/vulkan_draw_triangle_test.cpp`:

```cpp
RendTaskDescVK task_desc(render_allocator);
task_desc.task_id = "run_pipeline";
task_desc.alias(RenderTargetsVK::color_id(0), REND_RES_SWAP_CHAIN);
task_desc.factory = [=](const RendGraphContextVK& p_context) {
    return RTRunGraphicsPipelineVK::Builder(render_allocator)
        .set_pipeline(graphics_pipeline)
        .set_render_extent(p_context.swap_chain_extent)
        .create_unique(render_context);
};
graph_desc.get_task("run_shadow").connect("run_pipeline", "shadow_moments");
graph_desc.add_task(std::move(task_desc));
```

## Debugging

- `RenderGraphVK::dump_graph_metadata()` describes tasks (queue, order, subgraph), edges (stages, accesses, cross-queue), and
  subgraphs.
- With timestamp queries enabled, `gpu_timestamp_report.json` includes that structure; `python export_render_graph.py` renders
  it as Mermaid or SVG.
- `write_descriptor_debug_output(name, graph->list_descriptors())` dumps the bound descriptors.

## Examples

- `tests/executable/function/render/vulkan_draw_cube_unshaded_test.cpp` with `render_graphs/draw_cube_unshaded.tl`: minimal
  recipe.
- `vulkan_pixel_sorting_test.cpp` with `pixel_sorting_recipe_context.hh` and `render_graphs/pixel_sorting.tl`: custom
  context and an imported ImGui task.
- `deferred_test/deferred_render_graph.cpp` with `render_graphs/deferred.tl` and `render_graph_utils/features/`: full deferred
  pipeline with feature subgraphs, triggers, toggles, and C++ additions.
