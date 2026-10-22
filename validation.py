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

from argparse import ArgumentParser
from pathlib import Path
import subprocess
import sys
from typing import Callable, TextIO

import build_config
from run_shader_tests import run_shader_tests


ROOT_DIR = Path(__file__).resolve().parent
VALIDATION_LOG_DIR = ROOT_DIR / "build" / "validation_logs"


def _get_test_targets() -> list[str]:
    return [target for target, info in build_config.target_info.items() if info["generate-tests"]]


def _run_step(name: str, command: list[str], log_file_name: str) -> None:
    print(f"WBEValidator: Validating {name}...")
    VALIDATION_LOG_DIR.mkdir(parents=True, exist_ok=True)
    log_path = VALIDATION_LOG_DIR / log_file_name
    with log_path.open("w", encoding="utf-8") as log_file:
        result = subprocess.run(command, cwd=ROOT_DIR, stdout=log_file, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        print(f"WBEValidator: Failed {name}. See {log_path}.")
        raise SystemExit(result.returncode)


def _run_function_step(name: str, function: Callable[[TextIO], int], log_file_name: str) -> None:
    print(f"WBEValidator: Validating {name}...")
    VALIDATION_LOG_DIR.mkdir(parents=True, exist_ok=True)
    log_path = VALIDATION_LOG_DIR / log_file_name
    with log_path.open("w", encoding="utf-8") as log_file:
        return_code = function(log_file)
    if return_code != 0:
        print(f"WBEValidator: Failed {name}. See {log_path}.")
        raise SystemExit(return_code)


def _get_unit_test_path(target: str) -> Path:
    export_directory = build_config.target_info[target]["export-directory"]
    return ROOT_DIR / "build" / export_directory / "bin" / "wbe_unit_test"


def main() -> int:
    test_targets = _get_test_targets()
    argument_parser = ArgumentParser(description="Validate White Bird Engine build, unit tests, and shader tests.")
    argument_parser.add_argument("-t", "--target", choices=test_targets, default="debug", help="Build/test target to validate.")
    parsed_args, gtest_args = argument_parser.parse_known_args()
    if gtest_args and gtest_args[0] == "--":
        gtest_args = gtest_args[1:]

    _run_step("build", [sys.executable, "build.py", "-t", parsed_args.target], f"{parsed_args.target}_build.log")

    unit_test_path = _get_unit_test_path(parsed_args.target)
    if gtest_args:
        _run_step("requested tests", [str(unit_test_path), *gtest_args], f"{parsed_args.target}_requested_tests.log")

    _run_step("full unit tests", [str(unit_test_path)], f"{parsed_args.target}_full_unit_tests.log")
    _run_function_step("shader tests", run_shader_tests, f"{parsed_args.target}_shader_tests.log")
    print("WBEValidator: Finished successfully!")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
