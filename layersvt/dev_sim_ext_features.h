/*
 * (C) COPYRIGHT 2021 ARM Limited
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

#ifndef _DEV_SIM_EXT_FEATURES_H_
#define _DEV_SIM_EXT_FEATURES_H_

#include <string>

#include <json/json.h>

#include "vulkan/vulkan.h"

typedef union {
    struct {
        VkStructureType    sType;
        void*              pNext;
    } base;
    VkPhysicalDeviceASTCDecodeFeaturesEXT     astc_decode_feature;
    VkPhysicalDeviceScalarBlockLayoutFeatures scalar_block_layout_feature;
    VkPhysicalDeviceShaderFloat16Int8Features shader_float16_int8_feature;
    VkPhysicalDevice16BitStorageFeatures      bit16_storage_feature;
} ExtendedFeature;

VkStructureType ext_feature_name_to_stype(const std::string& ext_feat_name);
void get_extension_feature(VkStructureType stype, const Json::Value& value, ExtendedFeature& feature);

#endif // _DEV_SIM_EXT_FEATURES_H_