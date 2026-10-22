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
from typing import Any

import numpy as np
import pytest
import slangpy as spy


RANDOM_SEED = 20251003
SAMPLE_COUNT = 1024

@pytest.fixture(scope="module")
def utils(device: spy.Device, shader_include_dir: Path) -> spy.Module:
    return spy.Module.load_from_file(device, str(shader_include_dir / "utils" / "utils.slang"))


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def call(p_function: Any, *p_args: Any) -> np.ndarray:
    return p_function(*p_args, _result="numpy")


def random_unit_vectors(p_rng: np.random.Generator, p_count: int) -> np.ndarray:
    vectors = p_rng.normal(size=(p_count, 3))
    return (vectors / np.linalg.norm(vectors, axis=1, keepdims=True)).astype(np.float32)


def morton_spread_bits_3d_reference(p_value: int) -> int:
    result = 0
    for bit in range(10):
        result |= ((p_value >> bit) & 1) << (3 * bit)
    return result


def test_decode_normal_map(utils: spy.Module, rng: np.random.Generator) -> None:
    normals = random_unit_vectors(rng, SAMPLE_COUNT)
    normals[:, 2] = np.abs(normals[:, 2])
    encoded = ((normals[:, :2] + 1.0) * 0.5).astype(np.float32)
    decoded = call(utils.decode_normal_map, encoded)
    np.testing.assert_allclose(decoded, normals, atol=1e-4)


def test_decode_normal_map_clamps_outside_unit_disk(utils: spy.Module) -> None:
    decoded = call(utils.decode_normal_map, np.array([[1.0, 1.0], [0.0, 0.0]], dtype=np.float32))
    np.testing.assert_allclose(decoded, [[1.0, 1.0, 0.0], [-1.0, -1.0, 0.0]], atol=1e-6)


def test_transform_static_normal(utils: spy.Module, rng: np.random.Generator) -> None:
    transform = np.eye(4, dtype=np.float32)
    transform[:3, :3] = rng.normal(size=(3, 3))
    normal = random_unit_vectors(rng, 1)[0]
    result = utils.transform_static_normal(spy.float4x4(transform), spy.float3(*normal))
    expected = transform[:3, :3] @ normal
    np.testing.assert_allclose([result.x, result.y, result.z], expected / np.linalg.norm(expected), atol=1e-5)


def test_chebyshev_upper_bound(utils: spy.Module, rng: np.random.Generator) -> None:
    means = rng.uniform(0.0, 10.0, SAMPLE_COUNT)
    variances = rng.uniform(0.0, 2.0, SAMPLE_COUNT)
    moments = np.stack([means, variances + means * means], axis=1).astype(np.float32)
    values = rng.uniform(-5.0, 15.0, SAMPLE_COUNT).astype(np.float32)
    minimum_variance = np.full(SAMPLE_COUNT, 1e-3, dtype=np.float32)
    result = call(utils.chebyshev_upper_bound, moments, values, minimum_variance)
    clamped_variance = np.maximum(moments[:, 1] - moments[:, 0] ** 2, minimum_variance)
    expected = np.clip(clamped_variance / (clamped_variance + (values - moments[:, 0]) ** 2), 0.0, 1.0)
    np.testing.assert_allclose(result, expected, rtol=1e-3, atol=1e-4)


def test_octahedron_round_trip(utils: spy.Module, rng: np.random.Generator) -> None:
    normals = random_unit_vectors(rng, SAMPLE_COUNT)
    encoded = call(utils.octahedron_encode, normals)
    assert np.all(np.abs(encoded) <= 1.0)
    decoded = call(utils.octahedron_decode, encoded)
    np.testing.assert_allclose(decoded, normals, atol=1e-5)


def test_octahedron_encode_axes(utils: spy.Module) -> None:
    axes = np.array([[0, 0, 1], [0, 0, -1], [1, 0, 0], [0, -1, 0]], dtype=np.float32)
    encoded = call(utils.octahedron_encode, axes)
    np.testing.assert_allclose(encoded, [[0, 0], [1, 1], [1, 0], [0, -1]], atol=1e-6)


def test_srgb_round_trip(utils: spy.Module, rng: np.random.Generator) -> None:
    colors = rng.uniform(0.0, 1.0, (SAMPLE_COUNT, 3)).astype(np.float32)
    srgb = call(utils.linear_to_srgb, colors)
    np.testing.assert_allclose(srgb, colors ** (1.0 / 2.2), rtol=1e-4, atol=1e-5)
    np.testing.assert_allclose(call(utils.srgb_to_linear, srgb), colors, rtol=1e-4, atol=1e-5)


def test_srgb_clamps_negative(utils: spy.Module) -> None:
    colors = np.array([[-1.0, -0.5, 0.0]], dtype=np.float32)
    np.testing.assert_array_equal(call(utils.linear_to_srgb, colors), [[0.0, 0.0, 0.0]])
    np.testing.assert_array_equal(call(utils.srgb_to_linear, colors), [[0.0, 0.0, 0.0]])


