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
#include "offscreen_render.h"
#include "vulkan/vk_layer.h"
#include "headless_surface.h"

#if defined(__ANDROID__)

/**
 * @brief Implements vkCreateHeadlessSurfaceARM Vulkan entrypoint.
 */
VkResult vkCreateHeadlessSurface(VkInstance instance, const VkHeadlessSurfaceCreateInfo *pCreateInfo,
                                               const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
    CSTD_UNUSED(instance);
    CSTD_UNUSED(pCreateInfo);
    CSTD_UNUSED(pAllocator);

    VkIcdSurfaceHeadless *headless_surface = (VkIcdSurfaceHeadless *)malloc(sizeof(VkIcdSurfaceHeadless));
    assert(headless_surface != nullptr);

    headless_surface->base.platform = VK_ICD_WSI_PLATFORM_ANDROID;
    *pSurface = reinterpret_cast<VkSurfaceKHR>(headless_surface);
    return VK_SUCCESS;
}

/**
 * @brief Implements vkDestroySurfaceKHR Vulkan entrypoint for arm headless.
 */
void vkDestroyHeadlessSurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator)
{
    CSTD_UNUSED(instance);
    CSTD_UNUSED(pAllocator);
    VkIcdSurfaceHeadless *headless_surface = reinterpret_cast<VkIcdSurfaceHeadless *>(surface);
    free(headless_surface);
}


VkResult headless_surface_props::get_surface_caps(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                                  VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
    CSTD_UNUSED(surface);
    /* Image count limits */
    pSurfaceCapabilities->minImageCount = 1;
    /* There is no maximum theoretically speaking */
    pSurfaceCapabilities->maxImageCount = UINT32_MAX;

    /* Surface extents */
    pSurfaceCapabilities->currentExtent = { 0xffffffff, 0xffffffff };
    pSurfaceCapabilities->minImageExtent = { 1, 1 };
    /* Ask the device for max */
    VkPhysicalDeviceProperties dev_props;
    vkGetPhysicalDeviceProperties(physical_device, &dev_props);

    pSurfaceCapabilities->maxImageExtent = { dev_props.limits.maxImageDimension2D,
                                             dev_props.limits.maxImageDimension2D };
    pSurfaceCapabilities->maxImageArrayLayers = 1;

    /* Surface transforms */
    pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

    /* Composite alpha */
    pSurfaceCapabilities->supportedCompositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR | VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR |
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR);

    /* Image usage flags */
    pSurfaceCapabilities->supportedUsageFlags =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

    return VK_SUCCESS;
}

VkResult headless_surface_props::get_surface_formats(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                                     uint32_t *surfaceFormatCount, VkSurfaceFormatKHR *surfaceFormats)
{
    CSTD_UNUSED(surface);

    VkResult res = VK_SUCCESS;
    /* Construct a list of all formats supported by the driver - for color attachment */
    VkFormat formats[VK_FORMAT_RANGE_SIZE];
    uint32_t format_count = 0;
    for (int id = 0; id < VK_FORMAT_RANGE_SIZE; id++)
    {
        VkImageFormatProperties image_format_props;
        /* MT600-1499 For now, we need to use VK_IMAGE_USAGE_STORAGE_BIT to suppress AFBC */
        res = vkGetPhysicalDeviceImageFormatProperties(physical_device, static_cast<VkFormat>(id), VK_IMAGE_TYPE_2D,
                                                        VK_IMAGE_TILING_OPTIMAL,
                                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                                                        VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, &image_format_props);

        if (res != VK_ERROR_FORMAT_NOT_SUPPORTED)
        {
            formats[format_count] = static_cast<VkFormat>(id);
            format_count++;
        }
    }
    assert(format_count > 0);
    assert(surfaceFormatCount != nullptr);
    res = VK_SUCCESS;
    if (nullptr == surfaceFormats)
    {
        *surfaceFormatCount = format_count;
    }
    else
    {
        if (format_count > *surfaceFormatCount)
        {
            res = VK_INCOMPLETE;
        }

        *surfaceFormatCount = MIN(*surfaceFormatCount, format_count);
        for (uint32_t i = 0; i < *surfaceFormatCount; ++i)
        {
            surfaceFormats[i].format = formats[i];
            surfaceFormats[i].colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        }
    }

    return res;
}

VkResult headless_surface_props::get_surface_present_modes(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                                           uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes)
{
    CSTD_UNUSED(physical_device);
    CSTD_UNUSED(surface);

    VkResult res = VK_SUCCESS;
    static const VkPresentModeKHR modes[] = { VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR };

    CSTD_UNUSED(surface);

    assert(pPresentModeCount != nullptr);

    if (nullptr == pPresentModes)
    {
        *pPresentModeCount = NELEMS(modes);
    }
    else
    {
        if (NELEMS(modes) > *pPresentModeCount)
        {
            res = VK_INCOMPLETE;
        }
        *pPresentModeCount = MIN(*pPresentModeCount, NELEMS(modes));
        for (uint32_t i = 0; i < *pPresentModeCount; ++i)
        {
            pPresentModes[i] = modes[i];
        }
    }

    return res;
}

#endif