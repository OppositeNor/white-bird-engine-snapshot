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
RSM_SAMPLE_COUNT = 8


@pytest.fixture(scope="module")
def composition(device: spy.Device, shader_include_dir: Path) -> spy.Module:
    return spy.Module.load_from_file(device,
        str(shader_include_dir / "deferred_composition" / "deferred_composition_utils.slang"))


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def call(p_function: Any, *p_args: Any) -> np.ndarray:
    return p_function(*p_args, _result="numpy")


def cross_2d(p_first: np.ndarray, p_second: np.ndarray) -> float:
    return float(p_first[0] * p_second[1] - p_first[1] * p_second[0])


def test_build_fullscreen_triangle(composition: spy.Module) -> None:
    vertices = [composition.build_fullscreen_triangle(vertex_id) for vertex_id in range(3)]
    positions = np.array([[vertex["position"].x, vertex["position"].y, vertex["position"].z, vertex["position"].w]
        for vertex in vertices])
    uvs = np.array([[vertex["uv"].x, vertex["uv"].y] for vertex in vertices])
    np.testing.assert_array_equal(positions, [[-1, -1, 0, 1], [-1, 3, 0, 1], [3, -1, 0, 1]])
    # The UVs map clip space [-1, 1] to [0, 1] linearly, so they are exact over the visible square.
    np.testing.assert_array_equal(uvs, positions[:, :2] * 0.5 + 0.5)


def test_build_fullscreen_triangle_covers_clip_square(composition: spy.Module) -> None:
    corners = np.array([[vertex["position"].x, vertex["position"].y] for vertex in
        (composition.build_fullscreen_triangle(vertex_id) for vertex_id in range(3))])
    # Every corner of the clip-space square must be inside or on the triangle.
    for point in [(-1.0, -1.0), (-1.0, 1.0), (1.0, -1.0), (1.0, 1.0)]:
        signs = [cross_2d(corners[(i + 1) % 3] - corners[i], np.array(point) - corners[i]) for i in range(3)]
        assert all(sign <= 0.0 for sign in signs) or all(sign >= 0.0 for sign in signs), point


def random_view_projection(p_rng: np.random.Generator) -> np.ndarray:
    """A perspective projection times a random rigid view transform."""
    near, far, focal = 0.1, 100.0, 1.5
    projection = np.array([[focal, 0.0, 0.0, 0.0], [0.0, focal, 0.0, 0.0],
        [0.0, 0.0, far / (far - near), -far * near / (far - near)], [0.0, 0.0, 1.0, 0.0]])
    rotation, _ = np.linalg.qr(p_rng.normal(size=(3, 3)))
    view = np.eye(4)
    view[:3, :3] = rotation
    view[:3, 3] = p_rng.normal(size=3)
    return projection @ view


def test_world_position_from_depth(composition: spy.Module, rng: np.random.Generator) -> None:
    view_projection = random_view_projection(rng)
    inverse_view_projection = np.linalg.inv(view_projection)
    # Pick points in view space inside the frustum, then map them back to world space.
    ndc = np.stack([rng.uniform(-1.0, 1.0, SAMPLE_COUNT), rng.uniform(-1.0, 1.0, SAMPLE_COUNT),
        rng.uniform(0.05, 0.99, SAMPLE_COUNT), np.ones(SAMPLE_COUNT)], axis=1)
    world = ndc @ inverse_view_projection.T
    world = world[:, :3] / world[:, 3:]
    uvs = (ndc[:, :2] * 0.5 + 0.5).astype(np.float32)
    depths = ndc[:, 2].astype(np.float32)
    rows = [np.tile(inverse_view_projection[row].astype(np.float32), (SAMPLE_COUNT, 1)) for row in range(4)]
    result = call(composition.world_position_from_depth, uvs, depths, *rows)
    np.testing.assert_allclose(result, world, rtol=1e-3, atol=1e-3)


def test_random_rsm_disk_offset_is_stratified(composition: spy.Module, rng: np.random.Generator) -> None:
    # One call covers every (receiver, light, sample) combination.
    receiver_uvs = rng.uniform(0.0, 1.0, (SAMPLE_COUNT, 2)).astype(np.float32)
    light_indices, sample_indices = np.meshgrid(np.arange(4, dtype=np.uint32),
        np.arange(RSM_SAMPLE_COUNT, dtype=np.uint32), indexing="ij")
    combination_count = light_indices.size
    offsets = call(composition.random_rsm_disk_offset, np.repeat(receiver_uvs, combination_count, axis=0),
        np.tile(light_indices.ravel(), SAMPLE_COUNT), np.tile(sample_indices.ravel(), SAMPLE_COUNT))
    radii = np.linalg.norm(offsets, axis=1)
    ring_indices = np.tile(sample_indices.ravel(), SAMPLE_COUNT)
    # Sample i lies in the ring [i, i + 1] / RSM_SAMPLE_COUNT of the unit disk.
    assert np.all(radii >= ring_indices / RSM_SAMPLE_COUNT - 1e-5)
    assert np.all(radii <= (ring_indices + 1) / RSM_SAMPLE_COUNT + 1e-5)


def test_random_rsm_disk_offset_varies(composition: spy.Module, rng: np.random.Generator) -> None:
    receiver_uvs = rng.uniform(0.0, 1.0, (SAMPLE_COUNT, 2)).astype(np.float32)
    zeros = np.zeros(SAMPLE_COUNT, dtype=np.uint32)
    offsets = call(composition.random_rsm_disk_offset, receiver_uvs, zeros, zeros)
    other_light_offsets = call(composition.random_rsm_disk_offset, receiver_uvs, zeros + 1, zeros)
    angles = np.arctan2(offsets[:, 1], offsets[:, 0])
    # The angle must cover the whole circle across receivers, and lights must not share sample patterns.
    assert np.histogram(angles, bins=8, range=(-np.pi, np.pi))[0].min() > SAMPLE_COUNT // 16
    assert np.mean(np.all(np.isclose(offsets, other_light_offsets), axis=1)) < 0.01
