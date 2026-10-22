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

project(WhiteBirdEngine LANGUAGES C CXX)

# Use C++ 23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Export compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(WBE_MAKE_TEST "Export test for White Bird Engine" ON)
set(WBE_BINARY_DIR ${CMAKE_BINARY_DIR}/bin CACHE STRING "The binary output directory.")
option(WBE_BUILD_SHARED "Should build shared library" ON)
option(WBE_ENABLE_TSAN "Build with ThreadSanitizer" OFF)

# Output pathes
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${WBE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${WBE_BINARY_DIR}/libs)

set(WBE_BUILD_PLATFORM ${CMAKE_SYSTEM_NAME} CACHE STRING "Build platform.")

if(WBE_BUILD_SHARED)
    set(WBE_LIB_BUILD_TYPE SHARED)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
else()
    set(WBE_LIB_BUILD_TYPE STATIC)
endif()

# Add dependencies subdirectories
if (WBE_BUILD_PLATFORM STREQUAL "Android")
    # TODO: Android dependencies
else()
    set(SDL_TEST OFF)
    set(SDL_STATIC OFF)
    set(SDL_SHARED ON)
    add_subdirectory(dependencies/SDL2)
endif()
find_package(Vulkan REQUIRED)
set(VMA_ENABLE_INSTALL OFF)
set(VMA_BUILD_DOCUMENTATION OFF)
set(VMA_BUILD_SAMPLES OFF)
add_subdirectory(dependencies/VulkanMemoryAllocator)

