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
#define UNKNOWN_PROPERTY(extension) ErrorPrintf("Unknown property %s for extension %s\n", value["name"].asCString(), extension)

bool strToBool(const std::string& str_bool) {
    bool ret = false;
    if (str_bool.compare("true") == 0) {
        ret = true;
    }
    return ret;
}

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
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES:
        {
            if (strcmp(value["name"].asCString(), "multiview") == 0) {
                feature.multiview_feature.multiview = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "multiviewGeometryShader") == 0) {
                feature.multiview_feature.multiviewGeometryShader = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "multiviewTessellationShader") == 0) {
                feature.multiview_feature.multiviewTessellationShader = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_KHR_multiview");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES:
        {
            if (strcmp(value["name"].asCString(), "samplerYcbcrConversion") == 0) {
                feature.sampler_ycbcr_conv_feature.samplerYcbcrConversion = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_KHR_sampler_ycbcr_conversion");
            }
        }
        break;
    default:
        ErrorPrintf("Unknown extension %d", stype);
        break;
    }
}

VkStructureType ext_property_name_to_stype(const std::string& ext_prop_name) {
    static std::unordered_map<std::string, VkStructureType> s_prop_name_to_stype_map;

    VkStructureType stype = VK_STRUCTURE_TYPE_MAX_ENUM;

    if (s_prop_name_to_stype_map.size() == 0) {
        // initialize the mapping table
        s_prop_name_to_stype_map["VK_KHR_acceleration_structure"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        s_prop_name_to_stype_map["VK_EXT_external_memory_host"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;
        s_prop_name_to_stype_map["VK_EXT_transform_feedback"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT;
    }

    if (s_prop_name_to_stype_map.find(ext_prop_name) != s_prop_name_to_stype_map.end()) {
        stype = s_prop_name_to_stype_map[ext_prop_name];
    } else {
        ErrorPrintf("Unhandled extended property: %s\n", ext_prop_name.c_str());
    }

    return stype;
}

void get_extension_property(VkStructureType stype, const Json::Value& value, ExtendedProperty& property) {
    assert(stype == property.base.sType);
    switch (stype)
    {
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR:
        {
            if (strcmp(value["name"].asCString(), "maxGeometryCount") == 0) {
                property.acceleration_structure_prop.maxGeometryCount = std::stoull(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxInstanceCount") == 0) {
                property.acceleration_structure_prop.maxInstanceCount = std::stoull(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxPrimitiveCount") == 0) {
                property.acceleration_structure_prop.maxPrimitiveCount = std::stoull(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxPerStageDescriptorAccelerationStructures") == 0) {
                property.acceleration_structure_prop.maxPerStageDescriptorAccelerationStructures = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxPerStageDescriptorUpdateAfterBindAccelerationStructures") == 0) {
                property.acceleration_structure_prop.maxPerStageDescriptorUpdateAfterBindAccelerationStructures = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxDescriptorSetAccelerationStructures") == 0) {
                property.acceleration_structure_prop.maxDescriptorSetAccelerationStructures = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxDescriptorSetUpdateAfterBindAccelerationStructures") == 0) {
                property.acceleration_structure_prop.maxDescriptorSetUpdateAfterBindAccelerationStructures = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "minAccelerationStructureScratchOffsetAlignment") == 0) {
                property.acceleration_structure_prop.minAccelerationStructureScratchOffsetAlignment = std::stoul(value["value"].asString());
            } else {
                UNKNOWN_PROPERTY("VK_KHR_acceleration_structure");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT:
        {
            if (strcmp(value["name"].asCString(), "minImportedHostPointerAlignment") == 0) {
                property.external_memory_host_prop.minImportedHostPointerAlignment = std::stoull(value["value"].asString());
            } else {
                UNKNOWN_PROPERTY("VK_EXT_external_memory_host");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT:
        {
            if (strcmp(value["name"].asCString(), "maxTransformFeedbackStreams") == 0) {
                property.transform_feedback_prop.maxTransformFeedbackStreams = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxTransformFeedbackBuffers") == 0) {
                property.transform_feedback_prop.maxTransformFeedbackBuffers = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxTransformFeedbackBufferSize") == 0) {
                property.transform_feedback_prop.maxTransformFeedbackBufferSize = std::stoull(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxTransformFeedbackStreamDataSize") == 0) {
                property.transform_feedback_prop.maxTransformFeedbackStreamDataSize = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxTransformFeedbackBufferDataSize") == 0) {
                property.transform_feedback_prop.maxTransformFeedbackBufferDataSize = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxTransformFeedbackBufferDataStride") == 0) {
                property.transform_feedback_prop.maxTransformFeedbackBufferDataStride = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "transformFeedbackQueries") == 0) {
                property.transform_feedback_prop.transformFeedbackQueries = strToBool(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "transformFeedbackStreamsLinesTriangles") == 0) {
                property.transform_feedback_prop.transformFeedbackStreamsLinesTriangles = strToBool(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "transformFeedbackRasterizationStreamSelect") == 0) {
                property.transform_feedback_prop.transformFeedbackRasterizationStreamSelect = strToBool(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "transformFeedbackDraw") == 0) {
                property.transform_feedback_prop.transformFeedbackDraw = strToBool(value["value"].asString());
            } else {
                UNKNOWN_PROPERTY("VK_EXT_transform_feedback");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES:
        // ignore
        break;
    default:
        ErrorPrintf("Unknown extension %d", stype);
        break;
    }
}
