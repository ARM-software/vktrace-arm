/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2019-2021 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include "vk_loader_platform.h"
#include "vulkan/vk_layer.h"
#include "vk_layer_config.h"
#include "vk_layer_table.h"
#include "vk_layer_extension_utils.h"
#include "vk_layer_utils.h"
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#define OFFSCREEN 1
#define PRINT_LOG 0

#if defined(__ANDROID__) && (PRINT_LOG)
#define printf(...) __android_log_print(ANDROID_LOG_DEBUG, "OFFSCREEN: ", __VA_ARGS__);
#else
#define printf(...)
#endif


VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          flags,
    VkImageFormatProperties                     *pImageFormatProperties);
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkWaitForFences(
    VkDevice                                    device,
    uint32_t                                    fenceCount,
    const                                       VkFence* pFences,
    VkBool32                                    waitAll,
    uint64_t                                    timeout);
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkQueueWaitIdle(
    VkQueue                                     queue);
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkQueueSubmit(
    VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     fence);
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkResetFences(
    VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences);
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkCreateImage(
    VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage);
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkBindImageMemory(
    VkDevice                                    device,
    VkImage                                     image,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset);
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkCreateFence(
    VkDevice                                    device,
    const VkFenceCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence);
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL  vkAllocateMemory(
    VkDevice                                    device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMemory);
VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties                  *pProperties);
VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice                                    device,
    VkFence                                     fence,
    const VkAllocationCallbacks*                pAllocator);
VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyImage(
    VkDevice                                    device,
    VkImage                                     image,
    const VkAllocationCallbacks*                pAllocator);
VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    const VkAllocationCallbacks*                pAllocator);
VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    VkMemoryRequirements*                       pMemoryRequirements);
VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(
    VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue);