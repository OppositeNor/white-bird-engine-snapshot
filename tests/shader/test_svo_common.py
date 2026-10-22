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
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

import numpy as np
import pytest
import slangpy as spy


RANDOM_SEED = 20251003
KERNEL_DIR = Path(__file__).parent / "kernels"

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]
Voxel = tuple[int, int, int]

# ChildrenState values and children_data packing, written from the SVONode::children_data documentation.
STATE_NODE = 0
STATE_EMPTY = 1
STATE_ALLOCATING = 2
STATE_LEAF = 3
STATE_ALLOCATING_DYNAMIC = 4
STATE_NODE_DYNAMIC = 5
STATE_LEAF_DYNAMIC = 6
STATE_BIT_OFFSET = 29
OCCUPANCY_BIT_OFFSET = 21
BLOCK_INDEX_BIT_COUNT = 21

# SVONodeTraceInfo::IndexType values.
INDEX_STATIC_NODE = 0
INDEX_DYNAMIC_NODE = 1
INDEX_STATIC_LEAF = 2
INDEX_DYNAMIC_LEAF = 3

NODE_DTYPE = np.dtype([("children_data", "<u4"), ("data_index", "<u4"), ("parent_index", "<i4"),
    ("original_children_data", "<u4")])
LEAF_DTYPE = np.dtype([("data_index", "<u4"), ("parent_index", "<i4")])
INFO_DTYPE = np.dtype([("origin", "<f4", 3), ("width", "<f4"), ("max_depth", "<u4"), ("max_node_count", "<u4"),
    ("max_leaves_count", "<u4"), ("node_count", "<u4"), ("leaves_count", "<u4"), ("data_count", "<u4"),
    ("max_deferred_task_count", "<u4"), ("max_dynamic_node_count", "<u4"), ("max_dynamic_leaves_count", "<u4"),
    ("dynamic_node_count", "<u4"), ("dynamic_leaves_count", "<u4")])
TRACE_DTYPE = np.dtype([("node_index", "<u4"), ("index_type", "<u4"), ("curr_depth", "<u4"), ("remain_depth", "<i4"),
    ("svo_position", "<f4", 3), ("voxel_index", "<u4")])
BUFFER_USAGE = spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


@pytest.fixture(scope="module")
def svo_common(device: spy.Device) -> spy.Module:
    return spy.Module.load_from_file(device, str(KERNEL_DIR / "svo_common_functions.slang"))


def pack_children_data(p_state: int, p_block_index: int = 0, p_occupancy: int = 0) -> int:
    return (p_state << STATE_BIT_OFFSET) | (p_occupancy << OCCUPANCY_BIT_OFFSET) | p_block_index


def subregion_reference(p_positions: np.ndarray) -> np.ndarray:
    high = (p_positions >= 0.5).astype(np.uint32)
    return (high[..., 0] << 2) | (high[..., 1] << 1) | high[..., 2]


def voxel_sector(p_voxel: Voxel, p_depth: int, p_max_depth: int) -> int:
    """Return the sector, within its parent, of the depth-p_depth node containing p_voxel."""
    bit = p_max_depth - p_depth
    x, y, z = ((coordinate >> bit) & 1 for coordinate in p_voxel)
    return (x << 2) | (y << 1) | z


def voxel_prefix(p_voxel: Voxel, p_depth: int, p_max_depth: int) -> Voxel:
    """Return the top p_depth bits of each coordinate, which identify the depth-p_depth node containing p_voxel."""
    x, y, z = (coordinate >> (p_max_depth - p_depth) for coordinate in p_voxel)
    return (x, y, z)


def voxel_index(p_voxel: Voxel, p_max_depth: int) -> int:
    """Return the flattened voxel coordinate, with x in the lowest p_max_depth bits and z in the highest."""
    return p_voxel[0] | (p_voxel[1] << p_max_depth) | (p_voxel[2] << (2 * p_max_depth))


