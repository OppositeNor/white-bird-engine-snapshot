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


RANDOM_SEED = 20261024
# Must match radix_sort_utils.slang and radix_sort_uint2_utils.slang.
WORKGROUP_SIZE = 64
BUCKET_COUNT = 256
DIGIT_BIT_SIZE = 8
ELEMENTS_PER_WORKGROUP = 256
MAX_WORKGROUP_COUNT = 16384
SCAN_HISTOGRAM_NUM = 16
DATA_START = 4
KEY_BYTE_SIZE = 8
LANE_START = 4
SUB_HISTOGRAM_NUM_INDEX = 0
OVERFLOW_START_INDEX = 1
LANE_START_INDEX = 2

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]
USAGE = spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def local_sort_num(p_element_count: int) -> int:
    return min(max((p_element_count + ELEMENTS_PER_WORKGROUP - 1) // ELEMENTS_PER_WORKGROUP, 1), MAX_WORKGROUP_COUNT)


def make_key_buffer(p_device: spy.Device, p_keys: np.ndarray) -> spy.Buffer:
    """A uint2 key buffer holding the header, with the byte size of the keys, and p_keys, an (n, 2) uint32 array of (x, y)
    keys."""
    metadata = np.array([len(p_keys) * KEY_BYTE_SIZE, local_sort_num(len(p_keys)), 0, 0], dtype=np.uint32)
    data = np.concatenate([metadata, p_keys.reshape(-1)])
    return p_device.create_buffer(element_count=len(data) // 2, struct_size=8, usage=USAGE, data=data)


def read_keys(p_buffer: spy.Buffer) -> tuple[np.ndarray, np.ndarray]:
    """The metadata and the (n, 2) keys of a uint2 key buffer."""
    data = p_buffer.to_numpy().view(np.uint32)
    return data[:DATA_START], data[DATA_START:].reshape(-1, 2)


def digits(p_keys: np.ndarray, p_bit_region: int) -> np.ndarray:
    values = (p_keys[:, 0].astype(np.uint64) << np.uint64(32)) | p_keys[:, 1].astype(np.uint64)
    return ((values >> np.uint64(p_bit_region)) & np.uint64(BUCKET_COUNT - 1)).astype(np.int64)


def run_pass(p_device: spy.Device, p_load_kernel: KernelLoader, p_input: spy.Buffer, p_output: spy.Buffer,
        p_keys: np.ndarray, p_bit_region: int) -> None:
    """Runs one radix sort pass on p_keys, the keys of p_input, into p_output. The histogram prefix sum between the
    histogram and scatter passes is computed on the host, after the histogram pass is checked."""
    sub_histogram_num = local_sort_num(len(p_keys))
    overflow_num = (sub_histogram_num + SCAN_HISTOGRAM_NUM - 1) // SCAN_HISTOGRAM_NUM
    histogram = p_device.create_buffer(element_count=LANE_START + (sub_histogram_num + overflow_num) * BUCKET_COUNT,
        struct_size=4, usage=USAGE)
    thread_count = [sub_histogram_num * WORKGROUP_SIZE, 1, 1]
    p_load_kernel("radix_sort_uint2.slang", {"KERNEL_PASS": 0, "WBE_RADIX_HISTOGRAM_BIT_REGION": p_bit_region}).dispatch(
        thread_count=thread_count, vars={"input_keys": p_input, "histogram_buffer": histogram, "output_keys": p_output})

    histogram_data = histogram.to_numpy().view(np.uint32).copy()
    assert histogram_data[SUB_HISTOGRAM_NUM_INDEX] == sub_histogram_num
    assert histogram_data[OVERFLOW_START_INDEX] == LANE_START + sub_histogram_num * BUCKET_COUNT
    assert histogram_data[LANE_START_INDEX] == LANE_START
    lanes_end = LANE_START + sub_histogram_num * BUCKET_COUNT
    sub_histograms = histogram_data[LANE_START:lanes_end].reshape(sub_histogram_num, BUCKET_COUNT)
    elements_per_group = (len(p_keys) + sub_histogram_num - 1) // sub_histogram_num
    key_digits = digits(p_keys, p_bit_region)
    for group in range(sub_histogram_num):
        group_digits = key_digits[group * elements_per_group:(group + 1) * elements_per_group]
        np.testing.assert_array_equal(sub_histograms[group], np.bincount(group_digits, minlength=BUCKET_COUNT))
    histogram_data[LANE_START:lanes_end] = np.cumsum(sub_histograms, axis=0, dtype=np.uint32).reshape(-1)
    histogram.copy_from_numpy(histogram_data)

    p_load_kernel("radix_sort_uint2.slang", {"KERNEL_PASS": 1, "WBE_RADIX_HISTOGRAM_BIT_REGION": p_bit_region}).dispatch(
        thread_count=thread_count, vars={"input_keys": p_input, "histogram_buffer": histogram, "output_keys": p_output})


def sort_reference(p_keys: np.ndarray, p_begin_bit: int, p_end_bit: int) -> np.ndarray:
    values = (p_keys[:, 0].astype(np.uint64) << np.uint64(32)) | p_keys[:, 1].astype(np.uint64)
    mask = np.uint64((1 << (p_end_bit - p_begin_bit)) - 1) if p_end_bit - p_begin_bit < 64 else np.uint64(2 ** 64 - 1)
    return p_keys[np.argsort((values >> np.uint64(p_begin_bit)) & mask, kind="stable")]


def check_sort(p_device: spy.Device, p_load_kernel: KernelLoader, p_keys: np.ndarray, p_begin_bit: int,
        p_end_bit: int) -> None:
    buffers = [make_key_buffer(p_device, p_keys), make_key_buffer(p_device, np.zeros_like(p_keys))]
    keys = p_keys
    for pass_index, bit_region in enumerate(range(p_begin_bit, p_end_bit, DIGIT_BIT_SIZE)):
        run_pass(p_device, p_load_kernel, buffers[pass_index % 2], buffers[(pass_index + 1) % 2], keys, bit_region)
        metadata, keys = read_keys(buffers[(pass_index + 1) % 2])
        np.testing.assert_array_equal(metadata[:2], [len(p_keys) * KEY_BYTE_SIZE, local_sort_num(len(p_keys))])
        np.testing.assert_array_equal(keys, sort_reference(p_keys, p_begin_bit, bit_region + DIGIT_BIT_SIZE))
    np.testing.assert_array_equal(keys, sort_reference(p_keys, p_begin_bit, p_end_bit))


def random_keys(p_rng: np.random.Generator, p_count: int, p_x_limit: int = 2 ** 32) -> np.ndarray:
    return np.stack([p_rng.integers(0, p_x_limit, p_count, dtype=np.uint32),
        p_rng.integers(0, 2 ** 32, p_count, dtype=np.uint32)], axis=1)


# Counts below one batch, of exactly one batch, spanning several local sorts, and past 16 local sorts, so that the
# sub-histograms span several scan groups.
@pytest.mark.parametrize("p_count", [1, 37, 256, 1000, 70000])
def test_radix_sort_uint2_full(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_count: int) -> None:
    check_sort(device, load_kernel, random_keys(rng, p_count), 0, 64)


@pytest.mark.parametrize("p_count", [1000, 70000])
def test_radix_sort_uint2_key_value(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_count: int) -> None:
    # Few distinct x make long runs of equal keys, whose y payloads must keep their input order.
    check_sort(device, load_kernel, random_keys(rng, p_count, 16), 32, 64)


@pytest.mark.parametrize(("p_begin_bit", "p_end_bit"), [(0, 16), (16, 48), (24, 40)])
def test_radix_sort_uint2_bit_range(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_begin_bit: int, p_end_bit: int) -> None:
    check_sort(device, load_kernel, random_keys(rng, 5000), p_begin_bit, p_end_bit)


def test_radix_sort_uint2_equal_keys(device: spy.Device, load_kernel: KernelLoader) -> None:
    keys = np.full((3000, 2), 0xFFFFFFFF, dtype=np.uint32)
    check_sort(device, load_kernel, keys, 0, 64)


def test_radix_sort_uint2_multiple_batches_per_local_sort(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    # Past MAX_WORKGROUP_COUNT local sorts of one batch, each local sort scatters several batches.
    count = MAX_WORKGROUP_COUNT * ELEMENTS_PER_WORKGROUP + 1000
    check_sort(device, load_kernel, random_keys(rng, count), 48, 64)
