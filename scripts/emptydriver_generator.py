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

EMPTYDRIVER_CODEGEN = """
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
#include "vk_loader_platform.h"
#include "vulkan/vk_layer.h"
#include "vk_layer_config.h"
#include "vk_layer_table.h"
#include "vk_layer_extension_utils.h"
#include "vk_layer_utils.h"
#include "vktrace_emptydriver.h"

static uint64_t  dummy_handle = 0x0000FFFF;
//============================= API EntryPoints =============================//

// Specifically implemented functions

@foreach function where('{funcName}' == 'vkCreateInstance')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    // Get the function pointer
    VkLayerInstanceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    assert(chain_info->u.pLayerInfo != 0);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    assert(fpGetInstanceProcAddr != 0);
    PFN_vkCreateInstance fpCreateInstance = (PFN_vkCreateInstance) fpGetInstanceProcAddr(NULL, "vkCreateInstance");
    if(fpCreateInstance == NULL) {{
        return VK_ERROR_INITIALIZATION_FAILED;
    }}

    // Call the function and create the dispatch table
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
    {funcReturn} result = fpCreateInstance({funcNamedParams});
    if(result == VK_SUCCESS) {{
        initInstanceTable(*pInstance, fpGetInstanceProcAddr);
    }}
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkDestroyInstance')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    // Destroy the dispatch table
    dispatch_key key = get_dispatch_key({funcDispatchParam});
    instance_dispatch_table({funcDispatchParam})->DestroyInstance({funcNamedParams});
    destroy_instance_dispatch_table(key);
}}
@end function

@foreach function where('{funcName}' == 'vkCreateDevice')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    // Get the function pointer
    VkLayerDeviceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    assert(chain_info->u.pLayerInfo != 0);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice) fpGetInstanceProcAddr(NULL, "vkCreateDevice");
    if(fpCreateDevice == nullptr) {{
        return VK_ERROR_INITIALIZATION_FAILED;
    }}

    // Call the function and create the dispatch table
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
    {funcReturn} result = fpCreateDevice({funcNamedParams});
    if(result == VK_SUCCESS) {{
        initDeviceTable(*pDevice, fpGetDeviceProcAddr);
    }}

    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkDestroyDevice')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    // Destroy the dispatch table
    dispatch_key key = get_dispatch_key({funcDispatchParam});
    device_dispatch_table({funcDispatchParam})->DestroyDevice({funcNamedParams});
    destroy_device_dispatch_table(key);
}}
@end function

@foreach function where('{funcName}' == 'vkEnumerateInstanceExtensionProperties')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = util_GetExtensionProperties(0, NULL, pPropertyCount, pProperties);
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkEnumerateInstanceLayerProperties')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    static const VkLayerProperties layerProperties[] = {{
        {{
            "VK_LAYER_ARM_emptydriver",
            VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION), // specVersion
            VK_MAKE_VERSION(0, 2, 0), // implementationVersion
            "layer: emptydriver",
        }}
    }};

    {funcReturn} result = util_GetLayerProperties(ARRAY_SIZE(layerProperties), layerProperties, pPropertyCount, pProperties);

    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkEnumerateDeviceLayerProperties')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    static const VkLayerProperties layerProperties[] = {{
        {{
            "VK_LAYER_ARM_emptydriver",
            VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION),
            VK_MAKE_VERSION(0, 2, 0),
            "layer: emptydriver",
        }}
    }};
    {funcReturn} result = util_GetLayerProperties(ARRAY_SIZE(layerProperties), layerProperties, pPropertyCount, pProperties);
    return result;
}}
@end function

// instance function begin
@foreach function where('{funcName}' == 'vkCreateDebugReportCallbackEXT')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = instance_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pCallback = (VkDebugReportCallbackEXT)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreateDebugUtilsMessengerEXT')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = instance_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pMessenger = (VkDebugUtilsMessengerEXT)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkSubmitDebugUtilsMessageEXT')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
#ifndef USE_EMPTYDRIVER
    instance_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#endif

}}
@end function

@foreach function where('{funcName}' == 'vkDestroyDebugReportCallbackEXT')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
#ifndef USE_EMPTYDRIVER
    instance_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#endif

}}
@end function

@foreach function where('{funcName}' == 'vkDestroyDebugUtilsMessengerEXT')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
#ifndef USE_EMPTYDRIVER
    instance_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#endif

}}
@end function

@foreach function where('{funcName}' == 'vkDebugReportMessageEXT')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
#ifndef USE_EMPTYDRIVER
    instance_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#endif

}}
@end function

// instance function end


// device function begin

@foreach function where('{funcName}' == 'vkQueuePresentKHR')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreateFramebuffer')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
else
    dummy_handle++;
    *pFramebuffer = (VkFramebuffer)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreateRenderPass')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pRenderPass = (VkRenderPass)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreateRenderPass2')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pRenderPass = (VkRenderPass)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreateRenderPass2KHR')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pRenderPass = (VkRenderPass)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreateComputePipelines')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pPipelines = (VkPipeline)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreatePipelineCache')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pPipelineCache = (VkPipelineCache)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreatePipelineLayout')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pPipelineLayout = (VkPipelineLayout)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreateGraphicsPipelines')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pPipelines = (VkPipeline)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreateRayTracingPipelinesNV')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pPipelines = (VkPipeline)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreateShaderModule')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pShaderModule = (VkShaderModule)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkAcquireNextImageKHR')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    *pImageIndex = 1;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkAcquireNextImage2KHR')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pImageIndex = 1;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreateDescriptorUpdateTemplate')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pDescriptorUpdateTemplate = (VkDescriptorUpdateTemplate)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreateSamplerYcbcrConversion')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pYcbcrConversion = (VkSamplerYcbcrConversion)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkCreateFence')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pFence = (VkFence)dummy_handle;
#endif
    return result;
}}
@end function


@foreach function where('{funcName}' == 'vkCreateSampler')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pSampler = (VkSampler)dummy_handle;
#endif
    return result;
}}
@end function


@foreach function where('{funcName}' == 'vkCreateIndirectCommandsLayoutNVX')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pIndirectCommandsLayout = (VkIndirectCommandsLayoutNVX)dummy_handle;
#endif
    return result;
}}
@end function


@foreach function where('{funcName}' == 'vkCreateObjectTableNVX')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pObjectTable = (VkObjectTableNVX)dummy_handle;
#endif
    return result;
}}
@end function


@foreach function where('{funcName}' == 'vkCreateSamplerYcbcrConversionKHR')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pYcbcrConversion = (VkSamplerYcbcrConversion)dummy_handle;
#endif
    return result;
}}
@end function


@foreach function where('{funcName}' == 'vkCreateValidationCacheEXT')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pValidationCache = (VkValidationCacheEXT)dummy_handle;
#endif
    return result;
}}
@end function


@foreach function where('{funcName}' == 'vkCreateDescriptorUpdateTemplateKHR')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pDescriptorUpdateTemplate = (VkDescriptorUpdateTemplate)dummy_handle;
#endif
    return result;
}}
@end function


@foreach function where('{funcName}' == 'vkCreateAccelerationStructureNV')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pAccelerationStructure = (VkAccelerationStructureNV)dummy_handle;
#endif
    return result;
}}
@end function


@foreach function where('{funcName}' == 'vkCreateSemaphore')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pSemaphore = (VkSemaphore)dummy_handle;
#endif
    return result;
}}
@end function


@foreach function where('{funcName}' == 'vkCreateEvent')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pEvent = (VkEvent)dummy_handle;
#endif
    return result;
}}
@end function


@foreach function where('{funcName}' == 'vkCreateQueryPool')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#else
    dummy_handle++;
    *pQueryPool = (VkQueryPool)dummy_handle;
#endif
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkGetEventStatus')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_EVENT_SET;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#endif
    return result;
}}
@end function

// device function end

// Autogen instance functions

@foreach function where('{funcDispatchType}' == 'instance' and '{funcReturn}' != 'void' and '{funcName}' not in \
    ['vkCreateInstance', 'vkDestroyInstance', 'vkCreateDevice', 'vkGetInstanceProcAddr', 'vkEnumerateDeviceExtensionProperties', 'vkEnumerateDeviceLayerProperties', 'vkCreateDebugReportCallbackEXT', 'vkCreateDebugUtilsMessengerEXT'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = instance_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
    return result;
}}
@end function

@foreach function where('{funcDispatchType}' == 'instance' and '{funcReturn}' == 'void' and '{funcName}' not in \
    ['vkCreateInstance', 'vkDestroyInstance', 'vkCreateDevice', 'vkGetInstanceProcAddr', 'vkEnumerateDeviceExtensionProperties', 'vkEnumerateDeviceLayerProperties', 'vkSubmitDebugUtilsMessageEXT', 'vkDestroyDebugReportCallbackEXT', 'vkDestroyDebugUtilsMessengerEXT', 'vkDebugReportMessageEXT'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    instance_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
}}
@end function

// Autogen device functions

@foreach function where('{funcDispatchType}' == 'device' and '{funcReturn}' != 'void' and '{funcName}' not in \
    ['vkDestroyDevice', 'vkEnumerateInstanceExtensionProperties', 'vkEnumerateInstanceLayerProperties', 'vkQueuePresentKHR', 'vkGetDeviceProcAddr', \
    'vkCreateFramebuffer', 'vkCreateRenderPass', 'vkCreateRenderPass2', 'vkCreateRenderPass2KHR', 'vkCreateComputePipelines', 'vkCreatePipelineCache', 'vkCreatePipelineLayout', 'vkCreateGraphicsPipelines', 'vkCreateRayTracingPipelinesNV', 'vkCreateSwapchainKHR', 'vkCreateSharedSwapchainsKHR', 'vkAllocateMemory', 'vkCreateImage', 'vkCreateImageView', 'vkCreateBuffer', 'vkCreateBufferView', 'vkCreateShaderModule', 'vkAcquireNextImageKHR', 'vkAcquireNextImage2KHR', \
    'vkCreateDescriptorUpdateTemplate', 'vkCreateSamplerYcbcrConversion', 'vkCreateFence', 'vkCreateSampler', 'vkCreateIndirectCommandsLayoutNVX', 'vkCreateObjectTableNVX', 'vkCreateSamplerYcbcrConversionKHR', 'vkCreateValidationCacheEXT', 'vkCreateDescriptorUpdateTemplateKHR', 'vkCreateAccelerationStructureNV', 'vkCreateSemaphore', 'vkCreateEvent', 'vkCreateQueryPool', 'vkGetEventStatus', \
    'vkBindBufferMemory2KHR', 'vkBindImageMemory2KHR', 'vkBindAccelerationStructureMemoryNV', 'vkBindBufferMemory2', 'vkBindImageMemory2', 'vkBindBufferMemory', 'vkBindImageMemory', 'vkMapMemory', 'vkGetSwapchainImagesKHR', 'vkCreateDescriptorPool', 'vkAllocateDescriptorSets', 'vkFreeDescriptorSets', 'vkCreateDescriptorSetLayout', 'vkCreateCommandPool', 'vkAllocateCommandBuffers', 'vkGetDeviceMemoryOpaqueCaptureAddressKHR', \
    'vkGetDeviceMemoryOpaqueCaptureAddress', 'vkGetMemoryWin32HandleNV', 'vkGetMemoryHostPointerPropertiesEXT', 'vkGetMemoryWin32HandleKHR', 'vkGetMemoryWin32HandlePropertiesKHR', 'vkGetMemoryAndroidHardwareBufferANDROID', 'vkGetMemoryFdKHR', 'vkGetMemoryFdPropertiesKHR', 'vkGetBufferDeviceAddressKHR', 'vkGetBufferOpaqueCaptureAddressKHR', 'vkGetBufferDeviceAddress', 'vkGetBufferOpaqueCaptureAddress', 'vkGetBufferDeviceAddressEXT', \
    'vkGetAndroidHardwareBufferPropertiesANDROID'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = VK_SUCCESS;
#ifndef USE_EMPTYDRIVER
    result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#endif
    return result;
}}
@end function

@foreach function where('{funcDispatchType}' == 'device' and '{funcReturn}' != 'void' and '{funcName}' in \
    ['vkCreateSwapchainKHR', 'vkCreateSharedSwapchainsKHR', 'vkAllocateMemory', 'vkCreateImage', 'vkCreateImageView', 'vkCreateBuffer', 'vkCreateBufferView',  \
    'vkBindBufferMemory2KHR', 'vkBindImageMemory2KHR', 'vkBindAccelerationStructureMemoryNV', 'vkBindBufferMemory2', 'vkBindImageMemory2', 'vkBindBufferMemory', 'vkBindImageMemory', 'vkMapMemory', 'vkGetSwapchainImagesKHR', 'vkCreateDescriptorPool', 'vkAllocateDescriptorSets', 'vkFreeDescriptorSets', 'vkCreateDescriptorSetLayout', 'vkCreateCommandPool', 'vkAllocateCommandBuffers', 'vkGetDeviceMemoryOpaqueCaptureAddressKHR', \
    'vkGetDeviceMemoryOpaqueCaptureAddress', 'vkGetMemoryWin32HandleNV', 'vkGetMemoryHostPointerPropertiesEXT', 'vkGetMemoryWin32HandleKHR', 'vkGetMemoryWin32HandlePropertiesKHR', 'vkGetMemoryAndroidHardwareBufferANDROID', 'vkGetMemoryFdKHR', 'vkGetMemoryFdPropertiesKHR',  'vkGetBufferDeviceAddressKHR', 'vkGetBufferOpaqueCaptureAddressKHR', 'vkGetBufferDeviceAddress', 'vkGetBufferOpaqueCaptureAddress', 'vkGetBufferDeviceAddressEXT', \
    'vkGetAndroidHardwareBufferPropertiesANDROID'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    {funcReturn} result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
    return result;
}}
@end function

@foreach function where('{funcDispatchType}' == 'device' and '{funcReturn}' == 'void' and '{funcName}' not in \
    ['vkDestroyDevice', 'vkEnumerateInstanceExtensionProperties', 'vkEnumerateInstanceLayerProperties', 'vkGetDeviceProcAddr', \
    'vkFreeMemory', 'vkDestroyImage', 'vkDestroyImageView', 'vkDestroyBuffer', 'vkDestroyBufferView', 'vkUnmapMemory', 'vkDestroyDescriptorPool', 'vkDestroyDescriptorSetLayout', 'vkGetDeviceQueue', 'vkGetDeviceQueue2', 'vkFreeCommandBuffers', 'vkDestroyCommandPool', 'vkGetBufferMemoryRequirements', 'vkGetBufferMemoryRequirements2', \
    'vkGetDeviceGroupPeerMemoryFeaturesKHR', 'vkGetDeviceGroupPeerMemoryFeatures', 'vkGetImageSparseMemoryRequirements2KHR', 'vkGetImageMemoryRequirements2', 'vkGetImageSparseMemoryRequirements2', 'vkGetImageMemoryRequirements2KHR', 'vkGetBufferMemoryRequirements2KHR', 'vkGetDeviceMemoryCommitment', 'vkGetAccelerationStructureMemoryRequirementsNV', \
    'vkGetImageMemoryRequirements', 'vkGetImageSparseMemoryRequirements'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
#ifndef USE_EMPTYDRIVER
    device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
#endif
}}
@end function

@foreach function where('{funcDispatchType}' == 'device' and '{funcReturn}' == 'void' and '{funcName}' in \
    ['vkFreeMemory', 'vkDestroyImage', 'vkDestroyImageView', 'vkDestroyBuffer', 'vkDestroyBufferView', 'vkUnmapMemory', 'vkDestroyDescriptorPool', 'vkDestroyDescriptorSetLayout', 'vkGetDeviceQueue', 'vkGetDeviceQueue2', 'vkFreeCommandBuffers', 'vkDestroyCommandPool', 'vkGetBufferMemoryRequirements', 'vkGetBufferMemoryRequirements2', \
    'vkGetDeviceGroupPeerMemoryFeaturesKHR', 'vkGetDeviceGroupPeerMemoryFeatures', 'vkGetImageSparseMemoryRequirements2KHR', 'vkGetImageMemoryRequirements2', 'vkGetImageSparseMemoryRequirements2', 'vkGetImageMemoryRequirements2KHR', 'vkGetBufferMemoryRequirements2KHR', 'vkGetDeviceMemoryCommitment', 'vkGetAccelerationStructureMemoryRequirementsNV', \
    'vkGetImageMemoryRequirements', 'vkGetImageSparseMemoryRequirements'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
}}
@end function

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName)
{{
    @foreach function where('{funcType}' == 'instance'  and '{funcName}' not in [ 'vkEnumerateDeviceExtensionProperties' ])
    if(strcmp(pName, "{funcName}") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>({funcName});
    @end function

    if(instance_dispatch_table(instance)->GetInstanceProcAddr == NULL)
        return NULL;
    return instance_dispatch_table(instance)->GetInstanceProcAddr(instance, pName);
}}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName)
{{
    @foreach function where('{funcType}' == 'device')
    if(strcmp(pName, "{funcName}") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>({funcName});
    @end function

    if(device_dispatch_table(device)->GetDeviceProcAddr == NULL)
        return NULL;
    return device_dispatch_table(device)->GetDeviceProcAddr(device, pName);
}}
"""