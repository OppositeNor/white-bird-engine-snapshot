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

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def run_max_downsample(p_device: spy.Device, p_load_kernel: KernelLoader, p_source: np.ndarray) -> np.ndarray:
    height, width = p_source.shape[:2]
    destination_width = max(width // 2, 1)
    destination_height = max(height // 2, 1)
    kernel = p_load_kernel("max_downsample.slang", {"SOURCE_WIDTH": width, "SOURCE_HEIGHT": height,
        "DESTINATION_WIDTH": destination_width, "DESTINATION_HEIGHT": destination_height})
    texture = p_device.create_texture(format=spy.Format.rgba32_float, width=width, height=height,
        usage=spy.TextureUsage.shader_resource, data=p_source)
    output = p_device.create_buffer(element_count=destination_width * destination_height, struct_size=16,
        usage=spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access)
    kernel.dispatch(thread_count=[destination_width, destination_height, 1], vars={"source": texture, "output": output})
    return output.to_numpy().view(np.float32).reshape(destination_height, destination_width, 4)


def footprint_bounds(p_source_extent: int, p_destination_extent: int, p_texel: int) -> tuple[int, int]:
    """Inclusive source range covered by one destination texel; the last texel absorbs an odd remainder."""
    begin = min(p_texel * 2, p_source_extent - 1)
    end = p_source_extent - 1 if p_texel == p_destination_extent - 1 else min(p_texel * 2 + 1, p_source_extent - 1)
    return begin, end


def max_downsample_reference(p_source: np.ndarray) -> np.ndarray:
    height, width = p_source.shape[:2]
    destination_width = max(width // 2, 1)
    destination_height = max(height // 2, 1)
    result = np.empty((destination_height, destination_width, 4), dtype=p_source.dtype)
    for y in range(destination_height):
        y_begin, y_end = footprint_bounds(height, destination_height, y)
        for x in range(destination_width):
            x_begin, x_end = footprint_bounds(width, destination_width, x)
            result[y, x] = p_source[y_begin:y_end + 1, x_begin:x_end + 1].max(axis=(0, 1))
    return result


@pytest.mark.parametrize("p_extent", [(16, 12), (17, 13), (9, 1), (1, 7), (1, 1), (2, 2)])
def test_max_downsample(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_extent: tuple[int, int]) -> None:
    width, height = p_extent
    source = rng.uniform(-1.0, 1.0, (height, width, 4)).astype(np.float32)
    np.testing.assert_array_equal(run_max_downsample(device, load_kernel, source), max_downsample_reference(source))


def test_max_downsample_takes_maximum_of_each_2x2_region(device: spy.Device, load_kernel: KernelLoader) -> None:
    source = np.zeros((4, 4, 4), dtype=np.float32)
    source[1, 0] = [1.0, 0.0, 0.0, 0.0]
    source[0, 1] = [0.0, 2.0, 0.0, 0.0]
    source[3, 3] = [0.0, 0.0, 3.0, -1.0]
    expected = np.zeros((2, 2, 4), dtype=np.float32)
    expected[0, 0] = [1.0, 2.0, 0.0, 0.0]
    expected[1, 1] = [0.0, 0.0, 3.0, 0.0]
    np.testing.assert_array_equal(run_max_downsample(device, load_kernel, source), expected)


def test_max_downsample_odd_extent_keeps_last_column_and_row(device: spy.Device, load_kernel: KernelLoader) -> None:
    source = np.zeros((5, 5, 4), dtype=np.float32)
    source[4, 4] = 7.0
    result = run_max_downsample(device, load_kernel, source)
    assert result.shape == (2, 2, 4)
    np.testing.assert_array_equal(result[1, 1], np.full(4, 7.0, dtype=np.float32))
    assert np.count_nonzero(result) == 4
