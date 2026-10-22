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


RANDOM_SEED = 20251013
JOINT_COUNT = 8
VERTEX_COUNT = 256
BUFFER_USAGE = spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def random_rotations(p_rng: np.random.Generator, p_count: int) -> np.ndarray:
    rotations, _ = np.linalg.qr(p_rng.normal(size=(p_count, 3, 3)))
    # Flip reflections into rotations.
    rotations[np.linalg.det(rotations) < 0.0, :, 0] *= -1.0
    return rotations


def random_skinning_transforms(p_rng: np.random.Generator, p_count: int) -> np.ndarray:
    """Random affine 4x4 transforms built from a rotation, a uniform scale, and a translation."""
    transforms = np.tile(np.eye(4), (p_count, 1, 1))
    transforms[:, :3, :3] = random_rotations(p_rng, p_count) * p_rng.uniform(0.5, 2.0, size=(p_count, 1, 1))
    transforms[:, :3, 3] = p_rng.normal(size=(p_count, 3))
    return transforms.astype(np.float32)


def random_joint_bindings(p_rng: np.random.Generator, p_count: int) -> tuple[np.ndarray, np.ndarray]:
    ids = p_rng.integers(0, JOINT_COUNT, size=(p_count, 4), dtype=np.int32)
    weights = p_rng.uniform(0.0, 1.0, size=(p_count, 4))
    # Unused influences are padded with joint 0 and a zero weight, like the compiled joint bindings.
    unused = p_rng.uniform(size=(p_count, 4)) < 0.3
    unused[:, 0] = False
    ids[unused] = 0
    weights[unused] = 0.0
    weights /= weights.sum(axis=1, keepdims=True)
    return ids, weights.astype(np.float32)


