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
from typing import override
from build_script.resource.acp.acp_compiler import ManifestResource, WBEACPCompiler

from wbe_utils_model_compiler import SDFCompiler, TextureCompiler, WBEUtilsModelCompiler

class WBEACPCompilerModel(WBEACPCompiler):
    """The model compiler.
    """
    def __init__(self, texture_compiler: TextureCompiler, sdf_compiler: SDFCompiler, cache_dir: Path | None = None) -> None:
        self._utils_mesh_compiler = WBEUtilsModelCompiler(
            texture_compiler=texture_compiler, cache_dir=cache_dir, sdf_compiler=sdf_compiler)

    @override
    def get_supported_resource_types(self) -> list[str]:
        return self._utils_mesh_compiler.get_supported_resource_types()

    @override
    def compile(self, resource: ManifestResource, manifest_path: Path,
            res_dir: Path, res_output_dir: Path) -> list[ManifestResource]:
        resolved_resource = dict(resource)
        source_type = self._get_source_resource_type(resource)
        resolved_resource["type"] = "static_geometry" if source_type == "static_geometry" else "model"
        if source_type == "static_geometry":
            source_files = resource.get("source_files")
            if (not isinstance(source_files, list) or not source_files
                    or any(not isinstance(path, str) or not path for path in source_files)):
                raise RuntimeError(f"Static geometry requires a non-empty source_files array of strings in {manifest_path}.")
            resolved_resource["source_files"] = [
                self._resolve_resource_file_path(path, manifest_path, res_dir)[0].as_posix()
                for path in source_files
            ]
        else:
            resource_path = self._get_source_resource_file(resource)
            if resource_path is None:
                raise RuntimeError(
                    f"Model resource must declare a non-empty source file field in {manifest_path}: {resource}."
                )
            source_path, _ = self._resolve_resource_file_path(resource_path, manifest_path, res_dir)
            resolved_resource["file"] = source_path.as_posix()
        resolved_manifest_path = Path.joinpath(res_dir, manifest_path)
        return self._utils_mesh_compiler.compile(
            resolved_resource,
            resolved_manifest_path,
            res_dir,
            res_output_dir,
        )
