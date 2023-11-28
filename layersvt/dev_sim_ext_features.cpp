/*
 * Copyright (C) 2021-2023 ARM Limited
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
extern void WarningPrintf(const char *fmt, ...);
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
        s_feature_name_to_stype_map["VK_KHR_buffer_device_address"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        s_feature_name_to_stype_map["VK_KHR_acceleration_structure"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        s_feature_name_to_stype_map["VK_KHR_ray_tracing_pipeline"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        s_feature_name_to_stype_map["VK_KHR_ray_query"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        s_feature_name_to_stype_map["VK_EXT_fragment_density_map"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT;
        s_feature_name_to_stype_map["VK_EXT_fragment_density_map2"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_FEATURES_EXT;
        s_feature_name_to_stype_map["VK_KHR_multiview"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
        s_feature_name_to_stype_map["VK_KHR_imageless_framebuffer"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES;
        s_feature_name_to_stype_map["VK_EXT_transform_feedback"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
        s_feature_name_to_stype_map["VK_EXT_vertex_attribute_divisor"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT;
        s_feature_name_to_stype_map["VK_KHR_timeline_semaphore"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR;
        s_feature_name_to_stype_map["VK_EXT_texture_compression_astc_hdr"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES;
        s_feature_name_to_stype_map["VK_EXT_descriptor_indexing"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    }

    if (s_feature_name_to_stype_map.find(ext_feat_name) != s_feature_name_to_stype_map.end()) {
        stype = s_feature_name_to_stype_map[ext_feat_name];
    } else {
        WarningPrintf("Unhandled extended feature: %s\n", ext_feat_name.c_str());
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
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR:
        {
            if (strcmp(value["name"].asCString(), "accelerationStructure") == 0) {
                feature.acceleration_structure_feature.accelerationStructure = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "accelerationStructureCaptureReplay") == 0) {
                feature.acceleration_structure_feature.accelerationStructureCaptureReplay = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "accelerationStructureHostCommands") == 0) {
                feature.acceleration_structure_feature.accelerationStructureHostCommands = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "accelerationStructureIndirectBuild") == 0) {
                feature.acceleration_structure_feature.accelerationStructureIndirectBuild = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "descriptorBindingAccelerationStructureUpdateAfterBind") == 0) {
                feature.acceleration_structure_feature.descriptorBindingAccelerationStructureUpdateAfterBind = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_KHR_acceleration_structure");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR:
        {
            if (strcmp(value["name"].asCString(), "rayTracingPipeline") == 0) {
                feature.ray_tracing_pipeline_feature.rayTracingPipeline = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "rayTracingPipelineShaderGroupHandleCaptureReplay") == 0) {
                feature.ray_tracing_pipeline_feature.rayTracingPipelineShaderGroupHandleCaptureReplay = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "rayTracingPipelineShaderGroupHandleCaptureReplayMixed") == 0) {
                feature.ray_tracing_pipeline_feature.rayTracingPipelineShaderGroupHandleCaptureReplayMixed = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "rayTracingPipelineTraceRaysIndirect") == 0) {
                feature.ray_tracing_pipeline_feature.rayTracingPipelineTraceRaysIndirect = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "rayTraversalPrimitiveCulling") == 0) {
                feature.ray_tracing_pipeline_feature.rayTraversalPrimitiveCulling = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_KHR_ray_tracing_pipeline");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR:
        {
            if (strcmp(value["name"].asCString(), "rayQuery") == 0) {
                feature.ray_query_feature.rayQuery = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_KHR_ray_query");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:
        {
            if (strcmp(value["name"].asCString(), "bufferDeviceAddress") == 0) {
                feature.buffer_device_address_feature.bufferDeviceAddress = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "bufferDeviceAddressCaptureReplay") == 0) {
                feature.buffer_device_address_feature.bufferDeviceAddressCaptureReplay = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "bufferDeviceAddressMultiDevice") == 0) {
                feature.buffer_device_address_feature.bufferDeviceAddressMultiDevice = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_KHR_buffer_device_address");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT:
        {
            if (strcmp(value["name"].asCString(), "fragmentDensityMap") == 0) {
                feature.fragment_density_map_feature.fragmentDensityMap = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "fragmentDensityMapDynamic") == 0) {
                feature.fragment_density_map_feature.fragmentDensityMapDynamic = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "fragmentDensityMapNonSubsampledImages") == 0) {
                feature.fragment_density_map_feature.fragmentDensityMapNonSubsampledImages = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_EXT_fragment_density_map");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_FEATURES_EXT:
        {
            if (strcmp(value["name"].asCString(), "fragmentDensityMapDeferred") == 0) {
                feature.fragment_density_map2_feature.fragmentDensityMapDeferred = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_EXT_fragment_density_map2");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES:
        {
            if (strcmp(value["name"].asCString(), "imagelessFramebuffer") == 0) {
                feature.imageless_framebuffer_feature.imagelessFramebuffer = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_KHR_imageless_framebuffer");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT:
        {
            if (strcmp(value["name"].asCString(), "transformFeedback") == 0) {
                feature.transform_feedback_feature.transformFeedback = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "geometryStreams") == 0) {
                feature.transform_feedback_feature.geometryStreams = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_EXT_transform_feedback");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT:
        {
            if (strcmp(value["name"].asCString(), "vertexAttributeInstanceRateDivisor") == 0) {
                feature.vertex_attribute_divisor_feature.vertexAttributeInstanceRateDivisor = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "vertexAttributeInstanceRateZeroDivisor") == 0) {
                feature.vertex_attribute_divisor_feature.vertexAttributeInstanceRateZeroDivisor = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_EXT_vertex_attribute_divisor");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR:
        {
            if (strcmp(value["name"].asCString(), "timelineSemaphore") == 0) {
                feature.timeline_semaphore_feature.timelineSemaphore = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_KHR_timeline_semaphore");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES:
        {
            if (strcmp(value["name"].asCString(), "textureCompressionASTC_HDR") == 0) {
                feature.texture_compression_astc_hdr_feature.textureCompressionASTC_HDR = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_EXT_texture_compression_astc_hdr");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES:
        {
            if (strcmp(value["name"].asCString(), "shaderInputAttachmentArrayDynamicIndexing") == 0) {
                feature.descriptor_indexing_feature.shaderInputAttachmentArrayDynamicIndexing = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "shaderUniformTexelBufferArrayDynamicIndexing") == 0) {
                feature.descriptor_indexing_feature.shaderUniformTexelBufferArrayDynamicIndexing = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "shaderStorageTexelBufferArrayDynamicIndexing") == 0) {
                feature.descriptor_indexing_feature.shaderStorageTexelBufferArrayDynamicIndexing = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "shaderUniformBufferArrayNonUniformIndexing") == 0) {
                feature.descriptor_indexing_feature.shaderUniformBufferArrayNonUniformIndexing = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "shaderSampledImageArrayNonUniformIndexing") == 0) {
                feature.descriptor_indexing_feature.shaderSampledImageArrayNonUniformIndexing = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "shaderStorageBufferArrayNonUniformIndexing") == 0) {
                feature.descriptor_indexing_feature.shaderStorageBufferArrayNonUniformIndexing = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "shaderStorageImageArrayNonUniformIndexing") == 0) {
                feature.descriptor_indexing_feature.shaderStorageImageArrayNonUniformIndexing = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "shaderInputAttachmentArrayNonUniformIndexing") == 0) {
                feature.descriptor_indexing_feature.shaderInputAttachmentArrayNonUniformIndexing = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "shaderUniformTexelBufferArrayNonUniformIndexing") == 0) {
                feature.descriptor_indexing_feature.shaderUniformTexelBufferArrayNonUniformIndexing = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "shaderStorageTexelBufferArrayNonUniformIndexing") == 0) {
                feature.descriptor_indexing_feature.shaderStorageTexelBufferArrayNonUniformIndexing = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "descriptorBindingUniformBufferUpdateAfterBind") == 0) {
                feature.descriptor_indexing_feature.descriptorBindingUniformBufferUpdateAfterBind = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "descriptorBindingSampledImageUpdateAfterBind") == 0) {
                feature.descriptor_indexing_feature.descriptorBindingSampledImageUpdateAfterBind = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "descriptorBindingStorageImageUpdateAfterBind") == 0) {
                feature.descriptor_indexing_feature.descriptorBindingStorageImageUpdateAfterBind = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "descriptorBindingStorageBufferUpdateAfterBind") == 0) {
                feature.descriptor_indexing_feature.descriptorBindingStorageBufferUpdateAfterBind = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "descriptorBindingUniformTexelBufferUpdateAfterBind") == 0) {
                feature.descriptor_indexing_feature.descriptorBindingUniformTexelBufferUpdateAfterBind = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "descriptorBindingStorageTexelBufferUpdateAfterBind") == 0) {
                feature.descriptor_indexing_feature.descriptorBindingStorageTexelBufferUpdateAfterBind = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "descriptorBindingUpdateUnusedWhilePending") == 0) {
                feature.descriptor_indexing_feature.descriptorBindingUpdateUnusedWhilePending = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "descriptorBindingPartiallyBound") == 0) {
                feature.descriptor_indexing_feature.descriptorBindingPartiallyBound = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "descriptorBindingVariableDescriptorCount") == 0) {
                feature.descriptor_indexing_feature.descriptorBindingVariableDescriptorCount = value["supported"].asBool();
            } else if (strcmp(value["name"].asCString(), "runtimeDescriptorArray") == 0) {
                feature.descriptor_indexing_feature.runtimeDescriptorArray = value["supported"].asBool();
            } else {
                UNKNOWN_FEATURE("VK_EXT_descriptor_indexing");
            }
        }
        break;
    default:
        ErrorPrintf("Unknown extension %d\n", stype);
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
        s_prop_name_to_stype_map["VK_KHR_ray_tracing_pipeline"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        s_prop_name_to_stype_map["VK_EXT_fragment_density_map"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT;
        s_prop_name_to_stype_map["VK_EXT_fragment_density_map2"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_PROPERTIES_EXT;
        s_prop_name_to_stype_map["VK_KHR_multiview"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES;
        s_prop_name_to_stype_map["VK_EXT_vertex_attribute_divisor"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT;
        s_prop_name_to_stype_map["VK_KHR_timeline_semaphore"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES_KHR;
        s_prop_name_to_stype_map["VK_KHR_depth_stencil_resolve"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES;
        s_prop_name_to_stype_map["VK_KHR_maintenance3"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
        s_prop_name_to_stype_map["VK_KHR_shader_float_controls"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR;
        s_prop_name_to_stype_map["VK_EXT_descriptor_indexing"] = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
    }

    if (s_prop_name_to_stype_map.find(ext_prop_name) != s_prop_name_to_stype_map.end()) {
        stype = s_prop_name_to_stype_map[ext_prop_name];
    } else {
        WarningPrintf("Unhandled extended property: %s\n", ext_prop_name.c_str());
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
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR:
        {
            if (strcmp(value["name"].asCString(), "maxRayDispatchInvocationCount") == 0) {
                property.ray_tracing_pipeline_prop.maxRayDispatchInvocationCount = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxRayHitAttributeSize") == 0) {
                property.ray_tracing_pipeline_prop.maxRayHitAttributeSize = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxRayRecursionDepth") == 0) {
                property.ray_tracing_pipeline_prop.maxRayRecursionDepth = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxShaderGroupStride") == 0) {
                property.ray_tracing_pipeline_prop.maxShaderGroupStride = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "shaderGroupBaseAlignment") == 0) {
                property.ray_tracing_pipeline_prop.shaderGroupBaseAlignment = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "shaderGroupHandleAlignment") == 0) {
                property.ray_tracing_pipeline_prop.shaderGroupHandleAlignment = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "shaderGroupHandleCaptureReplaySize") == 0) {
                property.ray_tracing_pipeline_prop.shaderGroupHandleCaptureReplaySize = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "shaderGroupHandleSize") == 0) {
                property.ray_tracing_pipeline_prop.shaderGroupHandleSize = std::stoul(value["value"].asString());
            } else {
                UNKNOWN_PROPERTY("VK_KHR_ray_tracing_pipeline");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT:
        {
            if (strcmp(value["name"].asCString(), "minFragmentDensityTexelSize") == 0) {
                property.fragment_density_map_prop.minFragmentDensityTexelSize.width = std::stoul(value["value"][0].asString());
                property.fragment_density_map_prop.minFragmentDensityTexelSize.height = std::stoul(value["value"][1].asString());
            } else if (strcmp(value["name"].asCString(), "maxFragmentDensityTexelSize") == 0) {
                property.fragment_density_map_prop.maxFragmentDensityTexelSize.width = std::stoul(value["value"][0].asString());
                property.fragment_density_map_prop.maxFragmentDensityTexelSize.height = std::stoul(value["value"][1].asString());
            } else if (strcmp(value["name"].asCString(), "fragmentDensityInvocations") == 0) {
                bool ret = false;
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.fragment_density_map_prop.fragmentDensityInvocations = ret;
            } else {
                UNKNOWN_PROPERTY("VK_EXT_fragment_density_map");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_PROPERTIES_EXT:
        {
            if (strcmp(value["name"].asCString(), "subsampledLoads") == 0) {
                bool ret = false;
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.fragment_density_map2_prop.subsampledLoads = ret;
            } else if (strcmp(value["name"].asCString(), "subsampledCoarseReconstructionEarlyAccess") == 0) {
                bool ret = false;
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.fragment_density_map2_prop.subsampledCoarseReconstructionEarlyAccess = ret;
            } else if (strcmp(value["name"].asCString(), "maxSubsampledArrayLayers") == 0) {
                property.fragment_density_map2_prop.maxSubsampledArrayLayers = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxDescriptorSetSubsampledSamplers") == 0) {
                property.fragment_density_map2_prop.maxDescriptorSetSubsampledSamplers = std::stoul(value["value"].asString());
            } else {
                UNKNOWN_PROPERTY("VK_EXT_fragment_density_map2");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES:
        {
            if (strcmp(value["name"].asCString(), "maxMultiviewViewCount") == 0) {
                property.multiview_prop.maxMultiviewViewCount = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxMultiviewInstanceIndex") == 0) {
                property.multiview_prop.maxMultiviewInstanceIndex = std::stoul(value["value"].asString());
            } else {
                UNKNOWN_PROPERTY("VK_KHR_multiview");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT:
        {
            if (strcmp(value["name"].asCString(), "maxVertexAttribDivisor") == 0) {
                property.vertex_attribute_divisor_prop.maxVertexAttribDivisor = std::stoul(value["value"].asString());
            } else {
                UNKNOWN_PROPERTY("VK_EXT_vertex_attribute_divisor");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES_KHR:
        {
            if (strcmp(value["name"].asCString(), "maxTimelineSemaphoreValueDifference") == 0) {
                property.timeline_semaphore_prop.maxTimelineSemaphoreValueDifference = std::stoul(value["value"].asString());
            } else {
                UNKNOWN_PROPERTY("VK_KHR_timeline_semaphore");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES:
        {
            if (strcmp(value["name"].asCString(), "supportedDepthResolveModes") == 0) {
                property.depth_stencil_resolve_prop.supportedDepthResolveModes = (VkResolveModeFlags)std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "supportedStencilResolveModes") == 0) {
                property.depth_stencil_resolve_prop.supportedStencilResolveModes = (VkResolveModeFlags)std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "independentResolveNone") == 0) {
                bool ret = false;
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.depth_stencil_resolve_prop.independentResolveNone = ret;
            } else if (strcmp(value["name"].asCString(), "independentResolve") == 0) {
                bool ret = false;
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.depth_stencil_resolve_prop.independentResolve = ret;
            } else {
                UNKNOWN_PROPERTY("VK_KHR_depth_stencil_resolve");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES:
        {
            if (strcmp(value["name"].asCString(), "maxPerSetDescriptors") == 0) {
                property.maintenance3_prop.maxPerSetDescriptors = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxMemoryAllocationSize") == 0) {
                property.maintenance3_prop.maxMemoryAllocationSize = std::stoul(value["value"].asString());
            } else {
                UNKNOWN_PROPERTY("VK_KHR_maintenance3");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR:
        {
            bool ret = false;
            if (strcmp(value["name"].asCString(), "denormBehaviorIndependence") == 0) {
                property.float_controls_prop.denormBehaviorIndependence = (VkShaderFloatControlsIndependence)std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "roundingModeIndependence") == 0) {
                property.float_controls_prop.roundingModeIndependence = (VkShaderFloatControlsIndependence)std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "shaderSignedZeroInfNanPreserveFloat16") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderSignedZeroInfNanPreserveFloat16 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderSignedZeroInfNanPreserveFloat32") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderSignedZeroInfNanPreserveFloat32 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderSignedZeroInfNanPreserveFloat64") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderSignedZeroInfNanPreserveFloat64 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderDenormPreserveFloat16") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderDenormPreserveFloat16 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderDenormPreserveFloat32") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderDenormPreserveFloat32 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderDenormPreserveFloat64") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderDenormPreserveFloat64 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderDenormFlushToZeroFloat16") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderDenormFlushToZeroFloat16 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderDenormFlushToZeroFloat32") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderDenormFlushToZeroFloat32 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderDenormFlushToZeroFloat64") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderDenormFlushToZeroFloat64 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderRoundingModeRTEFloat16") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderRoundingModeRTEFloat16 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderRoundingModeRTEFloat32") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderRoundingModeRTEFloat32 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderRoundingModeRTEFloat64") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderRoundingModeRTEFloat64 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderRoundingModeRTZFloat16") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderRoundingModeRTZFloat16 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderRoundingModeRTZFloat32") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderRoundingModeRTZFloat32 = ret;
            } else if (strcmp(value["name"].asCString(), "shaderRoundingModeRTZFloat64") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.float_controls_prop.shaderRoundingModeRTZFloat64 = ret;
            } else {
                UNKNOWN_PROPERTY("VK_KHR_shader_float_controls");
            }
        }
        break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES:
        {
            bool ret = false;
            if (strcmp(value["name"].asCString(), "maxUpdateAfterBindDescriptorsInAllPools") == 0) {
                property.descriptor_indexing_prop.maxUpdateAfterBindDescriptorsInAllPools = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxPerStageDescriptorUpdateAfterBindSamplers") == 0) {
                property.descriptor_indexing_prop.maxPerStageDescriptorUpdateAfterBindSamplers = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxPerStageDescriptorUpdateAfterBindUniformBuffers") == 0) {
                property.descriptor_indexing_prop.maxPerStageDescriptorUpdateAfterBindUniformBuffers = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxPerStageDescriptorUpdateAfterBindStorageBuffers") == 0) {
                property.descriptor_indexing_prop.maxPerStageDescriptorUpdateAfterBindStorageBuffers = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxPerStageDescriptorUpdateAfterBindSampledImages") == 0) {
                property.descriptor_indexing_prop.maxPerStageDescriptorUpdateAfterBindSampledImages = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxPerStageDescriptorUpdateAfterBindStorageImages") == 0) {
                property.descriptor_indexing_prop.maxPerStageDescriptorUpdateAfterBindStorageImages = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxPerStageDescriptorUpdateAfterBindInputAttachments") == 0) {
                property.descriptor_indexing_prop.maxPerStageDescriptorUpdateAfterBindInputAttachments = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxPerStageUpdateAfterBindResources") == 0) {
                property.descriptor_indexing_prop.maxPerStageUpdateAfterBindResources = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxDescriptorSetUpdateAfterBindSamplers") == 0) {
                property.descriptor_indexing_prop.maxDescriptorSetUpdateAfterBindSamplers = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxDescriptorSetUpdateAfterBindUniformBuffers") == 0) {
                property.descriptor_indexing_prop.maxDescriptorSetUpdateAfterBindUniformBuffers = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxDescriptorSetUpdateAfterBindUniformBuffersDynamic") == 0) {
                property.descriptor_indexing_prop.maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxDescriptorSetUpdateAfterBindStorageBuffers") == 0) {
                property.descriptor_indexing_prop.maxDescriptorSetUpdateAfterBindStorageBuffers = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxDescriptorSetUpdateAfterBindStorageBuffersDynamic") == 0) {
                property.descriptor_indexing_prop.maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxDescriptorSetUpdateAfterBindSampledImages") == 0) {
                property.descriptor_indexing_prop.maxDescriptorSetUpdateAfterBindSampledImages = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxDescriptorSetUpdateAfterBindStorageImages") == 0) {
                property.descriptor_indexing_prop.maxDescriptorSetUpdateAfterBindStorageImages = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "maxDescriptorSetUpdateAfterBindInputAttachments") == 0) {
                property.descriptor_indexing_prop.maxDescriptorSetUpdateAfterBindInputAttachments = std::stoul(value["value"].asString());
            } else if (strcmp(value["name"].asCString(), "shaderUniformBufferArrayNonUniformIndexingNative") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.descriptor_indexing_prop.shaderUniformBufferArrayNonUniformIndexingNative = ret;
            } else if (strcmp(value["name"].asCString(), "shaderSampledImageArrayNonUniformIndexingNative") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.descriptor_indexing_prop.shaderSampledImageArrayNonUniformIndexingNative = ret;
            } else if (strcmp(value["name"].asCString(), "shaderStorageBufferArrayNonUniformIndexingNative") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.descriptor_indexing_prop.shaderStorageBufferArrayNonUniformIndexingNative = ret;
            } else if (strcmp(value["name"].asCString(), "shaderStorageImageArrayNonUniformIndexingNative") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.descriptor_indexing_prop.shaderStorageImageArrayNonUniformIndexingNative = ret;
            } else if (strcmp(value["name"].asCString(), "shaderInputAttachmentArrayNonUniformIndexingNative") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.descriptor_indexing_prop.shaderInputAttachmentArrayNonUniformIndexingNative = ret;
            } else if (strcmp(value["name"].asCString(), "robustBufferAccessUpdateAfterBind") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.descriptor_indexing_prop.robustBufferAccessUpdateAfterBind = ret;
            } else if (strcmp(value["name"].asCString(), "quadDivergentImplicitLod") == 0) {
                std::istringstream(value["value"].asString()) >> std::boolalpha >> ret;
                property.descriptor_indexing_prop.quadDivergentImplicitLod = ret;
            } else {
                UNKNOWN_PROPERTY("VK_EXT_descriptor_indexing");
            }
        }
        break;
    default:
        ErrorPrintf("Unknown extension %d\n", stype);
        break;
    }
}
