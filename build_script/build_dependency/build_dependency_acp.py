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
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Any

from build_script.build_dependency.build_dependency import WBEBuildDependency


class WBEBuildDependencyACP(WBEBuildDependency):
    """Configure and build native libraries required by the ACP."""

    def __init__(self,
            p_root_dir: Path,
            p_build_root_dir: Path,
            p_dependencies_dir: Path,
            p_build_info: dict[str, Any]) -> None:
        self._source_dir = p_root_dir / "build_script" / "build_dependency" / "acp"
        self._build_dir = p_build_root_dir / "acp"
        self._dependencies_dir = p_dependencies_dir
        self._build_info = p_build_info

    def get_name(self) -> str:
        return "ACP dependencies"

    def get_ktx_library_dir(self) -> Path:
        """Return the directory containing the ACP-specific libktx build."""
        return self._build_dir / "lib"

    def get_python_package_dir(self) -> Path:
        """Return the directory containing ACP-specific Python packages."""
        return self._build_dir / "python"

    def compile(self) -> None:
        """Configure and build all native ACP dependencies."""
        environment = os.environ.copy()
        ccache_dir = self._build_dir / "ccache"
        ccache_dir.mkdir(parents=True, exist_ok=True)
        environment["CCACHE_DIR"] = ccache_dir.as_posix()
        self._discard_cmake_cache_on_generator_change(self._build_dir, self._build_info.get("generator"))
        configure_command = [
            "cmake",
            "-S",
            self._source_dir.as_posix(),
            "-B",
            self._build_dir.as_posix(),
            f"-DCMAKE_BUILD_TYPE={self._build_info['cmake-build-type']}",
            f"-DKTX_SOFTWARE_DIR={(self._dependencies_dir / 'KTX-Software').as_posix()}",
            f"-DWBE_MODEL_COMPILER_DIR={(self._dependencies_dir / 'wbe-utils-model-compiler').as_posix()}",
            f"-DWBE_DEPENDENCIES_ROOT={self._dependencies_dir.as_posix()}",
        ]
        if self._build_info.get("generator") is not None:
            configure_command.extend(["-G", self._build_info["generator"]])
        if self._build_info.get("cpp-compiler") is not None:
            configure_command.append(f"-DCMAKE_CXX_COMPILER={self._build_info['cpp-compiler']}")
        if self._build_info.get("c-compiler") is not None:
            configure_command.append(f"-DCMAKE_C_COMPILER={self._build_info['c-compiler']}")
        if subprocess.run(configure_command, check=False, env=environment).returncode != 0:
            raise RuntimeError("Failed to configure ACP dependencies.")

        model_package_source_dir = self._dependencies_dir / "wbe-utils-model-compiler" / "wbe_utils_model_compiler"
        model_package_build_dir = self.get_python_package_dir() / "wbe_utils_model_compiler"
        shutil.copytree(model_package_source_dir,
            model_package_build_dir,
            dirs_exist_ok=True,
            ignore=shutil.ignore_patterns("_native*.so", "_native*.pyd", "__pycache__"))

        build_command = ["cmake", "--build", self._build_dir.as_posix(), "--target", "ktx", "_native"]
        if os.cpu_count() is not None:
            build_command.extend(["-j", str(os.cpu_count())])
        if subprocess.run(build_command, check=False, env=environment).returncode != 0:
            raise RuntimeError("Failed to build ACP dependencies.")

        version_path = self._build_dir / "ktx" / "ktx.version"
        if not version_path.is_file():
            raise RuntimeError(f"Failed to locate the built libktx version file: {version_path}.")
        environment.update({
            "LIBKTX_VERSION": version_path.read_text(encoding="utf-8").strip(),
            "LIBKTX_INCLUDE_DIR": (self._dependencies_dir / "KTX-Software" / "lib" / "include").as_posix(),
            "DFDUTILS_INCLUDE_DIR": (self._dependencies_dir / "KTX-Software" / "external" / "dfdutils").as_posix(),
            "LIBKTX_LIB_DIR": self.get_ktx_library_dir().as_posix(),
        })
        binding_source_dir = self._dependencies_dir / "KTX-Software" / "interface" / "python_binding"
        binding_build_dir = self.get_python_package_dir()
        shutil.copytree(binding_source_dir, binding_build_dir, dirs_exist_ok=True)
        build_command = [sys.executable, "buildscript.py"]
        if subprocess.run(build_command, check=False, cwd=binding_build_dir, env=environment).returncode != 0:
            raise RuntimeError("Failed to build pyktx for the ACP.")
