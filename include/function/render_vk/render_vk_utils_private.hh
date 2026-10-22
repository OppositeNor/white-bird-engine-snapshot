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
#ifndef WBE_FILE_RENDER_VK_UTILS_PRIVATE_HH
#define WBE_FILE_RENDER_VK_UTILS_PRIVATE_HH

#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @brief Check one Boolean Vulkan device feature through a features2 query.
 *
 * @tparam StructureType Structure type identifying FeatureType.
 * @tparam FeatureType Vulkan feature structure containing the requested member.
 * @param p_physical_device Physical device to query.
 * @param p_feature Boolean feature member to inspect.
 * @return True when the feature is supported.
 */
template <VkStructureType StructureType, typename FeatureType>
bool check_device_feature_support(VkPhysicalDevice p_physical_device, VkBool32 FeatureType::* p_feature) {
    FeatureType feature{};
    feature.sType = StructureType;
    VkPhysicalDeviceFeatures2 device_features{};
    device_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    device_features.pNext = &feature;
    vkGetPhysicalDeviceFeatures2(p_physical_device, &device_features);
    return feature.*p_feature == VK_TRUE;
}

} // namespace WhiteBirdEngine

#endif