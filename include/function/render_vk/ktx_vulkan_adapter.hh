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
#ifndef WBE_FILE_KTX_VULKAN_ADAPTER_HH
#define WBE_FILE_KTX_VULKAN_ADAPTER_HH

// ktxvulkan.h deliberately relies on Vulkan declarations supplied by its consumer, so this order is required.
// NOLINTBEGIN
// clang-format off
#include <vulkan/vulkan_core.h> // IWYU pragma: export
#include <ktxvulkan.h> // IWYU pragma: export
// clang-format on
// NOLINTEND

#endif
