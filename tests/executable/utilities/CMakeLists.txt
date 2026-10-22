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

# Dear ImGui is a test-only dependency: it is built here and linked only into executable tests, never into the engine.
set(WBE_IMGUI_DIR ${PROJECT_SOURCE_DIR}/dependencies/imgui)
add_library(wbe_executable_test_imgui STATIC
    ${WBE_IMGUI_DIR}/imgui.cpp
    ${WBE_IMGUI_DIR}/imgui_demo.cpp
    ${WBE_IMGUI_DIR}/imgui_draw.cpp
    ${WBE_IMGUI_DIR}/imgui_tables.cpp
    ${WBE_IMGUI_DIR}/imgui_widgets.cpp
    ${WBE_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(wbe_executable_test_imgui SYSTEM PUBLIC
    ${WBE_IMGUI_DIR}
    ${WBE_IMGUI_DIR}/backends
)
target_link_libraries(wbe_executable_test_imgui PUBLIC
    Vulkan::Vulkan
)
# Third-party code is not held to the engine's warning flags.
target_compile_options(wbe_executable_test_imgui PRIVATE -w)

# Shared support code linked into every executable test.
file(GLOB WBE_EXECUTABLE_TEST_UTILITIES_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(wbe_executable_test_utilities STATIC
    ${WBE_EXECUTABLE_TEST_UTILITIES_SOURCES}
)

target_include_directories(wbe_executable_test_utilities PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(wbe_executable_test_utilities PUBLIC
    WBE_ENGINE
    wbe_executable_test_imgui
)
