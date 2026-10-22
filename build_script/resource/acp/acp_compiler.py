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
import abc
from pathlib import Path
from typing import Any

ManifestResource = dict[str, Any]
ManifestData = dict[str, Any]

SOURCE_RESOURCE_TYPE_KEY = "source_type"
SOURCE_RESOURCE_FILE_KEY = "source_file"

class WBEACPCompiler(abc.ABC):
    """The compiler interface for the ACP."""
    @abc.abstractmethod
    def get_supported_resource_types(self) -> list[str]:
        """Get the resource types supported by this compiler.

        Returns:
            A list of strings of the resource types supported by this compiler.
        """
        pass

    @abc.abstractmethod
    def compile(self, resource: ManifestResource, manifest_path: Path,
            res_dir: Path, res_output_dir: Path) -> list[ManifestResource]:
        """Compile or transform one resource into one or more resource entries.

        Args:
            resource: Input resource manifest entry.
            manifest_path: Relative path of the manifest file that owns this entry.
            res_dir: Root source resource directory.
            res_output_dir: Root output resource directory.

        Returns:
            Resource entries that replace the input entry in the owning manifest.
        """
        pass

    def _normalize_resource_path(self, resource_path: str, manifest_path: Path) -> Path:
        relative_path = Path(resource_path)
        if relative_path.is_absolute():
            raise RuntimeError(
                f"Absolute resource path is not supported in {manifest_path}: {resource_path}."
            )

        normalized_path = Path(relative_path.as_posix())
        if normalized_path.as_posix() == ".":
            raise RuntimeError(
                f"Resource path must be non-empty in {manifest_path}."
            )
        return normalized_path

    def _get_source_resource_type(self, resource: ManifestResource) -> str | None:
        source_type = resource.get(SOURCE_RESOURCE_TYPE_KEY)
        if isinstance(source_type, str) and source_type:
            return source_type
        return None

    def _get_source_resource_file(self, resource: ManifestResource) -> str | None:
        source_file = resource.get(SOURCE_RESOURCE_FILE_KEY)
        if isinstance(source_file, str) and source_file:
            return source_file
        return None

    def _resolve_resource_file_path(self, resource_path: str, manifest_path: Path, res_dir: Path) -> tuple[Path, Path]:
        relative_path = self._normalize_resource_path(resource_path, manifest_path)
        candidates = [Path.joinpath(res_dir, relative_path)]
        manifest_relative_path = Path.joinpath(res_dir, manifest_path.parent, relative_path)
        if manifest_relative_path != candidates[0]:
            candidates.append(manifest_relative_path)

        for candidate in candidates:
            if candidate.exists() and candidate.is_file():
                return candidate.resolve(), candidate.relative_to(res_dir)

        raise RuntimeError(
            f"WBEACP: Resource file does not exist for {manifest_path}: {relative_path.as_posix()}."
        )
