# Copyright 2026 OppositeNor
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

from test_svo_common import make_buffer, read_buffer


RANDOM_SEED = 20261011

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]
Voxel = tuple[int, int, int]

# Mirrors octree/octree_common.slang.
OCTREE_EMPTY = 255
OCTREE_OCCUPIED = 0

INFO_DTYPE = np.dtype([("origin", "<f4", 3), ("width", "<f4"), ("max_depth", "<u4"), ("max_debug_node_count", "<u4"),
    ("padding", "<u4", 2)])
# Mirrors OCTREE_DEBUG_SUBGRID_EXTENT of octree/octree_debug.slang, as (x, y, z).
SUBGRID_EXTENT = (8, 8, 16)
SUBGRID_MASK_WORD_COUNT = 32
# A non-unit octree placement, exactly representable so world positions map back to voxel centers without rounding.
OCTREE_ORIGIN = np.array([-3.0, 1.5, 2.0], dtype=np.float32)
OCTREE_WIDTH = 8.0


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def random_voxels(p_rng: np.random.Generator, p_count: int, p_max_depth: int) -> list[Voxel]:
    extent = 1 << p_max_depth
    return [tuple(int(axis) for axis in p_rng.integers(0, extent, 3)) for _ in range(p_count)]


def build_levels(p_voxels: list[Voxel], p_max_depth: int, p_occupied_value: int = OCTREE_OCCUPIED) -> list[np.ndarray]:
    """Mip levels of an octree texture indexed [z, y, x], with every ancestor of an occupied leaf occupied."""
    levels: list[np.ndarray] = []
    for level in range(p_max_depth + 1):
        extent = 1 << (p_max_depth - level)
        texels = np.full((extent, extent, extent), OCTREE_EMPTY, dtype=np.uint8)
        for x, y, z in p_voxels:
            texels[z >> level, y >> level, x >> level] = p_occupied_value
        levels.append(texels)
    return levels


def make_octree_texture(p_device: spy.Device, p_levels: list[np.ndarray], p_format: spy.Format) -> spy.Texture:
    extent = p_levels[0].shape[0]
    texture = p_device.create_texture(type=spy.TextureType.texture_3d, format=p_format, width=extent, height=extent,
        depth=extent, mip_count=len(p_levels), usage=spy.TextureUsage.shader_resource)
    for mip, texels in enumerate(p_levels):
        texture.copy_from_numpy(np.ascontiguousarray(texels), mip=mip)
    return texture


def make_octree_info(p_max_depth: int) -> np.ndarray:
    info = np.zeros(1, dtype=INFO_DTYPE)
    info["origin"] = OCTREE_ORIGIN
    info["width"] = OCTREE_WIDTH
    info["max_depth"] = p_max_depth
    return info


def random_levels(p_rng: np.random.Generator, p_max_depth: int, p_dtype: np.dtype) -> list[np.ndarray]:
    """Independent random values per texel and level, so a lookup is only correct at its own mip level and texel."""
    levels: list[np.ndarray] = []
    for level in range(p_max_depth + 1):
        extent = 1 << (p_max_depth - level)
        shape = (extent, extent, extent)
        if np.issubdtype(p_dtype, np.integer):
            levels.append(p_rng.integers(0, 256, shape).astype(p_dtype))
        else:
            levels.append(p_rng.uniform(-10.0, 10.0, shape).astype(p_dtype))
    return levels


def run_trace(p_device: spy.Device, p_load_kernel: KernelLoader, p_levels: list[np.ndarray], p_format: spy.Format,
        p_texel_type: str, p_positions: np.ndarray, p_max_depth: int, p_depth: int) -> np.ndarray:
    """Trace world-space positions at one depth and return the traced texels."""
    kernel = p_load_kernel("octree_trace_data.slang", {"TEXEL_TYPE": p_texel_type, "DEPTH": p_depth})
    positions = np.zeros((len(p_positions), 4), dtype=np.float32)
    positions[:, :3] = p_positions
    result_dtype = np.uint32 if p_texel_type == "uint" else np.float32
    results = make_buffer(p_device, np.zeros(len(p_positions), dtype=result_dtype))
    kernel.dispatch(thread_count=[len(p_positions), 1, 1], vars={
        "levels": make_octree_texture(p_device, p_levels, p_format),
        "tree_info": make_buffer(p_device, make_octree_info(p_max_depth)),
        "positions": make_buffer(p_device, positions), "results": results})
    return read_buffer(results, result_dtype)


def world_positions(p_voxels: list[Voxel], p_max_depth: int) -> np.ndarray:
    centers = (np.array(p_voxels, dtype=np.float32) + 0.5) / float(1 << p_max_depth)
    return OCTREE_ORIGIN + centers * OCTREE_WIDTH


