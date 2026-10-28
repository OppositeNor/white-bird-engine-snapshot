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


RANDOM_SEED = 20251008
SORT_ELEMENT_COUNT = 256
SORT_WORKGROUP_SIZE = 64

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def check_bitonic_sort_64_256(p_device: spy.Device, p_load_kernel: KernelLoader, p_values: np.ndarray) -> None:
    kernel = p_load_kernel("workgroup_bitonic_sort_64_256.slang", {})
    usage = spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access
    data_buffer = p_device.create_buffer(element_count=SORT_ELEMENT_COUNT, struct_size=4, usage=usage, data=p_values)
    kernel.dispatch(thread_count=[SORT_WORKGROUP_SIZE, 1, 1], vars={"data": data_buffer})
    np.testing.assert_array_equal(data_buffer.to_numpy().view(np.uint32), np.sort(p_values))


# Upper bounds of the random keys: small bounds produce many equal keys, and full keys use the comparison's top bit.
@pytest.mark.parametrize("p_key_limit", [2, 2 ** 8, 2 ** 17, 2 ** 32])
def test_workgroup_bitonic_sort_64_256(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_key_limit: int) -> None:
    values = rng.integers(0, p_key_limit, SORT_ELEMENT_COUNT, dtype=np.uint32)
    check_bitonic_sort_64_256(device, load_kernel, values)


@pytest.mark.parametrize("p_value", [0, 0xFFFFFFFF])
def test_workgroup_bitonic_sort_64_256_identical(device: spy.Device, load_kernel: KernelLoader, p_value: int) -> None:
    values = np.full(SORT_ELEMENT_COUNT, p_value, dtype=np.uint32)
    check_bitonic_sort_64_256(device, load_kernel, values)


def test_workgroup_bitonic_sort_64_256_descending(device: spy.Device, load_kernel: KernelLoader) -> None:
    values = np.arange(SORT_ELEMENT_COUNT - 1, -1, -1, dtype=np.uint32)
    check_bitonic_sort_64_256(device, load_kernel, values)


def test_workgroup_bitonic_sort_64_256_sorted(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    values = np.sort(rng.integers(0, 2 ** 32, SORT_ELEMENT_COUNT, dtype=np.uint32))
    check_bitonic_sort_64_256(device, load_kernel, values)


def test_workgroup_bitonic_sort_64_256_permutation(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    # Distinct keys, as in the radix sort scatter's digit and batch index entries, so any misplaced element shows up.
    values = rng.permutation(SORT_ELEMENT_COUNT).astype(np.uint32)
    check_bitonic_sort_64_256(device, load_kernel, values)


LARGE_SORT_ELEMENT_COUNT = 1024
LARGE_SORT_WORKGROUP_SIZE = 256


def check_bitonic_sort_256_1024(p_device: spy.Device, p_load_kernel: KernelLoader, p_values: np.ndarray) -> None:
    kernel = p_load_kernel("workgroup_bitonic_sort_256_1024.slang", {"WBE_TEST_KEY_TYPE": "uint"})
    usage = spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access
    data_buffer = p_device.create_buffer(element_count=LARGE_SORT_ELEMENT_COUNT, struct_size=4, usage=usage,
        data=p_values)
    kernel.dispatch(thread_count=[LARGE_SORT_WORKGROUP_SIZE, 1, 1], vars={"data": data_buffer})
    np.testing.assert_array_equal(data_buffer.to_numpy().view(np.uint32), np.sort(p_values))


def check_bitonic_sort_256_1024_uint2(p_device: spy.Device, p_load_kernel: KernelLoader, p_values: np.ndarray) -> None:
    """
    Sort uint2 keys, given as an (n, 2) array of (x, y), and compare against a lexicographic sort by x and then by y.
    """
    kernel = p_load_kernel("workgroup_bitonic_sort_256_1024.slang", {"WBE_TEST_KEY_TYPE": "uint2"})
    usage = spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access
    data_buffer = p_device.create_buffer(element_count=LARGE_SORT_ELEMENT_COUNT, struct_size=8, usage=usage,
        data=np.ascontiguousarray(p_values))
    kernel.dispatch(thread_count=[LARGE_SORT_WORKGROUP_SIZE, 1, 1], vars={"data": data_buffer})
    expected = p_values[np.lexsort((p_values[:, 1], p_values[:, 0]))]
    np.testing.assert_array_equal(data_buffer.to_numpy().view(np.uint32).reshape(-1, 2), expected)


@pytest.mark.parametrize("p_key_limit", [2, 2 ** 8, 2 ** 17, 2 ** 32])
def test_workgroup_bitonic_sort_256_1024(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_key_limit: int) -> None:
    values = rng.integers(0, p_key_limit, LARGE_SORT_ELEMENT_COUNT, dtype=np.uint32)
    check_bitonic_sort_256_1024(device, load_kernel, values)


def test_workgroup_bitonic_sort_256_1024_descending(device: spy.Device, load_kernel: KernelLoader) -> None:
    values = np.arange(LARGE_SORT_ELEMENT_COUNT - 1, -1, -1, dtype=np.uint32)
    check_bitonic_sort_256_1024(device, load_kernel, values)


def test_workgroup_bitonic_sort_256_1024_permutation(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    values = rng.permutation(LARGE_SORT_ELEMENT_COUNT).astype(np.uint32)
    check_bitonic_sort_256_1024(device, load_kernel, values)


# Small bounds make many keys share x, so the order of y among equal x is checked; full keys use both top bits.
@pytest.mark.parametrize("p_key_limit", [2, 2 ** 8, 2 ** 32])
def test_workgroup_bitonic_sort_256_1024_uint2(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator, p_key_limit: int) -> None:
    values = rng.integers(0, p_key_limit, (LARGE_SORT_ELEMENT_COUNT, 2), dtype=np.uint32)
    check_bitonic_sort_256_1024_uint2(device, load_kernel, values)


def test_workgroup_bitonic_sort_256_1024_uint2_equal_x(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    # Every key shares x, so only y orders them.
    values = np.stack([np.full(LARGE_SORT_ELEMENT_COUNT, 7, dtype=np.uint32),
        rng.permutation(LARGE_SORT_ELEMENT_COUNT).astype(np.uint32)], axis=1)
    check_bitonic_sort_256_1024_uint2(device, load_kernel, values)


def test_workgroup_bitonic_sort_256_1024_uint2_permutation(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    # Distinct 64-bit keys whose x and y orders disagree, so comparing the wrong component first misorders them.
    order = rng.permutation(LARGE_SORT_ELEMENT_COUNT).astype(np.uint32)
    values = np.stack([order // 32, (LARGE_SORT_ELEMENT_COUNT - 1 - order) % 32], axis=1).astype(np.uint32)
    check_bitonic_sort_256_1024_uint2(device, load_kernel, values)
