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
from concurrent.futures import ThreadPoolExecutor, as_completed
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

import build_config
from git import Repo
from git.exc import BadName, InvalidGitRepositoryError, NoSuchPathError

def find_compile_commands_dir(root_dir: Path) -> Path | None:
    candidates = [
        root_dir / "build",
        root_dir / "build" / "debug",
        root_dir / "build" / "release",
        root_dir / "build" / "deploy",
        root_dir,
        root_dir / "build-debug",
        root_dir / "build-release",
        root_dir / "build-deploy",
    ]

    for candidate in candidates:
        if (candidate / "compile_commands.json").exists():
            return candidate

    for compile_commands in sorted((root_dir / "build").glob("**/compile_commands.json")):
        return compile_commands.parent

    return None

def gather_changed_project_sources(root_dir: Path, project_sources: list[str]) -> list[str]:
    try:
        repo = Repo(root_dir, search_parent_directories=True)
    except (InvalidGitRepositoryError, NoSuchPathError):
        print("WBECheckLint: Current workspace is not in a Git repository.")
        return []

    if repo.working_tree_dir is None:
        print("WBECheckLint: Failed to determine Git working tree directory.")
        return []

    repo_root = Path(repo.working_tree_dir).resolve()
    source_set = {str(Path(source).resolve()) for source in project_sources}

    changed_rel_paths: set[str] = set()

    # Unstaged changes.
    for diff in repo.index.diff(None):
        if diff.a_path is not None:
            changed_rel_paths.add(diff.a_path)
        if diff.b_path is not None:
            changed_rel_paths.add(diff.b_path)

    # Staged changes.
    try:
        staged_diffs = repo.index.diff("HEAD")
    except BadName:
        staged_diffs = []
    for diff in staged_diffs:
        if diff.a_path is not None:
            changed_rel_paths.add(diff.a_path)
        if diff.b_path is not None:
            changed_rel_paths.add(diff.b_path)

    # Untracked files.
    for rel_path in repo.untracked_files:
        changed_rel_paths.add(rel_path)

    changed_sources: set[str] = set()
    for rel_path in changed_rel_paths:
        abs_path = str((repo_root / rel_path).resolve())
        if abs_path in source_set:
            changed_sources.add(abs_path)

    return sorted(changed_sources)


def gather_commit_project_sources(root_dir: Path, project_sources: list[str], commit_id: str) -> list[str]:
    try:
        repo = Repo(root_dir, search_parent_directories=True)
    except (InvalidGitRepositoryError, NoSuchPathError):
        print("WBECheckLint: Current workspace is not in a Git repository.")
        return []

    if repo.working_tree_dir is None:
        print("WBECheckLint: Failed to determine Git working tree directory.")
        return []

    try:
        commit = repo.commit(commit_id)
    except (BadName, ValueError):
        print(f"WBECheckLint: Failed to resolve commit id: {commit_id}")
        return []

    repo_root = Path(repo.working_tree_dir).resolve()
    source_set = {str(Path(source).resolve()) for source in project_sources}

    changed_sources: set[str] = set()
    for rel_path in commit.stats.files.keys():
        abs_path = str((repo_root / rel_path).resolve())
        if abs_path in source_set:
            changed_sources.add(abs_path)

    return sorted(changed_sources)


def run_clang_tidy_fix(file_path: str, compile_commands_dir: Path) -> subprocess.CompletedProcess[str]:
    cmd = [
        "clang-tidy",
        "--fix",
        "--quiet",
        "-p",
        str(compile_commands_dir),
        file_path,
    ]
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def run_clang_tidy_check(file_path: str, compile_commands_dir: Path) -> subprocess.CompletedProcess[str]:
    cmd = [
        "clang-tidy",
        "--quiet",
        "-p",
        str(compile_commands_dir),
        file_path,
    ]
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def run_clang_format_check(file_path: str) -> subprocess.CompletedProcess[str]:
    cmd = [
        "clang-format",
        "--dry-run",
        "--Werror",
        "--style=file",
        file_path,
    ]
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def run_clang_format_fix(file_path: str) -> subprocess.CompletedProcess[str]:
    cmd = [
        "clang-format",
        "-i",
        "--style=file",
        file_path,
    ]
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def fix_and_check_file(file_path: str, compile_commands_dir: Path) -> tuple[str, bool, bool, str]:
    fix_result = run_clang_tidy_fix(file_path, compile_commands_dir)
    format_fix_result = run_clang_format_fix(file_path)
    _, _, has_unfixed_diag, remaining_output = check_file(file_path, compile_commands_dir)
    run_failed = fix_result.returncode != 0 or format_fix_result.returncode != 0
    return file_path, run_failed, has_unfixed_diag, remaining_output


def check_file(file_path: str, compile_commands_dir: Path) -> tuple[str, bool, bool, str]:
    tidy_result = run_clang_tidy_check(file_path, compile_commands_dir)
    tidy_output = f"{tidy_result.stdout}\n{tidy_result.stderr}".strip()
    has_lints = re.search(r"\b(warning|error):", tidy_output) is not None

    format_result = run_clang_format_check(file_path)
    format_output = f"{format_result.stdout}\n{format_result.stderr}".strip()
    has_format_diag = format_result.returncode != 0 or len(format_output) > 0

    combined_parts: list[str] = []
    if tidy_output:
        combined_parts.append(tidy_output)
    if format_output:
        combined_parts.append("-- clang-format --\n" + format_output)
    combined_output = "\n\n".join(combined_parts)

    run_failed = tidy_result.returncode != 0
    return file_path, run_failed, has_lints or has_format_diag, combined_output


