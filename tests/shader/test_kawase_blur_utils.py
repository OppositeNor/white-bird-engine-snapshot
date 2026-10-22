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


RANDOM_SEED = 20251003
SOURCE_WIDTH = 16
SOURCE_HEIGHT = 12

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def run_kawase_blur(p_device: spy.Device, p_load_kernel: KernelLoader, p_source: np.ndarray,
        p_offset: float) -> np.ndarray:
    height, width = p_source.shape[:2]
    kernel = p_load_kernel("kawase_blur.slang", {"SOURCE_WIDTH": width, "SOURCE_HEIGHT": height,
        "BLUR_OFFSET": f"{p_offset:.6f}"})
    texture = p_device.create_texture(format=spy.Format.rgba32_float, width=width, height=height,
        usage=spy.TextureUsage.shader_resource, data=p_source)
    sampler = p_device.create_sampler(min_filter=spy.TextureFilteringMode.linear,
        mag_filter=spy.TextureFilteringMode.linear, mip_filter=spy.TextureFilteringMode.point,
        address_u=spy.TextureAddressingMode.clamp_to_edge, address_v=spy.TextureAddressingMode.clamp_to_edge)
    output = p_device.create_buffer(element_count=width * height, struct_size=16,
        usage=spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access)
    kernel.dispatch(thread_count=[width, height, 1], vars={"source": texture, "source_sampler": sampler,
        "output": output})
    return output.to_numpy().view(np.float32).reshape(height, width, 4)


def bilinear_sample_clamped(p_source: np.ndarray, p_x: np.ndarray, p_y: np.ndarray) -> np.ndarray:
    """Bilinear sample at continuous texel coordinates (texel centers at +0.5) with clamp-to-edge addressing."""
    height, width = p_source.shape[:2]
    x = p_x - 0.5
    y = p_y - 0.5
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    fx = (x - x0)[..., None]
    fy = (y - y0)[..., None]

    def texel(p_texel_x: np.ndarray, p_texel_y: np.ndarray) -> np.ndarray:
        return p_source[np.clip(p_texel_y, 0, height - 1), np.clip(p_texel_x, 0, width - 1)]

    top = texel(x0, y0) * (1.0 - fx) + texel(x0 + 1, y0) * fx
    bottom = texel(x0, y0 + 1) * (1.0 - fx) + texel(x0 + 1, y0 + 1) * fx
    return top * (1.0 - fy) + bottom * fy


def kawase_blur_reference(p_source: np.ndarray, p_offset: float) -> np.ndarray:
    height, width = p_source.shape[:2]
    center_y, center_x = np.meshgrid(np.arange(height) + 0.5, np.arange(width) + 0.5, indexing="ij")
    taps = [bilinear_sample_clamped(p_source, center_x + sign_x * p_offset, center_y + sign_y * p_offset)
        for sign_x in (-1.0, 1.0) for sign_y in (-1.0, 1.0)]
    return np.mean(taps, axis=0)


def test_kawase_blur_zero_offset_is_identity(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    source = rng.uniform(0.0, 1.0, (SOURCE_HEIGHT, SOURCE_WIDTH, 4)).astype(np.float32)
    np.testing.assert_allclose(run_kawase_blur(device, load_kernel, source, 0.0), source, atol=1e-6)


def test_kawase_blur_constant_texture(device: spy.Device, load_kernel: KernelLoader) -> None:
    source = np.tile(np.array([0.25, 0.5, 0.75, 1.0], dtype=np.float32), (SOURCE_HEIGHT, SOURCE_WIDTH, 1))
    for offset in [0.5, 1.5, 2.5]:
        np.testing.assert_allclose(run_kawase_blur(device, load_kernel, source, offset), source, atol=1e-6)


@pytest.mark.parametrize("p_offset", [0.5, 1.5, 2.5, 3.5])
def test_kawase_blur(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_offset: float) -> None:
    # Half-texel offsets land every tap on a texel corner, where bilinear weights are exactly one quarter, so the
    # comparison is not limited by the sampler's fixed-point filtering precision.
    source = rng.uniform(0.0, 1.0, (SOURCE_HEIGHT, SOURCE_WIDTH, 4)).astype(np.float32)
    result = run_kawase_blur(device, load_kernel, source, p_offset)
    np.testing.assert_allclose(result, kawase_blur_reference(source.astype(np.float64), p_offset), atol=1e-5)


def test_kawase_blur_preserves_mean_away_from_edges(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    # A single bright texel spreads into a 4x4 footprint for a 1.5 offset without losing energy.
    source = np.zeros((SOURCE_HEIGHT, SOURCE_WIDTH, 4), dtype=np.float32)
    source[6, 8] = 1.0
    result = run_kawase_blur(device, load_kernel, source, 1.5)
    np.testing.assert_allclose(result.sum(axis=(0, 1)), 1.0, atol=1e-5)
    assert np.count_nonzero(result[..., 0]) == 16
