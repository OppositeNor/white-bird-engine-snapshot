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
add_subdirectory(executable)

file(GLOB_RECURSE WBE_UNIT_TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)

add_executable(wbe_unit_test
    ${WBE_UNIT_TEST_SOURCE}
)

target_link_libraries(wbe_unit_test PRIVATE
    WBE_ENGINE
    gtest_main
)

# Shared unit test support headers, such as test_global.hh.
target_include_directories(wbe_unit_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)

# The Teal declarations of the WBE_REFLECT_SCRIPT types (templates/teal_types.d.tl.jinja).
target_compile_definitions(wbe_unit_test PRIVATE WBE_TEAL_DECLARATIONS_DIR="${CMAKE_BINARY_DIR}/teal")

file(GLOB_RECURSE WBE_UNIT_TEST_BENCHMARK ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/*.cpp)

add_executable(wbe_benchmark_test
    ${WBE_UNIT_TEST_BENCHMARK}
)

target_link_libraries(wbe_benchmark_test PRIVATE
    WBE_ENGINE
    benchmark_main
)

if (WBE_USE_RPATH_LIBRARY)
    set_target_properties(wbe_unit_test PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/libs"
        SKIP_BUILD_RPATH FALSE
    )
    set_target_properties(wbe_benchmark_test PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/libs"
        SKIP_BUILD_RPATH FALSE
    )
endif()

include(GoogleTest)

gtest_discover_tests(wbe_unit_test)
