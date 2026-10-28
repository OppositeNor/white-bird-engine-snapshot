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
from dataclasses import dataclass
from typing import Callable

import numpy as np
import pytest
import slangpy as spy

from test_svo_common import (BLOCK_INDEX_BIT_COUNT, LEAF_DTYPE, NODE_DTYPE, OCCUPANCY_BIT_OFFSET, STATE_ALLOCATING,
    STATE_BIT_OFFSET, STATE_EMPTY, STATE_LEAF, STATE_NODE, TRACE_DTYPE, Voxel, make_buffer, make_tree_info,
    pack_children_data, random_voxels, read_buffer, root_traces, subregion_reference, voxel_prefix, voxel_sector)


RANDOM_SEED = 20251003

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]

# (version, header) of each SVO utils implementation. Version 1 publishes children within the pass; version 2 leaves
# new allocations ALLOCATING until a separate state-update pass publishes them.
UTILS_VERSIONS = [(1, "svo/svo_utils.slang"), (2, "svo_2/svo_utils.slang")]
UTILS_VERSION_IDS = ["svo", "svo_2"]

OPERATION_RESERVE_NODES = 0
OPERATION_RESERVE_LEAVES = 1
OPERATION_CREATE_CHILDREN_NODE = 2
OPERATION_CREATE_CHILDREN_LEAF = 3
OPERATION_CREATE_ENTRY = 4
OPERATION_OCCUPY_LEAF = 5
OPERATION_LOAD_NODE = 6
OPERATION_PUBLISH_CHILDREN_DATA = 7
OPERATION_VOXEL_ALLOCATE = 8

BLOCK_MASK = (1 << BLOCK_INDEX_BIT_COUNT) - 1
OCCUPANCY_MASK = 0xFF << OCCUPANCY_BIT_OFFSET
PAYLOAD_MASK = (1 << STATE_BIT_OFFSET) - 1


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


@dataclass
class SVOState:
    """Buffers of an SVO under construction, as read back after a dispatch."""

    nodes: np.ndarray
    leaves: np.ndarray
    tree_info: np.ndarray
    traces: np.ndarray
    results: np.ndarray


def reset_svo(p_max_depth: int, p_node_capacity: int, p_leaf_capacity: int) -> SVOState:
    """Return the state left by the reset pass: eight EMPTY root sectors owning data blocks 0 to 7."""
    nodes = np.zeros(p_node_capacity, dtype=NODE_DTYPE)
    nodes["children_data"] = pack_children_data(STATE_EMPTY)
    nodes["data_index"][:8] = np.arange(8)
    nodes["parent_index"][:8] = -1
    tree_info = make_tree_info(p_max_depth)
    tree_info["node_count"] = 8
    tree_info["data_count"] = 8
    return SVOState(nodes, np.zeros(p_leaf_capacity, dtype=LEAF_DTYPE), tree_info, np.zeros(1, dtype=TRACE_DTYPE),
        np.zeros((1, 4), dtype=np.uint32))


def run_operation(p_device: spy.Device, p_load_kernel: KernelLoader, p_version: tuple[int, str], p_operation: int,
        p_state: SVOState, p_invocation_count: int, p_arguments: np.ndarray | None = None,
        p_positions: np.ndarray | None = None, p_is_leaf: bool = False) -> SVOState:
    kernel = p_load_kernel("svo_utils_operations.slang", {"SVO_UTILS_HEADER": f'"{p_version[1]}"',
        "SVO_UTILS_VERSION": p_version[0], "OPERATION": p_operation, "IS_LEAF": int(p_is_leaf)})
    arguments = np.zeros((p_invocation_count, 2), dtype=np.uint32)
    if p_arguments is not None:
        arguments[:, :p_arguments.reshape(p_invocation_count, -1).shape[1]] = p_arguments.reshape(p_invocation_count, -1)
    positions = np.zeros((p_invocation_count, 4), dtype=np.float32)
    if p_positions is not None:
        positions[:, :3] = p_positions
    traces = p_state.traces if len(p_state.traces) == p_invocation_count else np.zeros(p_invocation_count, TRACE_DTYPE)
    buffers = {"nodes": make_buffer(p_device, p_state.nodes), "leaves": make_buffer(p_device, p_state.leaves),
        "tree_info": make_buffer(p_device, p_state.tree_info), "arguments": make_buffer(p_device, arguments),
        "positions": make_buffer(p_device, positions), "traces": make_buffer(p_device, traces),
        "results": make_buffer(p_device, np.zeros((p_invocation_count, 4), dtype=np.uint32))}
    kernel.dispatch(thread_count=[p_invocation_count, 1, 1], vars=buffers)
    return SVOState(read_buffer(buffers["nodes"], NODE_DTYPE), read_buffer(buffers["leaves"], LEAF_DTYPE),
        read_buffer(buffers["tree_info"], p_state.tree_info.dtype), read_buffer(buffers["traces"], TRACE_DTYPE),
        read_buffer(buffers["results"], np.uint32).reshape(-1, 4))


