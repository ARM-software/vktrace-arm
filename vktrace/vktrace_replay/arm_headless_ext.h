/*
 * (C) COPYRIGHT 2015-2019 ARM Limited
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
 */

#pragma once

#include <vulkan/vulkan.h>

#if MALI_INCLUDE_ARM_HEADLESS_SURFACE
#include <vulkan/vk_icd.h>

#define VK_USE_PLATFORM_HEADLESS_ARM 1
#ifdef VK_USE_PLATFORM_HEADLESS_ARM
#define VK_ARMX_headless_surface 1
#define VK_ARMX_HEADLESS_SURFACE_SPEC_VERSION 0
#define VK_ARMX_HEADLESS_SURFACE_EXTENSION_NAME "VK_ARMX_headless_surface"

/* This ensures that we are not touching random surface in our extension */
#define VK_ICD_WSI_PLATFORM_HEADLESS (VkIcdWsiPlatform)(100200)
extern "C" {
typedef VkFlags VkHeadlessSurfaceCreateFlagsARM;

typedef struct VkHeadlessSurfaceCreateInfoARM
{
	VkStructureType sType;
	const void *pNext;
	VkHeadlessSurfaceCreateFlagsARM flags;
} VkHeadlessSurfaceCreateInfoARM;

/* A struct defined here, until the extension is public */
typedef struct
{
	VkIcdSurfaceBase base;
} VkIcdSurfaceARMHeadless;

typedef VkResult(VKAPI_PTR *PFN_vkCreateHeadlessSurfaceARM)(VkInstance instance,
                                                            const VkHeadlessSurfaceCreateInfoARM *pCreateInfo,
                                                            const VkAllocationCallbacks *pAllocator,
                                                            VkSurfaceKHR *pSurface);

#ifndef VK_NO_PROTOTYPES
VKAPI_ATTR VkResult VKAPI_CALL
vkCreateHeadlessSurfaceARM(VkInstance instance, const VkHeadlessSurfaceCreateInfoARM *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
#endif
}
#endif /* VK_USE_PLATFORM_HEADLESS_ARM */
#endif /* MALI_INCLUDE_ARM_HEADLESS_SURFACE */
