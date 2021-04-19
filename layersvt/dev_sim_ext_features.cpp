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

#include <cassert>
#include <cstring>
#include <unordered_map>

#include "dev_sim_ext_features.h"

extern void ErrorPrintf(const char *fmt, ...);
extern void DebugPrintf(const char *fmt, ...);

#define UNKNOWN_FEATURE(extension) ErrorPrintf("Unknown feature %s for extension %s\n", value["name"].asCString(), extension)

VkStructureType ext_feature_name_to_stype(const std::string& ext_feat_name) {
    static std::unordered_map<std::string, VkStructureType> s_feature_name_to_stype_map;

    VkStructureType stype = VK_STRUCTURE_TYPE_MAX_ENUM;

    if (s_feature_name_to_stype_map.size() == 0) {
        // initialize the mapping table
        s_feature_name_to_stype_map["VK_EXT_astc_decode_mode"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ASTC_DECODE_FEATURES_EXT;
        s_feature_name_to_stype_map["VK_EXT_scalar_block_layout"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
        s_feature_name_to_stype_map["VK_KHR_shader_float16_int8"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
        s_feature_name_to_stype_map["VK_KHR_16bit_storage"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
    }

    if (s_feature_name_to_stype_map.find(ext_feat_name) != s_feature_name_to_stype_map.end()) {
        stype = s_feature_name_to_stype_map[ext_feat_name];
    } else {
        ErrorPrintf("Unhandled extended feature: %s\n", ext_feat_name.c_str());
    }

    return stype;
}

void get_extension_feature(VkStructureType stype, const Json::Value& value, ExtendedFeature& feature) {
    assert(stype == feature.base.sType);
    switch (stype)
    {
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ASTC_DECODE_FEATURES_EXT:
        {
            if (strcmp(value["name"].asCString(), "decodeModeSharedExponent") == 0) {
                feature.astc_decode_feature.decodeModeSharedExponent = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_EXT_astc_decode_mode");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES:
        {
            if (strcmp(value["name"].asCString(), "scalarBlockLayout") == 0) {
                feature.scalar_block_layout_feature.scalarBlockLayout = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_EXT_scalar_block_layout");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES:
        {
            if (strcmp(value["name"].asCString(), "shaderFloat16") == 0) {
                feature.shader_float16_int8_feature.shaderFloat16 = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "shaderInt8") == 0) {
                feature.shader_float16_int8_feature.shaderInt8 = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_KHR_shader_float16_int8");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES:
        {
            if (strcmp(value["name"].asCString(), "storageBuffer16BitAccess") == 0) {
                feature.bit16_storage_feature.storageBuffer16BitAccess = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "uniformAndStorageBuffer16BitAccess") == 0) {
                feature.bit16_storage_feature.uniformAndStorageBuffer16BitAccess = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "storagePushConstant16") == 0) {
                feature.bit16_storage_feature.storagePushConstant16 = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "storageInputOutput16") == 0) {
                feature.bit16_storage_feature.storageInputOutput16 = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_KHR_16bit_storage");
            }
        }
        break;
    default:
        ErrorPrintf("Unknown extension %d", stype);
        break;
    }
}
