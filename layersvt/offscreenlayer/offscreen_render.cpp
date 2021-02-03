/*
* This confidential and proprietary software may be used only as
* authorised by a licensing agreement from ARM Limited
* (C) COPYRIGHT 2019-2021 ARM Limited
* ALL RIGHTS RESERVED
* The entire notice above must be reproduced on all authorised
* copies and copies may only be made to the extent permitted
* by a licensing agreement from ARM Limited.
*/

#include <cstdlib>
#include <cassert>
#include <string.h>
#include <assert.h>
#include <unordered_map>
#include <iostream>
#include <algorithm>
#include <list>
#include <map>
#include <set>
#include <vector>
#include "offscreen_render.h"
#include "headless_surface.h"
#include "headless_swapchain.h"
#include "vk_dispatch_table_helper.h"
#include "vk_layer_dispatch_table.h"
const char *imagecount_var = "debug.offscreen.imagecount";

#if defined(__ANDROID__)

using namespace std;

VkPhysicalDeviceProperties gPhyDevProp;

static PFN_vkVoidFunction intercept_core_instance_command(const char *name);
static PFN_vkVoidFunction intercept_core_device_command(const char *name);
static PFN_vkVoidFunction intercept_khr_swapchain_command(const char *name, VkDevice dev);
static void createDeviceRegisterExtensions(const VkDeviceCreateInfo *pCreateInfo, VkDevice device);

static char android_env[64] = {};
static char *android_exec(const char *cmd) {
    FILE *pipe = popen(cmd, "r");
    if (pipe != nullptr) {
        fgets(android_env, 64, pipe);
        pclose(pipe);
    }

    // Only if the value is set will we get a string back
    if (strlen(android_env) > 0) {
        printf("%s = %s", cmd, android_env);
        // Do a right strip of " ", "\n", "\r", "\t" for the android_env string
        std::string android_env_str(android_env);
        android_env_str.erase(android_env_str.find_last_not_of(" \n\r\t") + 1);
        snprintf(android_env, sizeof(android_env), "%s", android_env_str.c_str());
        return android_env;
    }

    return nullptr;
}


static char *android_getenv(const char *key) {
    std::string command("getprop ");
    command += key;
    return android_exec(command.c_str());
}

static VkLayerDispatchTable sDeviceDispatchTable = {};
static VkLayerInstanceDispatchTable sInstanceDispatchTable = {};
static loader_platform_thread_mutex sLock;
static int sLockInitialized = 0;
static VkSurfaceKHR sHeadlessSurface = VK_NULL_HANDLE;
static int sFrameNumber = 0;
static offscreen::swapchain_headless* sHeadlessSwapChain = nullptr;

