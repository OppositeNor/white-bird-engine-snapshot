# Copyright 2025 OppositeNor
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Jinja filters that describe reflected C++ types as Teal declarations in `wbe`, and concrete render-task authoring
interfaces and constructors in the shared `render_graph` module."""
import re
from typing import Any, Callable
from build_script.reflection.metadata_types import WBEClassMetadata, WBEFieldMetadata, WBEMetadata


def _strip(p_type: str) -> str:
    """Remove namespaces, cv-qualifiers, and references the declarations do not need."""
    result = p_type.strip()
    result = re.sub(r"\bconst\b", "", result)
    result = result.replace("&", "").strip()
    for prefix in ("::WhiteBirdEngine::", "WhiteBirdEngine::", "WBE::"):
        result = result.replace(prefix, "")
    return result.strip()


_STRING_TYPES = {"std::string", "std::string_view", "const char *", "const char*"}
_BOOLEAN_TYPES = {"bool"}
_INTEGER_TYPES = {
    "int", "unsigned", "unsigned int", "short", "unsigned short", "long", "unsigned long", "long long", "unsigned long long",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "size_t", "char", "unsigned char",
    "HashCode", "MemID",
}
_NUMBER_TYPES = {"float", "double"}
_GLM_TYPES = {"glm::vec2": "Vec2", "glm::vec3": "Vec3", "glm::vec4": "Vec4", "glm::quat": "Quat"}
_TEMPLATE_PATTERN = re.compile(r"^(?P<name>[\w:]+)\s*<(?P<argument>.*)>$")
_IDENTIFIER_PATTERN = re.compile(r"^[A-Za-z_]\w*$")
# Lua keywords, and the words Teal reads as declarations inside a record body.
_RESERVED_FIELD_NAMES = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local", "nil", "not",
    "or", "repeat", "return", "then", "true", "until", "while", "global", "record", "interface", "enum", "type", "where",
    "is", "as", "userdata", "metamethod",
}


def teal_name(p_name: str) -> str:
    """The Teal name of a reflected class or enum inside the `wbe` record, e.g. `RendImageParamsVK`."""
    return _strip(p_name).replace("::", "_")


def teal_field_name(p_name: str) -> str:
    """A record field name, quoted when Teal would read it as a keyword."""
    if p_name in _RESERVED_FIELD_NAMES or not _IDENTIFIER_PATTERN.match(p_name):
        return f'["{p_name}"]'
    return p_name


def teal_type(p_type: str, p_scope: dict[str, Any], p_qualifier: str = "") -> str:
    """The Teal type of a reflected C++ field type.

    Reflected enums accept their names and aliases, or their integer values, as deserialization does.

    Args:
        p_type: The C++ type as reflection spells it.
        p_scope: The names the declarations can refer to: `classes` and `enums`, the script types they declare, and
            `reflected`, every reflected class and enum, whether visible to scripts or not.
        p_qualifier: The prefix of the declared names, such as `wbe.` outside the `wbe` record.

    Raises:
        ValueError: If the type is a reflected class or enum that is not visible to scripts.
    """
    stripped = _strip(p_type)
    match = _TEMPLATE_PATTERN.match(stripped)
    if match is not None:
        name = match.group("name")
        argument = match.group("argument")
        if name in ("std::vector", "Vector"):
            return f"{{{teal_type(argument.split(',')[0], p_scope, p_qualifier)}}}"
        if name == "std::optional":
            return teal_type(argument, p_scope, p_qualifier)
        if name == "Buffer":
            return "string"
    if stripped in _STRING_TYPES:
        return "string"
    if stripped in _BOOLEAN_TYPES:
        return "boolean"
    if stripped in _INTEGER_TYPES:
        return "integer"
    if stripped in _NUMBER_TYPES:
        return "number"
    if stripped in _GLM_TYPES:
        return p_qualifier + _GLM_TYPES[stripped]
    if stripped == "glm::mat4":
        return "{number}"
    if stripped == "DataValue":
        return "any"
    if stripped in p_scope["enums"]:
        return f"{p_qualifier}{teal_name(stripped)} | integer"
    if stripped in p_scope["classes"]:
        return p_qualifier + teal_name(stripped)
    if stripped in p_scope["reflected"]:
        raise ValueError(f"The reflected type '{stripped}' reaches scripts but is not WBE_REFLECT_SCRIPT; mark it so it is declared.")
    return "any"


def teal_parent(p_parent: str, p_scope: dict[str, Any]) -> str:
    """The Teal name of the interface a script class derives from, or an empty string for a base that is not reflected,
    which scripts never see.

    Raises:
        ValueError: If the base is a reflected class that is not visible to scripts.
    """
    stripped = _strip(p_parent)
    if stripped in p_scope["classes"]:
        return teal_name(stripped)
    if stripped in p_scope["reflected"]:
        raise ValueError(f"The script class base '{stripped}' is not WBE_REFLECT_SCRIPT; mark it so it is declared.")
    return ""


_RECIPE_RESOURCE = "WBE_RECIPE_RESOURCE"
_RECIPE_RESOURCE_SCOPES = ("graph", "task_local")
_RENDER_OBJECT = "WBE_RENDER_OBJECT"
# The shared render_graph module's Graph members. Teal lets them silently override a generated constructor of the
# same name, so generation rejects one.
_GRAPH_MEMBERS = {"tasks", "add", "get", "import_task", "chain", "build",
                  "add_mesh", "add_compute_pipeline", "add_graphics_pipeline", "add_buffer", "add_storage_buffer",
                  "add_image", "add_storage_image", "add_image_view", "add_clamp_to_edge_sampler", "import_resource",
                  "port", "reference", "indirect_dispatch", "as_subgraph"}


def _template_argument(p_type: str) -> tuple[str, str]:
    """The name and first argument of a template type, or two empty strings for another type."""
    match = _TEMPLATE_PATTERN.match(p_type)
    if match is None:
        return "", ""
    return match.group("name"), match.group("argument").split(",")[0]


class _RenderTaskDescriber:
    """Describes render-task constructors and the authoring interfaces of the params whose fields hold resource ids."""

    def __init__(self, p_metadata: WBEMetadata, p_reflect_script: list[str]) -> None:
        script = set(p_reflect_script)
        self._classes = {cpp_class.name: cpp_class for cpp_class in p_metadata.classes}
        self._scope: dict[str, Any] = {
            "classes": [cpp_class.name for cpp_class in p_metadata.classes if script & set(cpp_class.attribute)],
            "enums": [cpp_enum.name for cpp_enum in p_metadata.enums if script & set(cpp_enum.attribute)],
            "reflected": [cpp_class.name for cpp_class in p_metadata.classes] + [cpp_enum.name for cpp_enum in p_metadata.enums],
        }
        self._resource_fields: dict[str, list[dict[str, Any]]] = {}
        self._visiting: set[str] = set()
        self.interfaces: dict[str, dict[str, Any]] = {}

    def constructor(self, p_task: WBEClassMetadata, p_method: str) -> dict[str, Any]:
        """Describe the constructor of a render task."""
        creates = [method for method in p_task.methods if method.method_name == "create" and method.is_static]
        if len(creates) != 1 or len(creates[0].type) != 3:
            raise ValueError(f"The render task {p_task.name} needs one reflected static create(render data, params, graph context).")
        params = _strip(creates[0].type[1])
        if params not in self._scope["classes"]:
            raise ValueError(f"The params '{params}' of the render task {p_task.name} are not WBE_REFLECT_SCRIPT.")
        fields = self._resource_fields_of(params)
        return {
            "method": p_method,
            # Recipes name a task type by the first serialization alias of its class.
            "type": p_task.aliases[0] if p_task.aliases else p_task.name,
            "params_type": self._interface(params) if fields else "wbe." + teal_name(params),
            "fields": fields,
        }

    def _fields(self, p_class: WBEClassMetadata) -> list[WBEFieldMetadata]:
        """The reflected fields of a class, the inherited ones first."""
        fields: list[WBEFieldMetadata] = []
        for parent in p_class.extended_parents:
            parent_class = self._classes.get(_strip(str(parent)))
            if parent_class is not None:
                fields.extend(self._fields(parent_class))
        return fields + p_class.fields

    def _resource_scope(self, p_field: WBEFieldMetadata) -> tuple[str, str] | None:
        """The scope and optional render-object type of a resource field, or None for an ordinary field."""
        if _RECIPE_RESOURCE not in p_field.attribute:
            return None
        parameters = p_field.attribute_parameters.get(_RECIPE_RESOURCE, [])
        if len(parameters) == 1 and parameters[0] in _RECIPE_RESOURCE_SCOPES:
            return parameters[0], ""
        if len(parameters) == 2 and parameters[0] == "database":
            target = _strip(parameters[1])
            render_object = next((cpp_class for cpp_class in self._classes.values()
                                  if _strip(cpp_class.name) == target), None)
            if render_object is None or _RENDER_OBJECT not in render_object.attribute:
                raise ValueError(f"The database resource field '{p_field.name}' names '{parameters[1]}', which is not a reflected WBE_RENDER_OBJECT class.")
            return "database", target
        raise ValueError(f"The field '{p_field.name}' must be {_RECIPE_RESOURCE}(graph), {_RECIPE_RESOURCE}(task_local), or {_RECIPE_RESOURCE}(database, RenderObjectType).")

    def _value(self, p_type: str, p_scope: tuple[str, str] | None, p_field: str) -> tuple[dict[str, Any] | None, str]:
        """Describe the resource ids a field value holds, with its authoring Teal type; None and its declared type for a
        value without resource ids."""
        stripped = _strip(p_type)
        name, argument = _template_argument(stripped)
        if name == "std::optional":
            return self._value(argument, p_scope, p_field)
        if name in ("std::vector", "Vector"):
            element, element_type = self._value(argument, p_scope, p_field)
            if element is None:
                return None, teal_type(p_type, self._scope, "wbe.")
            return {"kind": "list", "element": element}, f"{{{element_type}}}"
        if p_scope is not None:
            scope, target = p_scope
            resource = {"kind": "resource", "scope": scope}
            resource_type = "Resource | string"
            if scope == "database":
                resource["expected_type"] = "WhiteBirdEngine::" + target
                resource_type = "Resource"
            if stripped == "std::string":
                return resource, resource_type
            if stripped == "DataValue":
                return {"kind": "map", "element": resource}, f"{{string:{resource_type}}}"
            raise ValueError(f"The {_RECIPE_RESOURCE} field '{p_field}' must hold std::string ids or a DataValue map of them, not '{p_type}'.")
        if stripped in self._classes and self._resource_fields_of(stripped):
            return {"kind": "record", "fields": self._resource_fields_of(stripped)}, self._interface(stripped)
        return None, teal_type(p_type, self._scope, "wbe.")

    def _resource_fields_of(self, p_class: str) -> list[dict[str, Any]]:
        """Describe the fields of a params class that hold resource ids, in declaration order and once per name."""
        if p_class not in self._resource_fields:
            if p_class in self._visiting:
                raise ValueError(f"The params '{p_class}' contain themselves.")
            self._visiting.add(p_class)
            fields: list[dict[str, Any]] = []
            for field in self._fields(self._classes[p_class]):
                description, _ = self._value(field.type, self._resource_scope(field), field.name)
                if description is not None:
                    fields.extend({"name": name, **description} for name in [field.name] + field.aliases)
            self._visiting.remove(p_class)
            self._resource_fields[p_class] = fields
        return self._resource_fields[p_class]

    def _interface(self, p_class: str) -> str:
        """Declare the authoring interface of params with resource ids, and return its name."""
        if p_class not in self._scope["classes"]:
            raise ValueError(f"The params '{p_class}' hold resource ids but are not WBE_REFLECT_SCRIPT.")
        if p_class not in self.interfaces:
            fields: list[dict[str, str]] = []
            for field in self._fields(self._classes[p_class]):
                _, field_type = self._value(field.type, self._resource_scope(field), field.name)
                fields.extend({"name": teal_field_name(name), "type": field_type} for name in [field.name] + field.aliases)
            self.interfaces[p_class] = {"name": teal_name(p_class), "fields": fields}
        return teal_name(p_class)


def teal_render_tasks(p_metadata: WBEMetadata, p_render_tasks: list[str], p_reflect_script: list[str]) -> dict[str, Any]:
    """Describe the Graph constructors of render_graph, one for each render task annotated WBE_RENDER_TASK(<method>).

    A bare WBE_RENDER_TASK registers its task type without a constructor. Params fields annotated
    WBE_RECIPE_RESOURCE(graph) hold graph resource ids, and those annotated WBE_RECIPE_RESOURCE(task_local) hold ids the
    task's aliases map. WBE_RECIPE_RESOURCE(database, T) requires a resource handle for a reflected render object T,
    whose canonical type is recorded for runtime validation.
    These are std::string fields, optional or in a vector, or a DataValue map of them. Params that hold such fields,
    directly or in nested params, get an authoring interface, which also accepts resource handles in their place.

    Args:
        p_metadata: The reflection metadata.
        p_render_tasks: The attributes that make a class a render task.
        p_reflect_script: The attributes that make a type visible to scripts.

    Returns:
        `constructors`, each with its `method` name, the task `type` it creates, its `params_type`, and the `fields` that
        hold resource ids; and `interfaces`, the authoring interfaces of params, each with its `name` and `fields`.

    Raises:
        ValueError: If a constructor is not named by one identifier that is not a Teal keyword, shadows a Graph member, or
            is declared twice, or if the create() or params of its task do not fit a constructor.
    """
    describer = _RenderTaskDescriber(p_metadata, p_reflect_script)
    constructors: dict[str, dict[str, Any]] = {}
    for task in p_metadata.classes:
        parameters = next((task.attribute_parameters[attribute] for attribute in p_render_tasks
                           if attribute in task.attribute_parameters), None)
        if parameters is None:
            continue
        if len(parameters) != 1 or teal_field_name(parameters[0]) != parameters[0]:
            raise ValueError(f"The render task {task.name} must name its constructor with one identifier, as in WBE_RENDER_TASK(compute).")
        method = parameters[0]
        if method in _GRAPH_MEMBERS:
            raise ValueError(f"The constructor '{method}' of {task.name} shadows a member of the Graph of render_graph.")
        if method in constructors:
            raise ValueError(f"The constructor '{method}' is declared by more than one render task.")
        constructors[method] = describer.constructor(task, method)
    return {
        "constructors": [constructors[method] for method in sorted(constructors)],
        "interfaces": [describer.interfaces[name] for name in sorted(describer.interfaces)],
    }


TEAL_FILTERS: dict[str, Callable[..., Any]] = {
    "teal_type": teal_type,
    "teal_name": teal_name,
    "teal_field_name": teal_field_name,
    "teal_parent": teal_parent,
    "teal_render_tasks": teal_render_tasks,
}
