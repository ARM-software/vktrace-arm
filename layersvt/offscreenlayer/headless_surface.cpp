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

#define ADRENO_VENDOR_ID 20803

#if defined(__ANDROID__)

extern VkPhysicalDeviceProperties gPhyDevProp;

static const VkFormat g_potential_cb_fmt[] = {
    VK_FORMAT_R8G8B8_UNORM,
    VK_FORMAT_R8G8B8_SNORM,
    VK_FORMAT_R8G8B8_USCALED,
    VK_FORMAT_R8G8B8_SSCALED,
    VK_FORMAT_R8G8B8_UINT,
    VK_FORMAT_R8G8B8_SINT,
    VK_FORMAT_R8G8B8_SRGB,
    VK_FORMAT_B8G8R8_UNORM,
    VK_FORMAT_B8G8R8_SNORM,
    VK_FORMAT_B8G8R8_USCALED,
    VK_FORMAT_B8G8R8_SSCALED,
    VK_FORMAT_B8G8R8_UINT,
    VK_FORMAT_B8G8R8_SINT,
    VK_FORMAT_B8G8R8_SRGB,
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R8G8B8A8_SNORM,
    VK_FORMAT_R8G8B8A8_USCALED,
    VK_FORMAT_R8G8B8A8_SSCALED,
    VK_FORMAT_R8G8B8A8_UINT,
    VK_FORMAT_R8G8B8A8_SINT,
    VK_FORMAT_R8G8B8A8_SRGB,
    VK_FORMAT_B8G8R8A8_UNORM,
    VK_FORMAT_B8G8R8A8_SNORM,
    VK_FORMAT_B8G8R8A8_USCALED,
    VK_FORMAT_B8G8R8A8_SSCALED,
    VK_FORMAT_B8G8R8A8_UINT,
    VK_FORMAT_B8G8R8A8_SINT,
    VK_FORMAT_B8G8R8A8_SRGB,
    VK_FORMAT_A8B8G8R8_UNORM_PACK32,
    VK_FORMAT_A8B8G8R8_SNORM_PACK32,
    VK_FORMAT_A8B8G8R8_USCALED_PACK32,
    VK_FORMAT_A8B8G8R8_SSCALED_PACK32,
    VK_FORMAT_A8B8G8R8_UINT_PACK32,
    VK_FORMAT_A8B8G8R8_SINT_PACK32,
    VK_FORMAT_A8B8G8R8_SRGB_PACK32,
    VK_FORMAT_A2R10G10B10_UNORM_PACK32,
    VK_FORMAT_A2R10G10B10_SNORM_PACK32,
    VK_FORMAT_A2R10G10B10_USCALED_PACK32,
    VK_FORMAT_A2R10G10B10_SSCALED_PACK32,
    VK_FORMAT_A2R10G10B10_UINT_PACK32,
    VK_FORMAT_A2R10G10B10_SINT_PACK32,
    VK_FORMAT_A2B10G10R10_UNORM_PACK32,
    VK_FORMAT_A2B10G10R10_SNORM_PACK32,
    VK_FORMAT_A2B10G10R10_USCALED_PACK32,
    VK_FORMAT_A2B10G10R10_SSCALED_PACK32,
    VK_FORMAT_A2B10G10R10_UINT_PACK32,
    VK_FORMAT_A2B10G10R10_SINT_PACK32,
    VK_FORMAT_R16G16B16_UNORM,
    VK_FORMAT_R16G16B16_SNORM,
    VK_FORMAT_R16G16B16_USCALED,
    VK_FORMAT_R16G16B16_SSCALED,
    VK_FORMAT_R16G16B16_UINT,
    VK_FORMAT_R16G16B16_SINT,
    VK_FORMAT_R16G16B16_SFLOAT,
    VK_FORMAT_R16G16B16A16_UNORM,
    VK_FORMAT_R16G16B16A16_SNORM,
    VK_FORMAT_R16G16B16A16_USCALED,
    VK_FORMAT_R16G16B16A16_SSCALED,
    VK_FORMAT_R16G16B16A16_UINT,
    VK_FORMAT_R16G16B16A16_SINT,
    VK_FORMAT_R16G16B16A16_SFLOAT,
};

static const uint32_t g_potential_cb_fmt_count = sizeof(g_potential_cb_fmt) / sizeof(VkFormat);

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

    if (gPhyDevProp.vendorID == ADRENO_VENDOR_ID)
    {
        /* Image usage flags */
        pSurfaceCapabilities->supportedUsageFlags =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    }
    else
    {
        /* Image usage flags */
        pSurfaceCapabilities->supportedUsageFlags =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    }

    return VK_SUCCESS;
}

VkResult headless_surface_props::get_surface_formats(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                                     uint32_t *surfaceFormatCount, VkSurfaceFormatKHR *surfaceFormats)
{
    CSTD_UNUSED(surface);

    VkResult res = VK_SUCCESS;
    /* Construct a list of all formats supported by the driver - for color attachment */
    VkFormat formats[g_potential_cb_fmt_count];
    uint32_t format_count = 0;
    for (int idx = 0; idx < g_potential_cb_fmt_count; idx++)
    {
        VkImageFormatProperties image_format_props;
        VkImageUsageFlags image_usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; /* MT600-1499 For now, we need to use VK_IMAGE_USAGE_STORAGE_BIT to suppress AFBC */
        if (gPhyDevProp.vendorID == ADRENO_VENDOR_ID)
        {
            image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        res = vkGetPhysicalDeviceImageFormatProperties(physical_device, g_potential_cb_fmt[idx], VK_IMAGE_TYPE_2D,
                                                        VK_IMAGE_TILING_OPTIMAL,
                                                        image_usage,
                                                        VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, &image_format_props);

        if (res != VK_ERROR_FORMAT_NOT_SUPPORTED)
        {
            formats[format_count] = g_potential_cb_fmt[idx];
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