def expected_texels(p_levels: list[np.ndarray], p_voxels: list[Voxel], p_max_depth: int, p_depth: int) -> np.ndarray:
    level = p_max_depth - p_depth
    return np.array([p_levels[level][z >> level, y >> level, x >> level] for x, y, z in p_voxels])


@pytest.mark.parametrize("p_max_depth", [1, 3, 5])
def test_trace_returns_texel_of_depth_level(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_max_depth: int) -> None:
    # Random values include OCTREE_EMPTY and OCTREE_OCCUPIED at every level; tracing does not interpret either.
    levels = random_levels(rng, p_max_depth, np.dtype(np.uint8))
    voxels = random_voxels(rng, 128, p_max_depth)
    for depth in range(p_max_depth + 1):
        results = run_trace(device, load_kernel, levels, spy.Format.r8_uint, "uint", world_positions(voxels, p_max_depth),
            p_max_depth, depth)
        np.testing.assert_array_equal(results, expected_texels(levels, voxels, p_max_depth, depth))


def test_trace_reads_through_empty_ancestors(device: spy.Device, load_kernel: KernelLoader) -> None:
    max_depth = 3
    levels = build_levels([], max_depth)
    levels[0][5, 6, 1] = OCTREE_OCCUPIED
    voxels: list[Voxel] = [(1, 6, 5)]
    for depth in range(max_depth + 1):
        results = run_trace(device, load_kernel, levels, spy.Format.r8_uint, "uint", world_positions(voxels, max_depth),
            max_depth, depth)
        assert int(results[0]) == (OCTREE_OCCUPIED if depth == max_depth else OCTREE_EMPTY)


@pytest.mark.parametrize("p_depth", [0, 2, 4])
def test_trace_float_texture(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_depth: int) -> None:
    max_depth = 4
    levels = random_levels(rng, max_depth, np.dtype(np.float32))
    voxels = random_voxels(rng, 128, max_depth)
    results = run_trace(device, load_kernel, levels, spy.Format.r32_float, "float", world_positions(voxels, max_depth),
        max_depth, p_depth)
    np.testing.assert_array_equal(results, expected_texels(levels, voxels, max_depth, p_depth))


