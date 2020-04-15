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

#include <vulkan/vk_icd.h>

#define CSTD_UNUSED(x) ((void)(x))
#define NELEMS(s) (sizeof(s) / sizeof((s)[0]))
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define VULKAN_API extern "C" __attribute__((visibility("default")))

typedef struct VkHeadlessSurfaceCreateInfo
{
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
} VkHeadlessSurfaceCreateInfo;

class headless_surface_props
{
public:
    static headless_surface_props *get_instance()
    {
        static headless_surface_props instance;
        return &instance;
    }
    VkResult get_surface_caps(VkPhysicalDevice physical_device, VkSurfaceKHR surface,VkSurfaceCapabilitiesKHR *pSurfaceCapabilities);
    VkResult get_surface_formats(VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t *surfaceFormatCount,VkSurfaceFormatKHR *surfaceFormats);
    VkResult get_surface_present_modes(VkPhysicalDevice physical_device, VkSurfaceKHR surface,uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes);
};

VkResult vkCreateHeadlessSurface(VkInstance instance, const VkHeadlessSurfaceCreateInfo *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
void vkDestroyHeadlessSurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator);
