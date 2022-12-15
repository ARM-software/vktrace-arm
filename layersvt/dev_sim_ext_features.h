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
    VkPhysicalDeviceASTCDecodeFeaturesEXT            astc_decode_feature;
    VkPhysicalDeviceScalarBlockLayoutFeatures        scalar_block_layout_feature;
    VkPhysicalDeviceShaderFloat16Int8Features        shader_float16_int8_feature;
    VkPhysicalDevice16BitStorageFeatures             bit16_storage_feature;
    VkPhysicalDeviceMultiviewFeatures                multiview_feature;
    VkPhysicalDeviceSamplerYcbcrConversionFeatures   sampler_ycbcr_conv_feature;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_feature;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR    ray_tracing_pipeline_feature;
    VkPhysicalDeviceRayQueryFeaturesKHR              ray_query_feature;
    VkPhysicalDeviceBufferDeviceAddressFeatures      buffer_device_address_feature;
    VkPhysicalDeviceFragmentDensityMapFeaturesEXT    fragment_density_map_feature;
    VkPhysicalDeviceFragmentDensityMap2FeaturesEXT   fragment_density_map2_feature;
    VkPhysicalDeviceImagelessFramebufferFeatures     imageless_framebuffer_feature;
    VkPhysicalDeviceTransformFeedbackFeaturesEXT     transform_feedback_feature;
    VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT vertex_attribute_divisor_feature;
    VkPhysicalDeviceTimelineSemaphoreFeaturesKHR     timeline_semaphore_feature;
    VkPhysicalDeviceTextureCompressionASTCHDRFeatures texture_compression_astc_hdr_feature;
    VkPhysicalDeviceDescriptorIndexingFeatures       descriptor_indexing_feature;
    // Todo: Add more extended features here
} ExtendedFeature;

typedef union {
    struct {
        VkStructureType    sType;
        void*              pNext;
    } base;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR  acceleration_structure_prop;
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT     external_memory_host_prop;
    VkPhysicalDeviceTransformFeedbackPropertiesEXT      transform_feedback_prop;
    VkPhysicalDeviceSubgroupProperties                  physical_device_subgroup_prop;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR     ray_tracing_pipeline_prop;
    VkPhysicalDeviceFragmentDensityMapPropertiesEXT     fragment_density_map_prop;
    VkPhysicalDeviceFragmentDensityMap2PropertiesEXT    fragment_density_map2_prop;
    VkPhysicalDeviceMultiviewProperties                 multiview_prop;
    VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT  vertex_attribute_divisor_prop;
    VkPhysicalDeviceTimelineSemaphorePropertiesKHR      timeline_semaphore_prop;
    VkPhysicalDeviceDepthStencilResolveProperties       depth_stencil_resolve_prop;
    VkPhysicalDeviceMaintenance3Properties              maintenance3_prop;
    VkPhysicalDeviceFloatControlsPropertiesKHR          float_controls_prop;
    VkPhysicalDeviceDescriptorIndexingProperties        descriptor_indexing_prop;
    // Todo: Add more extended properties here
} ExtendedProperty;

VkStructureType ext_feature_name_to_stype(const std::string& ext_feat_name);
void get_extension_feature(VkStructureType stype, const Json::Value& value, ExtendedFeature& feature);
VkStructureType ext_property_name_to_stype(const std::string& ext_prop_name);
void get_extension_property(VkStructureType stype, const Json::Value& value, ExtendedProperty& property);

#endif // _DEV_SIM_EXT_FEATURES_H_