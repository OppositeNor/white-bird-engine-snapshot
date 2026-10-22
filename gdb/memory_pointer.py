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
"""GDB pretty-printers for WhiteBirdEngine memory reference types."""

import re

import gdb


_POINTER_TYPE_RE = re.compile(r"(^|::)(Unique|Ref|RefWeak|RefRaw|RefRes)<")
_MEM_NULL = 0


class MemoryPointerPrinter:
    """Pretty-printer for Unique, Ref, RefWeak, RefRaw and aliases."""

    def __init__(self, value):
        self.value = value
        self.type_name = _type_name(value.type)
        self.kind = _pointer_kind(self.type_name)

    def to_string(self):
        mem_id, allocator, control_block = self._state()
        if control_block is not None and _is_null(control_block):
            return f"{self.type_name} = null control_block"
        if _is_null(allocator) or _to_int(mem_id) == _MEM_NULL:
            return f"{self.type_name} = null"

        pointee = _resolve_pointee(self.value, mem_id, allocator)
        if pointee is None:
            return f"{self.type_name} mem_id={mem_id}, allocator={allocator}"
        return f"{self.type_name} mem_id={mem_id}, ptr={pointee}"

    def children(self):
        mem_id, allocator, control_block = self._state()
        if control_block is not None:
            yield "control_block", control_block
            if not _is_null(control_block):
                yield "weak_ref_counter", control_block.dereference()["weak_ref_counter"]
                yield "strong_ref_counter", control_block.dereference()["strong_ref_counter"]

        yield "mem_id", mem_id
        yield "allocator", allocator

        pointee = _resolve_pointee(self.value, mem_id, allocator)
        if pointee is not None:
            yield "pointee", pointee
            if not _is_null(pointee):
                yield "object", pointee.dereference()

    def _state(self):
        if self.kind in ("Unique", "RefRaw", "RefRes"):
            return self.value["mem_id"], self.value["allocator"], None

        control_block = self.value["control_block"]
        if _is_null(control_block):
            return _zero_mem_id(), _null_void_pointer(), control_block

        block = control_block.dereference()
        return block["mem_id"], block["allocator"], control_block


def _type_name(value_type):
    stripped_type = value_type.strip_typedefs()
    while stripped_type.code in (gdb.TYPE_CODE_REF, gdb.TYPE_CODE_RVALUE_REF):
        stripped_type = stripped_type.target().strip_typedefs()
    return str(stripped_type)


def _pointer_kind(type_name):
    match = _POINTER_TYPE_RE.search(type_name)
    if match is None:
        return None
    return match.group(2)


def _lookup_memory_pointer(value):
    type_name = _type_name(value.type)
    if _POINTER_TYPE_RE.search(type_name) is None:
        return None
    return MemoryPointerPrinter(value)


def _resolve_pointee(value, mem_id, allocator):
    if _is_null(allocator) or _to_int(mem_id) == _MEM_NULL:
        return _null_typed_pointer(value)

    resolved = _resolve_fixed_size_pool_pointee(mem_id, allocator)
    if resolved is None:
        resolved = _mem_id_as_void_pointer(mem_id)
    if resolved is None:
        resolved = _call_allocator_get(mem_id, allocator)
    if resolved is None:
        return None

    pointee_type = _pointee_type(value.type)
    if pointee_type is None:
        return resolved

    try:
        return resolved.cast(pointee_type.pointer())
    except gdb.error:
        return resolved


def _call_allocator_get(mem_id, allocator):
    try:
        allocator_type = allocator.type.target().strip_typedefs()
        allocator_address = _to_int(allocator)
        mem_id_value = _to_int(mem_id)
        expression = f"(({allocator_type}*){allocator_address})->try_get((WhiteBirdEngine::MemID){mem_id_value})"
        return gdb.parse_and_eval(expression)
    except (gdb.error, RuntimeError):
        return None


def _resolve_fixed_size_pool_pointee(mem_id, allocator):
    allocator_value = _dynamic_allocator_value(allocator)
    if allocator_value is None or "HeapAllocatorFixedSizePool" not in str(allocator_value.type):
        return None

    try:
        internal_id = _to_int(mem_id)
        max_obj = _to_int(allocator_value["max_obj"])
        if internal_id == _MEM_NULL or internal_id > max_obj:
            return _null_void_pointer()

        data_index = _to_int((allocator_value["index_chunk_start"] + internal_id - 1).dereference())
        if data_index == _MEM_NULL:
            return _null_void_pointer()

        element_size = _to_int(allocator_value["element_size"])
        offset = (data_index - 1) * element_size
        return (allocator_value["mem_chunk"] + offset).cast(gdb.lookup_type("void").pointer())
    except (gdb.error, RuntimeError, TypeError, ValueError):
        return None


def _dynamic_allocator_value(allocator):
    try:
        dynamic_type = allocator.dereference().dynamic_type.strip_typedefs()
        return allocator.cast(dynamic_type.pointer()).dereference()
    except (gdb.error, RuntimeError):
        try:
            return allocator.dereference()
        except (gdb.error, RuntimeError):
            return None


def _pointee_type(value_type):
    stripped_type = value_type.strip_typedefs()
    while stripped_type.code in (gdb.TYPE_CODE_REF, gdb.TYPE_CODE_RVALUE_REF):
        stripped_type = stripped_type.target().strip_typedefs()
    try:
        return stripped_type.template_argument(0).strip_typedefs()
    except (gdb.error, RuntimeError):
        return None


def _to_int(value):
    try:
        return int(value)
    except (gdb.error, TypeError, ValueError):
        return _MEM_NULL


def _is_null(value):
    return _to_int(value) == _MEM_NULL


def _zero_mem_id():
    try:
        return gdb.Value(_MEM_NULL).cast(gdb.lookup_type("WhiteBirdEngine::MemID"))
    except gdb.error:
        return gdb.Value(_MEM_NULL)


def _null_void_pointer():
    return gdb.Value(_MEM_NULL).cast(gdb.lookup_type("void").pointer())


def _mem_id_as_void_pointer(mem_id):
    return gdb.Value(_to_int(mem_id)).cast(gdb.lookup_type("void").pointer())


def _null_typed_pointer(value):
    pointee_type = _pointee_type(value.type)
    if pointee_type is None:
        return _null_void_pointer()
    return gdb.Value(_MEM_NULL).cast(pointee_type.pointer())


def build_pretty_printer():
    return _lookup_memory_pointer


def register_memory_pointer_printers(objfile=None):
    if objfile is None:
        objfile = gdb.current_objfile()

    target_printers = objfile.pretty_printers if objfile is not None else gdb.pretty_printers
    target_printers[:] = [printer for printer in target_printers if getattr(printer, "name", None) != "white-bird-engine"]

    printer = build_pretty_printer()
    printer.name = "white-bird-engine"
    target_printers.append(printer)


register_memory_pointer_printers(gdb)
