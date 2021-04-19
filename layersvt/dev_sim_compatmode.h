/*
 * (C) COPYRIGHT 2019 ARM Limited
 * ALL RIGHTS RESERVED
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef _DEV_SIM_COMPATMODE_H_
#define _DEV_SIM_COMPATMODE_H_

void ClampPhyDevLimits(const VkPhysicalDeviceLimits& src, VkPhysicalDeviceLimits& dst);
void ClampPhyDevFeatures(const VkPhysicalDeviceFeatures& src, VkPhysicalDeviceFeatures& dst);
void ClampQueueFamilyProps(const VkQueueFamilyProperties& src, VkQueueFamilyProperties& dst);
void ClampFormatProps(const VkFormatProperties& src, VkFormatProperties& dst);
void ClampDevExtProps(const std::vector<VkExtensionProperties>& src, std::vector<VkExtensionProperties>& dst);
void ClampExtendedDevFeatures(const std::unordered_map<uint32_t, ExtendedFeature>& features, void* pNext);

#endif // _DEV_SIM_COMPATMODE_H_
