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

from test_svo_common import (INDEX_DYNAMIC_LEAF, INDEX_DYNAMIC_NODE, INDEX_STATIC_LEAF, INDEX_STATIC_NODE, LEAF_DTYPE,
    TRACE_DTYPE, Voxel, assert_trace, build_static_svo, dynamic_svo, expected_stop_depth, make_buffer, make_tree_info,
    random_voxels, read_buffer, static_query_voxels, voxel_center, voxel_prefix)


RANDOM_SEED = 20251003

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]

DYNAMIC_LEAF_BLOCK_DTYPE = np.dtype([("original_children_data", "<u4"), ("leaves", LEAF_DTYPE, 8)])
# Data indices are offset per storage so a result identifies the storage it was read from.
DYNAMIC_NODE_DATA_OFFSET = 50
DYNAMIC_LEAF_DATA_OFFSET = 200
# A non-unit SVO placement, exactly representable so world positions map back to voxel centers without rounding.
SVO_ORIGIN = np.array([-3.0, 1.5, 2.0], dtype=np.float32)
SVO_WIDTH = 8.0


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def run_trace_data(p_device: spy.Device, p_load_kernel: KernelLoader, p_nodes: np.ndarray, p_dynamic_nodes: np.ndarray,
        p_leaves: np.ndarray, p_dynamic_leaves: np.ndarray, p_tree_info: np.ndarray, p_voxels: list[Voxel],
        p_traverse_dynamic: bool) -> tuple[np.ndarray, np.ndarray]:
    """Trace every voxel center in world space and return the traced data indices and final traces."""
    max_depth = int(p_tree_info[0]["max_depth"])
    kernel = p_load_kernel("svo_trace_data.slang", {"TRAVERSE_DYNAMIC": int(p_traverse_dynamic)})
    tree_info = p_tree_info.copy()
    tree_info["origin"] = SVO_ORIGIN
    tree_info["width"] = SVO_WIDTH
    positions = np.zeros((len(p_voxels), 4), dtype=np.float32)
    for index, voxel in enumerate(p_voxels):
        positions[index, :3] = SVO_ORIGIN + voxel_center(voxel, max_depth) * SVO_WIDTH
    results = make_buffer(p_device, np.zeros(len(p_voxels), dtype=np.uint32))
    traces = make_buffer(p_device, np.zeros(len(p_voxels), dtype=TRACE_DTYPE))
    kernel.dispatch(thread_count=[len(p_voxels), 1, 1], vars={"nodes": make_buffer(p_device, p_nodes),
        "dynamic_nodes": make_buffer(p_device, p_dynamic_nodes), "leaves": make_buffer(p_device, p_leaves),
        "dynamic_leaves": make_buffer(p_device, p_dynamic_leaves),
        "tree_info": make_buffer(p_device, tree_info), "positions": make_buffer(p_device, positions),
        "results": results, "traces": traces})
    return read_buffer(results, np.uint32), read_buffer(traces, TRACE_DTYPE)


def empty_dynamic_leaves() -> np.ndarray:
    return np.zeros(1, dtype=DYNAMIC_LEAF_BLOCK_DTYPE)


@pytest.mark.parametrize("p_max_depth", [2, 3, 5])
def test_trace_data_static(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_max_depth: int) -> None:
    # Static nodes store their own index as data_index, and leaf i stores 100 + i.
    tree = build_static_svo(random_voxels(rng, 24, p_max_depth), p_max_depth)
    queries = static_query_voxels(rng, tree, 64)
    nodes = tree.node_array()
    leaves = tree.leaf_array()
    results, traces = run_trace_data(device, load_kernel, nodes, nodes, leaves, empty_dynamic_leaves(),
        make_tree_info(p_max_depth, len(nodes)), queries, False)
    for voxel, result, trace in zip(queries, results, traces):
        if voxel in tree.leaf_of_voxel:
            stop_depth = p_max_depth
            index, index_type = tree.leaf_of_voxel[voxel], INDEX_STATIC_LEAF
            data_index = int(leaves[index]["data_index"])
        else:
            stop_depth = expected_stop_depth(tree, voxel)
            index = tree.node_of_prefix[(stop_depth, voxel_prefix(voxel, stop_depth, p_max_depth))]
            index_type = INDEX_STATIC_NODE
            data_index = int(nodes[index]["data_index"])
        assert result == data_index
        assert_trace(trace, index, index_type, stop_depth, p_max_depth - stop_depth,
            np.modf(voxel_center(voxel, p_max_depth) * float(1 << stop_depth))[0])


# (voxel, expected with dynamic traversal, expected without), each expectation being (index, index type, depth), as in
# test_svo_common.DYNAMIC_TRAVERSAL_CASES.
DYNAMIC_TRACE_CASES = [
    ((1, 2, 3), (16 + 5, INDEX_DYNAMIC_LEAF, 3), (0, INDEX_STATIC_NODE, 1)),
    ((1, 2, 2), (3, INDEX_DYNAMIC_NODE, 2), (0, INDEX_STATIC_NODE, 1)),
    ((4, 6, 4), (8 + 2, INDEX_STATIC_NODE, 2), (8 + 2, INDEX_STATIC_NODE, 2)),
    ((5, 1, 1), (4, INDEX_STATIC_NODE, 1), (4, INDEX_STATIC_NODE, 1)),
]


@pytest.mark.parametrize("p_traverse_dynamic", [True, False])
def test_trace_data_dynamic(device: spy.Device, load_kernel: KernelLoader, p_traverse_dynamic: bool) -> None:
    nodes, dynamic_nodes, tree_info = dynamic_svo()
    nodes["data_index"] = np.arange(len(nodes))
    dynamic_nodes["data_index"] = DYNAMIC_NODE_DATA_OFFSET + np.arange(len(dynamic_nodes))
    # Dynamic node 3 owns dynamic leaf block 2.
    dynamic_leaves = np.zeros(3, dtype=DYNAMIC_LEAF_BLOCK_DTYPE)
    dynamic_leaves["leaves"]["data_index"] = DYNAMIC_LEAF_DATA_OFFSET + np.arange(24).reshape(3, 8)
    dynamic_leaves["leaves"]["parent_index"][2] = 3
    voxels = [case[0] for case in DYNAMIC_TRACE_CASES]
    results, traces = run_trace_data(device, load_kernel, nodes, dynamic_nodes, np.zeros(1, dtype=LEAF_DTYPE),
        dynamic_leaves, tree_info, voxels, p_traverse_dynamic)
    expected_data = {
        INDEX_STATIC_NODE: lambda p_index: p_index,
        INDEX_DYNAMIC_NODE: lambda p_index: DYNAMIC_NODE_DATA_OFFSET + p_index,
        INDEX_DYNAMIC_LEAF: lambda p_index: DYNAMIC_LEAF_DATA_OFFSET + p_index,
    }
    for (voxel, with_dynamic, without_dynamic), result, trace in zip(DYNAMIC_TRACE_CASES, results, traces):
        index, index_type, depth = with_dynamic if p_traverse_dynamic else without_dynamic
        assert result == expected_data[index_type](index)
        assert_trace(trace, index, index_type, depth, 3 - depth, np.modf(voxel_center(voxel, 3) * float(1 << depth))[0])