def children_state(p_children_data: int) -> int:
    return int(p_children_data) >> STATE_BIT_OFFSET


@pytest.mark.parametrize("p_version", UTILS_VERSIONS, ids=UTILS_VERSION_IDS)
@pytest.mark.parametrize(("p_operation", "p_counter", "p_initial_count"),
    [(OPERATION_RESERVE_NODES, "node_count", 8), (OPERATION_RESERVE_LEAVES, "leaves_count", 0)])
def test_try_reserve_svo_blocks(device: spy.Device, load_kernel: KernelLoader, p_version: tuple[int, str],
        p_operation: int, p_counter: str, p_initial_count: int) -> None:
    # Concurrent reservations receive disjoint eight-element blocks that tile the counter's growth.
    invocation_count = 100
    state = reset_svo(4, 8 + 8 * invocation_count, 8 * invocation_count)
    result = run_operation(device, load_kernel, p_version, p_operation, state, invocation_count)
    assert np.all(result.results[:, 0] == 1)
    np.testing.assert_array_equal(np.sort(result.results[:, 1]), p_initial_count + 8 * np.arange(invocation_count))
    assert result.tree_info[p_counter][0] == p_initial_count + 8 * invocation_count


@pytest.mark.parametrize("p_version", UTILS_VERSIONS, ids=UTILS_VERSION_IDS)
@pytest.mark.parametrize("p_is_leaf", [False, True], ids=["node", "leaf"])
def test_create_svo_children(device: spy.Device, load_kernel: KernelLoader, p_version: tuple[int, str],
        p_is_leaf: bool) -> None:
    invocation_count = 40
    state = reset_svo(4, 8 + 8 * invocation_count, 8 * invocation_count)
    parents = np.arange(invocation_count, dtype=np.uint32) + 3
    operation = OPERATION_CREATE_CHILDREN_LEAF if p_is_leaf else OPERATION_CREATE_CHILDREN_NODE
    result = run_operation(device, load_kernel, p_version, operation, state, invocation_count, parents)
    assert np.all(result.results[:, 0] == 1)
    children = result.leaves if p_is_leaf else result.nodes
    first_children = result.results[:, 1]
    assert np.all(first_children % 8 == 0)
    for parent, first_child in zip(parents, first_children):
        block = children[first_child:first_child + 8]
        np.testing.assert_array_equal(block["parent_index"], parent)
        # Each block owns eight consecutive data slots.
        np.testing.assert_array_equal(block["data_index"], block["data_index"][0] + np.arange(8))
        if not p_is_leaf:
            np.testing.assert_array_equal(block["children_data"], pack_children_data(STATE_EMPTY))
    data_indices = np.concatenate([children[first:first + 8]["data_index"] for first in first_children])
    np.testing.assert_array_equal(np.sort(data_indices), 8 + np.arange(8 * invocation_count))
    assert result.tree_info["data_count"][0] == 8 + 8 * invocation_count
    if not p_is_leaf:
        # Root sectors are not overwritten.
        np.testing.assert_array_equal(result.nodes["data_index"][:8], np.arange(8))


