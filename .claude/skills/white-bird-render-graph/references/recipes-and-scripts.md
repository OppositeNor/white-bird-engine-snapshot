# Render Graph Recipes and Scripts

## Flow

1. The rebuild callback builds a `RendGraphRecipeImportsVK` with every resource (`add(name, bindable)`) and C++ task
   (`add_task(RendTaskDescVK)`) owned outside the graph.
2. `RendGraphScriptVK::run(key_or_source, context)` runs the recipe in a fresh sandboxed `SBLua` and returns its table as a
   `DataValue`. `require` resolves other script resources through `SMRResourceManager`.
3. `RendGraphRecipeBuilderVK().build(recipe, imports, render_context)` returns a `RendGraphDescVK`. It adds tasks through
   `RenderTaskRegistryVK::add_task()` (which runs the type's `prepare_graph`), wraps a `trigger` first and a `toggle`
   outermost, copies imported tasks, and builds subgraphs. A declared-but-missing or supplied-but-undeclared import or task
   is an error.
4. The callback may add to that descriptor for parts not yet expressed in Lua (the deferred test adds its acceleration
   structure and dynamic mesh entities). Keep everything else in the recipe.

Recipes only build descriptors; they never touch render objects.

## The graph API

`local Graph = require("render_graph_utils.render_graph_utils")` gives the implementation; `Graph.new()` creates a graph,
and the module also exposes the types `Graph.Graph`, `Graph.Resource`, `Graph.Task`, `Graph.ResourceBundle`, and the
handle-accepting params interfaces `Graph.RTParams<Name>VK`. For types only, import the generated declarations with
`local type render_graph = require("render_graph_utils.render_graph")`.

### Resources

| Method | Returns |
| --- | --- |
| `:add_buffer(id, usage, size)`, `:add_storage_buffer(id, size)` | Graph-owned buffer |
| `:add_image(id, usage, format, extent?, additional_usage?, mip_levels?)` | Graph-owned 2D image; an omitted extent follows the swap chain |
| `:add_storage_image(id, format, width, height, additional_usage?)` | `image_read_write` image |
| `image:view()`, `image:view(id, view_params?)`, `:add_image_view(id, image, view_params?)` | View; `:view()` caches a default view with id `<image id>.view`, and a customized view needs an explicit id |
| `:add_clamp_to_edge_sampler(id, sampler_params?)` | Graph-owned sampler |
| `:import_resource(id)` | A resource C++ supplies through `RendGraphRecipeImportsVK` |
| `:reference(id)` | An existing enclosing resource or composite member (`<id>.<member>`), without declaring another import |
| `:port(id)` | A port of this graph, bound when it becomes a subgraph |
| `:add_mesh(key)`, `:add_compute_pipeline(key)`, `:add_graphics_pipeline(key)` | Render-database entry the application registered; usable only in task params, never as an edge or binding |
| `:resource_bundle({ ... })` | A reusable ordered list for edges; it declares nothing |

`graph:build()` collects every resource reachable from task params, aliases, edges, `uses`, and ports (views pull in their
image), emits each once, and omits unused handles. Two different owned handles with the same id fail. A view may also be
taken of an import, reference, or port; C++ validates the actual kind.

### Tasks

The typed constructors are generated from task classes annotated `WBE_RENDER_TASK(<method>)` (see the table in
[system-and-usage.md](system-and-usage.md#built-in-task-types)). Each is `graph:<method>(id, params, options?)`, where
`options` is `{ aliases?, uses?, trigger?, toggle? }`, and returns a `Graph.Task`. Params type-check against
`Graph.RTParams<Name>VK`, which accepts handles in the fields marked `WBE_RECIPE_RESOURCE`. Params and option tables are
copied, so a table can be reused for several tasks.

- `task:alias(local_name, resource)` maps a task-local id. A name maps once per task; different tasks may reuse it. A
  handle passed in params cannot be redirected by an alias of the same name.
- `task:color_target(index, resource)` and `task:depth_target(resource)` alias `render_target_colors[<index>]` and
  `render_target_depth`.
- `uses = { handle, ... }` keeps resources reachable that the task accesses only through same-name reflected shader
  bindings (for example a camera buffer or a single-task scratch buffer). It declares no access and adds no edge.
- `graph:import_task(id)` references a C++ task supplied with `imports.add_task()`.
- `graph:add(task)` adds a raw task description (a custom type without a constructor, or a feature subgraph). Annotate its
  `params` with the reflected `wbe.RTParams<Name>VK`, since raw params are untyped and use string ids.
- `graph:get(id)` looks up a task; prefer keeping the handles.

Task methods return the task, so they chain fluently.

### Edges

Each method below adds one edge per resource between task handles of the same graph; a resource argument is one handle, a
non-empty list, or a bundle.

- `graph:chain(resources, { a, b, c })` orders adjacent tasks: `a -> b -> c`. Every adjacent pair must access every resource.
- `a:connect(b, resources)` is a two-task chain.
- `a:connect_all({ b, c }, resources)` fans out, leaving `b` and `c` unordered with each other.
- `b:inputs_from(a, { local_name = resource, ... })` aliases each name on `b` and adds one `a -> b` edge per distinct
  resource.

### Emitted schema

`graph:build(imports?, ports?)` lowers everything to the plain `RendGraphRecipeVK` tables C++ reads
(`rend_graph_recipe_params_vk.hh`); handles and methods never cross the Lua/C++ boundary.

- `imports`: every import id. Pass the full list to `graph:build(imports)` when C++ supplies imports that only an imported
  task binds, so supplied-import matching stays strict.
- `resources`: each declares exactly one of `buffer`, `image`, `image_view` (`{ image, view }`), or `sampler`.
- `tasks`: `{ id, type, params, aliases, connect = { { to, resource } }, trigger, toggle, subgraph, ports }`. Imported
  tasks carry only `id`, `import = true`, and `connect`.

Params are strictly deserialized: unknown keys fail. Pipeline keys name pipelines the application compiled outside the
callback and keeps alive. Enums serialize as canonical names; compare `Graph.enum_name(value) == "CANONICAL_NAME"`, which
also accepts the lowercase aliases.

## Context

`RendRecipeContextVK` holds `frames_in_flight`, `swap_chain_format`, and `swap_chain_extent`. `make_context()` builds the
plain one. An application that adds entries declares a `WBE_REFLECT_SCRIPT` struct deriving from it, assigns the base with
`get_recipe_context()`, and passes `serialize_context(context)` to `run()`; the recipe annotates
`build(context: wbe.<ContextType>)`. Size swap-chain-dependent resources and dispatches in Lua from `swap_chain_extent`.
Pass runtime state that rebuilds must preserve (toggle states, feature parameters) through the context.

Examples: `tests/executable/function/render/pixel_sorting_recipe_context.hh`,
`deferred_test/deferred_recipe_context.hh`.

## Triggers and toggles

- `trigger = { event, activate }` wraps the task with `RTTriggeredVK`, which records its child while it has remaining
  frames. The application emits `RTTriggeredVK::Command::trigger()` or `enable_for(n)` through `EventManager::emit(event,
  command)`. Run-once work (a static build) is a trigger activated while still pending; the application emits
  `enable_for(0)` after the first recorded frame, so later rebuilds create it inactive.
- `toggle = { event, enabled }` wraps it with `RTToggleVK`, outside any trigger, recording every frame while enabled
  (`Command::enable()` / `disable()`). Rebuilds re-run the recipe, so pass the toggle's current state through the context.
- A trigger or toggle on a subgraph task wraps every member; set it on the task `as_subgraph()` returns
  (`subgraph.toggle = { ... }`).

`EventManager` lives in `function/event/event_manager.hh`; CPU readback tasks also emit their results through it under the
source resource id.

## Imported C++ tasks

Build a `RendTaskDescVK` with its factory and aliases, supply it with `imports.add_task()`, and reference it in the recipe
with `graph:import_task(id)`; connect it like any task. Imported tasks set no `type`, `params`, `aliases`, `trigger`, or
`toggle` in the recipe. Examples: `tests/executable/utilities/rt_imgui_vk.hh`, `deferred_render_graph.cpp`,
`vulkan_pixel_sorting_test.cpp`.

## Feature subgraph modules

A reusable recipe part lives in `render_graph_utils/features/` and exports `subgraph(id, params, target)`. It builds its
own `Graph.new()`, declares its port handles with `graph:port(name)`, and returns
`graph:as_subgraph(id, { name = parent_resource, ... }, imports?)`, a task the parent adds with `parent:add(...)` and
connects like any task. The port map declares every port and binds it to a parent resource; owned resources stay scoped
to the subgraph.

Its C++ side provides `configure_recipe(DataValue&, RendGraphRecipeImportsVK&)`, which serializes its
`WBE_REFLECT_SCRIPT` params struct (derived from `RecipeFeatureParams`, including the `module` name) into the context and
supplies its resources and C++ tasks. The recipe calls `require(params.module).subgraph(id, params, target)`.

Examples: `features/pixel_sort.tl` with `deferred_test/pixel_sort.{hh,cpp}`, `features/voxel_svo.tl` and
`features/voxel_octree.tl` with the deferred test's voxel features, and `render_graphs/deferred.tl`.

## Adding a task type

1. Write `include/function/render_vk/rt_params_<name>_vk.hh` with
   `struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RTParams<Name>VK`; mark required
   fields `WBE_REFLECT_REQUIRED`. Mark fields that hold ids so the constructor accepts handles there:
   `WBE_RECIPE_RESOURCE(graph)` for graph resource ids, `WBE_RECIPE_RESOURCE(task_local)` for ids the task's aliases map,
   and `WBE_RECIPE_RESOURCE(database, <RenderObjectType>)` for render-database keys such as pipelines and meshes (the type
   must be a `WBE_RENDER_OBJECT` class). Such a field is a `std::string`, optional or in a vector, or a `DataValue` map.
2. Mark the task `WBE_META(WBE_RENDER_TASK(<method>), WBE_SERIALIZATION_ALIASES(<name>))`, or with a bare
   `WBE_RENDER_TASK` to register it without a recipe constructor, and declare
   `WBE_META(WBE_REFLECT) static Unique<IRenderTaskVK> create(const RenderContextVK*, const RTParams<Name>VK&, const RendGraphContextVK&)`.
   Add `static void prepare_graph(RendGraphDescVK&, RendTaskDescVK&, const RTParams<Name>VK&, const RenderContextVK*)` if
   the task must add imports or aliases.
3. Use `render_task_create_utils_vk.hh` (`acquire_task_compute_pipeline_vk`, `acquire_task_graphics_pipeline_vk`,
   `resolve_task_push_constants_vk`) in `create()`.
4. Rebuild. `render_task_registry.gen.cpp` registers the class name and aliases; a missing `create` or duplicate name fails
   the build. The params struct appears in `wbe.d.tl`, its handle-accepting interface in the generated
   `render_graph_utils.render_graph`, and the `graph:<method>()` constructor, which creates the type `<name>`, in the
   generated `render_graph_utils.render_graph_tasks`.

Examples: `rt_full_memory_barrier_vk.hh` (simple), `rt_run_compute_pipeline_vk.hh` (standard), `rt_graphics_draw_mesh_vk.hh`
(with `prepare_graph`), and `tests/unit/function/render_task_registry_vk_test_tasks.hh` (test probes).

## Scripts and Teal

- Sources live under `res/assets/scripts/teal/` and are `script` resources declared in `lua_scripts.toml`: recipes in
  `render_graphs/` (`deferred.tl`, `draw_cube_unshaded.tl`, `pixel_sorting.tl`), features in `render_graph_utils/features/`,
  graph helpers in `render_graph_utils/` (`render_graph_utils.tl`, `sort.tl`, `large_prefix_scan.tl`, `dedup_large.tl`,
  `svo_3.tl`), and general helpers in `utils/` (`math.tl`). Test-only scripts under `tests/` use `scope = "test_only"`.
- A script's resource id is its module name: path under `scripts/teal/` without extension, dots for slashes. Keep them in
  sync so `require` resolves the same module at runtime and in the editor.
- Reflection generates `render_graph_utils.render_graph` (declarations) and `render_graph_utils.render_graph_tasks`
  (constructor installer) into `build/<target>/teal/`; their manifest entries give `source_file` relative to that directory.
- `WBEACPCompilerScript` copies `.lua` sources and type-checks and compiles `.tl` sources with the vendored compiler; a type
  error fails the build. A Teal module requires a Lua module only through a `.d.tl` declaration next to it.
- Import engine types as types only (`local type wbe = require("wbe")`); declare data C++ hands a recipe as
  `WBE_REFLECT_SCRIPT` structs rather than Teal records.
- `require` is the only import mechanism; modules load once per backend and cyclic requires fail. New import sources are
  `IScriptModuleResolver`s whose `try_resolve()` must not throw.

`render_graph_utils.sort` provides `radix_sort_buffers(names, max_count, graph?)` and
`add_radix_sort_task(graph, id, buffers, queue_family?)`; `utils.math` provides `ceil_div` and `clamp`.

## Tests

- `tests/unit/function/rend_graph_recipe_builder_vk_test.cpp`: imports, tasks, subgraphs, triggers, toggles.
- `rend_graph_script_vk_test.cpp`: runs `tests.teal_recipe`, `tests.teal_graph_api`, and the shipped recipes with their
  features.
- `render_task_registry_vk_test.cpp`, `render_task_registry_vk_builtin_test.cpp`: registration, params, push constants.
- `rt_triggered_vk_test.cpp`, `rt_toggle_vk_test.cpp`, `rt_graphics_draw_mesh_vk_test.cpp`, `teal_declarations_test.cpp`.
- `tests/build_script/reflection/test_teal_render_tasks.py`: constructor and params-interface generation.