def main() -> int:
    parser = ArgumentParser(
        prog="check_lint.py",
        description="Check clang-tidy diagnostics (changed files by default).",
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="Enable auto-fix mode (previous fix_lint behavior).",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Run lint on all project source files instead of only changed files.",
    )
    parser.add_argument(
        "--commit-id",
        type=str,
        default=None,
        help="Run lint on project source files changed in a specific commit.",
    )
    parser.add_argument(
        "--no-tests",
        action="store_true",
        help="Exclude files under tests/ from lint fixing.",
    )
    parser.add_argument(
        "--compile-commands-dir",
        type=str,
        default=None,
        help="Directory containing compile_commands.json. Auto-detected by default.",
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=max(1, os.cpu_count() or 1),
        help="Number of parallel workers used to run clang-tidy.",
    )
    args = parser.parse_args()

    if args.all and args.commit_id is not None:
        parser.error("--all and --commit-id cannot be used together.")

    root_dir = Path(__file__).resolve().parent

    if shutil.which("clang-tidy") is None:
        print("WBECheckLint: clang-tidy is not available in PATH.")
        return 1

    if shutil.which("clang-format") is None:
        print("WBECheckLint: clang-format is not available in PATH.")
        return 1

    if args.compile_commands_dir is not None:
        compile_commands_dir = Path(args.compile_commands_dir).resolve()
    else:
        found_dir = find_compile_commands_dir(root_dir)
        if found_dir is None:
            print("WBECheckLint: Failed to locate compile_commands.json.")
            print("WBECheckLint: Build the project first or provide --compile-commands-dir.")
            return 1
        compile_commands_dir = found_dir

    if not (compile_commands_dir / "compile_commands.json").exists():
        print(f"WBECheckLint: compile_commands.json not found in {compile_commands_dir}.")
        return 1

    project_sources = build_config.project_sources
    if args.all:
        sources = sorted(project_sources)
    elif args.commit_id is not None:
        sources = gather_commit_project_sources(root_dir, project_sources, args.commit_id)
    else:
        sources = gather_changed_project_sources(root_dir, project_sources)

    if len(sources) == 0:
        if args.all:
            print("WBECheckLint: No source files found.")
        else:
            print("WBECheckLint: No changed source files found.")
        return 0

    print(f"WBECheckLint: Using compile_commands from {compile_commands_dir}")
    if args.fix:
        print("WBECheckLint: Running in auto-fix mode (--fix).")
    else:
        print("WBECheckLint: Running in check-only mode (default).")
    if args.all:
        print("WBECheckLint: Running in full-project scope (--all).")
    elif args.commit_id is not None:
        print(f"WBECheckLint: Running in commit scope (--commit-id={args.commit_id}).")
    else:
        print("WBECheckLint: Running in changed-files scope (default).")

    action_label = "Lint-fixing" if args.fix else "Checking lint for"
    print(f"WBECheckLint: {action_label} {len(sources)} files with {args.jobs} workers...")

    failed_runs: list[str] = []
    linted_files: list[str] = []
    lint_outputs: dict[str, str] = {}

    total_sources = len(sources)
    completed_count = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
        if args.fix:
            future_to_source = {
                executor.submit(fix_and_check_file, source, compile_commands_dir): source
                for source in sources
            }
        else:
            future_to_source = {
                executor.submit(check_file, source, compile_commands_dir): source
                for source in sources
            }

        for future in as_completed(future_to_source):
            completed_count += 1
            source = future_to_source[future]
            if args.fix:
                print(f"[{completed_count}/{total_sources}] clang-tidy --fix {source}")
                file_path, fix_failed, has_unfixed_lints, output = future.result()
                if fix_failed:
                    failed_runs.append(file_path)
                if has_unfixed_lints:
                    linted_files.append(file_path)
                    lint_outputs[file_path] = output
            else:
                print(f"[{completed_count}/{total_sources}] clang-tidy {source}")
                file_path, check_failed, has_lints, output = future.result()
                if check_failed:
                    failed_runs.append(file_path)
                if has_lints:
                    linted_files.append(file_path)
                    lint_outputs[file_path] = output

    print("WBECheckLint: Finished running clang-tidy.")

    if failed_runs:
        print("WBECheckLint: clang-tidy failed on these files:")
        for file_path in failed_runs:
            print(f"  - {file_path}")

    if args.fix:
        fix_output_path = root_dir / "build" / "lint_fix_output.txt"
        fix_output_path.parent.mkdir(parents=True, exist_ok=True)
        with fix_output_path.open("w", encoding="utf-8") as fix_output_file:
            for file_path in sorted(linted_files):
                fix_output_file.write(f"== {file_path} ==\n{lint_outputs[file_path]}\n\n")
        if linted_files:
            print("WBECheckLint: These files still have diagnostics after auto-fix:")
            for file_path in linted_files:
                print(f"  - {file_path}")
            print(f"WBECheckLint: Remaining diagnostics written to {fix_output_path}")
        if failed_runs or linted_files:
            return 1
        print("WBECheckLint: No remaining diagnostics were found after auto-fix.")
        return 0

    if linted_files:
        print("WBECheckLint: Lint diagnostics:")
        for file_path in sorted(linted_files):
            print(f"\n== {file_path} ==")
            print(lint_outputs[file_path])

    if failed_runs or linted_files:
        return 1

    print("WBECheckLint: No lint diagnostics found.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
