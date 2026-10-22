/* Copyright 2026 OppositeNor

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
#ifndef WBE_FILE_SVOGI_UTILS_SLANG
#define WBE_FILE_SVOGI_UTILS_SLANG

struct SVOGIData {
    /**
     * @brief RGB color, alpha used for opacity.
     */
    float4 color;
    /**
     * @brief The average vector. The length encodes the variance: var = (1 - |average_vector|) / |average_vector|.
     */
    float3 average_vector;
};

#endif
