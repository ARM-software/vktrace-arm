#!/usr/bin/python3 -i
#
# Copyright (c) 2015-2020, 2020 ARM Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import os,re,sys,string
import xml.etree.ElementTree as etree
import generator as gen
from generator import *
from collections import namedtuple
from common_codegen import *

VK_STRUCT_MEMBER_CODEGEN = """
/*
 * (C) COPYRIGHT 2020 ARM Limited
 * ALL RIGHTS RESERVED
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     LICENSE-2.0" target="_blank" rel="nofollow">http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

@foreach struct where('{sctName}' in ['VkPhysicalDeviceVulkan11Features',\
                                      'VkPhysicalDeviceASTCDecodeFeaturesEXT',\
                                      'VkPhysicalDeviceScalarBlockLayoutFeatures',\
                                      'VkPhysicalDeviceMultiviewFeatures',\
                                      'VkPhysicalDeviceVulkan12Features',\
                                      'VkPhysicalDeviceVariablePointersFeatures',\
                                      'VkPhysicalDeviceShaderAtomicFloatFeaturesEXT',\
                                      'VkPhysicalDeviceShaderAtomicInt64Features',\
                                      'VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT',\
                                      'VkPhysicalDevice8BitStorageFeatures',\
                                      'VkPhysicalDevice16BitStorageFeatures',\
                                      'VkPhysicalDeviceShaderFloat16Int8Features',\
                                      'VkPhysicalDeviceShaderClockFeaturesKHR',\
                                      'VkPhysicalDeviceSamplerYcbcrConversionFeatures',\
                                      'VkPhysicalDeviceProtectedMemoryFeatures',\
                                      'VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT',\
                                      'VkPhysicalDeviceConditionalRenderingFeaturesEXT',\
                                      'VkPhysicalDeviceShaderDrawParametersFeatures',\
                                      'VkPhysicalDeviceDescriptorIndexingFeatures',\
                                      'VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT',\
                                      'VkPhysicalDeviceTransformFeedbackFeaturesEXT',\
                                      'VkPhysicalDeviceVulkanMemoryModelFeatures',\
                                      'VkPhysicalDeviceInlineUniformBlockFeaturesEXT',\
                                      'VkPhysicalDeviceInlineUniformBlockFeatures',\
                                      'VkPhysicalDeviceFragmentDensityMapFeaturesEXT',\
                                      'VkPhysicalDeviceFragmentDensityMap2FeaturesEXT',\
                                      'VkPhysicalDeviceUniformBufferStandardLayoutFeatures',\
                                      'VkPhysicalDeviceDepthClipEnableFeaturesEXT',\
                                      'VkPhysicalDeviceBufferDeviceAddressFeatures',\
                                      'VkPhysicalDeviceImagelessFramebufferFeatures',\
                                      'VkPhysicalDeviceYcbcrImageArraysFeaturesEXT',\
                                      'VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures',\
                                      'VkPhysicalDeviceHostQueryResetFeatures',\
                                      'VkPhysicalDeviceTimelineSemaphoreFeatures',\
                                      'VkPhysicalDeviceIndexTypeUint8FeaturesEXT',\
                                      'VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures',\
                                      'VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR',\
                                      'VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT',\
                                      'VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures',\
                                      'VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT',\
                                      'VkPhysicalDeviceTextureCompressionASTCHDRFeaturesEXT',\
                                      'VkPhysicalDeviceTextureCompressionASTCHDRFeatures',\
                                      'VkPhysicalDeviceLineRasterizationFeaturesEXT',\
                                      'VkPhysicalDeviceSubgroupSizeControlFeaturesEXT',\
                                      'VkPhysicalDeviceSubgroupSizeControlFeatures',\
                                      'VkPhysicalDeviceAccelerationStructureFeaturesKHR',\
                                      'VkPhysicalDeviceRayTracingPipelineFeaturesKHR',\
                                      'VkPhysicalDeviceRayQueryFeaturesKHR',\
                                      'VkPhysicalDeviceExtendedDynamicStateFeaturesEXT',\
                                      'VkPhysicalDeviceDeviceMemoryReportFeaturesEXT',\
                                      'VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT',\
                                      'VkPhysicalDevicePipelineCreationCacheControlFeatures',\
                                      'VkPhysicalDeviceImageRobustnessFeaturesEXT',\
                                      'VkPhysicalDeviceImageRobustnessFeatures',\
                                      'VkPhysicalDeviceShaderTerminateInvocationFeaturesKHR',\
                                      'VkPhysicalDeviceShaderTerminateInvocationFeatures',\
                                      'VkPhysicalDeviceCustomBorderColorFeaturesEXT',\
                                      'VkPhysicalDevicePortabilitySubsetFeaturesKHR',\
                                      'VkPhysicalDevicePerformanceQueryFeaturesKHR',\
                                      'VkPhysicalDevice4444FormatsFeaturesEXT',\
                                      'VkPhysicalDeviceFragmentShadingRateFeaturesKHR',\
                                      'VkPhysicalDeviceFeatures'])
std::string getString{sctName}(int idx)
{{
    std::string features[] = {{
    @foreach member
    @if({memPtrLevel} == 0)
        @if('{memName}' != 'pNext' and '{memType}' == 'VkBool32')
        "{memName}",
        @end if
    @end if
    @end member
    }};

    return features[idx];
}}

@end struct

std::string getStringVkPhysicalDeviceFeatures2(int idx)
{{
    return getStringVkPhysicalDeviceFeatures(idx);
}}

"""