def shared_prefix_depth(p_first: Voxel, p_second: Voxel, p_max_depth: int) -> int:
    """Return the number of leading coordinate bits two voxels share on every axis."""
    depth = 0
    while depth < p_max_depth and voxel_prefix(p_first, depth + 1, p_max_depth) == voxel_prefix(p_second, depth + 1,
            p_max_depth):
        depth += 1
    return depth


def voxel_center(p_voxel: Voxel, p_max_depth: int) -> np.ndarray:
    return (np.array(p_voxel, dtype=np.float32) + 0.5) / np.float32(1 << p_max_depth)


@dataclass
class StaticSVO:
    """
    Reference static SVO laid out like the construction passes.

    The eight root sectors are nodes 0 to 7 at depth one; every allocation appends an eight-child block. Nodes at depth
    max_depth - 1 own leaf blocks.
    """

    max_depth: int
    nodes: list[list[int]] = field(default_factory=list)
    leaves: list[list[int]] = field(default_factory=list)
    node_of_prefix: dict[tuple[int, Voxel], int] = field(default_factory=dict)
    leaf_of_voxel: dict[Voxel, int] = field(default_factory=dict)

    def node_array(self, p_capacity: int = 0) -> np.ndarray:
        result = np.zeros(max(p_capacity, len(self.nodes)), dtype=NODE_DTYPE)
        result["children_data"] = pack_children_data(STATE_EMPTY)
        for index, (children_data, data_index, parent_index) in enumerate(self.nodes):
            result[index] = (children_data, data_index, parent_index, 0)
        return result

    def leaf_array(self) -> np.ndarray:
        result = np.zeros(max(len(self.leaves), 1), dtype=LEAF_DTYPE)
        for index, (data_index, parent_index) in enumerate(self.leaves):
            result[index] = (data_index, parent_index)
        return result