def expected_created_children_data(p_version: int, p_is_leaf: bool, p_block_index: int, p_position: np.ndarray) -> int:
    if p_version == 1:
        return pack_children_data(STATE_LEAF if p_is_leaf else STATE_NODE, p_block_index)
    # Version 2 keeps the claim ALLOCATING and carries the block and, for leaves, the creator's occupancy bit.
    occupancy = 1 << int(subregion_reference(p_position)) if p_is_leaf else 0
    return pack_children_data(STATE_ALLOCATING, p_block_index, occupancy)


@pytest.mark.parametrize("p_version", UTILS_VERSIONS, ids=UTILS_VERSION_IDS)
@pytest.mark.parametrize("p_is_leaf", [False, True], ids=["node", "leaf"])
def test_try_create_svo_entry_on_empty_node(device: spy.Device, load_kernel: KernelLoader,
        p_version: tuple[int, str], p_is_leaf: bool) -> None:
    state = reset_svo(4, 64, 64)
    position = np.array([[0.7, 0.2, 0.6]], dtype=np.float32)
    result = run_operation(device, load_kernel, p_version, OPERATION_CREATE_ENTRY, state, 1, np.array([5]), position,
        p_is_leaf)
    assert result.results[0, 0] == 1
    # The first node block follows the root sectors; the first leaf block starts at zero.
    block_index = 0 if p_is_leaf else 1
    assert result.nodes["children_data"][5] == expected_created_children_data(p_version[0], p_is_leaf, block_index,
        position[0])
    assert result.tree_info["node_count"][0] == (8 if p_is_leaf else 16)
    assert result.tree_info["leaves_count"][0] == (8 if p_is_leaf else 0)
    children = result.leaves[:8] if p_is_leaf else result.nodes[8:16]
    np.testing.assert_array_equal(children["parent_index"], 5)


@pytest.mark.parametrize("p_version", UTILS_VERSIONS, ids=UTILS_VERSION_IDS)
@pytest.mark.parametrize("p_children_data", [pack_children_data(STATE_NODE, 3), pack_children_data(STATE_LEAF, 2, 0x81),
    pack_children_data(STATE_ALLOCATING), pack_children_data(STATE_ALLOCATING, 4, 0x10)],
    ids=["node", "leaf", "allocating", "allocating_with_payload"])
def test_try_create_svo_entry_on_claimed_node(device: spy.Device, load_kernel: KernelLoader,
        p_version: tuple[int, str], p_children_data: int) -> None:
    state = reset_svo(4, 64, 64)
    state.nodes["children_data"][2] = p_children_data
    result = run_operation(device, load_kernel, p_version, OPERATION_CREATE_ENTRY, state, 1, np.array([2]),
        np.full((1, 3), 0.25, dtype=np.float32), True)
    assert result.results[0, 0] == 0
    np.testing.assert_array_equal(result.nodes, state.nodes)
    np.testing.assert_array_equal(result.tree_info, state.tree_info)


@pytest.mark.parametrize("p_version", UTILS_VERSIONS, ids=UTILS_VERSION_IDS)
@pytest.mark.parametrize("p_is_leaf", [False, True], ids=["node", "leaf"])
def test_try_create_svo_entry_contention(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_version: tuple[int, str], p_is_leaf: bool) -> None:
    # Many invocations race for three nodes; exactly one claim per node succeeds and only its block is reserved.
    invocation_count = 96
    state = reset_svo(4, 64, 64)
    targets = np.array([1, 4, 6], dtype=np.uint32)[np.arange(invocation_count) % 3]
    positions = rng.uniform(0.0, 1.0, (invocation_count, 3)).astype(np.float32)
    result = run_operation(device, load_kernel, p_version, OPERATION_CREATE_ENTRY, state, invocation_count, targets,
        positions, p_is_leaf)
    succeeded = result.results[:, 0] == 1
    for target in [1, 4, 6]:
        assert np.count_nonzero(succeeded & (targets == target)) == 1
    assert result.tree_info["node_count"][0] == (8 if p_is_leaf else 32)
    assert result.tree_info["leaves_count"][0] == (24 if p_is_leaf else 0)
    assert result.tree_info["data_count"][0] == 32
    blocks = sorted(int(result.nodes["children_data"][target]) & BLOCK_MASK for target in [1, 4, 6])
    assert blocks == ([0, 1, 2] if p_is_leaf else [1, 2, 3])
    for winner in np.flatnonzero(succeeded):
        target = targets[winner]
        block_index = int(result.nodes["children_data"][target]) & BLOCK_MASK
        assert result.nodes["children_data"][target] == expected_created_children_data(p_version[0], p_is_leaf,
            block_index, positions[winner])


