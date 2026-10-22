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
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wunused-variable"
#elifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

#ifdef _DEBUG
#include <cstdio>
// List every allocation still alive when VMA destroys a memory block, before its leak assertion fires.
#define VMA_LEAK_LOG_FORMAT(format, ...) /* NOLINT */                                                                     \
    do {                                                                                                                    \
        std::fprintf(stderr, "[VMA leak] " format "\n" __VA_OPT__(, ) __VA_ARGS__);                                       \
    } while (false)
#endif

#define VMA_IMPLEMENTATION // NOLINT
#include "function/render_vk/vma_vk.hh"

#ifdef __clang__
#pragma clang diagnostic pop
#elifdef __GNUC__
#pragma GCC diagnostic pop
#endif
