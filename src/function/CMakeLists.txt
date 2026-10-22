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
file(GLOB wbe_function_src ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_subdirectory(application)
add_subdirectory(audio_ma)
add_subdirectory(ecs)
add_subdirectory(event)
add_subdirectory(input_manager)
add_subdirectory(render)
add_subdirectory(render_software)
add_subdirectory(render_vk)
add_subdirectory(scene)
add_subdirectory(window)

set(WBE_SOURCES
    ${WBE_SOURCES}
    ${wbe_function_src}
    PARENT_SCOPE
)