def skin_reference(p_transforms: np.ndarray, p_ids: np.ndarray, p_weights: np.ndarray,
        p_points: np.ndarray, p_directions: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    blended = np.einsum("ni,nirc->nrc", p_weights.astype(np.float64), p_transforms[p_ids].astype(np.float64))
    points = np.einsum("nrc,nc->nr", blended[:, :3, :3], p_points) + blended[:, :3, 3]
    directions = np.einsum("nrc,nc->nr", blended[:, :3, :3], p_directions)
    return points, directions / np.linalg.norm(directions, axis=1, keepdims=True)


def run_skin_vertices(p_device: spy.Device, p_load_kernel: KernelLoader, p_transforms: np.ndarray, p_ids: np.ndarray,
        p_weights: np.ndarray, p_points: np.ndarray, p_directions: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    count = len(p_points)
    kernel = p_load_kernel("skeleton_skin_vertices.slang", {"VERTEX_COUNT": count})
    # SkinningTransform stores the first three matrix rows.
    transform_data = np.ascontiguousarray(p_transforms[:, :3, :], dtype=np.float32)
    binding_data = np.hstack([p_ids.astype(np.int32).view(np.uint8), p_weights.astype(np.float32).view(np.uint8)])

    def vec4_buffer(p_values: np.ndarray | None, p_w: float) -> spy.Buffer:
        data = None
        if p_values is not None:
            data = np.hstack([p_values, np.full((count, 1), p_w)]).astype(np.float32)
        return p_device.create_buffer(element_count=count, struct_size=16, usage=BUFFER_USAGE, data=data)

    skinned_points = vec4_buffer(None, 0.0)
    skinned_directions = vec4_buffer(None, 0.0)
    kernel.dispatch(thread_count=[count, 1, 1], vars={
        "skinning_transforms": p_device.create_buffer(element_count=len(transform_data), struct_size=48, usage=BUFFER_USAGE,
            data=transform_data.reshape(-1).view(np.uint8)),
        "joint_bindings": p_device.create_buffer(element_count=count, struct_size=32, usage=BUFFER_USAGE,
            data=binding_data.reshape(-1)),
        "points": vec4_buffer(p_points, 1.0),
        "directions": vec4_buffer(p_directions, 0.0),
        "skinned_points": skinned_points,
        "skinned_directions": skinned_directions,
    })
    return (skinned_points.to_numpy().view(np.float32).reshape(count, 4)[:, :3],
        skinned_directions.to_numpy().view(np.float32).reshape(count, 4)[:, :3])


def test_skin_vertices_blends_joint_transforms_by_weight(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    transforms = random_skinning_transforms(rng, JOINT_COUNT)
    ids, weights = random_joint_bindings(rng, VERTEX_COUNT)
    points = rng.normal(size=(VERTEX_COUNT, 3)).astype(np.float32)
    directions = rng.normal(size=(VERTEX_COUNT, 3)).astype(np.float32)

    skinned_points, skinned_directions = run_skin_vertices(device, load_kernel, transforms, ids, weights, points, directions)
    expected_points, expected_directions = skin_reference(transforms, ids, weights, points, directions)
    np.testing.assert_allclose(skinned_points, expected_points, rtol=1e-5, atol=1e-5)
    np.testing.assert_allclose(skinned_directions, expected_directions, rtol=1e-5, atol=1e-5)


def test_skin_vertices_with_one_influence_applies_its_joint_transform(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    transforms = random_skinning_transforms(rng, JOINT_COUNT)
    ids = np.zeros((JOINT_COUNT, 4), dtype=np.int32)
    ids[:, 0] = np.arange(JOINT_COUNT)
    weights = np.zeros((JOINT_COUNT, 4), dtype=np.float32)
    weights[:, 0] = 1.0
    points = rng.normal(size=(JOINT_COUNT, 3)).astype(np.float32)
    directions = rng.normal(size=(JOINT_COUNT, 3)).astype(np.float32)

    skinned_points, skinned_directions = run_skin_vertices(device, load_kernel, transforms, ids, weights, points, directions)
    np.testing.assert_allclose(skinned_points, np.einsum("nrc,nc->nr", transforms[:, :3, :3], points) + transforms[:, :3, 3],
        rtol=1e-5, atol=1e-5)
    expected_directions = np.einsum("nrc,nc->nr", transforms[:, :3, :3], directions)
    np.testing.assert_allclose(skinned_directions,
        expected_directions / np.linalg.norm(expected_directions, axis=1, keepdims=True), rtol=1e-5, atol=1e-5)


def test_skin_vertices_rotates_simple_skin_upper_joint(device: spy.Device, load_kernel: KernelLoader) -> None:
    # The SimpleSkin 1.0s keyframe: the upper joint at (0, 1, 0) is rotated by +90 degrees around z.
    upper = np.eye(4, dtype=np.float32)
    upper[:3, :3] = [[0.0, -1.0, 0.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]]
    upper[:3, 3] = [1.0, 1.0, 0.0]
    transforms = np.stack([np.eye(4, dtype=np.float32), upper])
    ids = np.array([[0, 0, 0, 0], [0, 1, 0, 0], [1, 0, 0, 0]], dtype=np.int32)
    weights = np.array([[1.0, 0.0, 0.0, 0.0], [0.5, 0.5, 0.0, 0.0], [1.0, 0.0, 0.0, 0.0]], dtype=np.float32)
    points = np.array([[0.5, 0.0, 0.0], [0.5, 1.0, 0.0], [0.5, 2.0, 0.0]], dtype=np.float32)
    directions = np.array([[1.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 0.0, 0.0]], dtype=np.float32)

    skinned_points, skinned_directions = run_skin_vertices(device, load_kernel, transforms, ids, weights, points, directions)
    np.testing.assert_allclose(skinned_points, [[0.5, 0.0, 0.0], [0.25, 1.25, 0.0], [-1.0, 1.5, 0.0]], atol=1e-6)
    np.testing.assert_allclose(skinned_directions, [[1.0, 0.0, 0.0], [np.sqrt(0.5), np.sqrt(0.5), 0.0], [0.0, 1.0, 0.0]],
        atol=1e-6)