set(KTX_FEATURE_DOC OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_JNI OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_PY OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS_CTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_LOADTEST_APPS OFF CACHE STRING "" FORCE)
set(LIBKTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "" FORCE)
set(LIBKTX_FEATURE_VK_UPLOAD ON CACHE BOOL "" FORCE)
set(LIBKTX_VERSION_FULL ON CACHE BOOL "" FORCE)
set(LIBKTX_VERSION_READ_ONLY OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
add_subdirectory(dependencies/KTX-Software)
set(KTX_PUBLIC_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/ktx_public_include)
file(MAKE_DIRECTORY ${KTX_PUBLIC_INCLUDE_DIR}/KHR)
configure_file(
    dependencies/KTX-Software/external/dfdutils/KHR/khr_df.h
    ${KTX_PUBLIC_INCLUDE_DIR}/KHR/khr_df.h
    COPYONLY
)
set_target_properties(ktx PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${WBE_BINARY_DIR}/libs
    LIBRARY_OUTPUT_DIRECTORY ${WBE_BINARY_DIR}/libs
    RUNTIME_OUTPUT_DIRECTORY ${WBE_BINARY_DIR}/libs
    # KTX exports all of external/dfdutils for khr_df.h, but that directory also
    # contains an older Vulkan header which would shadow the project's Vulkan SDK.
    INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dependencies/KTX-Software/lib/include>;$<BUILD_INTERFACE:${KTX_PUBLIC_INCLUDE_DIR}>;$<INSTALL_INTERFACE:include>"
)

set(BUILD_TESTING OFF)
set(BOOST_SUPERPROJECT_VERSION 1.89.0)
# Build Boost.Thread, Boost.ProgramOptions, Boost.Container, and their CMake target dependencies.
foreach(boost_library IN ITEMS
    algorithm
    align
    any
    array
    assert
    atomic
    bind
    chrono
    concept_check
    config
    container
    container_hash
    conversion
    core
    date_time
    describe
    detail
    exception
    function
    function_types
    functional
    fusion
    integer
    intrusive
    io
    iterator
    lexical_cast
    move
    mp11
    mpl
    numeric/conversion
    optional
    predef
    preprocessor
    program_options
    range
    ratio
    regex
    smart_ptr
    static_assert
    system
    thread
    throw_exception
    tokenizer
    tuple
    type_index
    type_traits
    typeof
    unordered
    utility
    variant2
    winapi
)
    add_subdirectory(dependencies/boost/libs/${boost_library})
endforeach()
if (WBE_MAKE_TEST)
    # KTX enables the global BUILD_SHARED_LIBS option. GoogleTest must remain static because
    # test discovery runs the executable before the project's deployed-library layout exists.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_TESTING OFF)
    add_compile_options(-Wno-c2y-extensions)
    add_subdirectory(dependencies/googletest)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-command-line-argument")
    add_subdirectory(dependencies/google-benchmark)
endif()
# Instrument everything built below: Lua, the engine, and the tests. The vendored CMake projects above stay uninstrumented,
# because some (SDL2) link shared libraries with --no-undefined and cannot resolve the sanitizer runtime, which only
# executables link. ThreadSanitizer still intercepts their pthread and libc calls.
if (WBE_ENABLE_TSAN)
    message("Building with ThreadSanitizer.")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

# Setup Lua
add_subdirectory(dependencies/lua)

# Setup miniaudio
add_library(WBE_MINIAUDIO_BUILD_LIB
    src/resource/miniaudio_instantiation.c
)
target_include_directories(WBE_MINIAUDIO_BUILD_LIB PRIVATE
    ${PROJECT_SOURCE_DIR}/dependencies/miniaudio
    ${PROJECT_SOURCE_DIR}/dependencies/stb
)


# Set C++ build flags
if (NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror -pedantic-errors")
endif()
if (CMAKE_BUILD_TYPE STREQUAL "Deploy")
    # for optimization
    add_compile_options(-O2 -DNDEBUG)
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    # for optimization and testing
    add_compile_options(-O2 -g -DNDEBUG)
else()
    # for gdb or valgrind
    add_compile_options(-O0 -g -fno-inline -fno-omit-frame-pointer -D_DEBUG)
endif()

if (WBE_MAKE_TEST AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message("Enabling coverage tests.")
    add_compile_options(--coverage -Wno-stringop-truncation)
    add_link_options(--coverage)
endif()

# Platform specific configurations.
if (WBE_BUILD_PLATFORM STREQUAL "Linux")
    add_compile_options(-DWBE_TARGET_PLATFORM_LINUX)
    if (WBE_BUILD_SHARED)
        set(WBE_USE_RPATH_LIBRARY ON)
    endif()
elseif(WBE_BUILD_PLATFORM STREQUAL "Windows")
    add_compile_options(-DWBE_TARGET_PLATFORM_WINDOWS)
elseif(WBE_BUILD_PLATFORM STREQUAL "Darwin")
    add_compile_options(-DWBE_TARGET_PLATFORM_MACOS)
    if (WBE_BUILD_SHARED)
        set(WBE_USE_RPATH_LIBRARY ON)
    endif()
endif()


if (WBE_MAKE_TEST)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DWBE_BUILD_TEST=OFF")
endif()

# Use default aligned gentypes in glm
add_definitions(-DGLM_FORCE_DEFAULT_ALIGNED_GENTYPES)

# Report toml++ parse failures through toml::parse_result instead of exceptions.
add_definitions(-DTOML_EXCEPTIONS=0)

# Set include directories. The dependency include directories are declared once in build_config.py and passed in by
# build.py, which also hands them to the reflection metaparser.
if (NOT DEFINED WBE_DEPENDENCY_INCLUDE_DIRS OR NOT DEFINED WBE_TEST_DEPENDENCY_INCLUDE_DIRS)
    message(FATAL_ERROR "WBE_DEPENDENCY_INCLUDE_DIRS and WBE_TEST_DEPENDENCY_INCLUDE_DIRS are not set; configure through build.py.")
endif()
set(include_dirs
    ${Vulkan_INCLUDE_DIR}
    ${WBE_INCLUDE_DIR}
    ${WBE_PER_TARGET_INCLUDE_DIR}
    ${WBE_DEPENDENCY_INCLUDE_DIRS}
)

if (WBE_BUILD_PLATFORM STREQUAL "Android")
    # TODO: Android include directories. SDL2/include comes from build_config.dependency_include_dirs on every platform.
endif()

include_directories(
    ${include_dirs}
)
# Test-only third-party headers (googletest, benchmark, imgui) are system headers, so -Werror does not apply to them.
if (WBE_MAKE_TEST)
    include_directories(SYSTEM ${WBE_TEST_DEPENDENCY_INCLUDE_DIRS})
endif()

set(libs ${libs}
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    SDL2::SDL2
    boost_thread
    boost_program_options
    WBE_MINIAUDIO_BUILD_LIB
    ktx
    lua
)

# Add engine source
set(WBE_SOURCES "")
add_subdirectory(src/platform)
add_subdirectory(src/core)
add_subdirectory(src/resource)
add_subdirectory(src/function)
add_subdirectory(src/global)
add_subdirectory(src/generated)

add_library(WBE_ENGINE ${WBE_LIB_BUILD_TYPE}
    ${WBE_SOURCES}
)
add_library(WBE::Engine ALIAS WBE_ENGINE)

target_link_libraries(WBE_ENGINE PUBLIC
    ${libs}
)

set(libs ${libs}
    WBE_ENGINE
)

# Create executable
if (WBE_BUILD_PLATFORM STREQUAL "Android")
    # TODO: Android build
else()
    add_executable(${PROJECT_NAME}
        ${PROJECT_SOURCE_DIR}/src/main.cpp
    )
    if (WBE_USE_RPATH_LIBRARY)
        set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/libs"
        SKIP_BUILD_RPATH FALSE
        )
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        WBE_ENGINE
    )
endif()

# Generate test executables
if (WBE_MAKE_TEST)
    # Export tests
    add_subdirectory(tests)
endif()
