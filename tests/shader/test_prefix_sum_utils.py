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


def run_general_prefix_scan_generic(p_device: spy.Device, p_load_kernel: KernelLoader, p_data: np.ndarray,
        p_data_start: int, p_data_count: int, p_element_type: str, p_operator: str) -> np.ndarray:
    kernel = p_load_kernel("workgroup_general_prefix_scan_generic.slang", {"KERNEL_WORKGROUP_SIZE": FIXED_WORKGROUP_SIZE,
        "DATA_START": p_data_start, "DATA_COUNT": p_data_count, "ELEMENT_TYPE": p_element_type,
        "OPERATOR": p_operator})
    buffer = p_device.create_buffer(element_count=len(p_data), struct_size=p_data.itemsize * p_data[0].size,
        usage=spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access, data=p_data)
    kernel.dispatch(thread_count=[FIXED_WORKGROUP_SIZE, 1, 1], vars={"data": buffer})
    return buffer.to_numpy().view(np.uint32).reshape(p_data.shape)


@pytest.mark.parametrize("p_data_count", [0, 100, 1000])
def test_workgroup_general_prefix_scan_uint2_add(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator, p_data_count: int) -> None:
    # Each component is summed independently; elements outside the range must be left unchanged.
    data_start = 8
    data = rng.integers(0, 1000, (data_start + p_data_count + GUARD_COUNT, 2), dtype=np.uint32)
    expected = data.copy()
    expected[data_start:data_start + p_data_count] = np.cumsum(data[data_start:data_start + p_data_count], axis=0,
        dtype=np.uint32)
    result = run_general_prefix_scan_generic(device, load_kernel, data, data_start, p_data_count, "uint2", "Add")
    np.testing.assert_array_equal(result, expected)


