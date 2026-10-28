/* Copyright 2025 OppositeNor

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
/**
 * @file flatten_index_functions.slang
 * @brief Test module exposing the uint2 and uint3 flatten() and unflatten() extensions of utils/utils.slang to slangpy
 * calls, which cannot call extension methods. No defines are required.
 */
#include "utils/utils.slang"

uint flatten_2d(uint2 p_coordinate, uint p_width) {
    return p_coordinate.flatten(p_width);
}

uint2 unflatten_2d(uint p_index, uint p_width) {
    return uint2::unflatten(p_index, p_width);
}

uint flatten_3d(uint3 p_coordinate, uint p_width, uint p_height) {
    return p_coordinate.flatten(p_width, p_height);
}

uint3 unflatten_3d(uint p_index, uint p_width, uint p_height) {
    return uint3::unflatten(p_index, p_width, p_height);
}
