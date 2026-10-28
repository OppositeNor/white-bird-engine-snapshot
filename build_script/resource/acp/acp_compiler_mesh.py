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

from pathlib import Path
import re
import struct
from typing import Any, override
from build_script.resource.acp.acp_compiler import ManifestResource, WBEACPCompiler

from wbe_utils_model_compiler import MeshSDFBuilder, SDFCompiler, SDFConfig


class WBEACPCompilerMesh(WBEACPCompiler):
    """Compile hand-authored mesh source geometry into a runtime mesh and its binary sections.

    Source submeshes have no instance data, so they are emitted as opaque submeshes drawn with one identity instance. A mesh
    whose sdf_config is enabled also emits its signed distance field, which requires every submesh to reference only
    binaries compiled with the mesh.
    """

    _SOURCE_ONLY_FIELDS = {
        "vertices_data", "indices_data", "geometry_output_dir", "_geometry_output_name", "source_type", "source_file", "sdf_config",
    }
    _SECTION_TYPES = {
        "position": "vec3",
        "normal": "vec3",
        "tangent": "vec3",
        "bitangent": "vec3",
        "uv": "vec2",
        "index": "uint32",
    }
    _OPTIONAL_VERTEX_SLOTS = ["normal", "tangent", "bitangent", "uv"]
    _MESH_CATEGORIES = ("opaque_instances", "masked_instances", "double_sided_opaque_instances")
    _IDENTITY_TRANSFORM = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]
    _RUNTIME_VERTEX_TYPES = {
        "float", "vec2", "vec3", "vec4",
        "int", "ivec2", "ivec3", "ivec4",
        "uint", "uvec2", "uvec3", "uvec4",
    }
    _JOINT_BINDING_TYPES = {"joint_binding_ids": "ivec4", "joint_binding_weights": "vec4"}
    _LEGACY_VERTEX_FIELDS = ("vertices", "joint_bindings")

    def __init__(self, p_sdf_compiler: SDFCompiler) -> None:
        self._mesh_sdf_builder = MeshSDFBuilder(p_sdf_compiler)

    @override
    def get_supported_resource_types(self) -> list[str]:
        return ["mesh"]

    @override
    def compile(self, resource: ManifestResource, manifest_path: Path,
            res_dir: Path, res_output_dir: Path) -> list[ManifestResource]:
        resource_type = self._get_source_resource_type(resource)
        if resource_type == "mesh":
            return self._compile_mesh(resource, manifest_path, res_output_dir)
        raise RuntimeError(
            f"WBEACPCompilerMesh does not support resource type '{resource_type}' in {manifest_path}."
        )

    def _compile_mesh(self, resource: ManifestResource, manifest_path: Path, res_output_dir: Path) -> list[ManifestResource]:
        if "vertices_data" in resource or "indices_data" in resource:
            raise RuntimeError(
                f"WBEACPCompilerMesh: mesh resources must declare geometry through submeshes in {manifest_path}."
            )

        sdf_config = SDFConfig.parse(resource)
        result = self._copy_runtime_fields(resource)
        result.setdefault("type", "mesh")
        geometry_output_dir = resource.get("geometry_output_dir")
        submeshes = result.pop("submeshes", None)
        if submeshes is None:
            raise RuntimeError(
                f"WBEACPCompilerMesh: mesh resources must declare submeshes in {manifest_path}."
            )
        if not isinstance(submeshes, list):
            raise RuntimeError(
                f"WBEACPCompilerMesh: mesh submeshes must be an array in {manifest_path}."
            )

        resource_id = resource.get("id")
        result_submeshes: list[Any] = []
        sibling_resources: list[ManifestResource] = []
        for submesh_index, submesh in enumerate(submeshes):
            if not isinstance(submesh, dict):
                raise RuntimeError(
                    f"WBEACPCompilerMesh: mesh submeshes must be objects in {manifest_path}."
                )
            result_submesh = dict(submesh)
            if geometry_output_dir is not None and "geometry_output_dir" not in result_submesh:
                result_submesh["geometry_output_dir"] = geometry_output_dir
            output_name = self._nested_submesh_output_name(resource_id, result_submesh, submesh_index, manifest_path)
            result_submesh.setdefault("_geometry_output_name", output_name)
            if result_submesh.get("source_type") == "submesh" or "vertices_data" in result_submesh or "indices_data" in result_submesh:
                compiled_submesh_entries = self._compile_submesh(result_submesh, manifest_path, res_output_dir)
                if len(compiled_submesh_entries) != 2:
                    raise RuntimeError(
                        f"WBEACPCompilerMesh: nested source submesh must compile to one submesh and one binary in {manifest_path}."
                    )
                result_submeshes.append(self._to_mesh_submesh(compiled_submesh_entries[0]))
                sibling_resources.append(compiled_submesh_entries[1])
            else:
                result_submeshes.append(self._to_mesh_submesh(self._compile_submesh(result_submesh, manifest_path, res_output_dir)[0]))
        for category in self._MESH_CATEGORIES:
            result[category] = {"submeshes": [], "instances": []}
        if result_submeshes:
            result["opaque_instances"] = {
                "submeshes": result_submeshes,
                "instances": [{"global_transform": list(self._IDENTITY_TRANSFORM)}],
            }
        if sdf_config is not None:
            # The SDF samples sit beside the mesh's geometry binaries.
            sdf_output_dir = self._geometry_output_path(resource, manifest_path).parent
            sibling_resources.extend(
                self._mesh_sdf_builder.build(result, sibling_resources, sdf_config, res_output_dir, sdf_output_dir))
        return [result, *sibling_resources]

    def _to_mesh_submesh(self, submesh: ManifestResource) -> ManifestResource:
        result = {key: value for key, value in submesh.items() if key not in ("id", "type")}
        result["first_instance"] = 0
        result["instance_count"] = 1
        return result

    def _compile_submesh(self, resource: ManifestResource, manifest_path: Path, res_output_dir: Path) -> list[ManifestResource]:
        for legacy_field in self._LEGACY_VERTEX_FIELDS:
            if legacy_field in resource:
                raise RuntimeError(
                    f"WBEACPCompilerMesh: submesh field '{legacy_field}' is no longer supported; declare vertex views in vertex_bindings in {manifest_path}."
                )
        has_inline_vertices = "vertices_data" in resource
        has_inline_indices = "indices_data" in resource
        has_runtime_geometry = "vertex_bindings" in resource or "indices" in resource
        if has_runtime_geometry and (has_inline_vertices or has_inline_indices):
            raise RuntimeError(
                f"WBEACPCompilerMesh: submesh cannot mix inline geometry and runtime views in {manifest_path}."
            )
        if has_runtime_geometry:
            self._validate_runtime_submesh(resource, manifest_path)
            result = self._copy_runtime_fields(resource)
            result.setdefault("type", "submesh")
            return [result]
        if has_inline_vertices != has_inline_indices:
            raise RuntimeError(
                f"WBEACPCompilerMesh: inline submesh requires both vertices_data and indices_data in {manifest_path}."
            )
        if not has_inline_vertices:
            raise RuntimeError(
                f"WBEACPCompilerMesh: submesh requires vertex_bindings/indices views or vertices_data/indices_data in {manifest_path}."
            )

        vertices = resource["vertices_data"]
        indices = resource["indices_data"]
        if not isinstance(vertices, list) or not isinstance(indices, list):
            raise RuntimeError(
                f"WBEACPCompilerMesh: vertices_data and indices_data must be arrays in {manifest_path}."
            )

        output_rel_path = self._geometry_output_path(resource, manifest_path)
        output_path = Path.joinpath(res_output_dir, output_rel_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        payload, vertices_view, indices_view = self._build_geometry_payload(vertices, indices, manifest_path)
        output_path.write_bytes(payload)
        binary_id = self._geometry_resource_id(resource, manifest_path)
        vertices_view["binary"]["binary_id"] = binary_id
        indices_view["binary"]["binary_id"] = binary_id

        result = self._copy_runtime_fields(resource)
        result.setdefault("type", "submesh")
        result["aabb"] = self._compute_aabb(vertices, manifest_path)
        result["vertex_bindings"] = [vertices_view]
        result["indices"] = indices_view
        binary_resource: ManifestResource = {"id": binary_id, "type": "binary", "path": output_rel_path.as_posix()}
        return [result, binary_resource]

    def _copy_runtime_fields(self, resource: ManifestResource) -> ManifestResource:
        return {key: value for key, value in resource.items() if key not in self._SOURCE_ONLY_FIELDS}

    def _nested_submesh_output_name(self, resource_id: Any, submesh: ManifestResource, submesh_index: int, manifest_path: Path) -> str:
        submesh_id = submesh.get("id")
        if isinstance(submesh_id, str) and submesh_id:
            return submesh_id
        if isinstance(resource_id, str) and resource_id:
            return f"{resource_id}.submesh.{submesh_index}"
        return f"{manifest_path.stem}.submesh.{submesh_index}"

    def _geometry_output_path(self, resource: ManifestResource, manifest_path: Path) -> Path:
        raw_output_dir = resource.get("geometry_output_dir", manifest_path.parent.as_posix())
        if not isinstance(raw_output_dir, str):
            raise RuntimeError(
                f"WBEACPCompilerMesh: geometry_output_dir must be a string in {manifest_path}."
            )
        output_dir = Path(raw_output_dir)
        if output_dir.is_absolute():
            raise RuntimeError(
                f"WBEACPCompilerMesh: geometry_output_dir must be resource-root-relative in {manifest_path}: {raw_output_dir}."
            )
        output_dir = Path(output_dir.as_posix())
        output_name = resource.get("_geometry_output_name", resource.get("id", manifest_path.stem))
        if not isinstance(output_name, str) or not output_name:
            output_name = manifest_path.stem
        return output_dir / f"{self._sanitize_file_stem(output_name)}.geometry.bin"

    def _geometry_resource_id(self, resource: ManifestResource, manifest_path: Path) -> str:
        output_name = resource.get("_geometry_output_name", resource.get("id", manifest_path.stem))
        if not isinstance(output_name, str) or not output_name:
            output_name = manifest_path.stem
        return f"{self._sanitize_file_stem(output_name)}.geometry"

    def _sanitize_file_stem(self, value: str) -> str:
        sanitized = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
        return sanitized if sanitized else "submesh"

    def _build_geometry_payload(self, vertices: list[Any], indices: list[Any], manifest_path: Path) -> tuple[bytes, dict[str, Any], dict[str, Any]]:
        payload = bytearray()
        attributes: list[dict[str, Any]] = []
        vertex_slots = ["position"]
        for slot in self._OPTIONAL_VERTEX_SLOTS:
            if self._has_vertex_slot(vertices, slot):
                vertex_slots.append(slot)
        stride = 0
        for slot in vertex_slots:
            attributes.append({"role": slot, "type": self._SECTION_TYPES[slot], "offset": stride})
            stride += 8 if slot == "uv" else 12
        vertex_start = len(payload)
        for vertex in vertices:
            if not isinstance(vertex, dict):
                raise RuntimeError(
                    f"WBEACPCompilerMesh: vertices_data entries must be objects in {manifest_path}."
                )
            for slot in vertex_slots:
                components = 2 if slot == "uv" else 3
                values = self._read_vertex_vector(vertex, slot, components, manifest_path, slot == "position")
                payload.extend(struct.pack("<" + "f" * components, *values))
        vertex_size = len(payload) - vertex_start
        index_start = len(payload)
        self._append_indices(payload, indices, len(vertices), manifest_path)
        vertices_view = {"binary": {"binary_id": "", "start": vertex_start, "size": vertex_size}, "stride": stride, "attributes": attributes}
        indices_view = {"binary": {"binary_id": "", "start": index_start, "size": len(payload) - index_start}}
        return bytes(payload), vertices_view, indices_view

    def _compute_aabb(self, vertices: list[Any], manifest_path: Path) -> dict[str, Any]:
        if not vertices:
            raise RuntimeError(
                f"WBEACPCompilerMesh: cannot compute bounds for a submesh without vertices in {manifest_path}."
            )
        positions = [self._read_vertex_vector(vertex, "position", 3, manifest_path, True) for vertex in vertices]
        minimum = [min(position[axis] for position in positions) for axis in range(3)]
        maximum = [max(position[axis] for position in positions) for axis in range(3)]
        return {
            "center": [(minimum[axis] + maximum[axis]) * 0.5 for axis in range(3)],
            "width": maximum[0] - minimum[0],
            "height": maximum[1] - minimum[1],
            "depth": maximum[2] - minimum[2],
        }

    def _append_indices(self, payload: bytearray, indices: list[Any], vertex_count: int, manifest_path: Path) -> None:
        if len(indices) % 3 != 0:
            raise RuntimeError(
                f"WBEACPCompilerMesh: indices_data size {len(indices)} is not a multiple of 3 in {manifest_path}."
            )
        for index in indices:
            if not isinstance(index, int) or isinstance(index, bool):
                raise RuntimeError(
                    f"WBEACPCompilerMesh: indices_data entries must be integers in {manifest_path}."
                )
            if index < 0 or index >= vertex_count:
                raise RuntimeError(
                    f"WBEACPCompilerMesh: index {index} is out of range for {vertex_count} vertices in {manifest_path}."
                )
            payload.extend(struct.pack("<I", index))

    def _has_vertex_slot(self, vertices: list[Any], slot: str) -> bool:
        return any(isinstance(vertex, dict) and vertex.get(slot) is not None for vertex in vertices)

    def _read_vertex_vector(self, vertex: ManifestResource, slot: str, component_count: int,
            manifest_path: Path, is_required: bool) -> tuple[float, ...]:
        raw_value = vertex.get(slot)
        if raw_value is None:
            if is_required:
                raise RuntimeError(
                    f"WBEACPCompilerMesh: vertex is missing required '{slot}' in {manifest_path}."
                )
            return tuple(0.0 for _ in range(component_count))
        if not isinstance(raw_value, dict):
            raise RuntimeError(
                f"WBEACPCompilerMesh: vertex '{slot}' must be an object in {manifest_path}."
            )
        component_names = ("u", "v") if slot == "uv" else ("x", "y", "z")
        fallback_component_names = ("x", "y") if slot == "uv" else component_names
        values: list[float] = []
        for component_name, fallback_component_name in zip(component_names, fallback_component_names):
            component = raw_value.get(component_name, raw_value.get(fallback_component_name))
            if not isinstance(component, int | float) or isinstance(component, bool):
                raise RuntimeError(
                    f"WBEACPCompilerMesh: vertex '{slot}.{component_name}' must be numeric in {manifest_path}."
                )
            values.append(float(component))
        return tuple(values)

    def _validate_runtime_submesh(self, resource: ManifestResource, manifest_path: Path) -> None:
        vertex_bindings = resource.get("vertex_bindings")
        if not isinstance(vertex_bindings, list):
            raise RuntimeError(
                f"WBEACPCompilerMesh: runtime submesh vertex_bindings must be an array in {manifest_path}."
            )
        if not vertex_bindings:
            raise RuntimeError(
                f"WBEACPCompilerMesh: runtime submesh vertex_bindings must not be empty in {manifest_path}."
            )
        indices = resource.get("indices")
        if not isinstance(indices, dict):
            raise RuntimeError(
                f"WBEACPCompilerMesh: runtime submesh requires an indices object in {manifest_path}."
            )
        seen_roles: set[str] = set()
        vertex_count: int | None = None
        for vertex_binding in vertex_bindings:
            if not isinstance(vertex_binding, dict):
                raise RuntimeError(
                    f"WBEACPCompilerMesh: runtime submesh vertex_bindings entries must be objects in {manifest_path}."
                )
            self._validate_runtime_vertex_view(vertex_binding, seen_roles, manifest_path)
            binding_vertex_count = self._runtime_vertex_count(vertex_binding, manifest_path)
            if vertex_count is not None and binding_vertex_count != vertex_count:
                raise RuntimeError(
                    f"WBEACPCompilerMesh: runtime submesh vertex_bindings must all hold the same vertex count in {manifest_path}."
                )
            vertex_count = binding_vertex_count
        self._validate_runtime_index_view(indices, manifest_path)

    def _runtime_vertex_count(self, vertex_binding: ManifestResource, manifest_path: Path) -> int:
        size = vertex_binding["binary"]["size"]
        stride = vertex_binding["stride"]
        if size % stride != 0:
            raise RuntimeError(
                f"WBEACPCompilerMesh: vertex binding size must be a multiple of its stride in {manifest_path}."
            )
        return size // stride

    def _validate_runtime_vertex_view(self, vertices: ManifestResource, seen_roles: set[str], manifest_path: Path) -> None:
        self._validate_binary_view(vertices.get("binary"), manifest_path, "vertices")
        stride = vertices.get("stride")
        if not isinstance(stride, int) or isinstance(stride, bool) or stride <= 0:
            raise RuntimeError(
                f"WBEACPCompilerMesh: vertex stride must be a positive integer in {manifest_path}."
            )
        attributes = vertices.get("attributes")
        if not isinstance(attributes, list) or not attributes:
            raise RuntimeError(
                f"WBEACPCompilerMesh: vertices attributes must be a non-empty array in {manifest_path}."
            )
        for attribute in attributes:
            if not isinstance(attribute, dict):
                raise RuntimeError(
                    f"WBEACPCompilerMesh: vertex attributes must be objects in {manifest_path}."
                )
            role = attribute.get("role")
            if not isinstance(role, str) or not role:
                raise RuntimeError(
                    f"WBEACPCompilerMesh: vertex attribute role must be a non-empty string in {manifest_path}."
                )
            if role in seen_roles:
                raise RuntimeError(
                    f"WBEACPCompilerMesh: duplicate vertex attribute role '{role}' across the submesh vertex_bindings in {manifest_path}."
                )
            seen_roles.add(role)
            attribute_type = attribute.get("type")
            if attribute_type not in self._RUNTIME_VERTEX_TYPES:
                raise RuntimeError(
                    f"WBEACPCompilerMesh: vertex attribute '{role}' has invalid type in {manifest_path}."
                )
            joint_binding_type = self._JOINT_BINDING_TYPES.get(role)
            if joint_binding_type is not None and attribute_type != joint_binding_type:
                raise RuntimeError(
                    f"WBEACPCompilerMesh: vertex attribute '{role}' must have type {joint_binding_type} in {manifest_path}."
                )
            offset = attribute.get("offset")
            if not isinstance(offset, int) or isinstance(offset, bool) or offset < 0:
                raise RuntimeError(
                    f"WBEACPCompilerMesh: vertex attribute '{role}' offset must be a non-negative integer in {manifest_path}."
                )

    def _validate_runtime_index_view(self, indices: ManifestResource, manifest_path: Path) -> None:
        self._validate_binary_view(indices.get("binary"), manifest_path, "indices")

    def _validate_binary_view(self, binary_view: Any, manifest_path: Path, context: str) -> None:
        if not isinstance(binary_view, dict):
            raise RuntimeError(
                f"WBEACPCompilerMesh: {context} binary view must be an object in {manifest_path}."
            )
        binary_id = binary_view.get("binary_id")
        if not isinstance(binary_id, str) or not binary_id:
            raise RuntimeError(
                f"WBEACPCompilerMesh: {context} binary_id must be a non-empty string in {manifest_path}."
            )
        for key in ("start", "size"):
            value = binary_view.get(key)
            if not isinstance(value, int) or isinstance(value, bool) or value < 0:
                raise RuntimeError(
                    f"WBEACPCompilerMesh: {context} binary {key} must be a non-negative integer in {manifest_path}."
                )
