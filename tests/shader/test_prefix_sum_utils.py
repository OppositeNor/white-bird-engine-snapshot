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
FIXED_ELEMENT_COUNT = 256
FIXED_WORKGROUP_SIZE = 64
# Guard elements around the summed range; they must stay untouched.
GUARD_COUNT = 64

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def create_data_buffer(p_device: spy.Device, p_values: np.ndarray) -> spy.Buffer:
    return p_device.create_buffer(element_count=len(p_values), struct_size=4,
        usage=spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access, data=p_values)


def run_inclusive_prefix_sum_64_256(p_device: spy.Device, p_load_kernel: KernelLoader, p_values: np.ndarray) -> np.ndarray:
    kernel = p_load_kernel("workgroup_inclusive_prefix_sum_64_256.slang", {})
    buffer = create_data_buffer(p_device, p_values)
    kernel.dispatch(thread_count=[FIXED_WORKGROUP_SIZE, 1, 1], vars={"data": buffer})
    return buffer.to_numpy().view(np.uint32)


def test_workgroup_inclusive_prefix_sum_64_256(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    values = rng.integers(0, 1000, FIXED_ELEMENT_COUNT, dtype=np.uint32)
    result = run_inclusive_prefix_sum_64_256(device, load_kernel, values)
    np.testing.assert_array_equal(result, np.cumsum(values, dtype=np.uint32))


def test_workgroup_inclusive_prefix_sum_64_256_ones(device: spy.Device, load_kernel: KernelLoader) -> None:
    values = np.ones(FIXED_ELEMENT_COUNT, dtype=np.uint32)
    result = run_inclusive_prefix_sum_64_256(device, load_kernel, values)
    np.testing.assert_array_equal(result, np.arange(1, FIXED_ELEMENT_COUNT + 1, dtype=np.uint32))


def test_workgroup_inclusive_prefix_sum_64_256_single_element(device: spy.Device, load_kernel: KernelLoader) -> None:
    # A lone value at the front must reach every later element and nothing may be added twice.
    values = np.zeros(FIXED_ELEMENT_COUNT, dtype=np.uint32)
    values[0] = 7
    result = run_inclusive_prefix_sum_64_256(device, load_kernel, values)
    np.testing.assert_array_equal(result, np.full(FIXED_ELEMENT_COUNT, 7, dtype=np.uint32))


def run_general_prefix_sum(p_device: spy.Device, p_load_kernel: KernelLoader, p_data: np.ndarray, p_data_start: int,
        p_data_count: int, p_workgroup_size: int) -> np.ndarray:
    kernel = p_load_kernel("workgroup_general_prefix_sum.slang", {"KERNEL_WORKGROUP_SIZE": p_workgroup_size,
        "DATA_START": p_data_start, "DATA_COUNT": p_data_count})
    buffer = create_data_buffer(p_device, p_data)
    kernel.dispatch(thread_count=[p_workgroup_size, 1, 1], vars={"data": buffer})
    return buffer.to_numpy().view(np.uint32)


def general_prefix_sum_input(p_rng: np.random.Generator, p_data_start: int, p_data_count: int,
        p_values: np.ndarray | None = None) -> np.ndarray:
    """Random guards around the range."""
    data = p_rng.integers(0, 1 << 20, p_data_start + p_data_count + GUARD_COUNT, dtype=np.uint32)
    if p_values is not None:
        data[p_data_start:p_data_start + p_data_count] = p_values
    return data


def general_prefix_sum_expected(p_data: np.ndarray, p_data_start: int, p_data_count: int) -> np.ndarray:
    expected = p_data.copy()
    expected[p_data_start:p_data_start + p_data_count] = np.cumsum(p_data[p_data_start:p_data_start + p_data_count],
        dtype=np.uint32)
    return expected


# (data start, data count, workgroup size). Counts cover a partial first chunk, counts that are not multiples of 4,
# exactly one chunk of 4 * workgroup size elements, and several chunks with a partial last one.
GENERAL_PREFIX_SUM_SHAPES = [
    (0, 1, 64),
    (0, 3, 64),
    (0, 4, 64),
    (0, 100, 64),
    (0, 255, 64),
    (0, 256, 64),
    (0, 257, 64),
    (0, 1000, 64),
    (0, 4096, 256),
    (0, 1000, 32),
    (13, 100, 64),
    (8, 1000, 64),
    (64, 4096, 128),
]


@pytest.mark.parametrize(("p_data_start", "p_data_count", "p_workgroup_size"), GENERAL_PREFIX_SUM_SHAPES)
def test_workgroup_general_prefix_sum(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_data_start: int, p_data_count: int, p_workgroup_size: int) -> None:
    # Elements outside the range must be left unchanged.
    data = general_prefix_sum_input(rng, p_data_start, p_data_count, rng.integers(0, 1000, p_data_count,
        dtype=np.uint32))
    result = run_general_prefix_sum(device, load_kernel, data, p_data_start, p_data_count, p_workgroup_size)
    np.testing.assert_array_equal(result, general_prefix_sum_expected(data, p_data_start, p_data_count))


@pytest.mark.parametrize(("p_data_count", "p_workgroup_size"), [(1000, 64), (4096, 256)])
def test_workgroup_general_prefix_sum_single_element(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator, p_data_count: int, p_workgroup_size: int) -> None:
    # A lone value at the front must reach every later chunk, and nothing may be added twice.
    values = np.zeros(p_data_count, dtype=np.uint32)
    values[0] = 7
    data = general_prefix_sum_input(rng, 0, p_data_count, values)
    result = run_general_prefix_sum(device, load_kernel, data, 0, p_data_count, p_workgroup_size)
    np.testing.assert_array_equal(result, general_prefix_sum_expected(data, 0, p_data_count))
