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
from typing import Any, Callable
from build_script.reflection.code_gen import WBECodeGenerator
from build_script.reflection.metadata_types import WBEMetadata
from build_script.reflection.gen_file_info import WBEGenFileInfo
from build_script.reflection.parameter_resolver import WBEDictParameterSource
from build_config import gen_info
from build_script.utils import hash_str
import build_setup

class WBEReflector:
    """Reflector. Used for managing all the reflections.

    Attributes: 
        metadata_path: The path to the metadata output file.
        metadata: The metadata of the engine.
    """

    def __init__(self, metadata_path : str, gen_file_infos : list[WBEGenFileInfo]) -> None:
        """Constructor.

        Args:
            metadata_path: The path to the metadata output file.
            gen_file_infos: The list of code generation information.
        """
        self.metadata_path = metadata_path
        self.metadata = WBEMetadata()
        self._gen_file_infos = gen_file_infos

    def register_metadata(self, metadata: WBEMetadata) -> None:
        self.metadata = metadata

    def dump(self) -> None:
        """Dump the metadata to the metadata file."""
        print("WBEReflect: Exporting metadata...")
        if build_setup.build_target["generate-tests"]:
            # Output metadata for debug purpose.
            self._write_to_file(build_setup.metadata_path, json.dumps(self.metadata.model_dump(), indent=4))
        # Generate code
        builtin_source = WBEDictParameterSource({
            "build_target": build_setup.args.target,
            "build_dir": build_setup.build_dir,
            "binary_dir": build_setup.binary_dir,
            "root_dir": build_setup.root_dir,
            "include_dir": build_setup.include_dir,
            "source_dir": build_setup.source_dir,
        })
        generator = WBECodeGenerator(
            gen_info | {"metadata" : self.metadata},
            self._gen_file_infos,
            extra_sources=[builtin_source],
        )
        generator.generate()

    def checks(self) -> None:
        """Do checkings."""
        print("WBEReflect: Checking...")
        self._check_redefinition(self.metadata.labels)
        self._check_redefinition(self.metadata.enums, lambda enum: enum.qualified_name)
        self._check_hashing_collision("channels", self.metadata.labels, lambda label: hash_str(label.name))

    def _write_to_file(self, path : str, str_content : str) -> None:
        with open(path, "w+") as channel_file:
            channel_file.write(str_content)

    def _check_redefinition(self, objs: list[Any], get_name: Callable[[Any], str] = lambda obj: obj.name) -> None:
        seen: set[str] = set()
        for check in objs:
            name = get_name(check)
            if name in seen:
                raise RuntimeError(f"\"{name}\" is redefined.")
            seen.add(name)

    def _check_hashing_collision(self, dict_name: str, objs: list[Any],
                                 hash_function: Callable[[Any], int]) -> None:
        hash_val_set: set[int] = set()
        dict_rev: dict[int, Any] = dict()
        for check in objs:
            hash_code = hash_function(check)
            if hash_code in hash_val_set:
                raise RuntimeError(f"Hasing collision of \"{dict_name}\" detected! Hash values of: \"{check}\" "
                    f"and \"{dict_rev[hash_code]}\" are identical: {hash_code}")
            dict_rev[hash_code] = check
            hash_val_set.add(hash_code)
