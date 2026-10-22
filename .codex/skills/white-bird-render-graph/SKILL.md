---
name: white-bird-render-graph
description: Build, extend, and diagnose White Bird Engine's Vulkan render graphs and their Lua/Teal recipes. Apply when registering a graph and its rebuild callback, writing or changing a render-graph recipe or feature subgraph module, declaring graph resources, imports, and task edges, adding or registering an IRenderTaskVK type and its RTParams struct, using triggers, toggles, imported C++ tasks, or subgraphs, choosing queues, or debugging render-graph synchronization, rebuild diffing, or lifecycle behavior.
---

# White Bird Render Graph

A render graph is a DAG of `IRenderTaskVK` nodes connected by resource dependency edges. Applications never build a graph
directly: they register a **rebuild callback** that returns a complete `RendGraphDescVK`, and the graph calls it for the first
build and every rebuild. The descriptor usually comes from a Teal **recipe**; C++ adds only what recipes cannot express.

## Register a graph

```cpp
Ref<RenderGraphVK> graph = renderer->register_render_graph("my_graph",
    [this, renderer](const RendGraphContextVK& p_context) {
        RendGraphRecipeImportsVK imports;
        imports.add(REND_RES_SWAP_CHAIN, *renderer->acquire_swap_chain_image_view_container())
            .add("camera_info", *camera_info_ubo);
        const DataValue recipe = RendGraphScriptVK::run("render_graphs.my_graph",
            RendGraphScriptVK::make_context(*render_context, p_context));
        return RendGraphRecipeBuilderVK().build(recipe, imports, render_context);
    });
renderer->activate_render_graph("my_graph");
```

- The callback runs at registration, when the context changes (swap-chain extent, checked at `execute()`), and on
  `RenderGraphVK::rebuild()`. Keep pipeline compiles, application-owned buffers, and initial-data snapshots outside it; only
  assemble the descriptor inside, and make sure its captures outlive the graph.
- Re-import renderer-owned objects such as the swap-chain container inside the callback; do not capture them once.
- Rebuilds diff the new descriptor against the current graph. Matching graph-owned resources keep their content; changed ones
  are recreated in place or replaced. All tasks are recreated from their factories, so state that must survive a rebuild
  lives outside the task.

## Choose where the graph is described

- **Recipe (default).** A Teal `script` resource under `res/assets/scripts/teal/render_graphs/` exporting
  `build(context)`. Create resource handles with `utils.render_graph`, add tasks to `graph_utils.graph()` through colon
  methods, and return `graph:build()`. It collects referenced resources automatically and emits the C++ recipe schema.
  Use registered task types by name with typed `params`.
- **C++ additions.** Tasks needing application objects (ImGui overlay, acceleration-structure builds, CPU readback sinks)
  are built as `RendTaskDescVK` and supplied with `RendGraphRecipeImportsVK::add_task()`; the recipe lists them as
  `{ id = ..., import = true }`. Parts not yet expressible in Lua may be added to the descriptor the builder returns.
- **Pure C++ graph.** Only for small tests or when a recipe is not available; see `vulkan_draw_triangle_test.cpp`.

Read [references/recipes-and-scripts.md](references/recipes-and-scripts.md) before writing or changing a recipe, a feature
module, a context struct, a trigger or toggle, an imported task, or a new task type.

## Core rules

- **Edges are the only ordering.** `connect(next_task_id, resource_id)` on the earlier task (or a recipe `connect` entry)
  carries one resource; both tasks must access it, and stages and accesses are derived from the `RendResUsageVK` usages
  both report. Every read-write, write-read, and write-write conflict on a resource must be ordered by a chain of edges on
  that same resource, or construction fails. Read-read pairs do not conflict.
- **Tasks report their own accesses** through `append_accesses()`: reflected shader resources (`WBE_DESCRIPTOR` access),
  render targets, fixed-function buffers. Map task-local ids with `alias()` only; graphics tasks render to
  `render_target_colors[<index>]` and `render_target_depth`. Unused aliases are rejected.
- **Recipe handles.** Resource helpers return `graph_utils.Resource`. Use `task:alias(local_name, resource)` for task-local bindings. `graph:add()` returns task objects;
  pass those objects to `graph:chain(resource, tasks)` or `producer:connect(consumer, resource)`, and use handles in
  subgraph ports; image views retain their source image. No explicit recipe resource list is authored. `uses` retains
  same-name/parameter resources without declaring accesses or ordering. `import_resource` names exact supplied imports;
  `port` names a subgraph port; `reference` names an existing ancestor/composite resource without adding an import.
- **Resources.** Graph-owned resources are declared with `add_buffer()`, `add_image()`, `add_image_view()`, and
  `add_sampler()` (factories of the context in C++, tables in recipes). Everything owned elsewhere is an import through
  `add_resource(id, *bindable)`; composites register `<id>.<member>`. Keep the description structs' `operator==` complete,
  because diffing relies on it.
- **Meshes** are drawn with `graphics_draw_mesh` (`RTGraphicsDrawMeshVK`, or `add_to_graph()` in C++), which imports the
  mesh under its render-database key and aliases the `REND_RES_MESH_*` ids. Do not hand-assemble mesh draws.
- **Queues.** Compute tasks default to `GRAPHICS`; pass `COMPUTE_1` / `COMPUTE_2` (recipe param `queue_family`) to run
  asynchronously. Same-queue edges become pipeline barriers; cross-queue edges use timeline semaphores. Every task on
  another queue must reach a graphics task.
- **Threading.** Render objects and backend helpers are rendering-thread-confined. Submit cross-thread work through
  `IRenderer::add_job()`; do not add locks.
- **Descriptors.** The graph binds the renderer-owned descriptor heaps before recording each task; pipeline-run tasks push
  their reflected descriptor indices. Do not reintroduce descriptor sets or pipeline layouts.

Read [references/system-and-usage.md](references/system-and-usage.md) before changing edge validation, synchronization,
queue behavior, descriptor binding, rebuild diffing, subgraph flattening, registration, or presentation, or when choosing
among the built-in task types.

## Validate

```sh
python validation.py -t debug -- --gtest_filter='WBERenderGraph*:WBERendGraph*:WBERenderTaskRegistryVK*:WBERT*'
```

The wrapper builds (including the Teal type check, which fails the build on a type error), runs the focused tests, then the
full unit suite and shader tests. Visual or synchronization changes also need a rendering executable run; see the
`white-bird-testing` skill. Do not invoke CMake directly or run lint.