def test_trace_clamps_positions_outside_the_octree(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    max_depth = 3
    levels = random_levels(rng, max_depth, np.dtype(np.uint8))
    extent = 1 << max_depth
    positions = np.array([OCTREE_ORIGIN - 1.0, OCTREE_ORIGIN + OCTREE_WIDTH + 1.0], dtype=np.float32)
    results = run_trace(device, load_kernel, levels, spy.Format.r8_uint, "uint", positions, max_depth, max_depth)
    np.testing.assert_array_equal(results, [levels[0][0, 0, 0], levels[0][extent - 1, extent - 1, extent - 1]])


def level_offsets(p_max_depth: int) -> list[int]:
    offsets = [0]
    for level in range(p_max_depth):
        extent = 1 << (p_max_depth - level)
        offsets.append(offsets[-1] + extent ** 3)
    return offsets


def to_pyramid(p_levels: list[np.ndarray]) -> np.ndarray:
    # Levels are indexed [z, y, x], so C order flattens each level x-major as the kernel expects.
    return np.concatenate([texels.reshape(-1) for texels in p_levels]).astype(np.uint32)


def run_write_ancestors(p_device: spy.Device, p_load_kernel: KernelLoader, p_pyramid: np.ndarray, p_voxels: list[Voxel],
        p_max_depth: int, p_first_level: int, p_value: int) -> np.ndarray:
    kernel = p_load_kernel("octree_write_ancestors.slang",
        {"MAX_DEPTH": p_max_depth, "FIRST_LEVEL": p_first_level, "VALUE": p_value})
    pyramid = make_buffer(p_device, p_pyramid)
    voxels = np.zeros((len(p_voxels), 4), dtype=np.uint32)
    voxels[:, :3] = np.array(p_voxels, dtype=np.uint32)
    kernel.dispatch(thread_count=[len(p_voxels), 1, 1], vars={"pyramid": pyramid, "voxels": make_buffer(p_device, voxels)})
    return read_buffer(pyramid, np.uint32)


@pytest.mark.parametrize("p_max_depth", [1, 3, 5])
def test_propagation_occupies_every_ancestor(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_max_depth: int) -> None:
    occupied = random_voxels(rng, 40, p_max_depth)
    # Leaves are occupied before their ancestors; duplicates model two pixels covering one leaf.
    rasterized = build_levels([], p_max_depth)
    for x, y, z in occupied:
        rasterized[0][z, y, x] = OCTREE_OCCUPIED
    result = run_write_ancestors(device, load_kernel, to_pyramid(rasterized), occupied + occupied[:5], p_max_depth, 1,
        OCTREE_OCCUPIED)
    np.testing.assert_array_equal(result, to_pyramid(build_levels(occupied, p_max_depth)))


def test_propagation_keeps_existing_occupancy(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    max_depth = 4
    existing = random_voxels(rng, 20, max_depth)
    added = random_voxels(rng, 20, max_depth)
    levels = build_levels(existing, max_depth)
    for x, y, z in added:
        levels[0][z, y, x] = OCTREE_OCCUPIED
    result = run_write_ancestors(device, load_kernel, to_pyramid(levels), added, max_depth, 1, OCTREE_OCCUPIED)
    np.testing.assert_array_equal(result, to_pyramid(build_levels(existing + added, max_depth)))


def test_clear_empties_every_listed_leaf_and_ancestor(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    max_depth = 4
    occupied = random_voxels(rng, 40, max_depth)
    result = run_write_ancestors(device, load_kernel, to_pyramid(build_levels(occupied, max_depth)),
        occupied + occupied[:5], max_depth, 0, OCTREE_EMPTY)
    np.testing.assert_array_equal(result, np.full(level_offsets(max_depth)[-1] + 1, OCTREE_EMPTY, dtype=np.uint32))


def run_subgrid(p_device: spy.Device, p_load_kernel: KernelLoader, p_masks: np.ndarray, p_ranks: np.ndarray,
        p_max_depth: int) -> tuple[np.ndarray, np.ndarray]:
    kernel = p_load_kernel("octree_debug_subgrid.slang", {"MAX_DEPTH": p_max_depth})
    bits = make_buffer(p_device, np.zeros((len(p_ranks), 4), dtype=np.uint32))
    subgrids = make_buffer(p_device, np.zeros((len(p_ranks), 4), dtype=np.uint32))
    local_indices = make_buffer(p_device, np.zeros(len(p_ranks), dtype=np.uint32))
    kernel.dispatch(thread_count=[len(p_ranks), 1, 1], vars={"masks": make_buffer(p_device, p_masks),
        "ranks": make_buffer(p_device, p_ranks), "bits": bits, "subgrids": subgrids, "local_indices": local_indices})
    bit_rows = read_buffer(bits, np.uint32).reshape(-1, 4)
    np.testing.assert_array_equal(read_buffer(local_indices, np.uint32), bit_rows[:, 0])
    return bit_rows, read_buffer(subgrids, np.uint32).reshape(-1, 4)


def test_debug_subgrid_set_bit_and_leaf(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator) -> None:
    max_depth = 5
    masks = rng.integers(0, 1 << 32, (256, SUBGRID_MASK_WORD_COUNT), dtype=np.uint64).astype(np.uint32)
    masks[0] = 0
    masks[0, 0] = 1
    masks[1] = 0
    masks[1, -1] = 1 << 31
    masks[2] = 0xffffffff
    # Sparse masks whose set bits lie in late words.
    masks[3:64, :28] = 0
    expected_bits: list[int] = []
    ranks = np.zeros(len(masks), dtype=np.uint32)
    for index, mask in enumerate(masks):
        set_bits = [word * 32 + bit for word in range(SUBGRID_MASK_WORD_COUNT) for bit in range(32)
            if (int(mask[word]) >> bit) & 1]
        ranks[index] = rng.integers(0, len(set_bits)) if index > 2 else len(set_bits) - 1
        expected_bits.append(set_bits[ranks[index]])
    bits, _ = run_subgrid(device, load_kernel, masks, ranks, max_depth)
    np.testing.assert_array_equal(bits[:, 0], expected_bits)
    for bit, row in zip(expected_bits, bits):
        width, height, _ = SUBGRID_EXTENT
        assert tuple(int(axis) for axis in row[1:]) == (bit % width, (bit // width) % height, bit // (width * height))


def test_debug_subgrid_index_round_trip(device: spy.Device, load_kernel: KernelLoader) -> None:
    max_depth = 5
    grid = [(1 << max_depth) // extent for extent in SUBGRID_EXTENT]
    count = grid[0] * grid[1] * grid[2]
    masks = np.full((count, SUBGRID_MASK_WORD_COUNT), 1, dtype=np.uint32)
    _, subgrids = run_subgrid(device, load_kernel, masks, np.zeros(count, dtype=np.uint32), max_depth)
    for index, row in enumerate(subgrids):
        x, y, z = (int(axis) for axis in row[:3])
        assert (x, y, z) == (index % grid[0], (index // grid[0]) % grid[1], index // (grid[0] * grid[1]))
        assert int(row[3]) == index
