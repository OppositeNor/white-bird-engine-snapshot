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
from typing import Callable

import numpy as np
import pytest
import slangpy as spy

from test_svo_common import (LEAF_DTYPE, OCCUPANCY_BIT_OFFSET, STATE_BIT_OFFSET, STATE_EMPTY, STATE_LEAF,
    STATE_LEAF_DYNAMIC, STATE_NODE_DYNAMIC, StaticSVO, build_static_svo, make_buffer, make_tree_info, pack_children_data,
    random_voxels, read_buffer, voxel_prefix)


RANDOM_SEED = 20261024

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]

# Elements nothing is expected to write; not a valid occupancy.
UNWRITTEN = -1.0


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def make_occupancy_buffer(p_device: spy.Device, p_values: np.ndarray) -> spy.Buffer:
    return make_buffer(p_device, p_values.astype(np.float32))


def read_occupancy_buffer(p_buffer: spy.Buffer, p_count: int) -> np.ndarray:
    return read_buffer(p_buffer, np.float32)[:p_count]


def run_propagation(p_device: spy.Device, p_load_kernel: KernelLoader, p_nodes: np.ndarray, p_leaves: np.ndarray,
        p_tree_info: np.ndarray, p_occupancy: spy.Buffer, p_child_depth: int) -> None:
    p_load_kernel("svogi_occupancy_propagation.slang", {}).dispatch(thread_count=[-(-len(p_nodes) // 8), 1, 1], vars={
        "nodes": make_buffer(p_device, p_nodes), "leaves": make_buffer(p_device, p_leaves),
        "tree_info": make_buffer(p_device, p_tree_info), "occupancy": p_occupancy, "child_depth": p_child_depth})


def run_injection(p_device: spy.Device, p_load_kernel: KernelLoader, p_nodes: np.ndarray, p_leaves: np.ndarray,
        p_tree_info: np.ndarray, p_occupancy: spy.Buffer, p_inject_parents: bool, p_thread_count: int) -> None:
    p_load_kernel("svogi_occupancy_injection.slang", {"INJECT_PARENTS": int(p_inject_parents)}).dispatch(
        thread_count=[p_thread_count, 1, 1], vars={"nodes": make_buffer(p_device, p_nodes),
        "leaves": make_buffer(p_device, p_leaves), "tree_info": make_buffer(p_device, p_tree_info), "occupancy": p_occupancy,
        "invocation_count": p_thread_count})


def injection_leaves(p_tree: StaticSVO, p_dynamic_leaves: np.ndarray, p_node_count: int) -> np.ndarray:
    """The static leaves followed by p_dynamic_leaves, with data indices past the nodes' so the two never share an element."""
    leaves = np.concatenate([p_tree.leaf_array()[:len(p_tree.leaves)], p_dynamic_leaves])
    leaves["data_index"] = p_node_count + np.arange(len(leaves))
    return leaves


def occupancy_count(p_children_data: int) -> int:
    return bin((p_children_data >> OCCUPANCY_BIT_OFFSET) & 0xFF).count("1")


def supersede_leaf_block(p_tree: StaticSVO, p_voxel: tuple[int, int, int]) -> tuple[np.ndarray, int, int]:
    """
    Move the leaf block of p_voxel's parent into one dynamic block, which follows the static leaves, keeping only that
    voxel occupied; return the dynamic leaves, the parent, and the static block.
    """
    static_leaf = p_tree.leaf_of_voxel[p_voxel]
    parent = p_tree.leaves[static_leaf][1]
    p_tree.nodes[parent][0] = pack_children_data(STATE_LEAF_DYNAMIC, len(p_tree.leaves) // 8, 1 << (static_leaf % 8))
    dynamic_leaves = np.zeros(8, dtype=LEAF_DTYPE)
    dynamic_leaves["parent_index"] = parent
    return dynamic_leaves, parent, static_leaf // 8 * 8


def add_unallocated_leaf_block(p_nodes: np.ndarray, p_leaves: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """
    Append, past the allocated nodes and leaves, a parent whose fully occupied dynamic leaf block follows the leaves, so
    that only the bounds of the SVO's totals keep them from being injected.
    """
    nodes = np.concatenate([p_nodes, np.zeros(1, dtype=p_nodes.dtype)])
    nodes[-1] = (pack_children_data(STATE_LEAF_DYNAMIC, len(p_leaves) // 8, 0xFF), len(nodes) + len(p_leaves) + 8, 0, 0)
    leaves = np.concatenate([p_leaves, np.zeros(8, dtype=LEAF_DTYPE)])
    leaves[-8:]["data_index"] = len(nodes) + len(p_leaves) + np.arange(8)
    leaves[-8:]["parent_index"] = len(nodes) - 1
    return nodes, leaves


@pytest.mark.parametrize("p_max_depth", [2, 4])
def test_inject_svo_leaf_occupancy_writes_occupied_leaves(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator, p_max_depth: int) -> None:
    voxels = random_voxels(rng, 40, p_max_depth)
    tree = build_static_svo(voxels, p_max_depth)
    nodes = tree.node_array()
    leaves = injection_leaves(tree, np.zeros(0, dtype=LEAF_DTYPE), len(nodes))
    nodes, leaves = add_unallocated_leaf_block(nodes, leaves)
    occupancy = make_occupancy_buffer(device, np.full(len(nodes) + len(leaves) + 1, UNWRITTEN, dtype=np.float32))
    # Threads past the last allocated leaf write nothing.
    run_injection(device, load_kernel, nodes, leaves, make_static_tree_info(tree), occupancy, False, len(leaves) + 13)
    expected = np.full(len(nodes) + len(leaves) + 1, UNWRITTEN, dtype=np.float32)
    for leaf_index in tree.leaf_of_voxel.values():
        expected[leaves[leaf_index]["data_index"]] = 1.0
    np.testing.assert_array_equal(read_occupancy_buffer(occupancy, len(expected)), expected)


def test_inject_svo_leaf_occupancy_skips_superseded_leaves(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    max_depth = 3
    voxels = random_voxels(rng, 20, max_depth)
    tree = build_static_svo(voxels, max_depth)
    dynamic_leaves, _, static_block = supersede_leaf_block(tree, voxels[0])
    nodes = tree.node_array(len(tree.nodes) + 8)
    leaves = injection_leaves(tree, dynamic_leaves, len(nodes))
    tree_info = make_static_tree_info(tree)
    tree_info["dynamic_node_count"] = 8
    tree_info["dynamic_leaves_count"] = 8
    occupancy = make_occupancy_buffer(device, np.full(len(nodes) + len(leaves), UNWRITTEN, dtype=np.float32))
    run_injection(device, load_kernel, nodes, leaves, tree_info, occupancy, False, len(leaves))
    expected = np.full(len(nodes) + len(leaves), UNWRITTEN, dtype=np.float32)
    for leaf_index in tree.leaf_of_voxel.values():
        if leaf_index // 8 * 8 != static_block:
            expected[leaves[leaf_index]["data_index"]] = 1.0
    # Only the moved voxel's dynamic leaf is written in place of its superseded static block.
    expected[leaves[len(tree.leaves) + tree.leaf_of_voxel[voxels[0]] % 8]["data_index"]] = 1.0
    np.testing.assert_array_equal(read_occupancy_buffer(occupancy, len(expected)), expected)


@pytest.mark.parametrize("p_max_depth", [2, 4])
def test_inject_svo_leaf_parent_occupancy_writes_occupied_fraction(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator, p_max_depth: int) -> None:
    voxels = random_voxels(rng, 40, p_max_depth)
    tree = build_static_svo(voxels, p_max_depth)
    nodes = tree.node_array()
    leaves = injection_leaves(tree, np.zeros(0, dtype=LEAF_DTYPE), len(nodes))
    nodes, leaves = add_unallocated_leaf_block(nodes, leaves)
    occupancy = make_occupancy_buffer(device, np.full(len(nodes) + len(leaves) + 1, UNWRITTEN, dtype=np.float32))
    # Threads past the last allocated block write nothing.
    run_injection(device, load_kernel, nodes, leaves, make_static_tree_info(tree), occupancy, True, len(leaves) // 8 + 5)
    expected = np.full(len(nodes) + len(leaves) + 1, UNWRITTEN, dtype=np.float32)
    for node_index, (children_data, data_index, _) in enumerate(tree.nodes):
        if children_data >> STATE_BIT_OFFSET == STATE_LEAF:
            expected[data_index] = occupancy_count(children_data) / 8.0
    np.testing.assert_array_equal(read_occupancy_buffer(occupancy, len(expected)), expected)


def test_inject_svo_leaf_parent_occupancy_skips_superseded_blocks(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    max_depth = 3
    voxels = random_voxels(rng, 20, max_depth)
    tree = build_static_svo(voxels, max_depth)
    dynamic_leaves, parent, _ = supersede_leaf_block(tree, voxels[0])
    nodes = tree.node_array(len(tree.nodes) + 8)
    leaves = injection_leaves(tree, dynamic_leaves, len(nodes))
    tree_info = make_static_tree_info(tree)
    tree_info["dynamic_node_count"] = 8
    tree_info["dynamic_leaves_count"] = 8
    occupancy = make_occupancy_buffer(device, np.full(len(nodes) + len(leaves), UNWRITTEN, dtype=np.float32))
    # Inject only the static blocks: the superseded one must not write the parent its dynamic block now owns.
    run_injection(device, load_kernel, nodes, leaves, tree_info, occupancy, True, len(tree.leaves) // 8)
    result = read_occupancy_buffer(occupancy, len(nodes) + len(leaves))
    assert result[nodes[parent]["data_index"]] == UNWRITTEN
    # The dynamic block writes it.
    run_injection(device, load_kernel, nodes, leaves, tree_info, occupancy, True, len(leaves) // 8)
    result = read_occupancy_buffer(occupancy, len(nodes) + len(leaves))
    assert result[nodes[parent]["data_index"]] == 1.0 / 8.0


def node_depths(p_tree: StaticSVO) -> dict[int, int]:
    return {node_index: depth for (depth, _), node_index in p_tree.node_of_prefix.items()}


def expected_occupancy(p_tree: StaticSVO, p_voxels: list[tuple[int, int, int]]) -> np.ndarray:
    """Occupancy of every node with children: the fraction of the leaves below it that are occupied."""
    result = np.full(len(p_tree.nodes), UNWRITTEN, dtype=np.float32)
    for (depth, prefix), node_index in p_tree.node_of_prefix.items():
        if p_tree.nodes[node_index][0] >> STATE_BIT_OFFSET == STATE_EMPTY:
            continue
        count = sum(1 for voxel in p_voxels if voxel_prefix(voxel, depth, p_tree.max_depth) == prefix)
        result[node_index] = count / 8.0 ** (p_tree.max_depth - depth)
    return result


def make_static_tree_info(p_tree: StaticSVO) -> np.ndarray:
    tree_info = make_tree_info(p_tree.max_depth)
    tree_info["node_count"] = len(p_tree.nodes)
    tree_info["leaves_count"] = len(p_tree.leaves)
    return tree_info


@pytest.mark.parametrize("p_max_depth", [3, 4, 5])
def test_propagate_svo_occupancy_writes_one_level_per_dispatch(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator, p_max_depth: int) -> None:
    voxels = random_voxels(rng, 60, p_max_depth)
    tree = build_static_svo(voxels, p_max_depth)
    expected = expected_occupancy(tree, voxels)
    depths = node_depths(tree)
    # The injection has written the parents of the leaves.
    initial = np.full(len(tree.nodes), UNWRITTEN, dtype=np.float32)
    for node_index, depth in depths.items():
        if depth == p_max_depth - 1:
            initial[node_index] = expected[node_index]
    occupancy = make_occupancy_buffer(device, initial)
    nodes = tree.node_array()
    for child_depth in range(p_max_depth - 1, 1, -1):
        run_propagation(device, load_kernel, nodes, tree.leaf_array(), make_static_tree_info(tree), occupancy, child_depth)
        # Each dispatch writes only the level above its children; shallower levels are still unwritten.
        written = np.array([UNWRITTEN if depths[i] < child_depth - 1 else expected[i] for i in range(len(tree.nodes))],
            dtype=np.float32)
        np.testing.assert_allclose(read_occupancy_buffer(occupancy, len(tree.nodes)), written, rtol=1e-6)
    # Every root sector with voxels below it has its fraction of occupied leaves.
    np.testing.assert_allclose(read_occupancy_buffer(occupancy, len(tree.nodes)), expected, rtol=1e-6)


def test_propagate_svo_occupancy_reads_the_referenced_dynamic_block(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    max_depth = 3
    voxels = random_voxels(rng, 20, max_depth)
    tree = build_static_svo(voxels, max_depth)
    expected = expected_occupancy(tree, voxels)
    # Replace the children of the first voxel's root sector with a dynamic block, which follows the static nodes, whose
    # only node with children is its first one.
    sector = tree.node_of_prefix[(1, voxel_prefix(voxels[0], 1, max_depth))]
    dynamic_block = len(tree.nodes)
    nodes = tree.node_array(dynamic_block + 8)
    nodes[sector]["children_data"] = pack_children_data(STATE_NODE_DYNAMIC, dynamic_block // 8)
    nodes[dynamic_block:]["data_index"] = np.arange(dynamic_block, dynamic_block + 8)
    nodes[dynamic_block:]["parent_index"] = sector
    nodes[dynamic_block]["children_data"] = pack_children_data(STATE_LEAF, 0, 1)
    tree_info = make_static_tree_info(tree)
    tree_info["dynamic_node_count"] = 8
    # The injection has written the parents of the leaves, including those of the superseded block.
    initial = np.full(dynamic_block + 8, UNWRITTEN, dtype=np.float32)
    for node_index in range(len(tree.nodes)):
        if tree.nodes[node_index][0] >> STATE_BIT_OFFSET == STATE_LEAF:
            initial[node_index] = expected[node_index]
    initial[dynamic_block] = 0.5
    occupancy = make_occupancy_buffer(device, initial)
    run_propagation(device, load_kernel, nodes, tree.leaf_array(), tree_info, occupancy, 2)
    result = read_occupancy_buffer(occupancy, dynamic_block + 8)
    # The superseded static block does not write its former parent.
    assert result[sector] == pytest.approx(0.5 / 8.0)
    # The other root sectors still read their static blocks.
    for other_sector in range(8):
        if other_sector != sector:
            assert result[other_sector] == pytest.approx(expected[other_sector], rel=1e-6)
