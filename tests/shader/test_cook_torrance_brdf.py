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
# Shader modules that each define the Cook-Torrance BRDF terms; both must implement the same functions.
BRDF_MODULE_PATHS = [
    "forward_cook_torrance_pbr/forward_cook_torrance_pbr_utils.slang",
    "deferred_composition/deferred_composition_utils.slang",
]


@pytest.fixture(scope="module", params=BRDF_MODULE_PATHS)
def brdf(request: pytest.FixtureRequest, device: spy.Device, shader_include_dir: Path) -> spy.Module:
    return spy.Module.load_from_file(device, str(shader_include_dir / request.param))


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def call(p_function: Any, *p_args: Any) -> np.ndarray:
    return p_function(*p_args, _result="numpy")


def random_unit_vectors(p_rng: np.random.Generator, p_count: int) -> np.ndarray:
    vectors = p_rng.normal(size=(p_count, 3))
    return (vectors / np.linalg.norm(vectors, axis=1, keepdims=True)).astype(np.float32)


def vectors_at_polar_angles(p_angles: np.ndarray) -> np.ndarray:
    """Unit vectors at the given angles from +z, used with a +z surface normal."""
    return np.stack([np.sin(p_angles), np.zeros_like(p_angles), np.cos(p_angles)], axis=1).astype(np.float32)


def distribution_ggx_reference(p_ndoth: np.ndarray, p_roughness: np.ndarray) -> np.ndarray:
    alpha_squared = p_roughness.astype(np.float64) ** 4
    ndoth = np.maximum(p_ndoth.astype(np.float64), 0.0)
    return alpha_squared / (np.pi * (ndoth ** 2 * (alpha_squared - 1.0) + 1.0) ** 2)


def geometry_schlick_ggx_reference(p_ndotv: np.ndarray, p_roughness: np.ndarray) -> np.ndarray:
    k = (p_roughness.astype(np.float64) + 1.0) ** 2 / 8.0
    return p_ndotv / (p_ndotv * (1.0 - k) + k)


def test_distribution_ggx(brdf: spy.Module, rng: np.random.Generator) -> None:
    normals = random_unit_vectors(rng, SAMPLE_COUNT)
    half_vectors = random_unit_vectors(rng, SAMPLE_COUNT)
    roughness = rng.uniform(0.1, 1.0, SAMPLE_COUNT).astype(np.float32)
    result = call(brdf.distribution_ggx, normals, half_vectors, roughness)
    expected = distribution_ggx_reference(np.sum(normals * half_vectors, axis=1), roughness)
    np.testing.assert_allclose(result, expected, rtol=1e-4, atol=1e-6)


def test_distribution_ggx_is_normalized(brdf: spy.Module) -> None:
    # The projected NDF integrates to one over the hemisphere: 2 pi * integral of D(theta) cos(theta) sin(theta).
    angle_count = 1 << 16
    angles = (np.arange(angle_count, dtype=np.float64) + 0.5) * (np.pi / 2.0) / angle_count
    half_vectors = vectors_at_polar_angles(angles)
    normals = np.tile(np.array([0.0, 0.0, 1.0], dtype=np.float32), (angle_count, 1))
    for roughness in [0.3, 0.5, 0.8, 1.0]:
        result = call(brdf.distribution_ggx, normals, half_vectors, np.full(angle_count, roughness, dtype=np.float32))
        integral = 2.0 * np.pi * np.sum(result * np.cos(angles) * np.sin(angles)) * (np.pi / 2.0) / angle_count
        assert integral == pytest.approx(1.0, abs=2e-3), f"roughness {roughness}"


def test_distribution_ggx_ignores_back_facing_half_vectors(brdf: spy.Module) -> None:
    # A half vector below the surface is clamped to n.h = 0, which yields alpha^2 / pi.
    result = call(brdf.distribution_ggx, np.array([[0.0, 0.0, 1.0]], dtype=np.float32),
        np.array([[0.0, 0.0, -1.0]], dtype=np.float32), np.array([0.5], dtype=np.float32))
    np.testing.assert_allclose(result, [0.5 ** 4 / np.pi], rtol=1e-5)


def test_geometry_schlick_ggx(brdf: spy.Module, rng: np.random.Generator) -> None:
    ndotv = rng.uniform(0.0, 1.0, SAMPLE_COUNT).astype(np.float32)
    roughness = rng.uniform(0.0, 1.0, SAMPLE_COUNT).astype(np.float32)
    result = call(brdf.geometry_schlick_ggx, ndotv, roughness)
    np.testing.assert_allclose(result, geometry_schlick_ggx_reference(ndotv, roughness), rtol=1e-5, atol=1e-7)


def test_geometry_schlick_ggx_bounds(brdf: spy.Module) -> None:
    roughness = np.array([0.0, 0.5, 1.0], dtype=np.float32)
    np.testing.assert_allclose(call(brdf.geometry_schlick_ggx, np.ones(3, dtype=np.float32), roughness), 1.0, rtol=1e-6)
    np.testing.assert_array_equal(call(brdf.geometry_schlick_ggx, np.zeros(3, dtype=np.float32), roughness), 0.0)


def test_geometry_smith(brdf: spy.Module, rng: np.random.Generator) -> None:
    normals = random_unit_vectors(rng, SAMPLE_COUNT)
    view_directions = random_unit_vectors(rng, SAMPLE_COUNT)
    light_directions = random_unit_vectors(rng, SAMPLE_COUNT)
    roughness = rng.uniform(0.0, 1.0, SAMPLE_COUNT).astype(np.float32)
    result = call(brdf.geometry_smith, normals, view_directions, light_directions, roughness)
    # Directions below the surface are clamped to zero, which makes the whole term zero.
    ndotv = np.maximum(np.sum(normals * view_directions, axis=1), 0.0)
    ndotl = np.maximum(np.sum(normals * light_directions, axis=1), 0.0)
    expected = geometry_schlick_ggx_reference(ndotv, roughness) * geometry_schlick_ggx_reference(ndotl, roughness)
    np.testing.assert_allclose(result, expected, rtol=1e-4, atol=1e-6)
    assert np.all(result[(ndotv == 0.0) | (ndotl == 0.0)] == 0.0)


def test_fresnel_schlick(brdf: spy.Module, rng: np.random.Generator) -> None:
    cos_theta = rng.uniform(-0.5, 1.5, SAMPLE_COUNT).astype(np.float32)
    f0 = rng.uniform(0.0, 1.0, (SAMPLE_COUNT, 3)).astype(np.float32)
    result = call(brdf.fresnel_schlick, cos_theta, f0)
    expected = f0 + (1.0 - f0) * (1.0 - np.clip(cos_theta, 0.0, 1.0))[:, None] ** 5
    np.testing.assert_allclose(result, expected, rtol=1e-5, atol=1e-6)


def test_fresnel_schlick_limits(brdf: spy.Module) -> None:
    f0 = np.array([[0.04, 0.5, 0.95], [0.04, 0.5, 0.95]], dtype=np.float32)
    result = call(brdf.fresnel_schlick, np.array([1.0, 0.0], dtype=np.float32), f0)
    # Normal incidence reflects f0 and grazing incidence reflects everything.
    np.testing.assert_allclose(result, [[0.04, 0.5, 0.95], [1.0, 1.0, 1.0]], rtol=1e-6)
