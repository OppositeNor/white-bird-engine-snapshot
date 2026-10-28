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


# Must match utils/counted_buffer.slang.
HEADER_WORD_NUM = 4
WORKGROUP_SIZE = 64

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]
USAGE = spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access
# Element type name to the number of uints per element.
ELEMENT_WIDTHS = {"uint": 1, "uint2": 2, "uint3": 3}
ELEMENT_TYPES = list(ELEMENT_WIDTHS)


def make_counted_buffer(p_device: spy.Device, p_element_type: str, p_header: list[int], p_capacity: int,
        p_fill: int = 0) -> spy.Buffer:
    """A counted buffer of p_capacity elements of p_element_type, with header words p_header and every element word
    p_fill."""
    data = np.full(HEADER_WORD_NUM + p_capacity * ELEMENT_WIDTHS[p_element_type], p_fill, dtype=np.uint32)
    data[:HEADER_WORD_NUM] = p_header
    return p_device.create_buffer(element_count=len(data), struct_size=4, usage=USAGE, data=data)


def read_counted_buffer(p_buffer: spy.Buffer, p_element_type: str) -> tuple[np.ndarray, np.ndarray]:
    """The header words and the (capacity, width) elements of a counted buffer."""
    data = p_buffer.to_numpy().view(np.uint32)
    return data[:HEADER_WORD_NUM], data[HEADER_WORD_NUM:].reshape(-1, ELEMENT_WIDTHS[p_element_type])


def create_results(p_device: spy.Device, p_count: int) -> spy.Buffer:
    return p_device.create_buffer(element_count=p_count, struct_size=4, usage=USAGE,
        data=np.zeros(p_count, dtype=np.uint32))


def element_byte_size(p_element_type: str) -> int:
    return 4 * ELEMENT_WIDTHS[p_element_type]


@pytest.mark.parametrize("p_element_type", ELEMENT_TYPES)
def test_counted_buffer_atomic_expand(device: spy.Device, load_kernel: KernelLoader, p_element_type: str) -> None:
    thread_count = 4 * WORKGROUP_SIZE
    initial_size = 3
    counts = np.arange(thread_count) % 3 + 1
    capacity = initial_size + int(counts.sum())
    byte_size = element_byte_size(p_element_type)
    buffer = make_counted_buffer(device, p_element_type, [initial_size * byte_size, 11, 22, 33], capacity, 0xFFFFFFFF)
    load_kernel("counted_buffer_operations.slang", {"ELEMENT_TYPE": p_element_type, "KERNEL_PASS": 0}).dispatch(
        thread_count=[thread_count, 1, 1], vars={"data": buffer, "results": create_results(device, 1)})

    header, elements = read_counted_buffer(buffer, p_element_type)
    np.testing.assert_array_equal(header, [capacity * byte_size, 11, 22, 33])
    np.testing.assert_array_equal(elements[:initial_size], 0xFFFFFFFF)
    reserved = elements[initial_size:]
    # Every component of an element holds the id of the invocation that reserved it.
    np.testing.assert_array_equal(reserved, np.repeat(reserved[:, :1], reserved.shape[1], axis=1))
    np.testing.assert_array_equal(np.bincount(reserved[:, 0], minlength=thread_count), counts)
    # Each invocation's elements are one contiguous range.
    for thread_id in range(thread_count):
        positions = np.flatnonzero(reserved[:, 0] == thread_id)
        assert positions[-1] - positions[0] == len(positions) - 1


@pytest.mark.parametrize("p_element_type", ELEMENT_TYPES)
def test_counted_buffer_try_atomic_expand(device: spy.Device, load_kernel: KernelLoader, p_element_type: str) -> None:
    thread_count = 4 * WORKGROUP_SIZE
    capacity = 100
    buffer = make_counted_buffer(device, p_element_type, [0, 0, 0, 0], capacity, 0xFFFFFFFF)
    results = create_results(device, thread_count)
    load_kernel("counted_buffer_operations.slang", {"ELEMENT_TYPE": p_element_type, "KERNEL_PASS": 1}).dispatch(
        thread_count=[thread_count, 1, 1], vars={"data": buffer, "results": results})

    fits = results.to_numpy().view(np.uint32)
    header, elements = read_counted_buffer(buffer, p_element_type)
    # Failed reservations still grow the byte size past the capacity.
    assert header[0] == thread_count * element_byte_size(p_element_type)
    assert fits.sum() == capacity
    np.testing.assert_array_equal(np.sort(elements[:, 0]), np.sort(np.flatnonzero(fits)))


@pytest.mark.parametrize("p_element_type", ELEMENT_TYPES)
def test_counted_buffer_operations(device: spy.Device, load_kernel: KernelLoader, p_element_type: str) -> None:
    capacity = 16
    initial_size = 5
    byte_size = element_byte_size(p_element_type)
    buffer = make_counted_buffer(device, p_element_type, [initial_size * byte_size, 11, 22, 33], capacity)
    results = create_results(device, 11)
    load_kernel("counted_buffer_operations.slang", {"ELEMENT_TYPE": p_element_type, "KERNEL_PASS": 2}).dispatch(
        thread_count=[1, 1, 1], vars={"data": buffer, "results": results})

    np.testing.assert_array_equal(results.to_numpy().view(np.uint32),
        [capacity, initial_size, 11, 22, 33, 4 * HEADER_WORD_NUM + byte_size, initial_size, initial_size + 2,
            initial_size + 5, 77, (initial_size + 5) * byte_size])
    header, elements = read_counted_buffer(buffer, p_element_type)
    np.testing.assert_array_equal(header, [(initial_size + 5) * byte_size, 11, 0x1234, 33])
    np.testing.assert_array_equal(elements[initial_size + 2], 77)


def test_counted_buffer_views_share_the_byte_size(device: spy.Device, load_kernel: KernelLoader) -> None:
    # Three uint2 elements read as six uints.
    thread_count = 2 * WORKGROUP_SIZE
    buffer = make_counted_buffer(device, "uint2", [3 * 8, 0, 0, 5], 4)
    buffer.copy_from_numpy(np.array([24, 0, 0, 5, 10, 20, 30, 40, 50, 60, 0, 0], dtype=np.uint32))
    results = create_results(device, 2)
    load_kernel("counted_buffer_operations.slang", {"ELEMENT_TYPE": "uint2", "KERNEL_PASS": 3}).dispatch(
        thread_count=[thread_count, 1, 1], vars={"data": buffer, "results": results})

    np.testing.assert_array_equal(results.to_numpy().view(np.uint32), [6, 20])
    header, _ = read_counted_buffer(buffer, "uint2")
    assert header[3] == 5 + thread_count * (thread_count - 1) // 2
