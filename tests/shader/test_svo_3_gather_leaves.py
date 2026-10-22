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

from test_svo_common import make_buffer, read_buffer


RANDOM_SEED = 20251003
# Pixel centers whose square lies within this many edge-normal lengths of an edge's half-plane are skipped, covering the
# shader's documented 0.0001 voxel-unit enlargement and float rounding.
BOUNDARY_TOLERANCE = 1e-3

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def triangle_orientation(p_triangle: np.ndarray) -> float:
    first = p_triangle[1] - p_triangle[0]
    second = p_triangle[2] - p_triangle[0]
    return float(np.sign(first[0] * second[1] - first[1] * second[0]))


def run_enlarged_edge_lines(p_device: spy.Device, p_load_kernel: KernelLoader, p_triangles: np.ndarray) -> np.ndarray:
    """Return the enlarged line of each triangle edge i, from vertex i to vertex (i + 1) % 3, shaped (triangles, 3, 3)."""
    edges = np.concatenate([p_triangles, np.roll(p_triangles, -1, axis=1)], axis=2).reshape(-1, 4).astype(np.float32)
    orientations = np.repeat([triangle_orientation(triangle) for triangle in p_triangles], 3).astype(np.float32)
    lines = make_buffer(p_device, np.zeros((len(edges), 4), dtype=np.float32))
    p_load_kernel("svo_enlarged_edge_line.slang", {}).dispatch(thread_count=[len(edges), 1, 1], vars={
        "edges": make_buffer(p_device, edges), "orientations": make_buffer(p_device, orientations), "lines": lines})
    return read_buffer(lines, np.float32).reshape(len(p_triangles), 3, 4)[:, :, :3]


def random_triangles(p_rng: np.random.Generator, p_count: int, p_extent: float) -> np.ndarray:
    triangles = p_rng.uniform(0.0, p_extent, (p_count, 3, 2))
    first = triangles[:, 1] - triangles[:, 0]
    second = triangles[:, 2] - triangles[:, 0]
    keep = np.abs(first[:, 0] * second[:, 1] - first[:, 1] * second[:, 0]) > 0.05 * p_extent
    return triangles[keep].astype(np.float32)


def square_touch_margin(p_start: np.ndarray, p_end: np.ndarray, p_orientation: float, p_centers: np.ndarray) -> np.ndarray:
    """
    Return, per pixel center, the largest inside distance of its unit square's corners from the original edge line.

    The distance is scaled by the edge length and positive toward the triangle, so a square touches the edge's closed
    inner half-plane exactly when the margin is nonnegative.
    """
    edge = (p_end - p_start).astype(np.float64)
    corners = p_centers[:, None, :] + np.array([[-0.5, -0.5], [-0.5, 0.5], [0.5, -0.5], [0.5, 0.5]])
    relative = corners - p_start.astype(np.float64)
    inside = (edge[0] * relative[..., 1] - edge[1] * relative[..., 0]) * p_orientation
    return inside.max(axis=1)


def test_get_enlarged_edge_line_points_inward(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    triangles = random_triangles(rng, 128, 16.0)
    # Reverse half the windings so both orientations are covered.
    triangles[::2] = triangles[::2, ::-1]
    lines = run_enlarged_edge_lines(device, load_kernel, triangles)
    for triangle, triangle_lines in zip(triangles, lines):
        for edge_index, line in enumerate(triangle_lines):
            start = triangle[edge_index]
            end = triangle[(edge_index + 1) % 3]
            opposite = triangle[(edge_index + 2) % 3]
            # The normal is perpendicular to the edge and the opposite vertex lies inside.
            assert abs(np.dot(line[:2], end - start)) <= 1e-4 * np.dot(end - start, end - start)
            assert np.dot(line[:2], opposite) + line[2] > 0.0


def test_get_enlarged_edge_line_covers_touched_pixels(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    triangles = random_triangles(rng, 64, 12.0)
    triangles[1::2] = triangles[1::2, ::-1]
    lines = run_enlarged_edge_lines(device, load_kernel, triangles)
    grid = np.arange(-3, 16) + 0.5
    centers = np.stack(np.meshgrid(grid, grid, indexing="ij"), axis=-1).reshape(-1, 2)
    for triangle, triangle_lines in zip(triangles, lines):
        orientation = triangle_orientation(triangle)
        for edge_index, line in enumerate(triangle_lines):
            start = triangle[edge_index]
            end = triangle[(edge_index + 1) % 3]
            margin = square_touch_margin(start, end, orientation, centers)
            inside = centers.astype(np.float64) @ line[:2].astype(np.float64) + float(line[2]) >= 0.0
            decisive = np.abs(margin) > BOUNDARY_TOLERANCE * np.linalg.norm(end - start)
            # A pixel center is inside the enlarged line exactly when its square touches the original half-plane.
            np.testing.assert_array_equal(inside[decisive], margin[decisive] >= 0.0)


def test_get_enlarged_edge_line_keeps_subpixel_triangle(device: spy.Device, load_kernel: KernelLoader) -> None:
    # A triangle inside one pixel covers no pixel center, but the enlarged triangle must contain that pixel's center.
    triangles = np.array([[[3.2, 5.1], [3.4, 5.15], [3.25, 5.3]], [[7.9, 1.05], [7.6, 1.1], [7.95, 1.3]]],
        dtype=np.float32)
    lines = run_enlarged_edge_lines(device, load_kernel, triangles)
    for triangle, triangle_lines in zip(triangles, lines):
        center = np.floor(triangle[0]) + 0.5
        assert np.all(triangle_lines[:, :2] @ center + triangle_lines[:, 2] >= 0.0)