def build_static_svo(p_voxels: list[Voxel], p_max_depth: int) -> StaticSVO:
    tree = StaticSVO(p_max_depth)
    tree.nodes = [[pack_children_data(STATE_EMPTY), index, -1] for index in range(8)]
    for sector in range(8):
        tree.node_of_prefix[(1, ((sector >> 2) & 1, (sector >> 1) & 1, sector & 1))] = sector
    block_mask = (1 << BLOCK_INDEX_BIT_COUNT) - 1
    for voxel in p_voxels:
        for depth in range(1, p_max_depth):
            node_index = tree.node_of_prefix[(depth, voxel_prefix(voxel, depth, p_max_depth))]
            node = tree.nodes[node_index]
            child_sector = voxel_sector(voxel, depth + 1, p_max_depth)
            if depth == p_max_depth - 1:
                if node[0] >> STATE_BIT_OFFSET == STATE_EMPTY:
                    first_leaf = len(tree.leaves)
                    tree.leaves.extend([[100 + first_leaf + i, node_index] for i in range(8)])
                    node[0] = pack_children_data(STATE_LEAF, first_leaf // 8)
                node[0] |= 1 << (OCCUPANCY_BIT_OFFSET + child_sector)
                tree.leaf_of_voxel[voxel] = (node[0] & block_mask) * 8 + child_sector
            elif node[0] >> STATE_BIT_OFFSET == STATE_EMPTY:
                first_child = len(tree.nodes)
                parent_prefix = voxel_prefix(voxel, depth, p_max_depth)
                for sector in range(8):
                    child_prefix = (parent_prefix[0] * 2 + ((sector >> 2) & 1), parent_prefix[1] * 2 + ((sector >> 1) & 1),
                        parent_prefix[2] * 2 + (sector & 1))
                    tree.node_of_prefix[(depth + 1, child_prefix)] = first_child + sector
                    tree.nodes.append([pack_children_data(STATE_EMPTY), first_child + sector, node_index])
                node[0] = pack_children_data(STATE_NODE, first_child // 8)
    return tree


def random_voxels(p_rng: np.random.Generator, p_count: int, p_max_depth: int) -> list[Voxel]:
    coordinates = p_rng.integers(0, 1 << p_max_depth, (p_count, 3))
    return list(dict.fromkeys((int(x), int(y), int(z)) for x, y, z in coordinates))


def make_buffer(p_device: spy.Device, p_data: np.ndarray) -> spy.Buffer:
    # Each row of p_data is one element; structured arrays are uploaded as raw bytes.
    data = np.ascontiguousarray(p_data)
    return p_device.create_buffer(element_count=len(data), struct_size=data.nbytes // len(data), usage=BUFFER_USAGE,
        data=data.reshape(-1).view(np.uint8))


def read_buffer(p_buffer: spy.Buffer, p_dtype: np.dtype) -> np.ndarray:
    return p_buffer.to_numpy().view(p_dtype)


def make_tree_info(p_max_depth: int, p_max_node_count: int) -> np.ndarray:
    info = np.zeros(1, dtype=INFO_DTYPE)
    info["max_depth"] = p_max_depth
    info["max_node_count"] = p_max_node_count
    return info


def root_traces(p_voxels: list[Voxel], p_max_depth: int, p_remain_depth: int) -> np.ndarray:
    """Return traversals starting at the root sector of each voxel center, built from the documented contract."""
    traces = np.zeros(len(p_voxels), dtype=TRACE_DTYPE)
    for index, voxel in enumerate(p_voxels):
        center = voxel_center(voxel, p_max_depth)
        traces[index] = (voxel_sector(voxel, 1, p_max_depth), INDEX_STATIC_NODE, 1, p_remain_depth,
            np.modf(center * 2.0)[0], voxel_index(voxel, p_max_depth))
    return traces


def run_traverse(p_device: spy.Device, p_load_kernel: KernelLoader, p_nodes: np.ndarray, p_dynamic_nodes: np.ndarray,
        p_tree_info: np.ndarray, p_traces: np.ndarray, p_static: bool, p_dynamic: bool) -> np.ndarray:
    kernel = p_load_kernel("svo_traverse.slang", {"TRAVERSE_STATIC": int(p_static), "TRAVERSE_DYNAMIC": int(p_dynamic)})
    trace_buffer = make_buffer(p_device, p_traces)
    kernel.dispatch(thread_count=[len(p_traces), 1, 1], vars={"nodes": make_buffer(p_device, p_nodes),
        "dynamic_nodes": make_buffer(p_device, p_dynamic_nodes), "tree_info": make_buffer(p_device, p_tree_info),
        "traces": trace_buffer})
    return read_buffer(trace_buffer, TRACE_DTYPE)


def test_svo_struct_layouts(svo_common: spy.Module) -> None:
    # SVONode documents a 16-byte stride; the other dtypes mirror the reflected layouts used by these tests.
    for type_name, dtype in [("SVONode", NODE_DTYPE), ("SVOLeaf", LEAF_DTYPE), ("SVOInfo", INFO_DTYPE),
            ("SVONodeTraceInfo", TRACE_DTYPE)]:
        layout = svo_common.layout.find_type_by_name(type_name).buffer_layout.reflection
        assert layout.stride == dtype.itemsize, type_name
        assert [(field.name, field.offset) for field in layout.fields] == [
            (name, dtype.fields[name][1]) for name in dtype.names], type_name
    assert svo_common.layout.find_type_by_name("SVONode").buffer_layout.reflection.stride == 16


def test_svo_dynamic_leaf_block_layout(svo_common: spy.Module) -> None:
    # The saved parent word is followed by eight leaves.
    layout = svo_common.layout.find_type_by_name("SVODynamicLeafBlock").buffer_layout.reflection
    assert [field.name for field in layout.fields] == ["original_children_data", "leaves"]
    assert layout.fields[1].offset == 4
    assert layout.stride == 4 + 8 * LEAF_DTYPE.itemsize


def test_svo_layout_constants(svo_common: spy.Module) -> None:
    masks = svo_common.svo_children_masks()
    # Bits 31:29 hold the state, 28:21 the occupancy, and 20:0 the block index.
    assert (masks.x, masks.y, masks.z) == (0xFF << OCCUPANCY_BIT_OFFSET, (1 << BLOCK_INDEX_BIT_COUNT) - 1,
        (1 << STATE_BIT_OFFSET) - 1)
    constants = svo_common.svo_layout_constants()
    # Deferred lists store their count first and entry i at offset one plus i.
    assert (constants.x, constants.y, constants.z, constants.w) == (STATE_BIT_OFFSET, OCCUPANCY_BIT_OFFSET, 0, 1)


def test_get_svo_children_state_and_index(svo_common: spy.Module, rng: np.random.Generator) -> None:
    states = rng.integers(0, 7, 1024)
    occupancies = rng.integers(0, 256, 1024)
    block_indices = rng.integers(0, 1 << BLOCK_INDEX_BIT_COUNT, 1024)
    children_data = np.array([pack_children_data(int(state), int(block), int(occupancy))
        for state, block, occupancy in zip(states, block_indices, occupancies)], dtype=np.uint32)
    np.testing.assert_array_equal(svo_common.svo_children_state(children_data, _result="numpy"), states)
    # The occupancy bits must not leak into the child index.
    np.testing.assert_array_equal(svo_common.svo_children_index(children_data, _result="numpy"),
        block_indices.astype(np.uint32) * 8)


def test_get_svo_node_subregion(svo_common: spy.Module, rng: np.random.Generator) -> None:
    positions = rng.uniform(0.0, 1.0, (1024, 3)).astype(np.float32)
    # Exact halves belong to the upper sector, values just below them to the lower one.
    edges = np.array([[0.5, 0.5, 0.5], [np.nextafter(np.float32(0.5), np.float32(0.0))] * 3, [0.0, 0.0, 0.0],
        [0.5, 0.0, 0.0], [0.0, 0.5, 0.0], [0.0, 0.0, 0.5]], dtype=np.float32)
    positions = np.concatenate([positions, edges])
    result = svo_common.get_svo_node_subregion(positions, _result="numpy")
    np.testing.assert_array_equal(result, subregion_reference(positions))
    np.testing.assert_array_equal(result[-6:], [7, 0, 0, 4, 2, 1])


@pytest.mark.parametrize("p_max_depth", [1, 2, 5, 10])
def test_deferred_list_entry_round_trip(svo_common: spy.Module, rng: np.random.Generator, p_max_depth: int) -> None:
    extent = 1 << p_max_depth
    voxels = rng.integers(0, extent, (256, 3)).astype(np.uint32)
    voxels = np.concatenate([voxels, [[0, 0, 0], [extent - 1] * 3]]).astype(np.uint32)
    positions = ((voxels + 0.5) / extent).astype(np.float32)
    depths = np.full(len(voxels), p_max_depth, dtype=np.uint32)
    entries = svo_common.to_deferred_list_entry(positions, depths, _result="numpy")
    # x occupies the lowest max_depth bits and z the highest.
    expected_entries = voxels[:, 0] | (voxels[:, 1] << p_max_depth) | (voxels[:, 2] << (2 * p_max_depth))
    np.testing.assert_array_equal(entries, expected_entries)
    np.testing.assert_array_equal(svo_common.get_deferred_list_entry_voxel(entries, depths, _result="numpy"), voxels)


def test_deferred_list_entry_clamps_to_grid(svo_common: spy.Module) -> None:
    # A position on the far boundary of the unit cube maps to the last voxel instead of overflowing into the next axis.
    max_depth = 4
    positions = np.array([[1.0, 1.0, 1.0], [1.0, 0.0, 0.0], [0.0, 0.0, 0.0]], dtype=np.float32)
    entries = svo_common.to_deferred_list_entry(positions, np.full(3, max_depth, dtype=np.uint32), _result="numpy")
    last = (1 << max_depth) - 1
    np.testing.assert_array_equal(entries, [last | (last << 4) | (last << 8), last, 0])


def assert_trace(p_trace: Any, p_node_index: int, p_index_type: int, p_curr_depth: int, p_remain_depth: int,
        p_svo_position: np.ndarray) -> None:
    assert (p_trace["node_index"], p_trace["index_type"], p_trace["curr_depth"], p_trace["remain_depth"]) == (
        p_node_index, p_index_type, p_curr_depth, p_remain_depth)
    np.testing.assert_allclose(np.array(list(p_trace["svo_position"]), dtype=np.float32), p_svo_position, atol=1e-6)


@pytest.mark.parametrize("p_max_depth", [2, 4, 7])
def test_get_svo_root_trace_info(svo_common: spy.Module, rng: np.random.Generator, p_max_depth: int) -> None:
    for position in rng.uniform(0.0, 1.0, (16, 3)).astype(np.float32):
        trace = svo_common.get_svo_root_trace_info(spy.float3(*position), p_max_depth)
        # The root sector is the stored node at depth one; the position becomes local to that sector.
        assert_trace(trace, int(subregion_reference(position)), INDEX_STATIC_NODE, 1, p_max_depth - 1,
            np.modf(position * 2.0)[0])
        # The voxel index is the containing leaf-level voxel's flattened coordinate.
        voxel = np.floor(position * float(1 << p_max_depth)).astype(np.int64)
        assert trace["voxel_index"] == voxel_index((int(voxel[0]), int(voxel[1]), int(voxel[2])), p_max_depth)


@pytest.mark.parametrize("p_max_depth", [2, 4, 7])
def test_to_trace_info(svo_common: spy.Module, rng: np.random.Generator, p_max_depth: int) -> None:
    for voxel_array in rng.integers(0, 1 << p_max_depth, (16, 3)):
        voxel = (int(voxel_array[0]), int(voxel_array[1]), int(voxel_array[2]))
        entry = voxel[0] | (voxel[1] << p_max_depth) | (voxel[2] << (2 * p_max_depth))
        trace = svo_common.to_trace_info(entry, p_max_depth)
        # The traversal targets the voxel's center from its root sector.
        center = voxel_center(voxel, p_max_depth)
        assert_trace(trace, voxel_sector(voxel, 1, p_max_depth), INDEX_STATIC_NODE, 1, p_max_depth - 1,
            np.modf(center * 2.0)[0])
        assert trace["voxel_index"] == entry


def expected_stop_depth(p_tree: StaticSVO, p_voxel: Voxel) -> int:
    """
    Return the depth of the node an unoccupied voxel's traversal stops at.

    A node at depth d below the roots exists exactly when some inserted voxel shares the top d - 1 coordinate bits, and
    nodes deeper than max_depth - 1 are leaves.
    """
    longest_shared = max((shared_prefix_depth(p_voxel, voxel, p_tree.max_depth) for voxel in p_tree.leaf_of_voxel),
        default=0)
    return min(p_tree.max_depth - 1, longest_shared + 1)


def static_query_voxels(p_rng: np.random.Generator, p_tree: StaticSVO, p_count: int) -> list[Voxel]:
    """Return inserted voxels, their unoccupied neighbors in the same leaf block, and random voxels."""
    inserted = list(p_tree.leaf_of_voxel)
    neighbors = [(voxel[0] ^ 1, voxel[1], voxel[2]) for voxel in inserted]
    return list(dict.fromkeys(inserted + neighbors + random_voxels(p_rng, p_count, p_tree.max_depth)))


@pytest.mark.parametrize("p_max_depth", [2, 3, 5])
@pytest.mark.parametrize("p_remain_depth", [-1, 1, 2])
def test_traverse_svo_static(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_max_depth: int, p_remain_depth: int) -> None:
    tree = build_static_svo(random_voxels(rng, 24, p_max_depth), p_max_depth)
    queries = static_query_voxels(rng, tree, 64)
    nodes = tree.node_array()
    result = run_traverse(device, load_kernel, nodes, nodes, make_tree_info(p_max_depth, len(nodes)),
        root_traces(queries, p_max_depth, p_remain_depth), False, False)
    for voxel, trace in zip(queries, result):
        if voxel in tree.leaf_of_voxel:
            stop_depth = p_max_depth
            stop = (tree.leaf_of_voxel[voxel], INDEX_STATIC_LEAF)
        else:
            stop_depth = expected_stop_depth(tree, voxel)
            stop = (tree.node_of_prefix[(stop_depth, voxel_prefix(voxel, stop_depth, p_max_depth))], INDEX_STATIC_NODE)
        steps = stop_depth - 1
        if p_remain_depth >= 0 and p_remain_depth < steps:
            steps = p_remain_depth
            stop_depth = 1 + steps
            stop = (tree.node_of_prefix[(stop_depth, voxel_prefix(voxel, stop_depth, p_max_depth))], INDEX_STATIC_NODE)
        center = voxel_center(voxel, p_max_depth)
        assert_trace(trace, stop[0], stop[1], stop_depth, p_remain_depth - steps if p_remain_depth >= 0 else -1,
            np.modf(center * float(1 << stop_depth))[0])


@pytest.mark.parametrize("p_max_depth", [2, 3, 5])
@pytest.mark.parametrize("p_remain_depth", [-1, 1, 2])
def test_traverse_static_svo(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_max_depth: int, p_remain_depth: int) -> None:
    tree = build_static_svo(random_voxels(rng, 24, p_max_depth), p_max_depth)
    queries = static_query_voxels(rng, tree, 64)
    nodes = tree.node_array()
    result = run_traverse(device, load_kernel, nodes, nodes, make_tree_info(p_max_depth, len(nodes)),
        root_traces(queries, p_max_depth, p_remain_depth), True, False)
    for voxel, trace in zip(queries, result):
        # Static traversal stops at the first node that is not NODE, so leaf blocks are never entered.
        stop_depth = p_max_depth - 1 if voxel in tree.leaf_of_voxel else expected_stop_depth(tree, voxel)
        steps = stop_depth - 1
        if p_remain_depth >= 0 and p_remain_depth <= steps:
            steps = p_remain_depth
            stop_depth = 1 + steps
            remain = 0
        else:
            remain = p_remain_depth - steps if p_remain_depth >= 0 else -1
        node_index = tree.node_of_prefix[(stop_depth, voxel_prefix(voxel, stop_depth, p_max_depth))]
        center = voxel_center(voxel, p_max_depth)
        assert_trace(trace, node_index, INDEX_STATIC_NODE, stop_depth, remain, np.modf(center * float(1 << stop_depth))[0])


def dynamic_svo() -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Return static nodes, dynamic nodes, and tree info for a depth-three tree whose root sector 0 has dynamic children.

    Root sector 0 is NODE_DYNAMIC with the first dynamic node block; dynamic node 3 owns dynamic leaf block 2 with only
    sector 5 occupied, and every other dynamic node is EMPTY. Root sector 7 is a static node whose children are EMPTY.
    """
    max_node_count = 16
    nodes = np.zeros(max_node_count, dtype=NODE_DTYPE)
    nodes["children_data"] = pack_children_data(STATE_EMPTY)
    # NODE_DYNAMIC stores the block index offset by max_node_count.
    nodes[0]["children_data"] = pack_children_data(STATE_NODE_DYNAMIC, max_node_count // 8)
    nodes[7]["children_data"] = pack_children_data(STATE_NODE, 1)
    dynamic_nodes = np.zeros(8, dtype=NODE_DTYPE)
    dynamic_nodes["children_data"] = pack_children_data(STATE_EMPTY)
    dynamic_nodes[3]["children_data"] = pack_children_data(STATE_LEAF_DYNAMIC, 2, 1 << 5)
    return nodes, dynamic_nodes, make_tree_info(3, max_node_count)


# (voxel, expected with dynamic traversal, expected without), each expectation being (index, index type, depth).
DYNAMIC_TRAVERSAL_CASES = [
    # Root sector 0, dynamic node 3 (sector bits 011), occupied leaf sector 5 (101).
    ((1, 2, 3), (16 + 5, INDEX_DYNAMIC_LEAF, 3), (0, INDEX_STATIC_NODE, 1)),
    # Same dynamic node, unoccupied leaf sector 4 (100).
    ((1, 2, 2), (3, INDEX_DYNAMIC_NODE, 2), (0, INDEX_STATIC_NODE, 1)),
    # Root sector 0, EMPTY dynamic node 6 (110).
    ((3, 3, 0), (6, INDEX_DYNAMIC_NODE, 2), (0, INDEX_STATIC_NODE, 1)),
    # Root sector 7, static EMPTY child 8 + 2 (010).
    ((4, 6, 4), (8 + 2, INDEX_STATIC_NODE, 2), (8 + 2, INDEX_STATIC_NODE, 2)),
    # EMPTY root sector 4.
    ((5, 1, 1), (4, INDEX_STATIC_NODE, 1), (4, INDEX_STATIC_NODE, 1)),
]


@pytest.mark.parametrize("p_traverse_dynamic", [True, False])
def test_traverse_svo_dynamic(device: spy.Device, load_kernel: KernelLoader, p_traverse_dynamic: bool) -> None:
    nodes, dynamic_nodes, tree_info = dynamic_svo()
    voxels = [case[0] for case in DYNAMIC_TRAVERSAL_CASES]
    result = run_traverse(device, load_kernel, nodes, dynamic_nodes, tree_info, root_traces(voxels, 3, -1), False,
        p_traverse_dynamic)
    for (voxel, with_dynamic, without_dynamic), trace in zip(DYNAMIC_TRAVERSAL_CASES, result):
        index, index_type, depth = with_dynamic if p_traverse_dynamic else without_dynamic
        assert_trace(trace, index, index_type, depth, -1, np.modf(voxel_center(voxel, 3) * float(1 << depth))[0])


def test_traverse_svo_resumes_from_dynamic_node(device: spy.Device, load_kernel: KernelLoader) -> None:
    # A traversal saved at a dynamic node continues in dynamic storage.
    nodes, dynamic_nodes, tree_info = dynamic_svo()
    voxel = (1, 2, 3)
    traces = np.zeros(1, dtype=TRACE_DTYPE)
    traces[0] = (3, INDEX_DYNAMIC_NODE, 2, 4, np.modf(voxel_center(voxel, 3) * 4.0)[0], voxel_index(voxel, 3))
    result = run_traverse(device, load_kernel, nodes, dynamic_nodes, tree_info, traces, False, True)
    assert_trace(result[0], 16 + 5, INDEX_DYNAMIC_LEAF, 3, 3, np.modf(voxel_center(voxel, 3) * 8.0)[0])


@pytest.mark.parametrize("p_max_depth", [2, 4])
def test_is_svo_leaf_occupied(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_max_depth: int) -> None:
    tree = build_static_svo(random_voxels(rng, 40, p_max_depth), p_max_depth)
    leaves = tree.leaf_array()
    nodes = tree.node_array()
    tree_info = make_tree_info(p_max_depth, len(nodes))
    tree_info["node_count"] = len(tree.nodes)
    occupied = make_buffer(device, np.zeros(len(tree.leaves), dtype=np.uint32))
    load_kernel("svo_leaf_occupied.slang", {}).dispatch(thread_count=[len(tree.leaves), 1, 1], vars={
        "nodes": make_buffer(device, nodes), "leaves": make_buffer(device, leaves),
        "tree_info": make_buffer(device, tree_info), "occupied": occupied})
    expected = np.zeros(len(tree.leaves), dtype=np.uint32)
    expected[list(tree.leaf_of_voxel.values())] = 1
    np.testing.assert_array_equal(read_buffer(occupied, np.uint32), expected)
