#!/usr/bin/python3 -i
#
# Copyright (c) 2015-2020, 2020 ARM Corporation
#
# Author: Parker

import os,re,sys,string
import xml.etree.ElementTree as etree
import generator as gen
from generator import *
from collections import namedtuple
from common_codegen import *

SYSTRACE_CODEGEN = """
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
#include <android/trace.h>
#include "vk_loader_platform.h"
#include "vulkan/vk_layer.h"
#include "vk_layer_config.h"
#include "vk_layer_table.h"
#include "vk_layer_extension_utils.h"
#include "vk_layer_utils.h"
#include "vktrace_systrace.h"

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
    ATrace_beginSection("{funcName}");
    {funcReturn} result = fpCreateInstance({funcNamedParams});
    ATrace_endSection();
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
    ATrace_beginSection("{funcName}");
    instance_dispatch_table({funcDispatchParam})->DestroyInstance({funcNamedParams});
    ATrace_endSection();
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
    ATrace_beginSection("{funcName}");
    {funcReturn} result = fpCreateDevice({funcNamedParams});
    ATrace_endSection();
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
    ATrace_beginSection("{funcName}");
    device_dispatch_table({funcDispatchParam})->DestroyDevice({funcNamedParams});
    ATrace_endSection();
    destroy_device_dispatch_table(key);
}}
@end function

@foreach function where('{funcName}' == 'vkEnumerateInstanceExtensionProperties')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    ATrace_beginSection("{funcName}");
    {funcReturn} result = util_GetExtensionProperties(0, NULL, pPropertyCount, pProperties);
    ATrace_endSection();
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkEnumerateInstanceLayerProperties')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    static const VkLayerProperties layerProperties[] = {{
        {{
            "VK_LAYER_ARM_systrace",
            VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION), // specVersion
            VK_MAKE_VERSION(0, 2, 0), // implementationVersion
            "layer: systrace",
        }}
    }};
    ATrace_beginSection("{funcName}");
    {funcReturn} result = util_GetLayerProperties(ARRAY_SIZE(layerProperties), layerProperties, pPropertyCount, pProperties);
    ATrace_endSection();
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkEnumerateDeviceLayerProperties')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    static const VkLayerProperties layerProperties[] = {{
        {{
            "VK_LAYER_ARM_systrace",
            VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION),
            VK_MAKE_VERSION(0, 2, 0),
            "layer: systrace",
        }}
    }};
    ATrace_beginSection("{funcName}");
    {funcReturn} result = util_GetLayerProperties(ARRAY_SIZE(layerProperties), layerProperties, pPropertyCount, pProperties);
    ATrace_endSection();
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkQueuePresentKHR')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    ATrace_beginSection("{funcName}");
    {funcReturn} result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
    ATrace_endSection();
    return result;
}}
@end function

// Autogen instance functions

@foreach function where('{funcDispatchType}' == 'instance' and '{funcReturn}' != 'void' and '{funcName}' not in ['vkCreateInstance', 'vkDestroyInstance', 'vkCreateDevice', 'vkGetInstanceProcAddr', 'vkEnumerateDeviceExtensionProperties', 'vkEnumerateDeviceLayerProperties'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    ATrace_beginSection("{funcName}");
    {funcReturn} result = instance_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
    ATrace_endSection();
    return result;
}}
@end function

@foreach function where('{funcDispatchType}' == 'instance' and '{funcReturn}' == 'void' and '{funcName}' not in ['vkCreateInstance', 'vkDestroyInstance', 'vkCreateDevice', 'vkGetInstanceProcAddr', 'vkEnumerateDeviceExtensionProperties', 'vkEnumerateDeviceLayerProperties'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    ATrace_beginSection("{funcName}");
    instance_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
    ATrace_endSection();
}}
@end function

// Autogen device functions

@foreach function where('{funcDispatchType}' == 'device' and '{funcReturn}' != 'void' and '{funcName}' not in ['vkDestroyDevice', 'vkEnumerateInstanceExtensionProperties', 'vkEnumerateInstanceLayerProperties', 'vkQueuePresentKHR', 'vkGetDeviceProcAddr'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    ATrace_beginSection("{funcName}");
    {funcReturn} result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
    ATrace_endSection();
    return result;
}}
@end function

@foreach function where('{funcDispatchType}' == 'device' and '{funcReturn}' == 'void' and '{funcName}' not in ['vkDestroyDevice', 'vkEnumerateInstanceExtensionProperties', 'vkEnumerateInstanceLayerProperties', 'vkGetDeviceProcAddr'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    ATrace_beginSection("{funcName}");
    device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
    ATrace_endSection();
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