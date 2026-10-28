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

from test_large_prefix_scan import DATA_START, GUARD_COUNT, GUARD_VALUE, create_buffer, make_scan_data, \
    run_large_prefix_scan


RANDOM_SEED = 20251003
# Must match dedup_large_utils.slang.
WORKGROUP_SIZE = 256
KEY_BYTE_SIZE = 4

KernelLoader = Callable[[str, dict[str, str | int]], spy.ComputeKernel]


@pytest.fixture
def rng() -> np.random.Generator:
    return np.random.default_rng(RANDOM_SEED)


def deduplicate(p_device: spy.Device, p_load_kernel: KernelLoader, p_sorted_keys: np.ndarray) -> np.ndarray:
    """Marks, scans, and scatters the sorted keys, and returns the unique keys buffer."""
    key_count = len(p_sorted_keys)
    # The keys are a counted buffer, whose header holds their byte size.
    key_data = make_scan_data(p_sorted_keys)
    key_data[0] = key_count * KEY_BYTE_SIZE
    keys = create_buffer(p_device, key_data)
    # The marks overwrite the guard-filled elements, and the scan reads the count the marks write.
    marks = create_buffer(p_device, np.full(DATA_START + key_count + GUARD_COUNT, GUARD_VALUE, dtype=np.uint32))
    unique_keys = create_buffer(p_device, np.full(DATA_START + key_count + GUARD_COUNT, GUARD_VALUE, dtype=np.uint32))
    buffers = {"keys": keys, "marks": marks, "unique_keys": unique_keys}
    group_count = (key_count + WORKGROUP_SIZE - 1) // WORKGROUP_SIZE
    p_load_kernel("dedup_large.slang", {"KERNEL_PASS": 0}).dispatch(
        thread_count=[group_count * WORKGROUP_SIZE, 1, 1], vars=buffers)
    run_large_prefix_scan(p_device, p_load_kernel, marks, key_count)
    p_load_kernel("dedup_large.slang", {"KERNEL_PASS": 1}).dispatch(
        thread_count=[group_count * WORKGROUP_SIZE, 1, 1], vars=buffers)
    return unique_keys.to_numpy().view(np.uint32)


def check_deduplicate(p_device: spy.Device, p_load_kernel: KernelLoader, p_keys: np.ndarray) -> None:
    sorted_keys = np.sort(p_keys)
    result = deduplicate(p_device, p_load_kernel, sorted_keys)
    unique = np.unique(sorted_keys)
    assert result[0] == len(unique) * KEY_BYTE_SIZE
    np.testing.assert_array_equal(result[DATA_START:DATA_START + len(unique)], unique)
    # Slots past the unique keys are not written.
    assert np.all(result[DATA_START + len(unique):] == GUARD_VALUE)


# (key count, key limit): few distinct keys give long runs of duplicates, and a large limit gives mostly unique keys.
DEDUPLICATION_SHAPES = [
    (1, 16),
    (300, 4),
    (1000, 1 << 30),
    (5000, 300),
    (256 * 256 + 1, 1 << 12),
    (256 * 256 * 2 + 777, 1 << 16),
]


@pytest.mark.parametrize(("p_key_count", "p_key_limit"), DEDUPLICATION_SHAPES)
def test_dedup_large(device: spy.Device, load_kernel: KernelLoader, rng: np.random.Generator, p_key_count: int,
        p_key_limit: int) -> None:
    check_deduplicate(device, load_kernel, rng.integers(0, p_key_limit, p_key_count, dtype=np.uint32))


def test_dedup_large_single_key(device: spy.Device, load_kernel: KernelLoader) -> None:
    check_deduplicate(device, load_kernel, np.full(1000, 0xFFFFFFFF, dtype=np.uint32))