@pytest.mark.parametrize("p_version", UTILS_VERSIONS, ids=UTILS_VERSION_IDS)
def test_occupy_svo_leaf(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_version: tuple[int, str]) -> None:
    # Concurrent occupation of different sectors accumulates, leaving the state, block index, and prior bits intact.
    invocation_count = 64
    state = reset_svo(4, 64, 64)
    state.nodes["children_data"][3] = pack_children_data(STATE_LEAF, 0x1ABCD, 0b00000010)
    positions = rng.uniform(0.0, 1.0, (invocation_count, 3)).astype(np.float32)
    positions[:8] = np.array([[(sector >> 2) & 1, (sector >> 1) & 1, sector & 1] for sector in range(8)]) * 0.5 + 0.25
    selected = np.arange(invocation_count) % 4 != 0
    result = run_operation(device, load_kernel, p_version, OPERATION_OCCUPY_LEAF, state, int(np.count_nonzero(selected)),
        np.full(np.count_nonzero(selected), 3), positions[selected])
    occupancy = 0b00000010
    for sector in subregion_reference(positions[selected]):
        occupancy |= 1 << int(sector)
    assert result.nodes["children_data"][3] == pack_children_data(STATE_LEAF, 0x1ABCD, occupancy)
    others = np.arange(len(state.nodes)) != 3
    np.testing.assert_array_equal(result.nodes[others], state.nodes[others])


@pytest.mark.parametrize("p_version", UTILS_VERSIONS, ids=UTILS_VERSION_IDS)
def test_load_svo_node(device: spy.Device, load_kernel: KernelLoader, p_version: tuple[int, str]) -> None:
    state = reset_svo(4, 16, 8)
    descriptions = [pack_children_data(STATE_NODE, 7), pack_children_data(STATE_LEAF, 9, 0x42),
        pack_children_data(STATE_EMPTY), pack_children_data(STATE_ALLOCATING, 3, 0x01)]
    for index, children_data in enumerate(descriptions):
        state.nodes[8 + index] = (children_data, 100 + index, index - 1, 0)
    result = run_operation(device, load_kernel, p_version, OPERATION_LOAD_NODE, state, len(descriptions),
        np.arange(8, 8 + len(descriptions)))
    np.testing.assert_array_equal(result.results[:, 0], descriptions)
    np.testing.assert_array_equal(result.results[:, 1], 100 + np.arange(len(descriptions)))
    np.testing.assert_array_equal(result.results[:, 2].view(np.int32), np.arange(len(descriptions)) - 1)


def test_publish_svo_children_data(device: spy.Device, load_kernel: KernelLoader) -> None:
    state = reset_svo(4, 16, 8)
    state.nodes["children_data"][:8] = pack_children_data(STATE_ALLOCATING)
    values = np.array([pack_children_data(STATE_NODE, 1), pack_children_data(STATE_LEAF, 2, 0xFF),
        pack_children_data(STATE_EMPTY)], dtype=np.uint32)
    targets = np.array([0, 3, 6], dtype=np.uint32)
    result = run_operation(device, load_kernel, UTILS_VERSIONS[0], OPERATION_PUBLISH_CHILDREN_DATA, state, 3,
        np.stack([targets, values], axis=1))
    np.testing.assert_array_equal(result.nodes["children_data"][targets], values)
    untouched = np.setdiff1d(np.arange(16), targets)
    np.testing.assert_array_equal(result.nodes[untouched], state.nodes[untouched])


