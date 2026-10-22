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
from typing import Any, override

import lupa.lua55

from build_script.resource.acp.acp_compiler_file import WBEACPCompilerFile

LUA_SUFFIX = ".lua"
TEAL_SUFFIX = ".tl"

# Type-check one Teal module and generate its Lua. Returns the Lua source, or nil and the errors, one per line.
TEAL_COMPILE_FUNCTION = """
function(p_tl, p_search_path, p_filename, p_input)
    p_tl.path = p_search_path
    -- Lua 5.4 code runs on the engine's Lua 5.5, which the Teal compiler does not target yet.
    local env = assert(p_tl.new_env({ defaults = { gen_target = "5.4", gen_compat = "off" } }))
    local result = p_tl.check_string(p_input, env, p_filename)
    local errors = {}
    for _, list in ipairs({ result.syntax_errors or {}, result.type_errors or {} }) do
        for _, error in ipairs(list) do
            errors[#errors + 1] = string.format("%s:%d:%d: %s", error.filename or p_filename, error.y, error.x, error.msg)
        end
    end
    if #errors > 0 then
        return nil, table.concat(errors, "\\n")
    end
    local code, generate_error = p_tl.generate(result.ast, env.defaults.gen_target)
    if code == nil then
        return nil, string.format("%s: %s", p_filename, tostring(generate_error))
    end
    return code, nil
end
"""


class WBEACPCompilerScript(WBEACPCompilerFile):
    """ACP compiler for script resources.

    Lua sources (.lua) are copied. Teal sources (.tl) are type-checked and compiled to Lua with the Teal compiler,
    which runs in an embedded Lua 5.5; a Teal script is emitted as <name>.lua next to where its source would be copied.
    Modules a Teal script requires resolve against the module directories, where each module is searched for as a
    declaration (.d.tl), a Teal source (.tl), or a Lua source (.lua) under its module path. A script whose source file
    is in the generated module directory, such as the render_graph_utils/render_graph_tasks.tl that reflection generates, is
    compiled from there.
    """

    def __init__(self, teal_dir: Path, module_dirs: list[Path], generated_modules_dir: Path | None = None) -> None:
        """
        Args:
            teal_dir: The directory containing the Teal compiler (tl.lua).
            module_dirs: The directories module names resolve against, in search order.
            generated_modules_dir: The directory reflection generates script modules into, where script source files
                are looked up before the resource directory.
        """
        super().__init__(["script"])
        self._teal_dir = teal_dir
        self._generated_modules_dir = generated_modules_dir
        self._search_path = ";".join((module_dir / "?.lua").as_posix() for module_dir in module_dirs)
        self._lua: Any = None
        self._teal: Any = None
        self._teal_compile: Any = None

    @override
    def _resolve_resource_file_path(self, resource_path: str, manifest_path: Path, res_dir: Path) -> tuple[Path, Path]:
        relative_path = self._normalize_resource_path(resource_path, manifest_path)
        if self._generated_modules_dir is not None:
            generated_path = self._generated_modules_dir / relative_path
            if generated_path.is_file():
                return generated_path.resolve(), relative_path
        return super()._resolve_resource_file_path(resource_path, manifest_path, res_dir)

    @override
    def _get_default_output_file(self, relative_file: Path) -> Path:
        if relative_file.suffix == TEAL_SUFFIX:
            return relative_file.with_suffix(LUA_SUFFIX)
        return relative_file

    @override
    def _write_output_file(self, source_path: Path, output_path: Path) -> None:
        if source_path.suffix == LUA_SUFFIX:
            super()._write_output_file(source_path, output_path)
            return
        if source_path.suffix != TEAL_SUFFIX:
            raise RuntimeError(f"WBEACP: Script source must be a Lua (.lua) or Teal (.tl) file: {source_path}.")
        output_path.write_text(self._compile_teal(source_path), encoding="utf-8")

    def _compile_teal(self, source_path: Path) -> str:
        """Type-check a Teal source and generate its Lua.

        Args:
            source_path: The Teal source file.

        Returns:
            The generated Lua source.
        """
        if self._teal_compile is None:
            self._load_teal()
        code, errors = self._teal_compile(self._teal, self._search_path, source_path.as_posix(),
            source_path.read_text(encoding="utf-8"))
        if code is None:
            print(errors)
            raise RuntimeError(f"WBEACP: Failed to compile Teal script {source_path}.")
        return str(code)

    def _load_teal(self) -> None:
        """Load the Teal compiler into an embedded Lua runtime."""
        teal_file = self._teal_dir / "tl.lua"
        if not teal_file.is_file():
            raise RuntimeError(f"WBEACP: Failed to find the Teal compiler at {teal_file}.")
        self._lua = lupa.lua55.LuaRuntime(unpack_returned_tuples=True)
        load_teal = self._lua.eval("function(p_path) package.path = p_path; return (require('tl')) end")
        self._teal = load_teal((self._teal_dir / "?.lua").as_posix())
        self._teal_compile = self._lua.eval(TEAL_COMPILE_FUNCTION)