def test_r11g11b10_round_trip(utils: spy.Module, rng: np.random.Generator) -> None:
    colors = rng.uniform(0.0, 100.0, (SAMPLE_COUNT, 3)).astype(np.float32)
    unpacked = call(utils.unpack_r11g11b10, call(utils.pack_r11g11b10, colors))
    # 6 mantissa bits for red/green and 5 for blue. Rounding through half precision first can exceed half an ulp, so the
    # bound is one ulp.
    np.testing.assert_allclose(unpacked[:, :2], colors[:, :2], rtol=2.0 ** -6)
    np.testing.assert_allclose(unpacked[:, 2], colors[:, 2], rtol=2.0 ** -5)


def test_r11g11b10_exact_values(utils: spy.Module) -> None:
    colors = np.array([[1.0, 2.0, 0.5], [0.0, 0.25, 4.0]], dtype=np.float32)
    np.testing.assert_array_equal(call(utils.unpack_r11g11b10, call(utils.pack_r11g11b10, colors)), colors)


def test_r11g11b10_clamps_range(utils: spy.Module) -> None:
    colors = np.array([[-1.0, 1e9, np.inf]], dtype=np.float32)
    unpacked = call(utils.unpack_r11g11b10, call(utils.pack_r11g11b10, colors))
    assert unpacked[0, 0] == 0.0
    assert np.all(np.isfinite(unpacked))
    # Every channel clamps to 64512, the largest value representable by the 10-bit blue channel.
    np.testing.assert_array_equal(unpacked[0, 1:], [64512.0, 64512.0])


def test_morton_spread_bits_3d(utils: spy.Module) -> None:
    values = np.arange(0, 2048, dtype=np.uint32)
    result = call(utils.morton_spread_bits_3d, values)
    expected = np.array([morton_spread_bits_3d_reference(int(value)) for value in values], dtype=np.uint32)
    np.testing.assert_array_equal(result, expected)


def test_morton_encode_3d(utils: spy.Module, rng: np.random.Generator) -> None:
    coordinates = rng.integers(0, 1024, (SAMPLE_COUNT, 3), dtype=np.uint32)
    result = call(utils.morton_encode_3d, coordinates)
    expected = np.array([morton_spread_bits_3d_reference(int(x)) | (morton_spread_bits_3d_reference(int(y)) << 1)
        | (morton_spread_bits_3d_reference(int(z)) << 2) for x, y, z in coordinates], dtype=np.uint32)
    np.testing.assert_array_equal(result, expected)


@pytest.mark.parametrize("p_extent", [(1, 1, 1), (3, 5, 7), (16, 8, 4)])
def test_flatten_index_3d(utils: spy.Module, rng: np.random.Generator, p_extent: tuple[int, int, int]) -> None:
    width, height, depth = p_extent
    coordinates = np.stack([rng.integers(0, extent, SAMPLE_COUNT, dtype=np.uint32) for extent in p_extent], axis=1)
    widths = np.full(SAMPLE_COUNT, width, dtype=np.uint32)
    heights = np.full(SAMPLE_COUNT, height, dtype=np.uint32)
    result = call(utils.flatten_index_3d, coordinates, widths, heights)
    # x varies fastest and z slowest, as in a C-ordered [depth][height][width] array.
    expected = np.ravel_multi_index((coordinates[:, 2], coordinates[:, 1], coordinates[:, 0]), (depth, height, width))
    np.testing.assert_array_equal(result, expected.astype(np.uint32))
    np.testing.assert_array_equal(call(utils.unflatten_index_3d, result, widths, heights), coordinates)


def test_morton_delta(utils: spy.Module, rng: np.random.Generator) -> None:
    first = rng.integers(0, 2 ** 32, SAMPLE_COUNT, dtype=np.uint32)
    second = rng.integers(0, 2 ** 32, SAMPLE_COUNT, dtype=np.uint32)
    second[first == second] ^= 1
    result = call(utils.morton_delta, first, second)
    expected = np.array([int(a ^ b).bit_length() - 1 for a, b in zip(first, second)], dtype=np.uint32)
    np.testing.assert_array_equal(result, expected)


def test_z_order_swizzle_8x8(utils: spy.Module) -> None:
    group_id = (3, 5)
    threads = np.array([(x, y) for y in range(8) for x in range(8)], dtype=np.uint32)
    groups = np.tile(np.array(group_id, dtype=np.uint32), (len(threads), 1))
    result = call(utils.z_order_swizzle_8x8, groups, threads)
    linear = threads[:, 1] * 8 + threads[:, 0]
    expected_x = (linear & 1) | ((linear >> 1) & 2) | ((linear >> 2) & 4)
    expected_y = ((linear >> 1) & 1) | ((linear >> 2) & 2) | ((linear >> 3) & 4)
    np.testing.assert_array_equal(result, np.stack([expected_x, expected_y], axis=1) + np.array(group_id) * 8)
    assert len({tuple(pixel) for pixel in result}) == 64


def test_random_range(utils: spy.Module, rng: np.random.Generator) -> None:
    seeds = rng.uniform(-1000.0, 1000.0, (SAMPLE_COUNT, 2)).astype(np.float32)
    result = call(utils.random, seeds)
    assert np.all((result >= 0.0) & (result < 1.0))
    np.testing.assert_array_equal(call(utils.random, seeds), result)
