---
name: white-bird-cpp-reflection
description: Use and extend White Bird Engine's build-time C++ reflection and code generation. Apply when annotating C++ types, fields, methods, or enums; adding DataValue serialization, strict deserialization, generated field access, factories, aliases, required fields, labels, type UUIDs, reflected enums, render-task registration, WBE_REFLECT_SCRIPT types and their generated Teal declarations, or new generated consumers. Do not use for Slang shader reflection.
---

# White Bird C++ Reflection

Reflection is annotation-driven code generation, not runtime language reflection. Before CMake runs, `build.py` parses
`WBE_META(...)` annotations in project `.hh` headers with Clang and renders Jinja templates into target-specific generated
files. In normal compilation `WBE_META(...)` expands to nothing.

All serialization goes through `DataValue` (`core/data/data_value.hh`), a format-neutral tree of nil, bool, int64, double,
string, list, and key-sorted map. JSON, TOML, and script backends only convert to and from it.

## Choose the annotation

| Need | Annotation |
| --- | --- |
| Static `SerializableSD<T>` / `SerializableAccess<T>` only | `WBE_META(WBE_SERIALIZABLE_STATIC)` (or `WBE_COMPONENT` for ECS components) |
| The above plus virtual `serialize(DataValue&)` / `deserialize(const DataValue&)` and name-based `SerializableFactory` construction | `WBE_META(WBE_SERIALIZABLE)`; derive from `Serializable` and add `WBE_DECL_SERIALIZABLE(T)`. `WBE_CONFIG_OPTION` joins this category. |
| Reject unknown keys | add `WBE_STRICT_DESERIALIZATION` to a static-serializable class |
| Name-based enum conversion | `WBE_META(WBE_REFLECT_ENUM)` on the enum; `WBE_META(WBE_REFLECT_ENUM_IGNORE)` on enumerators to skip |
| Visible to Lua/Teal scripts | add `WBE_REFLECT_SCRIPT` (class or enum); it also makes the type static-serializable or a reflected enum |
| A render-graph task type usable by recipes | `WBE_RENDER_TASK(<method>)` with `WBE_SERIALIZATION_ALIASES(recipe_type_name)` (see below) |
| Command-line configuration | `WBE_CLA_CONFIGURATION` |
| `TypeUUID<T>` only | any `WBE_META(...)` on a class |

Category membership is configured in `gen_info` in `build_config.py`, not in the metaparser. Preserve established patterns
instead of stacking a second annotation that already applies.

## Declare a serializable type

```cpp
#include "core/reflection/reflection_defs.hh"
#include "core/reflection/serializable.hh"

namespace WhiteBirdEngine {

struct WBE_META(WBE_SERIALIZABLE, WBE_SERIALIZATION_ALIASES(LegacySettings)) Settings final : public Serializable {
    WBE_DECL_SERIALIZABLE(Settings)

    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string name;

    WBE_META(WBE_REFLECT, WBE_SERIALIZATION_ALIASES(old_quality))
    int32_t quality = 0;
};

} // namespace WhiteBirdEngine
```

Only `WBE_REFLECT` fields enter generated code. `WBE_REFLECT_REQUIRED` makes deserialization throw when neither the canonical
key nor an alias is present; optional fields keep their current value, except `std::optional<T>`, which is reset. Declare
aliases with `WBE_SERIALIZATION_ALIASES(NameA, NameB)`; an attribute is a bare name or `NAME(<parameters>)`, never an
assignment. Enumerators take aliases too: `VALUE WBE_META(WBE_SERIALIZATION_ALIASES(value))`.

Script-facing parameter structs, such as render-task params, combine the categories:

```cpp
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RTParamsFooVK {
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string pipeline;
};
```

## Use the generated APIs

Include the generated header the caller needs (`generated/serializables_sd.gen.hh`, `serializables_access.gen.hh`,
`serializable_factory.gen.hh`, `enum_reflection.gen.hh`, `type_uuid.gen.hh`):

```cpp
Settings settings;
SerializableSD<Settings>::deserialize(ParserJSON::read(text), settings);

DataValue output;
SerializableSD<Settings>::serialize(output, settings);
std::string json = ParserJSON::write(output);
```

`Parser::get_data()` also returns a `DataValue`. `SerializableFactory` builds `WBE_SERIALIZABLE` types by class name or alias
from a `DataValue`. `SerializableAccess<T>::list_fields()` is static; `get_field<T>()` and `get_pointer_to_member<T>()` are
const members of an accessor instance, and unknown names or inexact types throw. `EnumReflection<E>` maps enumerators to
canonical names and aliases; reflected enums serialize as their canonical name and deserialize from a name, alias, or
in-range declared integer.

## Render tasks

A `WBE_RENDER_TASK` class declares
`WBE_META(WBE_REFLECT) static Unique<IRenderTaskVK> create(const RenderContextVK*, const TParams&, const RendGraphContextVK&)`,
where `TParams` is a strict, script-reflected params struct. The generated `render_task_registry.gen.cpp` implements
`RenderTaskRegistryVK` from these declarations; a missing `create` is a build `#error` and a wrong signature fails a
`static_assert`. An optional static `prepare_graph(...)` hook runs when the task is added. Each alias is a `type`
name recipes can use. `WBE_RENDER_TASK(<method>)` also generates the recipe constructor `graph:<method>(id, params,
options?)`, which uses the first alias; mark params fields that hold resource ids `WBE_RECIPE_RESOURCE(graph)` or
`WBE_RECIPE_RESOURCE(task_local)` so that it accepts resource handles there. See
`include/function/render_vk/render_task_registry_vk.hh` and `docs/renderer_vk/render_graph.md`.

## Script types and Teal declarations

`templates/teal_types.d.tl.jinja` renders every `WBE_REFLECT_SCRIPT` class and enum into `build/<target>/teal/wbe.d.tl`,
and `tlconfig.lua` at the repo root points the Teal language server at it. Every script class is an `interface` that `is`
its script bases; a script field or base naming a reflected type that is not `WBE_REFLECT_SCRIPT` fails generation.
Test-executable types appear only in targets that build tests, because only those scan `tests/`. Reflection runs before the
ACP, which type-checks Teal against these declarations.

## Regenerate and validate

Never edit `*.gen.*`, `wbe.d.tl`, or `tlconfig.lua`. Rebuild through `python build.py -t debug`; do not invoke CMake.

```sh
python validation.py -t debug -- --gtest_filter='WBEDeserializer*:WBESerializerTest*:WBESerializableFactoryTest*:WBEEnumReflectionTest*:WBEStrictDeserializationTest*:WBEDataValueTest*'
```

For render-task, params, or Teal changes, add
`:WBERenderTaskRegistryVK*:WBERendResourceParamsVKTest*:WBETealDeclarationsTest*:WBERendGraphScriptVK*`; for `DataValue` or
parser changes, add `:ParserJSONTest*:ParserTOMLTest*`. The wrapper runs the full suite afterward. Do not run lint.

## Advanced work

Read [references/system-and-extension.md](references/system-and-extension.md) before changing the metaparser, metadata
cache, annotation categories, supported field types, inheritance, strict deserialization, enum reflection, Teal generation,
templates, `generate.json`, labels, type UUIDs, factories, or the render-task registry.
