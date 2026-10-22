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

import os
from pathlib import Path
import shutil
from typing import override
from build_script.resource.acp.acp_compiler import ManifestResource, SOURCE_RESOURCE_FILE_KEY, SOURCE_RESOURCE_TYPE_KEY, WBEACPCompiler


class WBEACPCompilerFile(WBEACPCompiler):
    """ACP compiler that copies one file-backed resource into the output tree."""

    def __init__(self, supported_resource_types: list[str]) -> None:
        self._supported_resource_types = supported_resource_types

    @override
    def get_supported_resource_types(self) -> list[str]:
        return self._supported_resource_types

    @override
    def compile(self, resource: ManifestResource, manifest_path: Path,
            res_dir: Path, res_output_dir: Path) -> list[ManifestResource]:
        result = dict(resource)
        resource_type = self._get_source_resource_type(result)
        if resource_type is None:
            raise RuntimeError(
                f"File resource must declare a non-empty source type in {manifest_path}: {resource}."
            )
        result["type"] = resource_type

        resource_file = self._get_source_resource_file(result)
        if resource_file is None:
            raise RuntimeError(
                f"File resource must declare a non-empty source file field in {manifest_path}: {resource}."
            )

        source_path, relative_file = self._resolve_resource_file_path(resource_file, manifest_path, res_dir)
        output_file = result.get("path", self._get_default_output_file(relative_file).as_posix())
        if not isinstance(output_file, str) or not output_file:
            raise RuntimeError(
                f"File resource path must be a non-empty string in {manifest_path}: {resource}."
            )
        relative_output_file = self._normalize_resource_path(output_file, manifest_path)

        output_path = Path.joinpath(res_output_dir, relative_output_file)
        os.makedirs(output_path.parent, exist_ok=True)
        self._write_output_file(source_path, output_path)

        result["path"] = relative_output_file.as_posix()
        result.pop(SOURCE_RESOURCE_TYPE_KEY, None)
        result.pop(SOURCE_RESOURCE_FILE_KEY, None)
        return [result]

    def _get_default_output_file(self, relative_file: Path) -> Path:
        """Get the output path of a resource whose manifest entry sets no path.

        Args:
            relative_file: The source file, relative to the resource directory.

        Returns:
            The output file, relative to the output resource directory.
        """
        return relative_file

    def _write_output_file(self, source_path: Path, output_path: Path) -> None:
        """Write the output file of a resource.

        Args:
            source_path: The resolved source file.
            output_path: The output file, whose directory exists.
        """
        shutil.copyfile(source_path, output_path)
