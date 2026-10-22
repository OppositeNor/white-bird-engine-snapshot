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

import json
import os
import tomllib
from pathlib import Path
from typing import Any
from build_script.resource.acp.acp_compiler import ManifestData, ManifestResource, SOURCE_RESOURCE_FILE_KEY, SOURCE_RESOURCE_TYPE_KEY, WBEACPCompiler

SOURCE_MANIFEST_SUFFIX = ".toml"
RUNTIME_MANIFEST_SUFFIX = ".json"

class WBEACP:
    """The asset conditioning pipeline.

    This pipeline consumes TOML manifest files from the source resource
    directory, compiles/copies entries by resource type, and emits JSON runtime
    index files into the output resource directory. A source manifest
    ``<path>.toml`` is emitted as ``<path>.json``.
    """

    def __init__(self, res_dir: Path, res_output_dir: Path, enabled_test: bool = False) -> None:
        self._res_dir = res_dir
        self._res_output_dir = res_output_dir
        self._enabled_test = enabled_test
        self._compiler_map: dict[str, WBEACPCompiler] = {}
        self._root_manifest_paths: list[str] = []
        self._manifest_sources: dict[str, dict[str, Any]] = {}
        self._processed_manifests: dict[str, dict[str, Any]] = {}
        self._seen_resource_ids: set[str] = set()

    def add_compiler(self, compiler: WBEACPCompiler) -> None:
        """Register a compiler for one or more resource types.

        Args:
            compiler: Compiler implementation.
        """
        for resource_type in compiler.get_supported_resource_types():
            if resource_type in self._compiler_map:
                raise RuntimeError(
                    f"WBEACP: resource type '{resource_type}' already has a compiler registered."
                )
            self._compiler_map[resource_type] = compiler

    def process_resources(self) -> None:
        """Process manifests and generate runtime index files."""
        self._manifest_sources.clear()
        self._root_manifest_paths = self._load_root_manifest_paths()
        self._processed_manifests.clear()
        self._seen_resource_ids.clear()
        if not self._root_manifest_paths:
            print(f"WBEACP: No root manifest files found under {Path.joinpath(self._res_dir, Path('res_chunks'))}.")
            return

        for manifest_rel_path in sorted(self._root_manifest_paths):
            self._process_manifest(manifest_rel_path, [])

    def _load_root_manifest_paths(self) -> list[str]:
        manifest_paths: list[str] = []
        root_manifest_dir = Path.joinpath(self._res_dir, Path("res_chunks"))
        for manifest_file in sorted(root_manifest_dir.rglob(f"*{SOURCE_MANIFEST_SUFFIX}")):
            relative_manifest_path = manifest_file.relative_to(self._res_dir).as_posix()
            if not self._load_manifest_source(relative_manifest_path, False):
                continue
            manifest_paths.append(relative_manifest_path)
        return manifest_paths

    def _load_manifest_source(self, manifest_rel_path: str, is_required: bool) -> bool:
        normalized_manifest_path = Path(manifest_rel_path).as_posix()
        if normalized_manifest_path in self._manifest_sources:
            return True

        if Path(normalized_manifest_path).is_absolute():
            raise RuntimeError(
                f"WBEACP: Manifest path must be relative to the resource directory: {manifest_rel_path}."
            )

        manifest_path = Path.joinpath(self._res_dir, Path(normalized_manifest_path))
        if not manifest_path.exists() or not manifest_path.is_file():
            if is_required:
                raise RuntimeError(
                    f"WBEACP: Manifest include target does not exist: {normalized_manifest_path}."
                )
            return False

        with open(manifest_path, "rb") as file:
            try:
                data = tomllib.load(file)
            except tomllib.TOMLDecodeError as error:
                raise RuntimeError(
                    f"WBEACP: Failed to parse TOML manifest {normalized_manifest_path}: {error}."
                ) from error
        if not isinstance(data, dict) or "resources" not in data:
            if is_required:
                raise RuntimeError(
                    f"WBEACP: Manifest include target is not a manifest: {normalized_manifest_path}."
                )
            return False
        if not isinstance(data["resources"], list):
            raise RuntimeError(
                f"WBEACP: Manifest resources must be an array: {normalized_manifest_path}."
            )

        data["include"] = self._normalize_include_paths(data.get("include", []), normalized_manifest_path)
        self._scope_enabled(data.get("scope", "global"), normalized_manifest_path)
        self._manifest_sources[normalized_manifest_path] = data
        return True

    def _scope_enabled(self, scope: Any, context: str) -> bool:
        if scope not in ("global", "test_only"):
            raise RuntimeError(f"WBEACP: Invalid scope '{scope}' in {context}.")
        return scope == "global" or self._enabled_test

    def _normalize_include_paths(self, include_entries: Any, manifest_rel_path: str) -> list[str]:
        if include_entries is None:
            include_entries = []
        if not isinstance(include_entries, list):
            raise RuntimeError(
                f"WBEACP: Manifest include field must be an array: {manifest_rel_path}."
            )

        normalized_include: list[str] = []
        for include_entry in include_entries:
            if not isinstance(include_entry, str) or not include_entry:
                raise RuntimeError(
                    f"WBEACP: Manifest include entry must be a non-empty string: {manifest_rel_path}."
                )
            if Path(include_entry).suffix != SOURCE_MANIFEST_SUFFIX:
                raise RuntimeError(
                    f"WBEACP: Manifest include entry must reference a {SOURCE_MANIFEST_SUFFIX} manifest in {manifest_rel_path}: {include_entry}."
                )
            normalized_include.append(Path(include_entry).as_posix())
        return normalized_include

    def _process_manifest(self, manifest_rel_path: str, include_stack: list[str]) -> bool:
        if manifest_rel_path in self._processed_manifests:
            return True
        if manifest_rel_path in include_stack:
            recursion_chain = include_stack + [manifest_rel_path]
            raise RuntimeError(
                f"WBEACP: Manifest include recursion detected: {' -> '.join(recursion_chain)}."
            )

        source_manifest = self._manifest_sources.get(manifest_rel_path)
        if source_manifest is None:
            raise RuntimeError(
                f"WBEACP: Manifest include target was not found: {manifest_rel_path}."
            )
        if not self._scope_enabled(source_manifest.get("scope", "global"), manifest_rel_path):
            return False

        include_stack.append(manifest_rel_path)
        include_paths = source_manifest.get("include", [])
        processed_include_paths: list[str] = []
        for include_path in include_paths:
            if not self._load_manifest_source(include_path, True):
                raise RuntimeError(
                    f"WBEACP: Manifest include target does not exist: {include_path} (included by {manifest_rel_path})."
                )
            if self._process_manifest(include_path, include_stack):
                processed_include_paths.append(include_path)

        processed_manifest = self._process_manifest_data(source_manifest, manifest_rel_path, processed_include_paths)

        self._write_manifest(manifest_rel_path, processed_manifest)
        self._processed_manifests[manifest_rel_path] = processed_manifest
        include_stack.pop()
        return True

    def _process_manifest_data(self, manifest_data: ManifestData, manifest_rel_path: str,
                               include_paths: list[str]) -> ManifestData:
        manifest_resources = manifest_data.get("resources")
        if not isinstance(manifest_resources, list):
            raise RuntimeError(
                f"WBEACP: Manifest resources must be an array: {manifest_rel_path}."
            )

        processed_resources: list[ManifestResource] = []
        for resource in manifest_resources:
            if not isinstance(resource, dict):
                raise RuntimeError(
                    f"WBEACP: Manifest resource entry must be an object in {manifest_rel_path}."
                )
            if not self._scope_enabled(resource.get("scope", "global"), manifest_rel_path):
                continue
            processed_resources.extend(self._process_typed_resource(resource, manifest_rel_path, True))

        processed_manifest: ManifestData = {
            "resources": processed_resources,
        }
        if "include" in manifest_data:
            processed_manifest["include"] = [self._get_runtime_manifest_path(include_path) for include_path in include_paths]
        return processed_manifest

    def _get_runtime_manifest_path(self, manifest_rel_path: str) -> str:
        return Path(manifest_rel_path).with_suffix(RUNTIME_MANIFEST_SUFFIX).as_posix()

    def _write_manifest(self, manifest_rel_path: str, manifest_data: dict[str, Any]) -> None:
        output_path = Path.joinpath(self._res_output_dir, Path(self._get_runtime_manifest_path(manifest_rel_path)))
        os.makedirs(output_path.parent, exist_ok=True)
        with open(output_path, "w", encoding="utf-8") as output_file:
            json.dump(manifest_data, output_file, indent=4)
            output_file.write("\n")

    def _process_value(self, value: Any, manifest_rel_path: str) -> Any:
        if isinstance(value, list):
            return [self._process_value(element, manifest_rel_path) for element in value]
        if isinstance(value, dict):
            if self._is_nested_source_resource(value):
                # Nested resources live inside another resource field. They cannot
                # expand into multiple sibling resources without losing placement.
                processed_resources = self._process_typed_resource(value, manifest_rel_path, False)
                if len(processed_resources) != 1:
                    raise RuntimeError(
                        f"WBEACP: Nested typed resource in {manifest_rel_path} must compile to exactly one resource."
                    )
                return processed_resources[0]
            result: dict[str, Any] = {}
            for key, nested_value in value.items():
                result[key] = self._process_value(nested_value, manifest_rel_path)
            return result
        return value

    def _is_nested_source_resource(self, value: dict[str, Any]) -> bool:
        source_type = value.get(SOURCE_RESOURCE_TYPE_KEY)
        return isinstance(source_type, str) and bool(source_type)

    def _is_typed_resource(self, value: dict[str, Any]) -> bool:
        source_type = value.get(SOURCE_RESOURCE_TYPE_KEY)
        if isinstance(source_type, str) and source_type:
            return True
        resource_type = value.get("type")
        return isinstance(resource_type, str) and bool(resource_type)

    def _process_typed_resource(self, resource: ManifestResource, manifest_rel_path: str,
                                is_top_level: bool) -> list[ManifestResource]:
        if not self._is_typed_resource(resource):
            raise RuntimeError(
                f"WBEACP: Typed resource is missing required type field in {manifest_rel_path}: {resource}."
            )

        result = dict(resource)
        if not self._scope_enabled(result.pop("scope", "global"), manifest_rel_path):
            return []
        source_resource_type = result.get(SOURCE_RESOURCE_TYPE_KEY)
        has_source_resource_type = isinstance(source_resource_type, str) and bool(source_resource_type)
        runtime_resource_type = result.get("type")
        if has_source_resource_type:
            resource_type = source_resource_type
        elif isinstance(runtime_resource_type, str) and runtime_resource_type:
            resource_type = runtime_resource_type
        else:
            raise RuntimeError(
                f"WBEACP: Typed resource is missing required type field in {manifest_rel_path}: {resource}."
            )
        compiler = self._compiler_map.get(resource_type)
        if compiler is not None and (is_top_level or has_source_resource_type):
            # Compilers replace one typed source entry with one or more runtime
            # resource entries in the same manifest.
            replacement_resources = compiler.compile(result, Path(manifest_rel_path), self._res_dir, self._res_output_dir)
            if not isinstance(replacement_resources, list):
                raise RuntimeError(
                    f"WBEACP: Compiler output resources must be an array in {manifest_rel_path}."
                )
        else:
            # Resource types without compilers are allowed only when they already
            # describe runtime data and do not reference source files.
            replacement_resources = [self._validate_uncompiled_resource(result, manifest_rel_path)]

        normalized_resources: list[ManifestResource] = []
        for replacement_resource in replacement_resources:
            if not isinstance(replacement_resource, dict):
                raise RuntimeError(
                    f"WBEACP: Compiler output resource entry must be an object in {manifest_rel_path}."
                )
            normalized_result: ManifestResource = {}
            for key, value in replacement_resource.items():
                normalized_result[key] = self._process_value(value, manifest_rel_path)
            if is_top_level:
                self._track_resource_id(normalized_result, manifest_rel_path)
            normalized_resources.append(normalized_result)
        return normalized_resources

    def _track_resource_id(self, resource: ManifestResource, manifest_rel_path: str) -> None:
        resource_id = resource.get("id")
        if isinstance(resource_id, str) and resource_id:
            if resource_id in self._seen_resource_ids:
                raise RuntimeError(
                    f"WBEACP: Duplicated resource id '{resource_id}' found in {manifest_rel_path}."
                )
            self._seen_resource_ids.add(resource_id)

    def _validate_uncompiled_resource(self, resource: dict[str, Any], manifest_rel_path: str) -> dict[str, Any]:
        result = dict(resource)
        resource_type = str(result.get("type"))
        if SOURCE_RESOURCE_TYPE_KEY in result or SOURCE_RESOURCE_FILE_KEY in result:
            raise RuntimeError(
                f"WBEACP: Resource type '{resource_type}' in {manifest_rel_path} still declares source-only fields after compilation."
            )
        return result
