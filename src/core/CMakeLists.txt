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
file(GLOB wbe_core_src ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_subdirectory(allocator)
add_subdirectory(async)
add_subdirectory(data)
add_subdirectory(engine_config)
add_subdirectory(logging)
add_subdirectory(parser)
add_subdirectory(profiling)
add_subdirectory(script)
add_subdirectory(utils)
add_subdirectory(job)

set(WBE_SOURCES
    ${WBE_SOURCES}
    ${wbe_core_src}
    PARENT_SCOPE
)

