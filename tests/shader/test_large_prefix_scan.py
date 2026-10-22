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
# Must match large_prefix_scan_utils.slang.
WORKGROUP_SIZE = 64
BLOCK_SIZE = 256
SECTION_NUM = 256
DATA_START = 4
ELEMENT_COUNT_INDEX = 0
SECTION_TOTALS_START = 0
BLOCK_TOTALS_START = SECTION_NUM
# Guard elements past the scanned range; they must stay untouched.
GUARD_COUNT = 64
GUARD_VALUE = 0xDEADBEEF

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def create_buffer(p_device: spy.Device, p_values: np.ndarray) -> spy.Buffer:
    return p_device.create_buffer(element_count=len(p_values), struct_size=4,
        usage=spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access, data=p_values)


def make_scan_data(p_values: np.ndarray) -> np.ndarray:
    """The scanned buffer layout: the metadata with the element count, the elements, then the guards."""
    data = np.full(DATA_START + len(p_values) + GUARD_COUNT, GUARD_VALUE, dtype=np.uint32)
    data[:DATA_START] = 0
    data[ELEMENT_COUNT_INDEX] = len(p_values)
    data[DATA_START:DATA_START + len(p_values)] = p_values
    return data


def run_large_prefix_scan(p_device: spy.Device, p_load_kernel: KernelLoader, p_data: spy.Buffer,
        p_element_count: int) -> spy.Buffer:
    """Runs the four passes on p_data in place, in order, and returns the sections buffer."""
    block_num = (p_element_count + BLOCK_SIZE - 1) // BLOCK_SIZE
    # Stale totals from a previous scan must not leak into the result.
    sections = create_buffer(p_device, np.full(SECTION_NUM + max(block_num, 1), GUARD_VALUE, dtype=np.uint32))
    group_counts = [block_num, SECTION_NUM, 1, block_num]
    for kernel_pass, group_count in enumerate(group_counts):
        if group_count == 0:
            continue
        p_load_kernel("large_prefix_scan.slang", {"KERNEL_PASS": kernel_pass}).dispatch(
            thread_count=[group_count * WORKGROUP_SIZE, 1, 1], vars={"data": p_data, "sections": sections})
    return sections


def check_large_prefix_scan(p_device: spy.Device, p_load_kernel: KernelLoader, p_values: np.ndarray) -> None:
    data = make_scan_data(p_values)
    data_buffer = create_buffer(p_device, data)
    sections = run_large_prefix_scan(p_device, p_load_kernel, data_buffer, len(p_values))
    result = data_buffer.to_numpy().view(np.uint32)
    expected = data.copy()
    expected[DATA_START:DATA_START + len(p_values)] = np.cumsum(p_values, dtype=np.uint32)
    np.testing.assert_array_equal(result, expected)
    # The last section total is the sum of every element.
    section_totals = sections.to_numpy().view(np.uint32)[SECTION_TOTALS_START:SECTION_TOTALS_START + SECTION_NUM]
    assert section_totals[-1] == np.sum(p_values, dtype=np.uint32)


# Element counts around the block size, with one block per section, and past SECTION_NUM blocks, with several blocks
# per section and a partial last section.
ELEMENT_COUNTS = [1, 3, 255, 256, 257, 1000, BLOCK_SIZE * SECTION_NUM, BLOCK_SIZE * SECTION_NUM + 1,
    BLOCK_SIZE * SECTION_NUM * 2 + 777]


@pytest.mark.parametrize("p_element_count", ELEMENT_COUNTS)
def test_large_prefix_scan(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_element_count: int) -> None:
    check_large_prefix_scan(device, load_kernel, rng.integers(0, 1 << 8, p_element_count, dtype=np.uint32))


def test_large_prefix_scan_ones(device: spy.Device, load_kernel: KernelLoader) -> None:
    element_count = BLOCK_SIZE * SECTION_NUM * 2 + 777
    check_large_prefix_scan(device, load_kernel, np.ones(element_count, dtype=np.uint32))


def test_large_prefix_scan_wraps(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator) -> None:
    # Sums wrap modulo 2^32 like the reference.
    check_large_prefix_scan(device, load_kernel, rng.integers(0, 2 ** 32, 5000, dtype=np.uint32))


def test_large_prefix_scan_empty(device: spy.Device, load_kernel: KernelLoader) -> None:
    check_large_prefix_scan(device, load_kernel, np.zeros(0, dtype=np.uint32))