def publish_allocations(p_state: SVOState) -> None:
    """
    Apply the version 2 state-update pass, written from the SVONode::children_data documentation.

    ALLOCATING descriptions keep their payload and become LEAF when they carry occupancy, NODE otherwise.
    """
    node_count = int(p_state.tree_info["node_count"][0])
    children_data = p_state.nodes["children_data"][:node_count].astype(np.uint64)
    allocating = (children_data >> STATE_BIT_OFFSET) == STATE_ALLOCATING
    leaf = (children_data & OCCUPANCY_MASK) != 0
    states = np.where(leaf, STATE_LEAF, STATE_NODE).astype(np.uint64)
    published = (children_data & PAYLOAD_MASK) | (states << STATE_BIT_OFFSET)
    p_state.nodes["children_data"][:node_count] = np.where(allocating, published, children_data).astype(np.uint32)


def allocate_voxels(p_device: spy.Device, p_load_kernel: KernelLoader, p_version: tuple[int, str], p_voxels: list[Voxel],
        p_max_depth: int) -> tuple[SVOState, list[int]]:
    """
    Run deferred construction rounds until every voxel is allocated.

    Failed invocations retry from their saved traversal in the next round, and version 2 publishes allocations between
    rounds. Return the final state and the round in which each voxel succeeded.
    """
    capacity = 8 + 8 * len(p_voxels) * p_max_depth
    state = reset_svo(p_max_depth, capacity, capacity)
    centers = (np.array(p_voxels, dtype=np.float32) + 0.5) / np.float32(1 << p_max_depth)
    state.traces = root_traces(p_voxels, p_max_depth, -1)
    success_round = [-1] * len(p_voxels)
    for round_index in range(4 * p_max_depth):
        pending = [index for index, round_found in enumerate(success_round) if round_found < 0]
        if not pending:
            break
        round_state = SVOState(state.nodes, state.leaves, state.tree_info, state.traces[pending], state.results)
        round_state = run_operation(p_device, p_load_kernel, p_version, OPERATION_VOXEL_ALLOCATE, round_state,
            len(pending), p_positions=centers[pending])
        state = SVOState(round_state.nodes, round_state.leaves, round_state.tree_info, state.traces, state.results)
        state.traces[pending] = round_state.traces
        for invocation, index in enumerate(pending):
            if round_state.results[invocation, 0] == 1:
                success_round[index] = round_index
        if p_version[0] == 2:
            publish_allocations(state)
    return state, success_round


def decode_occupied(p_state: SVOState, p_voxel: Voxel, p_max_depth: int) -> bool:
    """Walk the built tree from the voxel's root sector following the documented children_data encoding."""
    node_index = voxel_sector(p_voxel, 1, p_max_depth)
    for depth in range(1, p_max_depth):
        children_data = int(p_state.nodes["children_data"][node_index])
        child_sector = voxel_sector(p_voxel, depth + 1, p_max_depth)
        state = children_state(children_data)
        if state == STATE_LEAF:
            assert depth == p_max_depth - 1, "leaf block above the last node level"
            return (children_data >> (OCCUPANCY_BIT_OFFSET + child_sector)) & 1 == 1
        if state != STATE_NODE:
            assert state == STATE_EMPTY, f"unpublished state {state}"
            return False
        assert depth < p_max_depth - 1, "node block at the last node level"
        node_index = (children_data & BLOCK_MASK) * 8 + child_sector
    raise AssertionError("traversal did not end at a leaf block")


def assert_built_svo(p_state: SVOState, p_voxels: list[Voxel], p_queries: list[Voxel], p_max_depth: int) -> None:
    for voxel in p_queries:
        assert decode_occupied(p_state, voxel, p_max_depth) == (voxel in p_voxels), voxel
    # Exactly one block per node that has an inserted voxel below it: no duplicated or leaked allocations.
    node_blocks = len({(depth, voxel_prefix(voxel, depth, p_max_depth)) for voxel in p_voxels
        for depth in range(1, p_max_depth - 1)})
    leaf_blocks = len({voxel_prefix(voxel, p_max_depth - 1, p_max_depth) for voxel in p_voxels})
    node_count = int(p_state.tree_info["node_count"][0])
    leaves_count = int(p_state.tree_info["leaves_count"][0])
    assert node_count == 8 + 8 * node_blocks
    assert leaves_count == 8 * leaf_blocks
    assert int(p_state.tree_info["data_count"][0]) == node_count + leaves_count
    nodes = p_state.nodes[:node_count]
    leaves = p_state.leaves[:leaves_count]
    data_indices = np.concatenate([nodes["data_index"], leaves["data_index"]])
    np.testing.assert_array_equal(np.sort(data_indices), np.arange(node_count + leaves_count))
    # Every child links back to the node whose published block contains it.
    for children, expected_state in [(nodes[8:], STATE_NODE), (leaves, STATE_LEAF)]:
        for child_index, parent_index in enumerate(children["parent_index"]):
            parent_data = int(nodes["children_data"][parent_index])
            assert children_state(parent_data) == expected_state
            first_child = (parent_data & BLOCK_MASK) * 8 - (8 if expected_state == STATE_NODE else 0)
            assert first_child <= child_index < first_child + 8