/******************call vulkan api by headless_surface or headless_swapchain************************/
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          flags,
    VkImageFormatProperties                     *pImageFormatProperties)
{
    VkResult result = instance_dispatch_table(physicalDevice)->GetPhysicalDeviceImageFormatProperties(physicalDevice, format, type, tiling, usage, flags, pImageFormatProperties);
    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkWaitForFences(
    VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence                               *pFences,
    VkBool32                                    waitAll,
    uint64_t                                    timeout)
{
    VkResult result = device_dispatch_table(device)->WaitForFences(device, fenceCount, pFences, waitAll,timeout);
    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkQueueWaitIdle(
    VkQueue                                     queue)
{
    VkResult result = device_dispatch_table(queue)->QueueWaitIdle(queue);
    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkQueueSubmit(
    VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     fence)
{
    VkResult result = device_dispatch_table(queue)->QueueSubmit(queue, submitCount, pSubmits, fence);
    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkResetFences(
    VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences)
{
    VkResult result = device_dispatch_table(device)->ResetFences(device, fenceCount, pFences);
    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkCreateImage(
    VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage)
{
    VkResult result = device_dispatch_table(device)->CreateImage(device, pCreateInfo, pAllocator, pImage);
    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkBindImageMemory(
    VkDevice                                    device,
    VkImage                                     image,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset)
{
    VkResult result = device_dispatch_table(device)->BindImageMemory(device, image, memory, memoryOffset);
    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkCreateFence(
    VkDevice                                    device,
    const VkFenceCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence)
{
    VkResult result = device_dispatch_table(device)->CreateFence(device, pCreateInfo, pAllocator, pFence);
    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkAllocateMemory(
    VkDevice                                    device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMemory)
{
    VkResult result = device_dispatch_table(device)->AllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties                  *pProperties)
{
    instance_dispatch_table(physicalDevice)->GetPhysicalDeviceProperties(physicalDevice, pProperties);
    gPhyDevProp = *pProperties;
}

VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice                                    device,
    VkFence                                     fence,
    const VkAllocationCallbacks*                pAllocator)
{
    device_dispatch_table(device)->DestroyFence(device, fence, pAllocator);
}

VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyImage(
    VkDevice                                    device,
    VkImage                                     image,
    const VkAllocationCallbacks*                pAllocator)
{
    device_dispatch_table(device)->DestroyImage(device, image, pAllocator);
}

VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    const VkAllocationCallbacks*                pAllocator)
{
    device_dispatch_table(device)->FreeMemory(device, memory, pAllocator);
}

VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    VkMemoryRequirements*                       pMemoryRequirements)
{
    device_dispatch_table(device)->GetImageMemoryRequirements(device, image, pMemoryRequirements);
}

VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(
    VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue)
{
    device_dispatch_table(device)->GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
}

/************************************hook vulkan api (wrapper)***************************************************/
static const VkLayerProperties global_layer = {
    "VK_LAYER_offscreenrender", VK_MAKE_VERSION(1, 0, VK_HEADER_VERSION), 1, "Layer: offscreenrender",
};

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceLayerProperties(uint32_t *pCount, VkLayerProperties *pProperties)
{
    return util_GetLayerProperties(1, &global_layer, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t *pCount,
                                                              VkLayerProperties *pProperties)
{
    return util_GetLayerProperties(1, &global_layer, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pCount,
                                                                    VkExtensionProperties *pProperties)
{
    if (pLayerName && !strcmp(pLayerName, global_layer.layerName))
        return util_GetExtensionProperties(0, nullptr, pCount, pProperties);

    return VK_ERROR_LAYER_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName,
                                                                  uint32_t *pCount, VkExtensionProperties *pProperties)
{
    if (pLayerName && !strcmp(pLayerName, global_layer.layerName))
        return util_GetExtensionProperties(0, nullptr, pCount, pProperties);

    assert(physicalDevice);

    VkLayerInstanceDispatchTable *pTable = instance_dispatch_table(physicalDevice);
    return pTable->EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pCount, pProperties);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice dev, const char *funcName)
{
    PFN_vkVoidFunction proc = intercept_core_device_command(funcName);
    if (proc) return proc;

    proc = intercept_khr_swapchain_command(funcName, dev);
    if (proc) return proc;

    if (dev == NULL) {
        return nullptr;
    }
    VkLayerDispatchTable *pTable = device_dispatch_table(dev);
    if (pTable->GetDeviceProcAddr == nullptr) {
        return nullptr;
    }
    proc = pTable->GetDeviceProcAddr(dev, funcName);

    return proc;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char *funcName)
{
    PFN_vkVoidFunction proc = intercept_core_instance_command(funcName);
    if (proc) return proc;

    assert(instance);
    VkLayerInstanceDispatchTable *pTable = instance_dispatch_table(instance);
    if (pTable->GetInstanceProcAddr == nullptr) {
        return nullptr;
    }
    proc = pTable->GetInstanceProcAddr(instance, funcName);

    return proc;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                                              VkInstance *pInstance) {
    printf("------------,function = %s,line = %d",__FUNCTION__,__LINE__);
    VkLayerInstanceCreateInfo *chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    assert(chain_info->u.pLayerInfo);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    assert(fpGetInstanceProcAddr);
    PFN_vkCreateInstance fpCreateInstance = (PFN_vkCreateInstance)fpGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    if (fpCreateInstance == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Advance the link info for the next element on the chain
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) return result;

    initInstanceTable(*pInstance, fpGetInstanceProcAddr);
    layer_init_instance_dispatch_table(*pInstance,&sInstanceDispatchTable,fpGetInstanceProcAddr);
    if (sLockInitialized == 0) {
        loader_platform_thread_create_mutex(&sLock);
        sLockInitialized = 1;
    }
    sFrameNumber = 0;
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    // Destroy the dispatch table
    printf("------------,function = %s,line = %d,sFrameNumber = %d",__FUNCTION__,__LINE__,sFrameNumber);
    if (sHeadlessSurface != VK_NULL_HANDLE) {
        vkDestroyHeadlessSurfaceKHR(instance,sHeadlessSurface,pAllocator);
        sHeadlessSurface = VK_NULL_HANDLE;
    }
    dispatch_key key = get_dispatch_key(instance);
    instance_dispatch_table(instance)->DestroyInstance(instance, pAllocator);
    destroy_instance_dispatch_table(key);
    if (sLockInitialized == 1) {
        loader_platform_thread_delete_mutex(&sLock);
        sLockInitialized = 0;
    }
}


VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    printf("------------,function = %s,line = %d",__FUNCTION__,__LINE__);
    // Get the function pointer
    VkLayerDeviceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    assert(chain_info->u.pLayerInfo != 0);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice) fpGetInstanceProcAddr(NULL, "vkCreateDevice");
    if (fpCreateDevice == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Call the function and create the dispatch table
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    VkResult result = fpCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) return result;
    initDeviceTable(*pDevice, fpGetDeviceProcAddr);
    layer_init_device_dispatch_table(*pDevice, &sDeviceDispatchTable, fpGetDeviceProcAddr);

    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    printf("------------,function = %s,line = %d",__FUNCTION__,__LINE__);
    sDeviceDispatchTable.DestroyDevice(device, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateAndroidSurfaceKHR(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR *pCreateInfo,
                                               const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
    assert(instance);
    VkResult result = VK_SUCCESS;
#if OFFSCREEN
    VkHeadlessSurfaceCreateInfo surface_create_info = {};
    surface_create_info.pNext = nullptr;
    surface_create_info.flags = 0;
    result = vkCreateHeadlessSurface(instance, &surface_create_info, pAllocator, pSurface);
    sHeadlessSurface = *pSurface;
#else
    VkLayerInstanceDispatchTable *pTable = instance_dispatch_table(instance);
    if (pTable->GetInstanceProcAddr == nullptr) {
        return VK_NOT_READY;
    }
    result = pTable->CreateAndroidSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
#endif
    printf("------------,function = %s,line = %d, result = %d",__FUNCTION__,__LINE__,result);
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator)
{
    printf("------------,function = %s,line = %d",__FUNCTION__,__LINE__);
    assert(instance);
#if OFFSCREEN
    vkDestroyHeadlessSurfaceKHR(instance, surface, pAllocator);
    return ;
#else
    VkLayerInstanceDispatchTable *pTable = instance_dispatch_table(instance);
    if (pTable->GetInstanceProcAddr == nullptr) {
        return ;
    }
    pTable->DestroySurfaceKHR(instance, surface, pAllocator);
#endif
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported)
{
    VkResult result = VK_SUCCESS;
#if OFFSCREEN
    *pSupported = VK_TRUE;
#else
    result = sInstanceDispatchTable.GetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, pSupported);
#endif
    printf("------------,function = %s,line = %d, result = %d",__FUNCTION__,__LINE__,result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats)
{
    VkResult result = VK_SUCCESS;
    assert(physicalDevice);
    assert(surface);
#if OFFSCREEN
    result = headless_surface_props::get_instance()->get_surface_formats(physicalDevice, surface, pSurfaceFormatCount,pSurfaceFormats);
#else
    result = sInstanceDispatchTable.GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
#endif
    printf("------------,function = %s,line = %d, result = %d",__FUNCTION__,__LINE__,result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
{
    VkResult result = VK_SUCCESS;
    assert(physicalDevice);
    assert(surface);
    assert(pSurfaceCapabilities);
#if OFFSCREEN
    result = headless_surface_props::get_instance()->get_surface_caps(physicalDevice, surface, pSurfaceCapabilities);
#else
    result = sInstanceDispatchTable.GetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pSurfaceCapabilities);
#endif
    printf("------------,function = %s,line = %d, result = %d",__FUNCTION__,__LINE__,result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes)
{
    VkResult result = VK_SUCCESS;
    assert(physicalDevice);
    assert(surface);
    assert(pPresentModeCount);
#if OFFSCREEN
    result = headless_surface_props::get_instance()->get_surface_present_modes(physicalDevice, surface,pPresentModeCount, pPresentModes);
#else
    result = sInstanceDispatchTable.GetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, pPresentModeCount, pPresentModes);
#endif
    printf("------------,function = %s,line = %d, result = %d",__FUNCTION__,__LINE__,result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL GetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes)
{
    VkResult result = VK_SUCCESS;
#if OFFSCREEN
    ;
#else
    result = sDeviceDispatchTable.GetDeviceGroupSurfacePresentModesKHR(device, surface, pModes);
#endif
    printf("------------,function = %s,line = %d, result = %d",__FUNCTION__,__LINE__,result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
                                                  const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain)
{
    if (sHeadlessSwapChain != nullptr) {
        return VK_SUCCESS;
    }
    VkResult result = VK_SUCCESS;
#if OFFSCREEN
    offscreen::swapchain_headless* pHeadlessSwapChain = new offscreen::swapchain_headless(pAllocator);
    sHeadlessSwapChain = pHeadlessSwapChain;
    const char *imagecount = android_getenv(imagecount_var);
    uint32_t count = (uint32_t)atoi(imagecount);
    if (count) {
        VkSwapchainCreateInfoKHR *temp = const_cast<VkSwapchainCreateInfoKHR *>(pCreateInfo);
        temp->minImageCount = count;
    }
    result = pHeadlessSwapChain->init(device, pCreateInfo);
    *pSwapchain = reinterpret_cast<VkSwapchainKHR>(pHeadlessSwapChain);
#else
    result = device_dispatch_table(device)->CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
#endif
    printf("------------,function = %s,line = %d, result = %d",__FUNCTION__,__LINE__,result);
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                               const VkAllocationCallbacks* pAllocator)
{
    printf("------------,function = %s,line = %d",__FUNCTION__,__LINE__);
#if OFFSCREEN
    if (sHeadlessSwapChain) {
        delete sHeadlessSwapChain;
        sHeadlessSwapChain = nullptr;
    }
#else
    device_dispatch_table(device)->DestroySwapchainKHR(device, swapchain, pAllocator);
#endif
}

VKAPI_ATTR VkResult VKAPI_CALL GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pCount,
                                                     VkImage *pSwapchainImages)
{
    assert(pCount != nullptr);
    assert(swapchain != VK_NULL_HANDLE);
    VkResult result = VK_SUCCESS;
#if OFFSCREEN
    offscreen::swapchain_headless *pHeadlessSwapChain = reinterpret_cast<offscreen::swapchain_headless *>(swapchain);
    result = pHeadlessSwapChain->get_swapchain_images(pCount, pSwapchainImages);
#else
    result = device_dispatch_table(device)->GetSwapchainImagesKHR(device, swapchain, pCount, pSwapchainImages);
#endif
    printf("------------,function = %s,line = %d, result = %d",__FUNCTION__,__LINE__,result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapc, uint64_t timeout,
                                                   VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex)
{
    assert(swapc != VK_NULL_HANDLE);
    assert(semaphore != VK_NULL_HANDLE || fence != VK_NULL_HANDLE);
    assert(pImageIndex != nullptr);
    VkResult result = VK_SUCCESS;
#if OFFSCREEN
    offscreen::swapchain_headless *pHeadlessSwapChain = reinterpret_cast<offscreen::swapchain_headless *>(swapc);
    result = pHeadlessSwapChain->acquire_next_image(timeout, semaphore, fence, pImageIndex);
#else
    result = device_dispatch_table(device)->AcquireNextImageKHR(device, swapc, timeout, semaphore, fence, pImageIndex);
#endif
    printf("------------,function = %s,line = %d, result = %d",__FUNCTION__,__LINE__,result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
{
    VkResult result = VK_SUCCESS;
    assert(queue != VK_NULL_HANDLE);
    assert(pPresentInfo != nullptr);
#if OFFSCREEN
    uint32_t resultMask = 0;

    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i)
    {
        offscreen::swapchain_headless *sc = reinterpret_cast<offscreen::swapchain_headless *>(pPresentInfo->pSwapchains[i]);
        assert(sc != nullptr);
        VkResult res = sc->queue_present(queue, pPresentInfo, pPresentInfo->pImageIndices[i]);
        if (pPresentInfo->pResults != nullptr)
        {
            pPresentInfo->pResults[i] = res;
        }
        if (res == VK_ERROR_DEVICE_LOST)
            resultMask |= (1u << 1);
        else if (res == VK_ERROR_SURFACE_LOST_KHR)
            resultMask |= (1u << 2);
        else if (res == VK_ERROR_OUT_OF_DATE_KHR)
            resultMask |= (1u << 3);
    }

    if (resultMask & (1u << 1))
        result = VK_ERROR_DEVICE_LOST;
    else if (resultMask & (1u << 2))
        result = VK_ERROR_SURFACE_LOST_KHR;
    else if (resultMask & (1u << 3))
        result = VK_ERROR_OUT_OF_DATE_KHR;
#else
    result = sDeviceDispatchTable.QueuePresentKHR(queue, pPresentInfo);
#endif
    printf("------------,function = %s,line = %d, result = %d",__FUNCTION__,__LINE__,result);
    sFrameNumber++;
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL GetRefreshCycleDurationGOOGLE(VkDevice device, VkSwapchainKHR swapchain, VkRefreshCycleDurationGOOGLE* pDisplayTimingProperties)
{
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetPastPresentationTimingGOOGLE(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pPresentationTimingCount, VkPastPresentationTimingGOOGLE* pPresentationTimings)
{
    return VK_SUCCESS;
}

static void createDeviceRegisterExtensions(const VkDeviceCreateInfo *pCreateInfo, VkDevice device)
{
    //uint32_t i;
    VkLayerDispatchTable *pDisp = &sDeviceDispatchTable;
    PFN_vkGetDeviceProcAddr gpa = pDisp->GetDeviceProcAddr;
    pDisp->CreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)gpa(device, "vkCreateSwapchainKHR");
    pDisp->DestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)gpa(device, "vkDestroySwapchainKHR");
    pDisp->GetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)gpa(device, "vkGetSwapchainImagesKHR");
    pDisp->AcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)gpa(device, "vkAcquireNextImageKHR");
    pDisp->QueuePresentKHR = (PFN_vkQueuePresentKHR)gpa(device, "vkQueuePresentKHR");
}

static PFN_vkVoidFunction intercept_core_instance_command(const char *name)
{
    static const struct {
        const char *name;
        PFN_vkVoidFunction proc;
    } core_instance_commands[] = {
        {"vkGetInstanceProcAddr", reinterpret_cast<PFN_vkVoidFunction>(GetInstanceProcAddr)},
        {"vkCreateInstance", reinterpret_cast<PFN_vkVoidFunction>(CreateInstance)},
        {"vkDestroyInstance", reinterpret_cast<PFN_vkVoidFunction>(DestroyInstance)},
        {"vkCreateDevice", reinterpret_cast<PFN_vkVoidFunction>(CreateDevice)},
        {"vkCreateAndroidSurfaceKHR", reinterpret_cast<PFN_vkVoidFunction>(CreateAndroidSurfaceKHR)},
        {"vkDestroySurfaceKHR", reinterpret_cast<PFN_vkVoidFunction>(DestroySurfaceKHR)},
        {"vkEnumerateInstanceLayerProperties", reinterpret_cast<PFN_vkVoidFunction>(EnumerateInstanceLayerProperties)},
        {"vkEnumerateDeviceLayerProperties", reinterpret_cast<PFN_vkVoidFunction>(EnumerateDeviceLayerProperties)},
        {"vkEnumerateInstanceExtensionProperties", reinterpret_cast<PFN_vkVoidFunction>(EnumerateInstanceExtensionProperties)},
        {"vkEnumerateDeviceExtensionProperties", reinterpret_cast<PFN_vkVoidFunction>(EnumerateDeviceExtensionProperties)},
        {"vkGetPhysicalDeviceSurfaceSupportKHR", reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceSurfaceSupportKHR)},
        {"vkGetPhysicalDeviceSurfaceFormatsKHR", reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceSurfaceFormatsKHR)},
        {"vkGetPhysicalDeviceSurfaceCapabilitiesKHR", reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceSurfaceCapabilitiesKHR)},
        {"vkGetPhysicalDeviceSurfacePresentModesKHR", reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceSurfacePresentModesKHR)},
    };

    for (size_t i = 0; i < ARRAY_SIZE(core_instance_commands); i++) {
        if (!strcmp(core_instance_commands[i].name, name)) {
            return core_instance_commands[i].proc;
        }
    }

    return nullptr;
}

static PFN_vkVoidFunction intercept_core_device_command(const char *name)
{
    static const struct {
        const char *name;
        PFN_vkVoidFunction proc;
    } core_device_commands[] = {
        {"vkGetDeviceProcAddr", reinterpret_cast<PFN_vkVoidFunction>(GetDeviceProcAddr)},
        {"vkDestroyDevice", reinterpret_cast<PFN_vkVoidFunction>(DestroyDevice)},
        {"vkGetDeviceGroupSurfacePresentModesKHR", reinterpret_cast<PFN_vkVoidFunction>(GetDeviceGroupSurfacePresentModesKHR)},
        {"vkGetRefreshCycleDurationGOOGLE", reinterpret_cast<PFN_vkVoidFunction>(GetRefreshCycleDurationGOOGLE)},
        {"vkGetPastPresentationTimingGOOGLE", reinterpret_cast<PFN_vkVoidFunction>(GetPastPresentationTimingGOOGLE)},
    };

    for (size_t i = 0; i < ARRAY_SIZE(core_device_commands); i++) {
        if (!strcmp(core_device_commands[i].name, name)) return core_device_commands[i].proc;
    }

    return nullptr;
}

static PFN_vkVoidFunction intercept_khr_swapchain_command(const char *name, VkDevice dev)
{
    static const struct {
        const char *name;
        PFN_vkVoidFunction proc;
    } khr_swapchain_commands[] = {
        {"vkCreateSwapchainKHR", reinterpret_cast<PFN_vkVoidFunction>(CreateSwapchainKHR)},
        {"vkDestroySwapchainKHR", reinterpret_cast<PFN_vkVoidFunction>(DestroySwapchainKHR)},
        {"vkGetSwapchainImagesKHR", reinterpret_cast<PFN_vkVoidFunction>(GetSwapchainImagesKHR)},
        {"vkAcquireNextImageKHR", reinterpret_cast<PFN_vkVoidFunction>(AcquireNextImageKHR)},
        {"vkQueuePresentKHR", reinterpret_cast<PFN_vkVoidFunction>(QueuePresentKHR)},
    };

    for (size_t i = 0; i < ARRAY_SIZE(khr_swapchain_commands); i++) {
        if (!strcmp(khr_swapchain_commands[i].name, name)) return khr_swapchain_commands[i].proc;
    }

    return nullptr;
}

/********************************offscreenrender layer interface function***********************/
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t *pCount,VkLayerProperties *pProperties)
{
    return EnumerateInstanceLayerProperties(pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t *pCount,
                                                                                VkLayerProperties *pProperties)
{
    assert(physicalDevice == VK_NULL_HANDLE);
    return EnumerateDeviceLayerProperties(VK_NULL_HANDLE, pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pCount,
                                                                                      VkExtensionProperties *pProperties)
{
    printf("------------,function = %s,line = %d,pLayerName = %s",__FUNCTION__,__LINE__,pLayerName);
    return EnumerateInstanceExtensionProperties(pLayerName, pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                                                                    const char *pLayerName, uint32_t *pCount,
                                                                                    VkExtensionProperties *pProperties)
{
    printf("------------,function = %s,line = %d,pLayerName = %s",__FUNCTION__,__LINE__,pLayerName);
    assert(physicalDevice == VK_NULL_HANDLE);
    return EnumerateDeviceExtensionProperties(VK_NULL_HANDLE, pLayerName, pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev, const char *funcName)
{
    printf("------------,function = %s,line = %d,funcName = %s",__FUNCTION__,__LINE__,funcName);
    return GetDeviceProcAddr(dev, funcName);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *funcName)
{
    printf("------------,function = %s,line = %d,funcName = %s",__FUNCTION__,__LINE__,funcName);
    return GetInstanceProcAddr(instance, funcName);
}

#endif
