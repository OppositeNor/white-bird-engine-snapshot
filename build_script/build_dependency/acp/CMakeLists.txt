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
cmake_minimum_required(VERSION 3.23)

project(WBEACPDependencies LANGUAGES C CXX)

if (NOT DEFINED KTX_SOFTWARE_DIR)
    message(FATAL_ERROR "KTX_SOFTWARE_DIR must point to the vendored KTX-Software source directory.")
endif()
if (NOT DEFINED WBE_MODEL_COMPILER_DIR)
    message(FATAL_ERROR "WBE_MODEL_COMPILER_DIR must point to the vendored model compiler source directory.")
endif()
if (NOT DEFINED WBE_DEPENDENCIES_ROOT)
    message(FATAL_ERROR "WBE_DEPENDENCIES_ROOT must point to the engine dependency directory.")
endif()

set(KTX_FEATURE_DOC OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_JNI OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_PY OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS_CTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_LOADTEST_APPS OFF CACHE STRING "" FORCE)
set(LIBKTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "" FORCE)
set(LIBKTX_FEATURE_VK_UPLOAD OFF CACHE BOOL "" FORCE)
set(LIBKTX_VERSION_FULL ON CACHE BOOL "" FORCE)
set(LIBKTX_VERSION_READ_ONLY OFF CACHE BOOL "" FORCE)

set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
add_subdirectory(${KTX_SOFTWARE_DIR} ${CMAKE_BINARY_DIR}/ktx)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set_target_properties(ktx PROPERTIES
    DEBUG_POSTFIX ""
    ARCHIVE_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/lib>"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/lib>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/lib>"
)

set(PYTHON_EXTENSION_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/wbe_utils_model_compiler")
add_subdirectory(${WBE_MODEL_COMPILER_DIR} ${CMAKE_BINARY_DIR}/model_compiler)