@pytest.mark.parametrize("p_version", UTILS_VERSIONS, ids=UTILS_VERSION_IDS)
@pytest.mark.parametrize(("p_max_depth", "p_voxel_count"), [(2, 20), (3, 60), (4, 200)])
def test_try_voxel_allocate_builds_svo(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_version: tuple[int, str], p_max_depth: int, p_voxel_count: int) -> None:
    voxels = random_voxels(rng, p_voxel_count, p_max_depth)
    # Duplicates race for the same leaf and must all succeed.
    requests = voxels + voxels[:len(voxels) // 4]
    state, success_round = allocate_voxels(device, load_kernel, p_version, requests, p_max_depth)
    assert all(round_found >= 0 for round_found in success_round)
    assert_built_svo(state, voxels, voxels + random_voxels(rng, 64, p_max_depth), p_max_depth)


@pytest.mark.parametrize("p_version", UTILS_VERSIONS, ids=UTILS_VERSION_IDS)
@pytest.mark.parametrize("p_max_depth", [2, 3, 5])
def test_try_voxel_allocate_uncontended_rounds(device: spy.Device, load_kernel: KernelLoader,
        p_version: tuple[int, str], p_max_depth: int) -> None:
    # Version 1 allocates a lone voxel in one call. Version 2 never descends into children created in the same pass,
    # so each of the max_depth - 1 levels costs one round before the final round marks the leaf.
    voxel = (1, (1 << p_max_depth) - 1, 0)
    state, success_round = allocate_voxels(device, load_kernel, p_version, [voxel], p_max_depth)
    assert success_round == [0 if p_version[0] == 1 else p_max_depth - 1]
    assert_built_svo(state, [voxel], [voxel, (0, 0, 0)], p_max_depth)


@pytest.mark.parametrize("p_version", UTILS_VERSIONS, ids=UTILS_VERSION_IDS)
def test_try_voxel_allocate_saves_retry_point(device: spy.Device, load_kernel: KernelLoader,
        p_version: tuple[int, str]) -> None:
    # A traversal that meets an ALLOCATING node returns false with the trace saved at that node.
    max_depth = 4
    state = reset_svo(max_depth, 64, 64)
    voxel = (1, 6, 3)
    root_sector = voxel_sector(voxel, 1, max_depth)
    allocating_node = 8 + voxel_sector(voxel, 2, max_depth)
    state.nodes["children_data"][root_sector] = pack_children_data(STATE_NODE, 1)
    state.nodes["children_data"][allocating_node] = pack_children_data(STATE_ALLOCATING)
    state.tree_info["node_count"] = 16
    state.traces = root_traces([voxel], max_depth, -1)
    center = (np.array([voxel], dtype=np.float32) + 0.5) / np.float32(1 << max_depth)
    result = run_operation(device, load_kernel, p_version, OPERATION_VOXEL_ALLOCATE, state, 1, p_positions=center)
    assert result.results[0, 0] == 0
    trace = result.traces[0]
    assert (trace["node_index"], trace["curr_depth"], trace["remain_depth"]) == (allocating_node, 2, -1)
    np.testing.assert_allclose(trace["svo_position"], np.modf(center[0] * 4.0)[0], atol=1e-6)
    np.testing.assert_array_equal(result.nodes, state.nodes)