def test_workgroup_general_prefix_scan_non_commutative(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    # Each element becomes the last non-zero element up to it, which holds only if earlier elements are passed first.
    data_count = 1000
    values = rng.integers(0, 1000, data_count, dtype=np.uint32)
    values[rng.random(data_count) < 0.7] = 0
    data = general_prefix_sum_input(rng, 0, data_count, values)
    expected = data.copy()
    last = np.uint32(0)
    for i in range(data_count):
        last = values[i] if values[i] != 0 else last
        expected[i] = last
    result = run_general_prefix_scan_generic(device, load_kernel, data, 0, data_count, "uint", "LastNonZero")
    np.testing.assert_array_equal(result, expected)


def segmented_scan_expected(p_values: np.ndarray, p_flags: np.ndarray) -> np.ndarray:
    """The (value, flag) pairs of the segmented inclusive prefix sum: each value is the sum from the start of its segment,
    which begins at an element with a nonzero flag or at the start of the range, and each flag is 1 when a segment starts
    at or before the element."""
    result = np.empty((len(p_values), 2), dtype=np.uint32)
    total = 0
    any_flag = 0
    for i, (value, flag) in enumerate(zip(p_values.tolist(), p_flags.tolist())):
        total = value if flag else (total + value) & 0xFFFFFFFF
        any_flag |= 1 if flag else 0
        result[i] = (total, any_flag)
    return result


def segmented_input(p_rng: np.random.Generator, p_count: int, p_flag_probability: float) -> tuple[np.ndarray, np.ndarray]:
    values = p_rng.integers(0, 1000, p_count, dtype=np.uint32)
    flags = (p_rng.random(p_count) < p_flag_probability).astype(np.uint32)
    return values, flags


def run_segmented_scan(p_device: spy.Device, p_load_kernel: KernelLoader, p_data: np.ndarray,
        p_defines: dict[str, str | int], p_thread_count: int) -> tuple[np.ndarray, np.ndarray]:
    """Runs the segmented scan kernel over p_data, an (n, 2) array of (value, flag) pairs, and returns the buffer and the
    (value, flag) total the general scan returns."""
    kernel = p_load_kernel("segmented_prefix_scan.slang", p_defines)
    usage = spy.BufferUsage.shader_resource | spy.BufferUsage.unordered_access
    buffer = p_device.create_buffer(element_count=len(p_data), struct_size=8, usage=usage, data=p_data)
    total = p_device.create_buffer(element_count=1, struct_size=8, usage=usage,
        data=np.full(2, 0xDEADBEEF, dtype=np.uint32))
    kernel.dispatch(thread_count=[p_thread_count, 1, 1], vars={"data": buffer, "total": total})
    return buffer.to_numpy().view(np.uint32).reshape(p_data.shape), total.to_numpy().view(np.uint32)


def run_general_segmented_scan(p_device: spy.Device, p_load_kernel: KernelLoader, p_rng: np.random.Generator,
        p_values: np.ndarray, p_flags: np.ndarray, p_data_start: int, p_workgroup_size: int,
        p_operator: str = "SegmentedPrefix") -> None:
    data_count = len(p_values)
    # Random guards around the range, with random flags, must stay untouched.
    data = p_rng.integers(0, 1 << 20, (p_data_start + data_count + GUARD_COUNT, 2), dtype=np.uint32)
    data[:, 1] &= 1
    data[p_data_start:p_data_start + data_count, 0] = p_values
    data[p_data_start:p_data_start + data_count, 1] = p_flags
    expected = data.copy()
    scanned = SEGMENTED_SCAN_REFERENCES[p_operator](p_values, p_flags)
    expected[p_data_start:p_data_start + data_count] = scanned
    result, total = run_segmented_scan(p_device, p_load_kernel, data, {"OPERATOR": p_operator, "KERNEL_SCAN": 0,
        "KERNEL_WORKGROUP_SIZE": p_workgroup_size, "DATA_START": p_data_start, "DATA_COUNT": data_count},
        p_workgroup_size)
    np.testing.assert_array_equal(result, expected)
    # The returned total combines the whole range, so it equals the scan of the last element.
    np.testing.assert_array_equal(total, scanned[-1])


# (data start, data count, workgroup size, flag probability). Sparse flags make segments that span invocations and chunks
# of 4 * workgroup size elements; dense flags make many short segments.
SEGMENTED_SCAN_SHAPES = [
    (0, 1, 64, 0.0),
    (0, 3, 64, 0.5),
    (0, 257, 64, 0.01),
    (0, 1000, 64, 0.01),
    (0, 1000, 64, 0.3),
    (13, 1000, 64, 0.05),
    (0, 4096, 256, 0.002),
    (64, 4096, 128, 0.2),
]


@pytest.mark.parametrize(("p_data_start", "p_data_count", "p_workgroup_size", "p_flag_probability"),
    SEGMENTED_SCAN_SHAPES)
def test_workgroup_general_prefix_sum_segmented(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator,
        p_data_start: int, p_data_count: int, p_workgroup_size: int, p_flag_probability: float) -> None:
    values, flags = segmented_input(rng, p_data_count, p_flag_probability)
    run_general_segmented_scan(device, load_kernel, rng, values, flags, p_data_start, p_workgroup_size)


def test_workgroup_general_prefix_sum_segmented_without_flags(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    # Without flags the whole range is one segment, so the result is the plain prefix sum.
    values = rng.integers(0, 1000, 1000, dtype=np.uint32)
    run_general_segmented_scan(device, load_kernel, rng, values, np.zeros(1000, dtype=np.uint32), 0, 64)


def test_workgroup_general_prefix_sum_segmented_every_element_flagged(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    # Every element starts its own segment, so every value is unchanged.
    values = rng.integers(0, 1000, 1000, dtype=np.uint32)
    run_general_segmented_scan(device, load_kernel, rng, values, np.ones(1000, dtype=np.uint32), 0, 64)


def test_workgroup_general_prefix_sum_segmented_boundary_flags(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    # Segments starting exactly at and right after invocation (4-element) and chunk (256-element) boundaries, and one
    # segment spanning a whole chunk, must neither leak earlier sums nor drop any.
    values = rng.integers(0, 1000, 1000, dtype=np.uint32)
    flags = np.zeros(1000, dtype=np.uint32)
    flags[[4, 5, 255, 256, 257, 300, 768]] = 1
    run_general_segmented_scan(device, load_kernel, rng, values, flags, 0, 64)


@pytest.mark.parametrize("p_flag_probability", [0.0, 0.02, 0.3, 1.0])
def test_workgroup_inclusive_prefix_sum_64_256_segmented(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator, p_flag_probability: float) -> None:
    values, flags = segmented_input(rng, FIXED_ELEMENT_COUNT, p_flag_probability)
    # Segments starting at and around invocation boundaries.
    if 0.0 < p_flag_probability < 1.0:
        flags[[3, 4, 128, 129]] = 1
    data = np.stack([values, flags], axis=1)
    result, _ = run_segmented_scan(device, load_kernel, data, {"OPERATOR": "SegmentedPrefix", "KERNEL_SCAN": 1},
        FIXED_WORKGROUP_SIZE)
    np.testing.assert_array_equal(result, segmented_scan_expected(values, flags))


def test_workgroup_general_prefix_sum_segmented_any_nonzero_flag(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    # Any nonzero flag starts a segment, and the combined flag is 1.
    values = rng.integers(0, 1000, 1000, dtype=np.uint32)
    flags = np.where(rng.random(1000) < 0.05, rng.integers(1, 1 << 31, 1000, dtype=np.uint32), 0).astype(np.uint32)
    flags[[256, 300]] = [2, 0xFFFFFFFF]
    run_general_segmented_scan(device, load_kernel, rng, values, flags, 0, 64)


def segmented_break_scan_expected(p_values: np.ndarray, p_flags: np.ndarray) -> np.ndarray:
    """The (value, flag) pairs of the SegmentedBreakPrefix scan: each value is unchanged, and each flag is the inclusive
    prefix sum of the flags."""
    return np.stack([p_values, np.cumsum(p_flags, dtype=np.uint32)], axis=1)


SEGMENTED_SCAN_REFERENCES: dict[str, Callable[[np.ndarray, np.ndarray], np.ndarray]] = {
    "SegmentedPrefix": segmented_scan_expected,
    "SegmentedBreakPrefix": segmented_break_scan_expected,
}


# (data start, data count, workgroup size, flag probability), with counts that fill the last chunk of 4 * workgroup size
# elements exactly and counts that leave it partial.
SEGMENTED_BREAK_SCAN_SHAPES = [
    (0, 1, 64, 1.0),
    (0, 3, 64, 0.5),
    (0, 256, 64, 0.3),
    (0, 257, 64, 0.3),
    (0, 1000, 64, 0.3),
    (13, 1000, 64, 0.05),
    (0, 4096, 256, 0.5),
    (64, 4096, 128, 0.2),
]


@pytest.mark.parametrize(("p_data_start", "p_data_count", "p_workgroup_size", "p_flag_probability"),
    SEGMENTED_BREAK_SCAN_SHAPES)
def test_workgroup_general_prefix_sum_segmented_break(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator, p_data_start: int, p_data_count: int, p_workgroup_size: int,
        p_flag_probability: float) -> None:
    values, flags = segmented_input(rng, p_data_count, p_flag_probability)
    run_general_segmented_scan(device, load_kernel, rng, values, flags, p_data_start, p_workgroup_size,
        "SegmentedBreakPrefix")


def test_workgroup_general_prefix_sum_segmented_break_counts(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator) -> None:
    # Flags are summed as counts, not ORed, and the sums wrap around.
    values = rng.integers(0, 1000, 1000, dtype=np.uint32)
    flags = rng.integers(0, 1 << 30, 1000, dtype=np.uint32)
    run_general_segmented_scan(device, load_kernel, rng, values, flags, 0, 64, "SegmentedBreakPrefix")


@pytest.mark.parametrize("p_flag_probability", [0.0, 0.3, 1.0])
def test_workgroup_inclusive_prefix_sum_64_256_segmented_break(device: spy.Device, load_kernel: KernelLoader,
        rng: np.random.Generator, p_flag_probability: float) -> None:
    values, flags = segmented_input(rng, FIXED_ELEMENT_COUNT, p_flag_probability)
    data = np.stack([values, flags], axis=1)
    result, _ = run_segmented_scan(device, load_kernel, data, {"OPERATOR": "SegmentedBreakPrefix", "KERNEL_SCAN": 1},
        FIXED_WORKGROUP_SIZE)
    np.testing.assert_array_equal(result, segmented_break_scan_expected(values, flags))
