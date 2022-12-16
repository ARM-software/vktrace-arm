
/*
 * (C) COPYRIGHT 2019 ARM Limited
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

#include "hwcProfiler.h"

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
{
    // Get the function pointer
    VkLayerInstanceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    assert(chain_info->u.pLayerInfo != 0);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    assert(fpGetInstanceProcAddr != 0);
    PFN_vkCreateInstance fpCreateInstance = (PFN_vkCreateInstance) fpGetInstanceProcAddr(NULL, "vkCreateInstance");
    if(fpCreateInstance == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Call the function and create the dispatch table
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
    VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);

    if(result == VK_SUCCESS) {
        initInstanceTable(*pInstance, fpGetInstanceProcAddr);
        HWCProfilerInstance::current().startCapture();
    }
    return result;
}


VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    HWCProfilerInstance::current().stopCapture();
    // Destroy the dispatch table
    dispatch_key key = get_dispatch_key(instance);
    instance_dispatch_table(instance)->DestroyInstance(instance, pAllocator);
    destroy_instance_dispatch_table(key);
}


VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    // Get the function pointer
    VkLayerDeviceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    assert(chain_info->u.pLayerInfo != 0);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice) fpGetInstanceProcAddr(NULL, "vkCreateDevice");
    if(fpCreateDevice == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Call the function and create the dispatch table
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
    VkResult result = fpCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);

    if(result == VK_SUCCESS) {
        initDeviceTable(*pDevice, fpGetDeviceProcAddr);
    }

    return result;
}


VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    // Destroy the dispatch table
    dispatch_key key = get_dispatch_key(device);
    device_dispatch_table(device)->DestroyDevice(device, pAllocator);
    destroy_device_dispatch_table(key);
}


VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
    return util_GetExtensionProperties(0, NULL, pPropertyCount, pProperties);
}


VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties)
{
    static const VkLayerProperties layerProperties[] = {
        {
            "VK_LAYER_ARM_HWC_Profiler",
            VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION), // specVersion
            VK_MAKE_VERSION(0, 2, 0), // implementationVersion
            "layer: hwcProfiler",
        }
    };

    return util_GetLayerProperties(ARRAY_SIZE(layerProperties), layerProperties, pPropertyCount, pProperties);
}


VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties)
{
    static const VkLayerProperties layerProperties[] = {
        {
            "VK_LAYER_ARM_HWC_Profiler",
            VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION),
            VK_MAKE_VERSION(0, 2, 0),
            "layer: hwcProfiler",
        }
    };

    return util_GetLayerProperties(ARRAY_SIZE(layerProperties), layerProperties, pPropertyCount, pProperties);
}


VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    VkResult result = device_dispatch_table(queue)->QueuePresentKHR(queue, pPresentInfo);
    HWCProfilerInstance::current().sampleProcess();
    HWCProfilerInstance::current().nextFrame();
    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    if(strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceLayerProperties);
    if(strcmp(pName, "vkCreateInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
    if(strcmp(pName, "vkDestroyInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyInstance);
    if(strcmp(pName, "vkCreateDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);

    if(instance_dispatch_table(instance)->GetInstanceProcAddr == NULL)
        return NULL;
    return instance_dispatch_table(instance)->GetInstanceProcAddr(instance, pName);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName)
{
    if(strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if(strcmp(pName, "vkDestroyDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDevice);
    if(strcmp(pName, "vkQueuePresentKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueuePresentKHR);
    if(strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);
    if(strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceExtensionProperties);

    if(device_dispatch_table(device)->GetDeviceProcAddr == NULL)
        return NULL;
    return device_dispatch_table(device)->GetDeviceProcAddr(device, pName);
}
