/*
 *
 * Copyright (C) 2015-2019 Valve Corporation
 * Copyright (C) 2015-2019 LunarG, Inc.
 * Copyright (C) 2019-2023 ARM Limited.
 * All Rights Reserved
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
 *
 * Author: Peter Lohrmann <peterl@valvesoftware.com>
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 * Author: Mark Lobodzinski <mark@lunarg.com>
 * Author: Tobin Ehlis <tobin@lunarg.com>
 * Author: David Pinedo <david@lunarg.com>
 */

#include <algorithm>

#include "vulkan/vulkan.h"
#include "vkreplay_vkreplay.h"
#include "vkreplay.h"
#include "vkreplay_settings.h"
#include "vkreplay_main.h"
#include "vktrace_vk_vk_packets.h"
#include "vk_enum_string_helper.h"
#include "vktrace_vk_packet_id.h"
#include "vktrace_trace_packet_utils.h"
#include "vkreplay_vk_objmapper.h"
#include "vk_struct_member.h"
#if defined(ARM_LINUX_64)
#include "vkreplay_dmabuffer.h"
#endif

#if defined(_WIN32) || defined(_WIN64)
#define strcasecmp _stricmp  // Used for argument parsing
#endif

using namespace std;
#include "vktrace_pageguard_memorycopy.h"

#if defined(PLATFORM_LINUX) && !defined(ANDROID)

namespace {
    enum HeadlessExtensionChoice {
        VK_NONE,
        VK_EXT,
        VK_ARMX
    };
}

typedef struct VkImportAndroidHardwareBufferInfoANDROID {
    VkStructureType            sType;
    const void*                pNext;
    void*                      buffer;
} VkImportAndroidHardwareBufferInfoANDROID;

typedef struct AHardwareBuffer_Desc {
    uint32_t    width;      // width in pixels
    uint32_t    height;     // height in pixels
    uint32_t    layers;     // number of images
    uint32_t    format;     // One of AHARDWAREBUFFER_FORMAT_*
    uint64_t    usage;      // Combination of AHARDWAREBUFFER_USAGE_*
    uint32_t    stride;     // Stride in pixels, ignored for AHardwareBuffer_allocate()
    uint32_t    rfu0;       // Initialize to zero, reserved for future use
    uint64_t    rfu1;       // Initialize to zero, reserved for future use
} __attribute__ ((packed)) AHardwareBuffer_Desc;

typedef enum AHardwareBufferFormat {
    AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM = 1,
    AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM = 2,
    AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM = 3,
    AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM = 4,
    AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT = 0x16,
    AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM = 0x2b,
    AHARDWAREBUFFER_FORMAT_D16_UNORM = 0x30,
    AHARDWAREBUFFER_FORMAT_D24_UNORM = 0x31,
    AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT = 0x32,
    AHARDWAREBUFFER_FORMAT_D32_FLOAT = 0x33,
    AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT = 0x34,
    AHARDWAREBUFFER_FORMAT_S8_UINT = 0x35,
    AHARDWAREBUFFER_FORMAT_BLOB = 0x21
} AHardwareBufferFormat;

static uint32_t getAHardwareBufBPP(uint32_t fmt) {
    uint32_t bpp = 1;
    switch(fmt) {
    case AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT:
        bpp = 8;
        break;
    case AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT:
        bpp = 5;
        break;
    case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
    case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
    case AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM:
    case AHARDWAREBUFFER_FORMAT_D32_FLOAT:
    case AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT:
        bpp = 4;
        break;
    case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:
    ///case AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420:
    case AHARDWAREBUFFER_FORMAT_D24_UNORM:
        bpp = 3;
        break;
    case AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM:
    case AHARDWAREBUFFER_FORMAT_D16_UNORM:
        bpp = 2;
        break;
    case AHARDWAREBUFFER_FORMAT_S8_UINT:
        bpp = 1;
        break;
    default:
        break;
    }
    return bpp;
}

#endif // #if defined(PLATFORM_LINUX) && !defined(ANDROID)

vkReplay* g_replay = nullptr;
int g_ruiFrames = 0;
bool g_hasAsApi = false;
bool timer_started = false;
static VkSurfaceKHR local_pSurface = VK_NULL_HANDLE;
std::unordered_map<VkDevice, deviceFeatureSupport> g_TraceDeviceToDeviceFeatures;
std::unordered_map<VkSwapchainKHR, uint32_t> g_TraceScToScImageCount;
std::unordered_map<std::string, std::string> g_TraceDeviceNameToReplayDeviceName;
std::string trace_device_name_list[] = {
    "Mali-G78",
    "Mali-G77"
};

#if VK_ANDROID_frame_boundary
VkInstance g_instance       = VK_NULL_HANDLE;
bool g_bCreateSurface       = false;
bool g_bCreateCommandPool   = false;
uint32_t g_scImgCount       = 3;
uint32_t g_windowWidth      = 1920;
uint32_t g_windowHeight     = 1080;

std::vector<VkImage> scImageVec;
std::vector<VkImageView> scImageViewVec;
std::unordered_map<VkFramebuffer, VkSwapchainKHR> replayFbToSc;

VkSurfaceKHR g_traceSurface     = (VkSurfaceKHR)0xFFFF0000;
VkSwapchainKHR g_traceSc        = (VkSwapchainKHR)0xFFFF0000;

VkFence g_fence         = VK_NULL_HANDLE;
VkDevice g_replayDevice = VK_NULL_HANDLE;
VkQueue g_queue         = VK_NULL_HANDLE;

VkSemaphore g_semaphore = VK_NULL_HANDLE;

VkCommandPool g_commandPool     = VK_NULL_HANDLE;
VkCommandBuffer g_commandBuffer = VK_NULL_HANDLE;
struct imageInfo {
    VkImage image;
    VkDevice device;
    VkExtent3D extent;
};

std::vector<imageInfo> androidhardwareBufferImages;

#endif
vkreplayer_settings replaySettings = {
                                        .pTraceFilePath = NULL,
                                        .numLoops = 1,
                                        .loopStartFrame = 0,
                                        .loopEndFrame = UINT_MAX,
                                        .compatibilityMode = true,
                                        .exitOnAnyError = false,
                                        .screenshotList = NULL,
                                        .screenshotColorFormat = NULL,
                                        .screenshotPrefix = NULL,
                                        .verbosity = NULL,
                                        .displayServer = NULL,
                                        .preloadTraceFile = TRUE,
                                        .enablePortabilityTable = TRUE,
                                        .vsyncOff = FALSE,
                                        .headless = FALSE,
                                        .selfManageMemAllocation = FALSE,
                                        .forceSingleWindow = FALSE,
                                        .forceDisableAF = FALSE,
                                        .memoryPercentage = 90,
                                        .premapping = FALSE,
                                        .enablePipelineCache = FALSE,
                                        .pipelineCachePath = NULL,
                                        .forceSyncImgIdx = FALSE,
                                        .disableRQAndRTPCaptureReplay = 0,
                                        .specialPatternConfig = 0,
                                        .forceRayQuery = FALSE,
                                        .triggerScript = UINT_MAX,
                                        .pScriptPath = NULL,
                                        .perfMeasuringMode = 0,
                                        .printCurrentPacketIndex = 0,
                                        .enableSyncValidation = FALSE,
                                        .overrideCreateDeviceFeatures = FALSE,
                                        .swapChainMinImageCount = 1,
                                        .instrumentationDelay = 0,
                                        .preloadChunkSize = 200,
                                        .skipGetFenceStatus = 0,
                                        .skipFenceRanges = NULL,
                                        .finishBeforeSwap = FALSE,
                                        .forceVariableRateShading = NULL,
                                        .enableVirtualSwapchain = FALSE,
                                        .enableVscPerfMode = FALSE,
                                        .forceUseFilter = UINT_MAX,
                                        .scCompressFlag = UINT_MAX,
                                        .scCompressRate = 0,
                                        .imgCompressFlag = UINT_MAX,
                                        .imgCompressRate = 0,
                                        .convertAndroidFrameBoundary = FALSE,
                                        .fDevBuild2HostBuild = FALSE,
                                        .useTraceSurfaceTransformFlagBit = FALSE,
                                     };

namespace vktrace_replay {
bool timerStarted()
{
    return timer_started;
}

uint64_t getStartFrame()
{
    return g_pReplaySettings->loopStartFrame == UINT_MAX ? 0 : g_pReplaySettings->loopStartFrame;
}

uint64_t getEndFrame()
{
    return g_pReplaySettings->loopEndFrame;
}
}

DEVBUILD_TO_HOSTBUILD_STATE g_devBuild2HostBuild_state = DEVBUILD_TO_HOSTBUILD_NONE;

vkReplay::vkReplay(vkreplayer_settings *pReplaySettings, vktrace_trace_file_header *pFileHeader,
                   vktrace_replay::ReplayDisplayImp *display)
    : m_objMapper(pReplaySettings->premapping)
    , initialized_screenshot_list("")
    , m_pipelinecache_accessor(std::make_shared<vktrace_replay::PipelineCacheAccessor>()) {
    g_pReplaySettings = pReplaySettings;
    m_pDSDump = NULL;
    m_pCBDump = NULL;
    m_display = display;
    m_ASCaptureReplaySupport = NO_VALUE;

#if defined(PLATFORM_LINUX) && !defined(ANDROID)
    m_headlessExtensionChoice = HeadlessExtensionChoice::VK_NONE;
    if (pReplaySettings->displayServer) {
        if (strcasecmp(pReplaySettings->displayServer, "xcb") == 0) {
            m_displayServer = VK_DISPLAY_XCB;
        } else if (strcasecmp(pReplaySettings->displayServer, "wayland") == 0) {
            m_displayServer = VK_DISPLAY_WAYLAND;
        } else {
            m_displayServer = VK_DISPLAY_NONE;
        }
    }
#endif

    //    m_pVktraceSnapshotPrint = NULL;
    m_objMapper.m_adjustForGPU = false;

    m_frameNumber = 0;
    g_ruiFrames = 0;
    m_pFileHeader = pFileHeader;
    m_pGpuinfo = (struct_gpuinfo *)(pFileHeader + 1);
    m_platformMatch = -1;
    g_replay = this;

    memset(m_replay_pipelinecache_uuid, 0, VK_UUID_SIZE);
    if (g_pReplaySettings->enablePipelineCache) {
        if (NULL == g_pReplaySettings->pipelineCachePath) {
            const std::string &default_path = vktrace_replay::GetDefaultPipelineCacheRootPath();
            g_pReplaySettings->pipelineCachePath = VKTRACE_NEW_ARRAY(char, default_path.size() + 1);
            g_pReplaySettings->pipelineCachePath[default_path.size()] = 0;
            strcpy(g_pReplaySettings->pipelineCachePath, default_path.data());
        }
        m_pipelinecache_accessor->SetPipelineCacheRootPath(g_pReplaySettings->pipelineCachePath);
    }
    rtHandler = new RayTracingPipelineHandlerVer1;

    if (g_pReplaySettings->fDevBuild2HostBuild == TRUE) {
        g_devBuild2HostBuild_state = DEVBUILD_TO_HOSTBUILD_REQUESTED;
    }
}

std::vector<uintptr_t> portabilityTablePackets;
FileLike *traceFile;

void vkReplay::destroyObjects(const VkDevice &device) {
    // Make sure no gpu job is running before quit vkreplay
    m_vkDeviceFuncs.DeviceWaitIdle(device);

    // Destroy all objects created from the device before destroy device.
    // Reference:
    // https://vulkan.lunarg.com/doc/view/1.0.37.0/linux/vkspec.chunked/ch02s03.html#fundamentals-objectmodel-lifetime
    // QueryPool
    for (auto subobj = m_objMapper.m_querypools.begin(); subobj != m_objMapper.m_querypools.end(); subobj++) {
        if (replayQueryPoolToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyQueryPool(device, subobj->second, NULL);
        }
    }

    // Event
    for (auto subobj = m_objMapper.m_events.begin(); subobj != m_objMapper.m_events.end(); subobj++) {
        if (replayEventToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyEvent(device, subobj->second, NULL);
        }
    }

    // Fence
    for (auto subobj = m_objMapper.m_fences.begin(); subobj != m_objMapper.m_fences.end(); subobj++) {
        if (replayFenceToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyFence(device, subobj->second, NULL);
        }
    }

    // Semaphore
    for (auto subobj = m_objMapper.m_semaphores.begin(); subobj != m_objMapper.m_semaphores.end(); subobj++) {
        if (replaySemaphoreToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroySemaphore(device, subobj->second, NULL);
        }
    }

    // Framebuffer
    for (auto subobj = m_objMapper.m_framebuffers.begin(); subobj != m_objMapper.m_framebuffers.end(); subobj++) {
        if (replayFramebufferToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyFramebuffer(device, subobj->second, NULL);
        }
    }

    // DescriptorPool
    for (auto subobj = m_objMapper.m_descriptorpools.begin(); subobj != m_objMapper.m_descriptorpools.end(); subobj++) {
        if (replayDescriptorPoolToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyDescriptorPool(device, subobj->second, NULL);
        }
    }

    // Pipeline
    for (auto subobj = m_objMapper.m_pipelines.begin(); subobj != m_objMapper.m_pipelines.end(); subobj++) {
        if (replayPipelineToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyPipeline(device, subobj->second, NULL);
        }
    }

    // PipelineCache
    for (auto subobj = m_objMapper.m_pipelinecaches.begin(); subobj != m_objMapper.m_pipelinecaches.end(); subobj++) {
        if (replayPipelineCacheToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyPipelineCache(device, subobj->second, NULL);
        }
    }

    // ShaderModule
    for (auto subobj = m_objMapper.m_shadermodules.begin(); subobj != m_objMapper.m_shadermodules.end(); subobj++) {
        if (replayShaderModuleToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyShaderModule(device, subobj->second, NULL);
        }
    }

    // RenderPass
    for (auto subobj = m_objMapper.m_renderpasss.begin(); subobj != m_objMapper.m_renderpasss.end(); subobj++) {
        if (replayRenderPassToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyRenderPass(device, subobj->second, NULL);
        }
    }

    // PipelineLayout
    for (auto subobj = m_objMapper.m_pipelinelayouts.begin(); subobj != m_objMapper.m_pipelinelayouts.end(); subobj++) {
        if (replayPipelineLayoutToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyPipelineLayout(device, subobj->second, NULL);
        }
    }

    // DescriptorSetLayout
    for (auto subobj = m_objMapper.m_descriptorsetlayouts.begin(); subobj != m_objMapper.m_descriptorsetlayouts.end();
         subobj++) {
        if (replayDescriptorSetLayoutToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyDescriptorSetLayout(device, subobj->second, NULL);
        }
    }

    // Sampler
    for (auto subobj = m_objMapper.m_samplers.begin(); subobj != m_objMapper.m_samplers.end(); subobj++) {
        if (replaySamplerToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroySampler(device, subobj->second, NULL);
        }
    }

    // Buffer
    for (auto subobj = m_objMapper.m_buffers.begin(); subobj != m_objMapper.m_buffers.end(); subobj++) {
        if (replayBufferToDevice[subobj->second.replayBuffer] == device) {
            m_vkDeviceFuncs.DestroyBuffer(device, subobj->second.replayBuffer, NULL);
        }
    }

    // BufferView
    for (auto subobj = m_objMapper.m_bufferviews.begin(); subobj != m_objMapper.m_bufferviews.end(); subobj++) {
        if (replayBufferViewToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyBufferView(device, subobj->second, NULL);
        }
    }

    // Image
    for (auto subobj = m_objMapper.m_images.begin(); subobj != m_objMapper.m_images.end(); subobj++) {
        if (replayImageToDevice[subobj->second.replayImage] == device) {
            if (virtualImageToVirtualMemory.find(subobj->second.replayImage) != virtualImageToVirtualMemory.end()) {
                continue;
            }
            // Only destroy non-swapchain image
            if (replaySwapchainImageToDevice.find(subobj->second.replayImage) == replaySwapchainImageToDevice.end() ||
                replaySwapchainImageToDevice[subobj->second.replayImage] != device) {
                m_vkDeviceFuncs.DestroyImage(device, subobj->second.replayImage, NULL);
                if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() &&
                    replayOptimalImageToDeviceMemory.find(subobj->second.replayImage) !=
                        replayOptimalImageToDeviceMemory.end()) {
                    m_vkDeviceFuncs.FreeMemory(device, replayOptimalImageToDeviceMemory[subobj->second.replayImage], NULL);
                    replayOptimalImageToDeviceMemory.erase(subobj->second.replayImage);
                }
            }
        }
    }

    // ImageView
    for (auto subobj = m_objMapper.m_imageviews.begin(); subobj != m_objMapper.m_imageviews.end(); subobj++) {
        if (replayImageViewToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyImageView(device, subobj->second, NULL);
        }
    }

    // DeviceMemory
    if (g_pReplaySettings->premapping) {
        for (auto subobj = m_objMapper.m_indirect_devicememorys.begin(); subobj != m_objMapper.m_indirect_devicememorys.end(); subobj++) {
            if (replayDeviceMemoryToDevice[subobj->second->replayDeviceMemory] == device) {
                m_vkDeviceFuncs.FreeMemory(device, subobj->second->replayDeviceMemory, NULL);
            }
        }
    }
    else {
        for (auto subobj = m_objMapper.m_devicememorys.begin(); subobj != m_objMapper.m_devicememorys.end(); subobj++) {
            if (replayDeviceMemoryToDevice[subobj->second.replayDeviceMemory] == device) {
                m_vkDeviceFuncs.FreeMemory(device, subobj->second.replayDeviceMemory, NULL);
            }
        }
    }

    // SwapchainKHR
    for (auto subobj = m_objMapper.m_swapchainkhrs.begin(); subobj != m_objMapper.m_swapchainkhrs.end(); subobj++) {
        if (replaySwapchainKHRToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroySwapchainKHR(device, subobj->second, NULL);
        }
        auto it0 = traceSwapchainToReplayImages.find(subobj->first);
        if (it0 != traceSwapchainToReplayImages.end()) {
            traceSwapchainToReplayImages.erase(it0);
        }
        auto it1 = traceSwapchainToVirtualImageCopyRegion.find(subobj->first);
        if (it1 != traceSwapchainToVirtualImageCopyRegion.end()) {
            traceSwapchainToVirtualImageCopyRegion.erase(it1);
        }
        auto it5 = traceSwapchainToImages.find(subobj->first);
        if (it5 != traceSwapchainToImages.end()) {
            for (uint32_t i = 0; i < it5->second.size(); i++) {
                deleteVirtualObject(device, it5->second.at(i));
            }
            traceSwapchainToImages.erase(it5);
        }
        auto it6 = traceSwapchainToCreateInfo.find(subobj->first);
        if (it6 != traceSwapchainToCreateInfo.end()) {
            if (it6->second.queueFamilyIndexCount && it6->second.pQueueFamilyIndices != nullptr) {
                free((void*)it6->second.pQueueFamilyIndices);
            }
            traceSwapchainToCreateInfo.erase(it6);
        }
    }

    // CommandPool
    for (auto subobj = m_objMapper.m_commandpools.begin(); subobj != m_objMapper.m_commandpools.end(); subobj++) {
        if (replayCommandPoolToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyCommandPool(device, subobj->second, NULL);
        }
    }

    //AS
    for (auto subobj = m_objMapper.m_accelerationstructurekhrs.begin(); subobj != m_objMapper.m_accelerationstructurekhrs.end(); subobj++) {
        if (replayAccelerationStructureKHRToDevice[subobj->second] == device) {
            m_vkDeviceFuncs.DestroyAccelerationStructureKHR(device, subobj->second, NULL);
        }
    }

    // added by Android Frame Boundary
    if(replaySettings.convertAndroidFrameBoundary) {
        m_vkDeviceFuncs.DestroyFence(g_replayDevice, g_fence, NULL);
        m_vkDeviceFuncs.FreeCommandBuffers(g_replayDevice, g_commandPool, 1, &g_commandBuffer);
        m_vkDeviceFuncs.DestroyCommandPool(g_replayDevice, g_commandPool, nullptr);
        g_fence         = VK_NULL_HANDLE;
        g_traceSurface  = VK_NULL_HANDLE; // created by manually_replay_vkCreateAndroidSurfaceKHR
        g_traceSc       = VK_NULL_HANDLE; // created by manually_replay_vkCreateSwapchainKHR
        g_commandPool   = VK_NULL_HANDLE;
        g_commandBuffer = VK_NULL_HANDLE;
    }
    m_vkDeviceFuncs.DestroyDevice(device, NULL);
}

vkReplay::~vkReplay() {
    for (auto subobj = traceQueueFamilyProperties.begin(); subobj != traceQueueFamilyProperties.end(); subobj++) {
        free(subobj->second.queueFamilyProperties);
    }
    traceQueueFamilyProperties.clear();
    for (auto subobj = replayQueueFamilyProperties.begin(); subobj != replayQueueFamilyProperties.end(); subobj++) {
        free(subobj->second.queueFamilyProperties);
    }
    replayQueueFamilyProperties.clear();
    g_replay = nullptr;

    if (g_pReplaySettings->premapping) {
        for (auto obj = m_objMapper.m_indirect_devices.begin(); obj != m_objMapper.m_indirect_devices.end(); obj++) {
            if (*obj->second != VK_NULL_HANDLE)
                destroyObjects(*obj->second);
        }
    }
    else {
        for (auto obj = m_objMapper.m_devices.begin(); obj != m_objMapper.m_devices.end(); obj++) {
            destroyObjects(obj->second);
        }
    }

    for (auto obj = m_objMapper.m_instances.begin(); obj != m_objMapper.m_instances.end(); obj++) {
        m_vkFuncs.DestroyInstance(obj->second, NULL);
    }

    // free host memory
    for (auto& e : replayASToCopyAddress) {
        free(e.second);
    }

#if defined(ARM_LINUX_64)
    // close the duplicated DMA Buffer fds incase they haven't been imported twice
    for (int fd : openedDmaBufferFd) {
        close(fd);
    }
#endif

    delete m_display;
    delete rtHandler;
    vktrace_platform_close_library(m_libHandle);
}

int vkReplay::init(vktrace_replay::ReplayDisplay &disp) {
    int err;
#if defined(PLATFORM_LINUX)
    void *handle = dlopen("libvulkan.so", RTLD_LAZY);
    if (handle == NULL) {
        handle = dlopen("libvulkan.so.1", RTLD_LAZY);
    }
#else
    HMODULE handle = LoadLibrary("vulkan-1.dll");
#endif

    if (handle == NULL) {
        vktrace_LogError("Failed to open vulkan library.");
        return -1;
    }
    init_funcs(handle);
#if VK_ANDROID_frame_boundary
    m_vkDeviceFuncs_tmp.FrameBoundaryANDROID = (PFN_vkFrameBoundaryANDROID)(vktrace_platform_get_library_entrypoint(handle, "vkFrameBoundaryANDROID"));
#endif
    disp.set_implementation(m_display);
    if ((err = m_display->init(disp.get_gpu())) != 0) {
        vktrace_LogError("Failed to init vulkan display.");
        return err;
    }

#if defined(PLATFORM_LINUX) && !defined(ANDROID)
    if (m_displayServer == VK_DISPLAY_NONE) {
        vkDisplay *pDisp = (vkDisplay *)m_display;
        if (g_pReplaySettings->headless == TRUE) {
            if ((err = pDisp->init_headless_wsi(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME)) == 0) {
                m_headlessExtensionChoice = HeadlessExtensionChoice::VK_EXT;
            } else {
                if ((err = pDisp->init_headless_wsi(VK_ARMX_HEADLESS_SURFACE_EXTENSION_NAME)) == 0) {
                    m_headlessExtensionChoice = HeadlessExtensionChoice::VK_ARMX;
                } else {
                    vktrace_LogError("Failed to init vulkan headless WSI extension.");
                    return err;
                }
            }
        } else {
            if ((err = pDisp->init_disp_wsi(&m_vkFuncs)) != 0) {
                vktrace_LogError("Failed to init vulkan display WSI extension. Maybe not use it.");
                if (!g_pReplaySettings->exitOnAnyError) {
                    return err;
                }
            }
        }
    }
#endif
    if ((err = m_display->create_window(disp.get_width(), disp.get_height())) != 0) {
        vktrace_LogError("Failed to create Window");
        return err;
    }

    m_replay_endianess = get_endianess();
    m_replay_ptrsize = sizeof(void *);
    m_replay_arch = get_arch();
    m_replay_os = get_os();
    // We save a value for m_replay_gpu and m_replay_drv_vers later when we replay vkGetPhysicalDeviceProperites
    m_replay_gpu = 0;
    m_replay_drv_vers = 0;

    // 32bit/64bit trace file is not supported by 64bit/32bit vkreplay
    if (m_replay_ptrsize != m_pFileHeader->ptrsize) {
        vktrace_LogError("%llu-bit trace file is not supported by %llu-bit vkreplay.", m_pFileHeader->ptrsize * 8,
                         m_replay_ptrsize * 8);
        return -1;
    }

    return 0;
}

vktrace_replay::VKTRACE_REPLAY_RESULT vkReplay::handle_replay_errors(const char *entrypointName, const VkResult resCall,
                                                                     const VkResult resTrace,
                                                                     const vktrace_replay::VKTRACE_REPLAY_RESULT resIn) {
    vktrace_replay::VKTRACE_REPLAY_RESULT res = resIn;
    if (resCall == VK_ERROR_DEVICE_LOST) {
        vktrace_LogError("API call %s returned VK_ERROR_DEVICE_LOST. vkreplay cannot continue, exiting.", entrypointName);
        exit(1);
    }
    // Success Codes:
    // * VK_SUCCESS Command successfully completed
    // * VK_NOT_READY A fence or query has not yet completed
    // * VK_TIMEOUT A wait operation has not completed in the specified time
    // * VK_EVENT_SET An event is signaled
    // * VK_EVENT_RESET An event is unsignaled
    // * VK_INCOMPLETE A return array was too small for the result
    // * VK_SUBOPTIMAL_KHR A swapchain no longer matches the surface properties exactly, but can still be used to present to the
    // surface successfully.
    if (resCall != VK_SUCCESS && resCall != VK_NOT_READY && resCall != VK_TIMEOUT && resCall != VK_EVENT_SET &&
        resCall != VK_EVENT_RESET && resCall != VK_INCOMPLETE && resCall != VK_SUBOPTIMAL_KHR) {
        if (resCall != resTrace) {
            res = vktrace_replay::VKTRACE_REPLAY_BAD_RETURN;
            vktrace_LogError("Return value %s from API call (%s) does not match return value from trace file %s.",
                            string_VkResult((VkResult)resCall), entrypointName, string_VkResult((VkResult)resTrace));
        }
    }
    return res;
}
void vkReplay::push_validation_msg(VkFlags msgFlags, VkDebugReportObjectTypeEXT objType, uint64_t srcObjectHandle, size_t location,
                                   int32_t msgCode, const char *pLayerPrefix, const char *pMsg, const void *pUserData) {
    struct ValidationMsg msgObj;
    msgObj.msgFlags = msgFlags;
    msgObj.objType = objType;
    msgObj.srcObjectHandle = srcObjectHandle;
    msgObj.location = location;
    strncpy(msgObj.layerPrefix, pLayerPrefix, 256);
    msgObj.layerPrefix[255] = '\0';
    msgObj.msgCode = msgCode;
    strncpy(msgObj.msg, pMsg, 256);
    msgObj.msg[255] = '\0';
    msgObj.pUserData = (void *)pUserData;
    m_validationMsgs.push_back(msgObj);
}

vktrace_replay::VKTRACE_REPLAY_RESULT vkReplay::pop_validation_msgs() {
    if (m_validationMsgs.size() == 0) return vktrace_replay::VKTRACE_REPLAY_SUCCESS;
    m_validationMsgs.clear();
    return vktrace_replay::VKTRACE_REPLAY_VALIDATION_ERROR;
}

int vkReplay::dump_validation_data() {
    if (m_pDSDump && m_pCBDump) {
        m_pDSDump((char *)"pipeline_dump.dot");
        m_pCBDump((char *)"cb_dump.dot");
    }
    //    if (m_pVktraceSnapshotPrint != NULL)
    //    {
    //        m_pVktraceSnapshotPrint();
    //    }
    return 0;
}

#if VK_ANDROID_frame_boundary
VkResult vkReplay:: copyImage(VkImage srcImage, VkImage dstImage, VkDevice device) {
    VkResult result = VK_ERROR_VALIDATION_FAILED_EXT;

    // begin command buffer
    VkCommandBufferBeginInfo commandBufferBeginInfo = {};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = m_vkDeviceFuncs.BeginCommandBuffer(g_commandBuffer, &commandBufferBeginInfo);
    if(result != VK_SUCCESS) {
        vktrace_LogError("Failed to begin command buffer when saving image data");
    }

    // create barriers
    std::vector<VkImageMemoryBarrier> image_barriers(2);
    VkImageMemoryBarrier& image_barrier_src = image_barriers.at(0);
    VkImageMemoryBarrier& image_barrier_dst = image_barriers.at(1);

    image_barrier_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier_src.pNext = nullptr;
    image_barrier_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    image_barrier_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_barrier_src.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // TBD
    image_barrier_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // TBD
    image_barrier_src.image = srcImage;
    image_barrier_src.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    image_barrier_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier_dst.pNext = nullptr;
    image_barrier_dst.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier_dst.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    image_barrier_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // TBD
    image_barrier_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // TBD
    image_barrier_dst.image = dstImage;
    image_barrier_dst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    m_vkDeviceFuncs.CmdPipelineBarrier(g_commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image_barrier_src);
    m_vkDeviceFuncs.CmdPipelineBarrier(g_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image_barrier_dst);

    uint32_t width = g_windowWidth, height = g_windowHeight;

    // copy image
    VkImageCopy imageCopy = {};
    imageCopy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    imageCopy.srcOffset = { 0, 0, 0 };
    imageCopy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    imageCopy.dstOffset = { 0, 0, 0 };
    imageCopy.extent.width = width;
    imageCopy.extent.height = height;
    imageCopy.extent.depth = 1;

    m_vkDeviceFuncs.CmdCopyImage(g_commandBuffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);
    image_barrier_src.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_barrier_src.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    image_barrier_src.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier_src.newLayout = VK_IMAGE_LAYOUT_UNDEFINED; // TBD could also be VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR
    image_barrier_dst.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier_src.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier_dst.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier_dst.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // TBD could also be VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR

    m_vkDeviceFuncs.CmdPipelineBarrier(g_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, image_barriers.size(), image_barriers.data());
    result = m_vkDeviceFuncs.EndCommandBuffer(g_commandBuffer);
    if(result != VK_SUCCESS) {
        vktrace_LogError("Failed to end command buffer when saving image data");
    }

    // queue submit
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &g_commandBuffer;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &g_semaphore;
    VkPipelineStageFlags flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submitInfo.pWaitDstStageMask = &flags;
    result = m_vkDeviceFuncs.QueueSubmit(g_queue, 1, &submitInfo, g_fence);

    return result;
}

VkResult vkReplay::manually_replay_vkFrameBoundaryANDROID(packet_vkFrameBoundaryANDROID* pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkFrameBoundaryANDROID() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkSemaphore remappedSemaphore = m_objMapper.remap_semaphores(pPacket->semaphore);
    if (remappedSemaphore == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkFrameBoundaryANDROID() due to invalid remapped VkSemaphore.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkImage remappedImage = m_objMapper.remap_images(pPacket->image);
    if (remappedImage == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkFrameBoundaryANDROID() due to invalid remapped VkImage.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if(m_vkDeviceFuncs_tmp.FrameBoundaryANDROID == nullptr) {
        vktrace_LogDebug("vkFrameBoundaryANDROID() is skipped due to no support of this function!");
    }
    else {
        replayResult = m_vkDeviceFuncs_tmp.FrameBoundaryANDROID(remappedDevice, remappedSemaphore, remappedImage);
    }

    if(replaySettings.convertAndroidFrameBoundary) {
        uint32_t imgIndex = 0;

        if(!g_bCreateCommandPool) {
            // create command buffer pool
            VkCommandPoolCreateInfo commandPoolCreateInfo = {};
            commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            commandPoolCreateInfo.queueFamilyIndex = 0;
            replayResult = m_vkDeviceFuncs.CreateCommandPool(g_replayDevice, &commandPoolCreateInfo, nullptr, &g_commandPool);
            if(replayResult != VK_SUCCESS) {
                vktrace_LogError("Failed to create command pool for converting the Android frame boundary");
            }

            // allocate command buffer from pool
            VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
            commandBufferAllocateInfo.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandBufferAllocateInfo.commandPool   = g_commandPool;
            commandBufferAllocateInfo.level         = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandBufferAllocateInfo.commandBufferCount = 1;

            replayResult = m_vkDeviceFuncs.AllocateCommandBuffers(g_replayDevice, &commandBufferAllocateInfo, &g_commandBuffer);
            if(replayResult != VK_SUCCESS) {
                vktrace_LogError("Failed to allocate command buffer when converting the Android frame boundary");
            }
            g_bCreateCommandPool = true;
        }

        if (g_bCreateCommandPool && scImageVec.size()) {
            // only present those images that have the same size as the window size for now
            for(auto& info : androidhardwareBufferImages) {
                if(info.image == pPacket->image && info.extent.width == g_windowWidth && info.extent.height == g_windowHeight) {
                    VkSwapchainKHR g_mappedTraceSwapchain = m_objMapper.remap_swapchainkhrs(g_traceSc);
                    replayResult = m_vkDeviceFuncs.AcquireNextImageKHR(remappedDevice, g_mappedTraceSwapchain, UINT64_MAX, g_semaphore, g_fence, &imgIndex);
                    if(replayResult != VK_SUCCESS) {
                        vktrace_LogError("AcquireNextImageKHR failed when converting the Android frame boundary(%d)", replayResult);
                    }

                    replayResult = m_vkDeviceFuncs.WaitForFences(remappedDevice, 1, &g_fence, VK_TRUE, UINT64_MAX);
                    if(replayResult != VK_SUCCESS) {
                        vktrace_LogError("WaitForFences failed when converting the Android frame boundary(%d)", replayResult);
                    }

                    replayResult = copyImage(remappedImage, scImageVec[imgIndex], remappedDevice);
                    if(replayResult != VK_SUCCESS) {
                        vktrace_LogError("copyImage failed when converting the Android frame boundary(%d)", replayResult);
                    }

                    VkSwapchainKHR sc[] = {g_mappedTraceSwapchain};
                    VkPresentInfoKHR presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr, 0, nullptr, 1, sc, &imgIndex};
                    replayResult = m_vkDeviceFuncs.QueuePresentKHR(g_queue, &presentInfo);
                    if(replayResult != VK_SUCCESS) {
                        vktrace_LogError("QueuePresentKHR failed when converting the Android frame boundary(%d)", replayResult);
                    }

                    replayResult = m_vkDeviceFuncs.WaitForFences(remappedDevice, 1, &g_fence, VK_TRUE, UINT64_MAX);
                    if(replayResult != VK_SUCCESS) {
                        vktrace_LogError("After QueuePresentKHR, WaitForFences failed when converting the Android frame boundary(%d)", replayResult);
                    }

                    replayResult = m_vkDeviceFuncs.ResetFences(remappedDevice, 1, &g_fence);
                    if(replayResult != VK_SUCCESS) {
                        vktrace_LogError("ResetFences failed when converting the Android frame boundary(%d)", replayResult);
                    }
                }
            }
        }
    }

    return replayResult;
}
#endif

VkResult vkReplay::manually_replay_vkCreateInstance(packet_vkCreateInstance *pPacket) {
    if (m_display->m_initedVK && initialized_screenshot_list == g_pReplaySettings->screenshotList) {
        return VK_SUCCESS;
    }
    initialized_screenshot_list = g_pReplaySettings->screenshotList;

    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkInstanceCreateInfo *pCreateInfo;
    char **ppEnabledLayerNames = NULL, **saved_ppLayers = NULL;
    uint32_t savedLayerCount = 0;
    VkInstance inst;

    const char strScreenShot[] = "VK_LAYER_LUNARG_screenshot";
    pCreateInfo = (VkInstanceCreateInfo *)pPacket->pCreateInfo;

    savedLayerCount = pCreateInfo->enabledLayerCount;
    saved_ppLayers = (char **)pCreateInfo->ppEnabledLayerNames;

    if (g_pReplaySettings->compatibilityMode && savedLayerCount) {
        vktrace_LogVerbose("Ignore all recorded instance layers in compatibility mode");
        pCreateInfo->enabledLayerCount = 0;
        pCreateInfo->ppEnabledLayerNames = NULL;
    }

    if (g_pReplaySettings->screenshotList != NULL) {
        // enable screenshot layer if it is available and not already in list
        bool found_ss = false;
        for (uint32_t i = 0; i < pCreateInfo->enabledLayerCount; i++) {
            if (!strcmp(pCreateInfo->ppEnabledLayerNames[i], strScreenShot)) {
                found_ss = true;
                break;
            }
        }
        if (!found_ss) {
            uint32_t count;

            // query to find if ScreenShot layer is available
            vkEnumerateInstanceLayerProperties(&count, NULL);
            VkLayerProperties *props = (VkLayerProperties *)vktrace_malloc(count * sizeof(VkLayerProperties));
            if (props && count > 0) vkEnumerateInstanceLayerProperties(&count, props);
            for (uint32_t i = 0; i < count; i++) {
                if (!strcmp(props[i].layerName, strScreenShot)) {
                    found_ss = true;
                    break;
                }
            }
            if (found_ss) {
                // screenshot layer is available so enable it
                ppEnabledLayerNames = (char **)vktrace_malloc((pCreateInfo->enabledLayerCount + 1) * sizeof(char *));
                for (uint32_t i = 0; i < pCreateInfo->enabledLayerCount && ppEnabledLayerNames; i++) {
                    ppEnabledLayerNames[i] = (char *)pCreateInfo->ppEnabledLayerNames[i];
                }
                ppEnabledLayerNames[pCreateInfo->enabledLayerCount] = (char *)vktrace_malloc(strlen(strScreenShot) + 1);
                strcpy(ppEnabledLayerNames[pCreateInfo->enabledLayerCount++], strScreenShot);
                pCreateInfo->ppEnabledLayerNames = ppEnabledLayerNames;
            }
            vktrace_free(props);
        }
    }

    char **saved_ppExtensions = (char **)pCreateInfo->ppEnabledExtensionNames;
    uint32_t savedExtensionCount = pCreateInfo->enabledExtensionCount;
    vector<std::string> extension_names;
    vector<string> outlist;

#if defined(PLATFORM_LINUX)
#if !defined(ANDROID)
    // LINUX
    if (m_displayServer == VK_DISPLAY_XCB) {
        extension_names.push_back("VK_KHR_xcb_surface");
        outlist.push_back("VK_KHR_wayland_surface");
    } else if (m_displayServer == VK_DISPLAY_WAYLAND) {
        extension_names.push_back("VK_KHR_wayland_surface");
        // VK_KHR_wayland_surface requires VK_KHR_surface to be enabled
        if(find(extension_names.begin(), extension_names.end(), "VK_KHR_surface") == extension_names.end()) {
            extension_names.push_back("VK_KHR_surface");
        }
        outlist.push_back("VK_KHR_xcb_surface");
    } else if (m_displayServer == VK_DISPLAY_NONE) {
        if (g_pReplaySettings->headless == TRUE) {
            if (HeadlessExtensionChoice::VK_EXT == m_headlessExtensionChoice) {
                extension_names.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
            } else {
                // Fallback to arm headless extension for the legacy driver
                extension_names.push_back(VK_ARMX_HEADLESS_SURFACE_EXTENSION_NAME);
            }
        } else {
            extension_names.push_back("VK_KHR_display");
        }
        outlist.push_back("VK_KHR_xcb_surface");
        outlist.push_back("VK_KHR_wayland_surface");
    }
    outlist.push_back("VK_KHR_android_surface");
    outlist.push_back("VK_KHR_xlib_surface");
    outlist.push_back("VK_KHR_win32_surface");
#else
    // ANDROID
    extension_names.push_back("VK_KHR_android_surface");
    outlist.push_back("VK_KHR_win32_surface");
    outlist.push_back("VK_KHR_xlib_surface");
    outlist.push_back("VK_KHR_xcb_surface");
    outlist.push_back("VK_KHR_wayland_surface");
#endif
#else
    // WIN32
    extension_names.push_back("VK_KHR_win32_surface");
    outlist.push_back("VK_KHR_xlib_surface");
    outlist.push_back("VK_KHR_xcb_surface");
    outlist.push_back("VK_KHR_wayland_surface");
    outlist.push_back("VK_KHR_android_surface");
#endif

    // Get replayable extensions in compatibility mode
    uint32_t extensionCount = 0;
    VkExtensionProperties *extensions = NULL;
    if (g_pReplaySettings->compatibilityMode) {
        if (VK_SUCCESS != vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, NULL)) {
            vktrace_LogError("vkEnumerateInstanceExtensionProperties failed to get extension count!");
        } else {
            extensions = (VkExtensionProperties *)vktrace_malloc(sizeof(VkExtensionProperties) * extensionCount);
            if (VK_SUCCESS != vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions)) {
                vktrace_LogError("vkEnumerateInstanceExtensionProperties failed to get extension name!");
                vktrace_free(extensions);
                extensionCount = 0;
            }
        }
    }

    // Add any extensions that are both replayable and in the packet
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        if (find(outlist.begin(), outlist.end(), pCreateInfo->ppEnabledExtensionNames[i]) == outlist.end()) {
            if (find(extension_names.begin(), extension_names.end(), string(pCreateInfo->ppEnabledExtensionNames[i])) ==
                extension_names.end()) {
                bool foundExtension = false;
                for (uint32_t j = 0; j < extensionCount; j++) {
                    // Check extension is replayable
                    if (strcmp(extensions[j].extensionName, pCreateInfo->ppEnabledExtensionNames[i]) == 0) {
                        foundExtension = true;
                        break;
                    }
                }
                if (!g_pReplaySettings->compatibilityMode || foundExtension) {
                    extension_names.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
                } else {
                    vktrace_LogVerbose("Instance extension filtered out: %s", pCreateInfo->ppEnabledExtensionNames[i]);
                }
            }
        }
    }

    // Disable debug report extension calls when debug report extension is not enabled/supported
    if (find(extension_names.begin(), extension_names.end(), "VK_EXT_debug_report") == extension_names.end()) {
        g_fpDbgMsgCallback = NULL;
    }

    if (extensions) {
        vktrace_free(extensions);
    }

    // vkGetPhysicalDeviceFeatures2KHR function need VK_KHR_get_physical_device_properties2 extension
    auto it = std::find(extension_names.begin(), extension_names.end(), VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    if (it == extension_names.end()) {
        uint32_t instanceExtCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtCount, nullptr);
        if (instanceExtCount > 0) {
            std::vector<VkExtensionProperties> extensionProps(instanceExtCount);
            if (vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtCount, (VkExtensionProperties*)&extensionProps.front()) == VK_SUCCESS) {
                for (VkExtensionProperties extension : extensionProps) {
                    if (strcmp(extension.extensionName, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) == 0) {
                        extension_names.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
                        break;
                    }
                }
            }
        }
    }

    vector<const char *> enable_extension_names;
    for (int i = 0; i < extension_names.size(); i++) {
        enable_extension_names.push_back(extension_names[i].c_str());
    }

    pCreateInfo->ppEnabledExtensionNames = enable_extension_names.data();
    pCreateInfo->enabledExtensionCount = (uint32_t)enable_extension_names.size();

    for (void **pTemp = (void**)&((pCreateInfo)); ((VkApplicationInfo*)(*pTemp))->pNext != NULL; ) {
        if (((VkApplicationInfo*)((VkApplicationInfo*)(*pTemp))->pNext)->sType == VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT ||
            ((VkApplicationInfo*)((VkApplicationInfo*)(*pTemp))->pNext)->sType == VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT) {
            ((VkApplicationInfo*)(*pTemp))->pNext = ((VkApplicationInfo*)((VkApplicationInfo*)((VkApplicationInfo*)(*pTemp))->pNext))->pNext;
        }
        else {
            pTemp = (void**)&(((VkApplicationInfo*)(*pTemp))->pNext);
        }
    }
    if (g_pReplaySettings->enableSyncValidation) {
        VkValidationFeatureEnableEXT enables[] = {VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
        VkValidationFeaturesEXT features = {};
        features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        features.pNext = pCreateInfo->pNext;
        features.enabledValidationFeatureCount = 1;
        features.pEnabledValidationFeatures = enables;

        pCreateInfo->pNext = &features;
    }
    // Mark the trace as OpenXR content if the second instance is created for adding necessary extensions.
    if (m_objMapper.m_instances.size() >= 1) {
        m_replayOpenXRContent = true;
#if defined(ANDROID)
        vktrace_LogVerbose(
            "Creation of the second instance is detected. VK_ANDROID_external_memory_android_hardware_buffer will be forced "
            "enabled");
#elif defined(ARM_LINUX_64)
        vktrace_LogVerbose("Creation of the second instance is detected. VK_KHR_external_memory_fd will be forced enabled");
#else
        m_replayOpenXRContent = false; // Platforms except android and arm_64 are unsupported
#endif
    }

    replayResult = vkCreateInstance(pPacket->pCreateInfo, NULL, &inst);

    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_instances_map(*(pPacket->pInstance), inst);
#if VK_ANDROID_frame_boundary
        g_instance = *(pPacket->pInstance);
#endif
        // Build instance dispatch table
        layer_init_instance_dispatch_table(inst, &m_vkFuncs, m_vkFuncs.GetInstanceProcAddr);
        // Not handled by codegen
        m_vkFuncs.CreateDevice = (PFN_vkCreateDevice)m_vkFuncs.GetInstanceProcAddr(inst, "vkCreateDevice");
#if !defined(ANDROID) && defined(PLATFORM_LINUX)
        if (g_pReplaySettings->headless == TRUE && HeadlessExtensionChoice::VK_ARMX == m_headlessExtensionChoice) {
            m_PFN_vkCreateHeadlessSurfaceARM =
                (PFN_vkCreateHeadlessSurfaceARM)m_vkFuncs.GetInstanceProcAddr(inst, "vkCreateHeadlessSurfaceARM");
        }
#endif
        m_instCount++;
    } else if (replayResult == VK_ERROR_LAYER_NOT_PRESENT) {
        vktrace_LogVerbose("vkCreateInstance failed with VK_ERROR_LAYER_NOT_PRESENT");
        vktrace_LogVerbose("List of requested layers:");
        for (uint32_t i = 0; i < pCreateInfo->enabledLayerCount; i++) {
            vktrace_LogVerbose("   %s", pCreateInfo->ppEnabledLayerNames[i]);
        }
    } else if (replayResult == VK_ERROR_EXTENSION_NOT_PRESENT) {
        vktrace_LogVerbose("vkCreateInstance failed with VK_ERROR_EXTENSION_NOT_PRESENT");
        vktrace_LogVerbose("List of requested extensions:");
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
            vktrace_LogVerbose("   %s", pCreateInfo->ppEnabledExtensionNames[i]);
        }
    }

    pCreateInfo->ppEnabledExtensionNames = saved_ppExtensions;
    pCreateInfo->enabledExtensionCount = savedExtensionCount;

    if (ppEnabledLayerNames) {
        // restore the packets CreateInfo struct
        vktrace_free(ppEnabledLayerNames[pCreateInfo->enabledLayerCount - 1]);
        vktrace_free(ppEnabledLayerNames);
    }
    if (ppEnabledLayerNames || (g_pReplaySettings->compatibilityMode && savedLayerCount)) {
        pCreateInfo->ppEnabledLayerNames = saved_ppLayers;
        pCreateInfo->enabledLayerCount = savedLayerCount;
    }
    // Do not skip vkCreateInstance to make sure VkInstance can be remapped successfully in later calls depending on the second or
    // later VkInstance.
    // m_display->m_initedVK = true;
    return replayResult;
}

// vkReplay::getReplayQueueFamilyIdx
// Translates the queue family idx on the trace device to a queue family index on the replay device
// If no translation is needed, *pIdx is left unchanged.
// If the translation cannot be done, an error is logged and *pIdx is left unchanged.

void vkReplay::getReplayQueueFamilyIdx(VkPhysicalDevice tracePhysicalDevice, VkPhysicalDevice replayPhysicalDevice,
                                       uint32_t *pIdx) {
    uint32_t newIdx;

    // Don't translate queue family index if the platform matches or we are not in compatibility mode
    if (platformMatch() || !g_pReplaySettings->compatibilityMode) {
        return;
    }
    if (*pIdx == VK_QUEUE_FAMILY_IGNORED) {
        return;
    }

    // If either the trace qf list or replay qf list is empty, fail
    if (traceQueueFamilyProperties.find(tracePhysicalDevice) == traceQueueFamilyProperties.end() ||
        replayQueueFamilyProperties.find(replayPhysicalDevice) == replayQueueFamilyProperties.end()) {
        goto fail;
    }
    if (min(traceQueueFamilyProperties[tracePhysicalDevice].count, replayQueueFamilyProperties[replayPhysicalDevice].count) == 0) {
        goto fail;
    }

    // If there is exactly one qf on the replay device, use it
    if (replayQueueFamilyProperties[replayPhysicalDevice].count == 1) {
        *pIdx = 0;
        return;
    }

    // If there is a replay qf that is a identical to the trace qf, use it.
    // Start the search with the trace qf idx.
    newIdx = *pIdx;
    for (uint32_t i = 0; i < replayQueueFamilyProperties[replayPhysicalDevice].count; i++) {
        if (traceQueueFamilyProperties[tracePhysicalDevice].queueFamilyProperties[*pIdx].queueFlags ==
            replayQueueFamilyProperties[replayPhysicalDevice].queueFamilyProperties[newIdx].queueFlags) {
            *pIdx = newIdx;
            return;
        }
        if (++newIdx >= replayQueueFamilyProperties[replayPhysicalDevice].count) newIdx = 0;
    }

    // If there is a replay qf that is a superset of the trace qf, us it.
    // Start the search with the trace qf idx.
    newIdx = *pIdx;
    for (uint32_t i = 0; i < replayQueueFamilyProperties[replayPhysicalDevice].count; i++) {
        if (traceQueueFamilyProperties[tracePhysicalDevice].queueFamilyProperties[*pIdx].queueFlags ==
            (traceQueueFamilyProperties[tracePhysicalDevice].queueFamilyProperties[*pIdx].queueFlags &
             replayQueueFamilyProperties[replayPhysicalDevice].queueFamilyProperties[newIdx].queueFlags)) {
            *pIdx = newIdx;
            return;
        }
        if (++newIdx >= replayQueueFamilyProperties[replayPhysicalDevice].count) newIdx = 0;
    }

    // If there is a replay qf that supports Graphics, Compute and Transfer, use it
    // If there is a replay qf that supports Graphics and Compute, use it
    // If there is a replay qf that supports Graphics, use it
    // Start each search with the trace qf idx.
    for (uint32_t j = 0; j < 3; j++) {
        uint32_t mask;
        if (j == 0)
            mask = (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT);
        else if (j == 1)
            mask = (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
        else
            // j == 2
            mask = (VK_QUEUE_GRAPHICS_BIT);
        newIdx = *pIdx;
        for (uint32_t i = 0; i < replayQueueFamilyProperties[replayPhysicalDevice].count; i++) {
            if ((replayQueueFamilyProperties[replayPhysicalDevice].queueFamilyProperties[newIdx].queueFlags & mask) == mask) {
                vktrace_LogWarning("Didn't find an exact match for queue family index, using index %d", i);
                *pIdx = newIdx;
                return;
            }
            if (++newIdx >= replayQueueFamilyProperties[replayPhysicalDevice].count) newIdx = 0;
        }
    }

fail:
    // Didn't find a match
    vktrace_LogError("Cannot determine replay device queue family index to use");
    return;
}

void vkReplay::getReplayQueueFamilyIdx(VkDevice traceDevice, VkDevice replayDevice, uint32_t *pReplayIdx) {
    VkPhysicalDevice tracePhysicalDevice;
    VkPhysicalDevice replayPhysicalDevice;

    if (tracePhysicalDevices.find(traceDevice) == tracePhysicalDevices.end() ||
        replayPhysicalDevices.find(replayDevice) == replayPhysicalDevices.end()) {
        vktrace_LogWarning("Cannot determine queue family index - has vkGetPhysicalDeviceQueueFamilyProperties been called?");
        return;
    }

    tracePhysicalDevice = tracePhysicalDevices[traceDevice];
    replayPhysicalDevice = replayPhysicalDevices[replayDevice];

    getReplayQueueFamilyIdx(tracePhysicalDevice, replayPhysicalDevice, pReplayIdx);
}

bool vkReplay::findImageFromOtherSwapchain(VkSwapchainKHR swapchain) {
    bool find = false;
    auto sc = traceSwapchainToImages.find(swapchain);
    if (sc != traceSwapchainToImages.end()) {
        std::vector<VkImage> &scImg = sc->second;
        for (auto& e : traceSwapchainToImages) {
            if (e.first == swapchain) {
                continue;
            } else {
                for (auto& img : scImg) {
                    auto it = std::find(e.second.begin(), e.second.end(), img);
                    if (it != e.second.end()) {
                        find = true;
                        break;
                    }
                }
            }
            if (find) {
                break;
            }
        }
    }
    return find;
}

#define CHECKDEVICEFEATURES(typeID, featuresType, featureNum)  \
case typeID: \
{   \
    VkBool32 *traceFeatures = (VkBool32*)((char*)pNext + offset);  \
    VkPhysicalDeviceFeatures2 df2 = {};  \
    df2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;  \
    featuresType df = {};  \
    df.sType = pNext->sType;  \
    df2.pNext = &df;  \
    if (m_vkFuncs.GetPhysicalDeviceFeatures2KHR != nullptr)  {\
        m_vkFuncs.GetPhysicalDeviceFeatures2KHR(physicalDevice, &df2);  \
    } else if (m_vkFuncs.GetPhysicalDeviceFeatures2 != nullptr) {  \
        m_vkFuncs.GetPhysicalDeviceFeatures2(physicalDevice, &df2);  \
    } else {  \
        vktrace_LogError("vkGetPhysicalDeviceFeatures2KHR & vkGetPhysicalDeviceFeatures2 function pointer are nullptr");  \
        return;  \
    }  \
    VkBool32 *deviceFeatures = (VkBool32*)((char*)&df + offset);  \
    if (typeID == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2) { \
        deviceFeatures = (VkBool32*)((char*)&df2 + offset);  \
    } \
    for (uint32_t i = 0; i < featureNum; i++) {  \
        if (traceFeatures[i] && !deviceFeatures[i]) {  \
            vktrace_LogError("Device feature (feature Type = %s, feature = %s, value = %d) in trace file does not match the physical replay device (value = %d)", string_VkStructureType(pNext->sType), getString##featuresType(i).c_str(), traceFeatures[i], deviceFeatures[i]);  \
            if (g_pReplaySettings->overrideCreateDeviceFeatures) { \
                vktrace_LogAlways("Disable device feature (feature Type = %s, feature = %s, value = %d) according to the user's demand.", string_VkStructureType(pNext->sType), getString##featuresType(i).c_str(), traceFeatures[i]);  \
                traceFeatures[i] = deviceFeatures[i]; \
            } \
        }  \
    }  \
}  \
break;

void vkReplay::checkDeviceExtendFeatures(const VkBaseOutStructure *pNext, VkPhysicalDevice physicalDevice) {
    int offset = offsetof(VkPhysicalDeviceVulkan11Features, storageBuffer16BitAccess);
    while(pNext != nullptr) {
        switch(pNext->sType) {
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, VkPhysicalDeviceFeatures2, 55)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, VkPhysicalDeviceVulkan11Features, 12)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ASTC_DECODE_FEATURES_EXT, VkPhysicalDeviceASTCDecodeFeaturesEXT, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES, VkPhysicalDeviceScalarBlockLayoutFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES, VkPhysicalDeviceMultiviewFeatures, 3)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, VkPhysicalDeviceVulkan12Features, 47)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES, VkPhysicalDeviceVariablePointersFeatures, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT, VkPhysicalDeviceShaderAtomicFloatFeaturesEXT, 12)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, VkPhysicalDeviceShaderAtomicInt64Features, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT, VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES, VkPhysicalDevice8BitStorageFeatures, 3)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES, VkPhysicalDevice16BitStorageFeatures, 4)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES, VkPhysicalDeviceShaderFloat16Int8Features, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR, VkPhysicalDeviceShaderClockFeaturesKHR, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES, VkPhysicalDeviceSamplerYcbcrConversionFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES, VkPhysicalDeviceProtectedMemoryFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_FEATURES_EXT, VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT, VkPhysicalDeviceConditionalRenderingFeaturesEXT, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES, VkPhysicalDeviceShaderDrawParametersFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, VkPhysicalDeviceDescriptorIndexingFeatures, 20)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT, VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT, VkPhysicalDeviceTransformFeedbackFeaturesEXT, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES, VkPhysicalDeviceVulkanMemoryModelFeatures, 3)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES_EXT, VkPhysicalDeviceInlineUniformBlockFeatures, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_FEATURES_EXT, VkPhysicalDeviceFragmentDensityMap2FeaturesEXT, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES, VkPhysicalDeviceUniformBufferStandardLayoutFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT, VkPhysicalDeviceDepthClipEnableFeaturesEXT, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES, VkPhysicalDeviceBufferDeviceAddressFeatures, 3)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES, VkPhysicalDeviceImagelessFramebufferFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_IMAGE_ARRAYS_FEATURES_EXT, VkPhysicalDeviceYcbcrImageArraysFeaturesEXT, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES, VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES, VkPhysicalDeviceHostQueryResetFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES, VkPhysicalDeviceTimelineSemaphoreFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT, VkPhysicalDeviceIndexTypeUint8FeaturesEXT, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT, VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES, VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR, VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT, VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT, VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES_EXT, VkPhysicalDeviceTextureCompressionASTCHDRFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT, VkPhysicalDeviceLineRasterizationFeaturesEXT, 6)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT, VkPhysicalDeviceSubgroupSizeControlFeatures, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, VkPhysicalDeviceAccelerationStructureFeaturesKHR, 5)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, VkPhysicalDeviceRayTracingPipelineFeaturesKHR, 5)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, VkPhysicalDeviceRayQueryFeaturesKHR, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT, VkPhysicalDeviceExtendedDynamicStateFeaturesEXT, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT, VkPhysicalDeviceExtendedDynamicState2FeaturesEXT, 3)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_MEMORY_REPORT_FEATURES_EXT, VkPhysicalDeviceDeviceMemoryReportFeaturesEXT, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES_EXT, VkPhysicalDeviceGlobalPriorityQueryFeaturesEXT, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES_EXT, VkPhysicalDevicePipelineCreationCacheControlFeatures, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES_KHR, VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeaturesKHR, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_FEATURES_KHR, VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES_EXT, VkPhysicalDeviceImageRobustnessFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES_KHR, VkPhysicalDeviceShaderTerminateInvocationFeatures, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT, VkPhysicalDeviceCustomBorderColorFeaturesEXT, 2)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT, VkPhysicalDeviceBorderColorSwizzleFeaturesEXT, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR, VkPhysicalDevicePortabilitySubsetFeaturesKHR, 15)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR, VkPhysicalDevicePerformanceQueryFeaturesKHR, 2)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT, VkPhysicalDevice4444FormatsFeaturesEXT, 2)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR, VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR, 4)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR, VkPhysicalDeviceSynchronization2FeaturesKHR, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT, VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR, VkPhysicalDeviceFragmentShadingRateFeaturesKHR, 3)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_2_PLANE_444_FORMATS_FEATURES_EXT, VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT, VkPhysicalDeviceColorWriteEnableFeaturesEXT, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT, VkPhysicalDeviceProvokingVertexFeaturesEXT, 2)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT, VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT, VkPhysicalDeviceMultiDrawFeaturesEXT, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, VkPhysicalDevicePresentIdFeaturesKHR, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR, VkPhysicalDevicePresentWaitFeaturesKHR, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES_KHR, VkPhysicalDeviceShaderIntegerDotProductFeaturesKHR, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RGBA10X6_FORMATS_FEATURES_EXT, VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES_KHR, VkPhysicalDeviceMaintenance4FeaturesKHR, 1)
            //CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR, VkPhysicalDeviceDynamicRenderingFeaturesKHR, 1)
            CHECKDEVICEFEATURES(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT, VkPhysicalDeviceFragmentDensityMapFeaturesEXT, 3)
            default:
            vktrace_LogWarning("Device Features id = %u", pNext->sType);
        }
        pNext = reinterpret_cast<const VkBaseOutStructure *>(pNext->pNext);
    }

}

void vkReplay::forceDisableCaptureReplayFeature() {
    if (replayDeviceToFeatureSupport.size() == 0 || g_TraceDeviceNameToReplayDeviceName.size() == 0) {
        return;
    }
    bool bForceDisable = false;
    auto it = g_TraceDeviceNameToReplayDeviceName.begin();
    if (!strcmp(it->first.c_str(), trace_device_name_list[0].c_str()) || !strcmp(it->first.c_str(), trace_device_name_list[1].c_str())) {
        bForceDisable = true;
    }
    if (it->first.find("Mali") == std::string::npos && it->second.find("Mali") != std::string::npos) {
        bForceDisable = true;
    }
    if (bForceDisable) {
        for (auto& e : replayDeviceToFeatureSupport) {
            e.second.accelerationStructureCaptureReplay = 0;
            e.second.bufferDeviceAddressCaptureReplay = 0;
            e.second.rayTracingPipelineShaderGroupHandleCaptureReplay = 0;
        }
    }
}

VkResult vkReplay::manually_replay_vkCreateDevice(packet_vkCreateDevice *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice device;
    VkPhysicalDevice remappedPhysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    VkDeviceCreateInfo *pCreateInfo;
    char **ppEnabledLayerNames = NULL, **saved_ppLayers = NULL;
    uint32_t savedLayerCount = 0;
    const char strScreenShot[] = "VK_LAYER_LUNARG_screenshot";

    if (remappedPhysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateDevice() due to invalid remapped VkPhysicalDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    pCreateInfo = (VkDeviceCreateInfo *)pPacket->pCreateInfo;

    savedLayerCount = pCreateInfo->enabledLayerCount;
    saved_ppLayers = (char **)pCreateInfo->ppEnabledLayerNames;

    if (g_pReplaySettings->compatibilityMode && savedLayerCount) {
        vktrace_LogVerbose("Ignore all recorded device layers in compatibility mode");
        pCreateInfo->enabledLayerCount = 0;
        pCreateInfo->ppEnabledLayerNames = NULL;
    }

    if (g_pReplaySettings->screenshotList != NULL) {
        // enable screenshot layer if it is available and not already in list
        bool found_ss = false;
        for (uint32_t i = 0; i < pCreateInfo->enabledLayerCount; i++) {
            if (!strcmp(pCreateInfo->ppEnabledLayerNames[i], strScreenShot)) {
                found_ss = true;
                break;
            }
        }
        if (!found_ss) {
            uint32_t count;

            // query to find if ScreenShot layer is available
            m_vkFuncs.EnumerateDeviceLayerProperties(remappedPhysicalDevice, &count, NULL);
            VkLayerProperties *props = (VkLayerProperties *)vktrace_malloc(count * sizeof(VkLayerProperties));
            if (props && count > 0) m_vkFuncs.EnumerateDeviceLayerProperties(remappedPhysicalDevice, &count, props);
            for (uint32_t i = 0; i < count; i++) {
                if (!strcmp(props[i].layerName, strScreenShot)) {
                    found_ss = true;
                    break;
                }
            }
            if (found_ss) {
                // screenshot layer is available so enable it
                ppEnabledLayerNames = (char **)vktrace_malloc((pCreateInfo->enabledLayerCount + 1) * sizeof(char *));
                for (uint32_t i = 0; i < pCreateInfo->enabledLayerCount && ppEnabledLayerNames; i++) {
                    ppEnabledLayerNames[i] = (char *)pCreateInfo->ppEnabledLayerNames[i];
                }
                ppEnabledLayerNames[pCreateInfo->enabledLayerCount] = (char *)vktrace_malloc(strlen(strScreenShot) + 1);
                strcpy(ppEnabledLayerNames[pCreateInfo->enabledLayerCount++], strScreenShot);
                pCreateInfo->ppEnabledLayerNames = ppEnabledLayerNames;
            }
            vktrace_free(props);
        }
    }

    // Convert all instances of queueFamilyIndex in structure
    if (pPacket->pCreateInfo->pQueueCreateInfos) {
        for (uint32_t i = 0; i < pPacket->pCreateInfo->queueCreateInfoCount; i++) {
            getReplayQueueFamilyIdx(pPacket->physicalDevice, remappedPhysicalDevice,
                                    (uint32_t *)&pPacket->pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex);
        }
    }

    char **saved_ppExtensions = (char **)pCreateInfo->ppEnabledExtensionNames;
    uint32_t savedExtensionCount = pCreateInfo->enabledExtensionCount;
    vector<std::string> extensionNames;

    // Get replayable extensions and features in compatibility mode
    uint32_t extensionCount = 0;
    VkExtensionProperties *extensions = NULL;
    if (g_pReplaySettings->compatibilityMode) {
        if (VK_SUCCESS != m_vkFuncs.EnumerateDeviceExtensionProperties(remappedPhysicalDevice, NULL, &extensionCount, NULL)) {
            vktrace_LogError("vkEnumerateDeviceExtensionProperties failed to get extension count!");
        } else {
            extensions = (VkExtensionProperties *)vktrace_malloc(sizeof(VkExtensionProperties) * extensionCount);
            if (VK_SUCCESS !=
                m_vkFuncs.EnumerateDeviceExtensionProperties(remappedPhysicalDevice, NULL, &extensionCount, extensions)) {
                vktrace_LogError("vkEnumerateDeviceExtensionProperties failed to get extension name!");
                vktrace_free(extensions);
                extensionCount = 0;
            }
        }
    }

    // Add any extensions that are both replayable and in the packet
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        if (find(extensionNames.begin(), extensionNames.end(), string(pCreateInfo->ppEnabledExtensionNames[i])) ==
            extensionNames.end()) {
            bool foundExtension = false;
            for (uint32_t j = 0; j < extensionCount; j++) {
                // Check extension is replayable
                if (strcmp(extensions[j].extensionName, pCreateInfo->ppEnabledExtensionNames[i]) == 0) {
                    foundExtension = true;
                    break;
                }
            }
            if (!g_pReplaySettings->compatibilityMode || foundExtension) {
                extensionNames.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
            } else {
                vktrace_LogVerbose("Device extension filtered out: %s", pCreateInfo->ppEnabledExtensionNames[i]);
            }
        }
    }

#if defined(ANDROID) || defined(ARM_LINUX_64)
    // Force necessary extensions enabled when replaying OpenXR content
    if (m_replayOpenXRContent) {
#if defined(ANDROID)
        const char *targetExtention = "VK_ANDROID_external_memory_android_hardware_buffer";
#elif defined(ARM_LINUX_64)
        const char *targetExtention = "VK_KHR_external_memory_fd";
#endif
        bool isExtSupported = false;
        if (std::find(extensionNames.begin(), extensionNames.end(), targetExtention) == extensionNames.end()) {
            for (uint32_t i = 0; i < extensionCount; i++) {
                if (strcmp(extensions[i].extensionName, targetExtention) == 0) {
                    isExtSupported = true;
                    break;
                }
            }
        }
        if (isExtSupported) {
            extensionNames.push_back(targetExtention);
        } else {
            vktrace_LogWarning(
                "%s is not supported. Null pointer dereference may occur for OpenXR contents during vkAllocateMemory.",
                targetExtention);
        }
    }
#endif
    if (g_pReplaySettings->forceUseFilter == 2 || g_pReplaySettings->forceUseFilter == 256+2) {
        bool foundExtension = false;
        for (uint32_t i = 0; i < extensionCount; i++) {
            if (strcmp(extensions[i].extensionName, "VK_EXT_filter_cubic") == 0) {
                foundExtension = true;
                break;
            }
        }
        if (!foundExtension) {
            vktrace_LogVerbose("Device extension filtered out: VK_EXT_filter_cubic");
        }
    }

    // Enable VK_KHR_fragment_shading_rate when FVRS is on
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR newPhysicalDeviceFSRFeature = {};
    if (g_pReplaySettings->forceVariableRateShading != nullptr) {
        // Check and read the parameter string first
        if (!checkFVRSParam()) return VK_ERROR_VALIDATION_FAILED_EXT;

        // Check VK_KHR_fragment_shading_rate is supported by the device
        bool foundVRSExt = false;
        for (uint32_t i = 0; i < extensionCount; i++) {
            if (strcmp(extensions[i].extensionName, "VK_KHR_fragment_shading_rate") == 0) {
                foundVRSExt = true;
                break;
            }
        }
        if (!foundVRSExt) {
            vktrace_LogError("VK_KHR_fragment_shading_rate is not supported by the replaying device.");
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        // Check whether VK_KHR_fragment_shading_rate is already enabled or not
        if (std::find(extensionNames.begin(), extensionNames.end(), "VK_KHR_fragment_shading_rate") == extensionNames.end()) {
            if (foundVRSExt) {
                extensionNames.push_back("VK_KHR_fragment_shading_rate");
            }
            // Manually add pnext struct
            newPhysicalDeviceFSRFeature = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR, NULL, 1, 0, 0};
            add_ext_struct((vulkan_struct_header *)pPacket->pCreateInfo, (vulkan_struct_header *)&newPhysicalDeviceFSRFeature);
        } else {
            VkPhysicalDeviceFragmentShadingRateFeaturesKHR *physicalDeviceFSRFeature =
                (VkPhysicalDeviceFragmentShadingRateFeaturesKHR *)find_ext_struct(
                    (vulkan_struct_header *)pPacket->pCreateInfo,
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR);
            if (!physicalDeviceFSRFeature) {
                vktrace_LogError(
                    "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR is missed in device create info.");
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }
            vktrace_LogWarning("VK_KHR_fragment_shading_rate is already enabled, overriding to enable pipeline fragment shading.");
            physicalDeviceFSRFeature->pipelineFragmentShadingRate = 1;
        }

        // Check extensions might conflict with FVRS
        vector<const char *> conflictExtensionNames{"VK_NV_shading_rate_image", "VK_EXT_fragment_density_map"};
        for (auto conflictExtension : conflictExtensionNames) {
            if (std::find(extensionNames.begin(), extensionNames.end(), conflictExtension) != extensionNames.end()) {
                vktrace_LogWarning("FVRS may conflict with enabled extension (%s).", conflictExtension);
            }
        }

        // Acquire whether the target shading rate is supported
        uint32_t fragmentShadingRateCount = 0;
        if (VK_SUCCESS !=
            m_vkFuncs.GetPhysicalDeviceFragmentShadingRatesKHR(remappedPhysicalDevice, &fragmentShadingRateCount, nullptr)) {
            vktrace_LogError("GetPhysicalDeviceFragmentShadingRatesKHR failed.");
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        } else {
            VkPhysicalDeviceFragmentShadingRateKHR fragmentShadingRates[fragmentShadingRateCount];
            m_vkFuncs.GetPhysicalDeviceFragmentShadingRatesKHR(remappedPhysicalDevice, &fragmentShadingRateCount,
                                                               &fragmentShadingRates[0]);
            vktrace_LogDebug("fragmentShadingRateCount = %d", fragmentShadingRateCount);
            bool foundFragmentSize = false;
            for (uint32_t i = 0; i < fragmentShadingRateCount; i++) {
                // SampleCount is ~0 if fragment size is {1, 1}
                vktrace_LogDebug("Supported fragmentShadingRate: sampleCount = %u, width = %u, height = %u.",
                                  fragmentShadingRates[i].sampleCounts, fragmentShadingRates[i].fragmentSize.width,
                                  fragmentShadingRates[i].fragmentSize.height);
                if (m_fragmentSize.width == fragmentShadingRates[i].fragmentSize.width &&
                    m_fragmentSize.height == fragmentShadingRates[i].fragmentSize.height) {
                    foundFragmentSize = true;
                    vktrace_LogDebug("Input fragment size found.");
                }
            }
            if (!foundFragmentSize) {
                vktrace_LogError("Input fragment size is not supported by the physical device.");
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }
        }
    }


    if (extensions) {
        vktrace_free(extensions);
    }
    if (g_pReplaySettings->forceUseFilter == 2 || g_pReplaySettings->forceUseFilter == 256+2) {
        if (std::find(extensionNames.begin(), extensionNames.end(), "VK_EXT_filter_cubic") == extensionNames.end()) {
            extensionNames.push_back("VK_EXT_filter_cubic");
        }
    }

    vector<const char *> enable_extension_names;
    for (int i = 0; i < extensionNames.size(); i++) {
        enable_extension_names.push_back(extensionNames[i].c_str());
    }

    if (enable_extension_names.size()) {
        pCreateInfo->ppEnabledExtensionNames = enable_extension_names.data();
        pCreateInfo->enabledExtensionCount = (uint32_t)enable_extension_names.size();
    }
    if (pPacket->pCreateInfo->pEnabledFeatures) {
        VkPhysicalDeviceFeatures physicalDeviceFeatures;
        m_vkFuncs.GetPhysicalDeviceFeatures(remappedPhysicalDevice, &physicalDeviceFeatures);
        VkBool32 *traceFeatures = (VkBool32 *)(pPacket->pCreateInfo->pEnabledFeatures);
        VkBool32 *deviceFeatures = (VkBool32 *)(&physicalDeviceFeatures);
        uint32_t numOfFeatures = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
        for (uint32_t i = 0; i < numOfFeatures; i++) {
            if (traceFeatures[i] && !deviceFeatures[i]) {
                vktrace_LogAlways("Device feature (%s = %d) in trace file does not match the physical replay device (%d)", GetPhysDevFeatureString(i), traceFeatures[i], deviceFeatures[i]);
                if (g_pReplaySettings->overrideCreateDeviceFeatures) {
                    vktrace_LogAlways("Disable device feature (%s = %d) according to the user's demand.", GetPhysDevFeatureString(i), traceFeatures[i]);
                    traceFeatures[i] = deviceFeatures[i];
                }
            }
        }
        changeDeviceFeature(traceFeatures,deviceFeatures,numOfFeatures);
    }
    const VkBaseOutStructure *pNext = reinterpret_cast<const VkBaseOutStructure *>(pPacket->pCreateInfo->pNext);
    checkDeviceExtendFeatures(pNext, remappedPhysicalDevice);

    replayResult = m_vkFuncs.CreateDevice(remappedPhysicalDevice, pPacket->pCreateInfo, NULL, &device);
    if (ppEnabledLayerNames) {
        // restore the packets CreateInfo struct
        vktrace_free(ppEnabledLayerNames[pCreateInfo->enabledLayerCount - 1]);
        vktrace_free(ppEnabledLayerNames);
    }
    if (ppEnabledLayerNames || (g_pReplaySettings->compatibilityMode && savedLayerCount)) {
        pCreateInfo->ppEnabledLayerNames = saved_ppLayers;
        pCreateInfo->enabledLayerCount = savedLayerCount;
    }
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_devices_map(*(pPacket->pDevice), device);
        tracePhysicalDevices[*(pPacket->pDevice)] = pPacket->physicalDevice;
        replayPhysicalDevices[device] = remappedPhysicalDevice;

        // Build device dispatch table
        layer_init_device_dispatch_table(device, &m_vkDeviceFuncs, m_vkDeviceFuncs.GetDeviceProcAddr);
#if VK_ANDROID_frame_boundary
    if(m_vkDeviceFuncs_tmp.FrameBoundaryANDROID == nullptr) {
        m_vkDeviceFuncs_tmp.FrameBoundaryANDROID = (PFN_vkFrameBoundaryANDROID)  m_vkDeviceFuncs.GetDeviceProcAddr(device, "vkFrameBoundaryANDROID");
    }
    if(m_vkDeviceFuncs_tmp.FrameBoundaryANDROID == nullptr) {
        vktrace_LogDebug("vkFrameBoundaryANDROID() is not supported on this device.");
    }
#endif
    } else if (replayResult == VK_ERROR_EXTENSION_NOT_PRESENT) {
        vktrace_LogVerbose("vkCreateDevice failed with VK_ERROR_EXTENSION_NOT_PRESENT");
        vktrace_LogVerbose("List of requested extensions:");
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
            vktrace_LogVerbose("   %s", pCreateInfo->ppEnabledExtensionNames[i]);
        }
    }

    if (extensionNames.size()) {
        pCreateInfo->ppEnabledExtensionNames = saved_ppExtensions;
        pCreateInfo->enabledExtensionCount = savedExtensionCount;
    }

    PFN_vkGetPhysicalDeviceFeatures2KHR func = m_vkFuncs.GetPhysicalDeviceFeatures2KHR;
    if (func == nullptr) {
        func = m_vkFuncs.GetPhysicalDeviceFeatures2;
    }
    PFN_vkGetPhysicalDeviceProperties2KHR funcProp = m_vkFuncs.GetPhysicalDeviceProperties2KHR;
    if (funcProp == nullptr) {
        funcProp = m_vkFuncs.GetPhysicalDeviceProperties2;
    }
    replayDeviceToFeatureSupport[device] = query_device_feature(func, funcProp, remappedPhysicalDevice, pPacket->pCreateInfo);
    auto it = g_TraceDeviceToDeviceFeatures.find(*(pPacket->pDevice));
    if (it != g_TraceDeviceToDeviceFeatures.end()) {
        if (it->second.accelerationStructureCaptureReplay && !replayDeviceToFeatureSupport[device].accelerationStructureCaptureReplay) {
            vktrace_LogWarning("Retrace device AS CaptureReplay is different from the trace one, so the retrace may be risky. retrace feature: %d, trace feature: %d", \
            replayDeviceToFeatureSupport[device].accelerationStructureCaptureReplay, it->second.accelerationStructureCaptureReplay);
        } else if (!it->second.accelerationStructureCaptureReplay) {
            replayDeviceToFeatureSupport[device].accelerationStructureCaptureReplay = 0;
        }

        if (it->second.bufferDeviceAddressCaptureReplay && !replayDeviceToFeatureSupport[device].bufferDeviceAddressCaptureReplay) {
            vktrace_LogWarning("Retrace device buffer CaptureReplay is different from the trace one, so the retrace may be risky. retrace feature: %d, trace feature: %d", \
            replayDeviceToFeatureSupport[device].bufferDeviceAddressCaptureReplay, it->second.bufferDeviceAddressCaptureReplay);
        } else if (!it->second.bufferDeviceAddressCaptureReplay) {
            replayDeviceToFeatureSupport[device].bufferDeviceAddressCaptureReplay = 0;
        }

        if (it->second.rayTracingPipelineShaderGroupHandleCaptureReplay && !replayDeviceToFeatureSupport[device].rayTracingPipelineShaderGroupHandleCaptureReplay) {
            vktrace_LogWarning("Retrace device RTP ShaderGroupHandleCaptureReplay is different from the trace one, so the retrace may be risky. retrace feature: %d, trace feature: %d", \
            replayDeviceToFeatureSupport[device].rayTracingPipelineShaderGroupHandleCaptureReplay, it->second.rayTracingPipelineShaderGroupHandleCaptureReplay);
        } else if (!it->second.rayTracingPipelineShaderGroupHandleCaptureReplay) {
            replayDeviceToFeatureSupport[device].rayTracingPipelineShaderGroupHandleCaptureReplay = 0;
        }
    } else {
        if (g_TraceDeviceToDeviceFeatures.size()) vktrace_LogError("Can't find the trace device capture replay feature support.");
        replayDeviceToFeatureSupport[device].accelerationStructureCaptureReplay = 0;
        replayDeviceToFeatureSupport[device].bufferDeviceAddressCaptureReplay = 0;
        replayDeviceToFeatureSupport[device].rayTracingPipelineShaderGroupHandleCaptureReplay = 0;
    }
    for (auto& e : replayDeviceToFeatureSupport) {
        if (g_pReplaySettings->disableRQAndRTPCaptureReplay & 0x01) { e.second.accelerationStructureCaptureReplay = 0; }
        if (g_pReplaySettings->disableRQAndRTPCaptureReplay & 0x02) { e.second.bufferDeviceAddressCaptureReplay = 0; }
        if (g_pReplaySettings->disableRQAndRTPCaptureReplay & 0x04) { e.second.rayTracingPipelineShaderGroupHandleCaptureReplay = 0; }
    }

    if (g_pReplaySettings->enableVirtualSwapchain) {
#if defined(ANDROID)
        char *tracing_layer_env = vktrace_get_global_var("debug.vulkan.layers");
        if (tracing_layer_env && *tracing_layer_env) {
            if (strstr(tracing_layer_env, "VK_LAYER_LUNARG_vktrace")) {
                vktrace_LogWarning("evsc: Enable VK_LAYER_LUNARG_vktrace tracing layer, then enableVirtualSwapchain will be disabled.");
                g_pReplaySettings->enableVirtualSwapchain = false;
            }
            if (strstr(tracing_layer_env, "VK_LAYER_LUNARG_screenshot")) {
                m_bScreenshotLayer = true;
            }
        }
        char *tracing_layer_env_1 = vktrace_get_global_var("debug.vulkan.layer.1");
        if (tracing_layer_env_1 && *tracing_layer_env_1) {
            if (0 == strcmp(tracing_layer_env_1, "VK_LAYER_LUNARG_vktrace")) {
                vktrace_LogWarning("evsc: Enable VK_LAYER_LUNARG_vktrace tracing layer, then enableVirtualSwapchain will be disabled.");
                g_pReplaySettings->enableVirtualSwapchain = false;
            }
            if (0 == strcmp(tracing_layer_env_1, "VK_LAYER_LUNARG_screenshot")) {
                m_bScreenshotLayer = true;
            }
        }
        char *tracing_layer_env_2 = vktrace_get_global_var("debug.vulkan.layer.2");
        if (tracing_layer_env_2 && *tracing_layer_env_2) {
            if (0 == strcmp(tracing_layer_env_2, "VK_LAYER_LUNARG_vktrace")) {
                vktrace_LogWarning("evsc: Enable VK_LAYER_LUNARG_vktrace tracing layer, then enableVirtualSwapchain will be disabled.");
                g_pReplaySettings->enableVirtualSwapchain = false;
            }
            if (0 == strcmp(tracing_layer_env_2, "VK_LAYER_LUNARG_screenshot")) {
                m_bScreenshotLayer = true;
            }
        }
#endif

#if defined(PLATFORM_LINUX) && !defined(ANDROID)
        char *tracing_layer_env = vktrace_get_global_var("VK_INSTANCE_LAYERS");
        if (tracing_layer_env && *tracing_layer_env) {
            if (strstr(tracing_layer_env, "VK_LAYER_LUNARG_vktrace")) {
                vktrace_LogWarning("evsc: Enable VK_LAYER_LUNARG_vktrace tracing layer, then enableVirtualSwapchain will be disabled.");
                g_pReplaySettings->enableVirtualSwapchain = false;
            }
            if (strstr(tracing_layer_env, "VK_LAYER_LUNARG_screenshot")) {
                m_bScreenshotLayer = true;
            }
        }
#endif
    }

    forceDisableCaptureReplayFeature();
    vktrace_LogAlways("Actual using device feature CaptureReplay: BDA %d, AS %d, RTPSGH %d, SGHSize %llu",
                    replayDeviceToFeatureSupport[device].bufferDeviceAddressCaptureReplay, replayDeviceToFeatureSupport[device].accelerationStructureCaptureReplay,
                    replayDeviceToFeatureSupport[device].rayTracingPipelineShaderGroupHandleCaptureReplay,
                    replayDeviceToFeatureSupport[device].propertyShaderGroupHandleCaptureReplaySize);

#if VK_ANDROID_frame_boundary
    g_replayDevice = device;
#endif

    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateBuffer(packet_vkCreateBuffer *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    bufferObj local_bufferObj;
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    traceBufferToASBuildSizes[*(pPacket->pBuffer)] = pPacket->pCreateInfo->size;

    // Convert queueFamilyIndices
    if (pPacket->pCreateInfo && pPacket->pCreateInfo->pQueueFamilyIndices) {
        for (uint32_t i = 0; i < pPacket->pCreateInfo->queueFamilyIndexCount; i++) {
            getReplayQueueFamilyIdx(pPacket->device, remappedDevice, (uint32_t *)&pPacket->pCreateInfo->pQueueFamilyIndices[i]);
        }
    }

    if (pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferUsageFlags extraUsageFlags = 0;
        for (void **pTemp = (void**)&((pPacket->pCreateInfo)); ((VkApplicationInfo*)(*pTemp))->pNext != NULL; ) {
            // According to Vulkan Spec, VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO is not a valid structure of VkBufferCreateInfo::pNext.
            // It must be added by vktrace_editor or vktrace_rq_pp to specify if this buffer is an AS buffer or scratch buffer.
            if (((VkApplicationInfo*)((VkApplicationInfo*)(*pTemp))->pNext)->sType == VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO) {
                VkExportFenceCreateInfo* p = (VkExportFenceCreateInfo*)((VkApplicationInfo*)(*pTemp))->pNext;
                extraUsageFlags = p->handleTypes;
                ((VkApplicationInfo*)(*pTemp))->pNext = ((VkApplicationInfo*)((VkApplicationInfo*)((VkApplicationInfo*)(*pTemp))->pNext))->pNext;
            }
            else {
                pTemp = (void**)&(((VkApplicationInfo*)(*pTemp))->pNext);
            }
        }

        // need to add TRANSFER_SRC usage to the buffer so that we can copy out of it.
        const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VkDeviceSize createInfoSize = pPacket->pCreateInfo->size;
        if (extraUsageFlags & VK_BUFFER_USAGE_EXTRA_MARK_ASBUFFER) {
            auto it1 = traceASSizeToReplayASBuildSizes.find(createInfoSize);
            if (it1 != traceASSizeToReplayASBuildSizes.end()) {
                const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->size = std::max(it1->second.accelerationStructureSize, pPacket->pCreateInfo->size);
            }
            auto it2 = traceASCompactSizeToReplayASCompactSize.find(createInfoSize);
            if (it2 != traceASCompactSizeToReplayASCompactSize.end()) {
                const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->size = std::max(it2->second, pPacket->pCreateInfo->size);
            }
            auto it3 = traceBuildSizeToReplayASBuildSizes.find(createInfoSize);
            if (it3 != traceBuildSizeToReplayASBuildSizes.end()) {
                const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->size = std::max(it3->second.buildScratchSize, pPacket->pCreateInfo->size);
            }
        } else {
            if ((pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) &&
                !(pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
                !(pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) &&
                !(pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) &&
                !(pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)) {
                auto it1 = traceASSizeToReplayASBuildSizes.find(createInfoSize);
                if (it1 != traceASSizeToReplayASBuildSizes.end()) {
                    const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->size = std::max(it1->second.accelerationStructureSize, pPacket->pCreateInfo->size);
                }
                auto it2 = traceASCompactSizeToReplayASCompactSize.find(createInfoSize);
                if (it2 != traceASCompactSizeToReplayASCompactSize.end()) {
                    const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->size = std::max(it2->second, pPacket->pCreateInfo->size);
                }
                auto it3 = traceBuildSizeToReplayASBuildSizes.find(createInfoSize);
                if (it3 != traceBuildSizeToReplayASBuildSizes.end()) {
                    const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->size = std::max(it3->second.buildScratchSize, pPacket->pCreateInfo->size);
                }
            }
        }

        if (extraUsageFlags & VK_BUFFER_USAGE_EXTRA_MARK_SCRATCH) {
            auto it1 = traceBuildSizeToReplayASBuildSizes.find(createInfoSize);
            if (it1 != traceBuildSizeToReplayASBuildSizes.end()) {
                const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->size = std::max(it1->second.buildScratchSize, pPacket->pCreateInfo->size);
            }
            auto it2 = traceUpdateSizeToReplayASBuildSizes.find(createInfoSize);
            if (it2 != traceUpdateSizeToReplayASBuildSizes.end()) {
                const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->size = std::max(it2->second.updateScratchSize, pPacket->pCreateInfo->size);
            }
        } else {
            if ((pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
                !(pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
                !(pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) &&
                !(pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) &&
                !(pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)) {
                auto it1 = traceBuildSizeToReplayASBuildSizes.find(createInfoSize);
                if (it1 != traceBuildSizeToReplayASBuildSizes.end()) {
                    const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->size = std::max(it1->second.buildScratchSize, pPacket->pCreateInfo->size);
                }
                auto it2 = traceUpdateSizeToReplayASBuildSizes.find(createInfoSize);
                if (it2 != traceUpdateSizeToReplayASBuildSizes.end()) {
                    const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->size = std::max(it2->second.updateScratchSize, pPacket->pCreateInfo->size);
                }
            }
        }

        VkBufferOpaqueCaptureAddressCreateInfo *pCaptureAddressCreateInfo = (VkBufferOpaqueCaptureAddressCreateInfo*)find_ext_struct((const vulkan_struct_header*)pPacket->pCreateInfo->pNext, VK_STRUCTURE_TYPE_BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO);
        if (pCaptureAddressCreateInfo == nullptr) {
            pCaptureAddressCreateInfo = (VkBufferOpaqueCaptureAddressCreateInfo*)find_ext_struct((const vulkan_struct_header*)pPacket->pCreateInfo->pNext, VK_STRUCTURE_TYPE_BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO_KHR);
        }

        if (pPacket->pCreateInfo->flags & VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_EXT) {
            auto it = replayDeviceToFeatureSupport.find(remappedDevice);
            if (it != replayDeviceToFeatureSupport.end() && it->second.bufferDeviceAddressCaptureReplay == 0) {
                const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->flags &= ~VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_EXT;
                vktrace_LogDebug("The device doesn't support bufferDeviceAddressCaptureReplay feature.");
                if (pCaptureAddressCreateInfo != nullptr) {
                    pCaptureAddressCreateInfo->opaqueCaptureAddress = 0;
                }
            }
        }
    }

    rtHandler->addSbtBufferFlag(const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->usage);
    replayResult = m_vkDeviceFuncs.CreateBuffer(remappedDevice, pPacket->pCreateInfo, NULL, &local_bufferObj.replayBuffer);
    if (replayResult == VK_SUCCESS) {
        traceBufferToDevice[*pPacket->pBuffer] = pPacket->device;
        replayBufferToDevice[local_bufferObj.replayBuffer] = remappedDevice;
        m_objMapper.add_to_buffers_map(*(pPacket->pBuffer), local_bufferObj);
        replayBufferToASBuildSizes[local_bufferObj.replayBuffer] = pPacket->pCreateInfo->size;
        rtHandler->addSbtBuffer(*pPacket->pBuffer, pPacket->pCreateInfo->usage);
    }
    if (pPacket->pCreateInfo->usage & (VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)) {
        replayBufferToUsageFlag[local_bufferObj.replayBuffer] = pPacket->pCreateInfo->usage;
    }
    return replayResult;
}

static int getImagePlaneCount(VkFormat format) {
    int plane = 0;
    switch(format) {
        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
        plane = 3;
        break;
        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
        case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_444_UNORM:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G16_B16R16_2PLANE_444_UNORM:
        plane = 2;
        break;
        default:
        plane = 1;
    }
    return plane;
}

VkResult vkReplay::manually_replay_vkCreateImage(packet_vkCreateImage *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    imageObj local_imageObj;
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    // Convert queueFamilyIndices
    if (pPacket->pCreateInfo && pPacket->pCreateInfo->pQueueFamilyIndices) {
        for (uint32_t i = 0; i < pPacket->pCreateInfo->queueFamilyIndexCount; i++) {
            getReplayQueueFamilyIdx(pPacket->device, remappedDevice, (uint32_t *)&pPacket->pCreateInfo->pQueueFamilyIndices[i]);
        }
    }

    VkExternalMemoryImageCreateInfo* pImportMemory = (VkExternalMemoryImageCreateInfo*)find_ext_struct(
                                                                    (const vulkan_struct_header*)pPacket->pCreateInfo,
                                                                    VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
#if defined(PLATFORM_LINUX) && !defined(ANDROID)
    VkImageDrmFormatModifierListCreateInfoEXT imageDrmFormatModifierListCreateInfo = {};
    if (pImportMemory && pImportMemory->handleTypes == VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID) {
        if (!m_replayOpenXRContent) {
            const_cast<VkImageCreateInfo*>(pPacket->pCreateInfo)->tiling = VK_IMAGE_TILING_LINEAR;
            const_cast<VkImageCreateInfo*>(pPacket->pCreateInfo)->pNext = nullptr;
        }
#if defined(ARM_LINUX_64)
        else {
            // Call vkGetPhysicalDeviceFormatProperties2() to get DRM handles
            VkPhysicalDevice replayPhysicalDevice = replayPhysicalDevices.at(remappedDevice);
            // Query drmFormatModifierCount first
            VkDrmFormatModifierPropertiesListEXT drmFormatModifierPropertiesList{
                VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT, NULL, 0, NULL};
            VkFormatProperties formatProperties;
            VkFormatProperties2 formatProperties2{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &drmFormatModifierPropertiesList,
                                                  formatProperties};
            m_vkFuncs.GetPhysicalDeviceFormatProperties2(replayPhysicalDevice, pPacket->pCreateInfo->format, &formatProperties2);
            std::vector<VkDrmFormatModifierPropertiesEXT> drmFormatModifierProperties(
                drmFormatModifierPropertiesList.drmFormatModifierCount);
            // Then get pDrmFormatModifierProperties
            if (drmFormatModifierPropertiesList.drmFormatModifierCount) {
                drmFormatModifierPropertiesList.pDrmFormatModifierProperties = drmFormatModifierProperties.data();
                m_vkFuncs.GetPhysicalDeviceFormatProperties2(replayPhysicalDevice, pPacket->pCreateInfo->format,
                                                             &formatProperties2);
            }
            // Call vkGetPhysicalDeviceImageFormatProperties2() to check if the physical device suppports external image format
            // Input parameters
            VkPhysicalDeviceImageDrmFormatModifierInfoEXT imageDrmFormatModifierInfo{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT, NULL, 0, VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
            VkPhysicalDeviceExternalImageFormatInfo externalImageFormatInfo{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO, &imageDrmFormatModifierInfo,
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
            VkPhysicalDeviceImageFormatInfo2 imageFormatInfo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
                                                             &externalImageFormatInfo,
                                                             pPacket->pCreateInfo->format,
                                                             pPacket->pCreateInfo->imageType,
                                                             VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
                                                             pPacket->pCreateInfo->usage,
                                                             pPacket->pCreateInfo->flags};
            // Return structures
            VkExternalMemoryProperties externalMemoryProperties;
            VkExternalImageFormatProperties externalImageFormatProperties{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES, NULL,
                                                                          externalMemoryProperties};
            VkImageFormatProperties imageFormatProperties;
            VkImageFormatProperties2 imageFormatProperties2{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
                                                            &externalImageFormatProperties, imageFormatProperties};
            std::vector<uint64_t> verifiedDrmFormatModifierProperties;
            for (uint i = 0; i < drmFormatModifierProperties.size(); ++i) {
                imageDrmFormatModifierInfo.drmFormatModifier = drmFormatModifierProperties[i].drmFormatModifier;
                VkResult result = m_vkFuncs.GetPhysicalDeviceImageFormatProperties2(replayPhysicalDevice, &imageFormatInfo,
                                                                                    &imageFormatProperties2);
                if (result == VK_SUCCESS) {
                    verifiedDrmFormatModifierProperties.push_back(drmFormatModifierProperties[i].drmFormatModifier);
                }
            }
            // Modify the image create info
            const_cast<VkImageCreateInfo *>(pPacket->pCreateInfo)->tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
            pImportMemory->handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            // Substitute VkImageDrmFormatModifierListCreateInfoEXT for VkExternalFormatANDROID in pnext chain
            delete_ext_struct((vulkan_struct_header *)pPacket->pCreateInfo, VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID);
            imageDrmFormatModifierListCreateInfo = {VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT, NULL,
                                                    (uint32_t)verifiedDrmFormatModifierProperties.size(),
                                                    verifiedDrmFormatModifierProperties.data()};
            add_ext_struct((vulkan_struct_header *)pPacket->pCreateInfo,
                           (vulkan_struct_header *)&imageDrmFormatModifierListCreateInfo);
        }
#endif
    }
#endif
    if (g_pReplaySettings->imgCompressFlag != UINT_MAX) {
        VkImageCreateInfo *pTemp = const_cast<VkImageCreateInfo*>(pPacket->pCreateInfo);
        static VkImageCompressionControlEXT imcompressionInfo = {};
        imcompressionInfo.sType = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT;
        imcompressionInfo.pNext = const_cast<void*>(pTemp->pNext);
        pTemp->pNext = &imcompressionInfo;
        imcompressionInfo.compressionControlPlaneCount = getImagePlaneCount(pPacket->pCreateInfo->format);
        static VkImageCompressionFixedRateFlagsEXT imgCompressFixedRateFlags[3] = {g_pReplaySettings->imgCompressRate, g_pReplaySettings->imgCompressRate, g_pReplaySettings->imgCompressRate};
        imcompressionInfo.pFixedRateFlags = imgCompressFixedRateFlags;
        imcompressionInfo.flags = g_pReplaySettings->imgCompressFlag;
    }

    replayResult = m_vkDeviceFuncs.CreateImage(remappedDevice, pPacket->pCreateInfo, NULL, &local_imageObj.replayImage);

#if VK_ANDROID_frame_boundary
    if(replaySettings.convertAndroidFrameBoundary && pImportMemory) {
        imageInfo tmp;
        tmp.image = *(pPacket->pImage);
        tmp.device = pPacket->device;
        tmp.extent = pPacket->pCreateInfo->extent;
        androidhardwareBufferImages.push_back(tmp);
    }
#endif

    if (replayResult == VK_SUCCESS) {
        traceImageToDevice[*pPacket->pImage] = pPacket->device;
        replayImageToDevice[local_imageObj.replayImage] = remappedDevice;
        m_objMapper.add_to_images_map(*(pPacket->pImage), local_imageObj);
        if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch()) {
            replayImageToTiling[local_imageObj.replayImage] = pPacket->pCreateInfo->tiling;
        }
        VkExternalMemoryImageCreateInfo* pExternalMemory = (VkExternalMemoryImageCreateInfo*)find_ext_struct(
                                                                    (const vulkan_struct_header*)pPacket->pCreateInfo, VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
        if (pExternalMemory != nullptr) {
            hardwarebufferImage.push_back(local_imageObj.replayImage);
        }
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateCommandPool(packet_vkCreateCommandPool *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkCommandPool local_pCommandPool;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    // No need to remap pAllocator

    // Convert queueFamilyIndex
    if (pPacket->pCreateInfo) {
        getReplayQueueFamilyIdx(pPacket->device, remappeddevice, (uint32_t *)&pPacket->pCreateInfo->queueFamilyIndex);
    }

    replayResult =
        m_vkDeviceFuncs.CreateCommandPool(remappeddevice, pPacket->pCreateInfo, pPacket->pAllocator, &local_pCommandPool);
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_commandpools_map(*(pPacket->pCommandPool), local_pCommandPool);
        replayCommandPoolToDevice[local_pCommandPool] = remappeddevice;
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkEnumeratePhysicalDevices(packet_vkEnumeratePhysicalDevices *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    uint32_t deviceCount = *(pPacket->pPhysicalDeviceCount);
    VkPhysicalDevice *pDevices = pPacket->pPhysicalDevices;

    VkInstance remappedInstance = m_objMapper.remap_instances(pPacket->instance);
    if (remappedInstance == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkEnumeratePhysicalDevices() due to invalid remapped VkInstance.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    if (pPacket->pPhysicalDevices != NULL) {
        // If we are querying for the list instead of the count, use a previously acquired count
        if (m_gpu_count != 0) deviceCount = m_gpu_count;
        pDevices = VKTRACE_NEW_ARRAY(VkPhysicalDevice, deviceCount);
    }
    replayResult = m_vkFuncs.EnumeratePhysicalDevices(remappedInstance, &deviceCount, pDevices);

    if (pDevices == NULL) {
        // If we are querying for the count, store it for later
        m_gpu_count = deviceCount;
    }

    if (deviceCount != *(pPacket->pPhysicalDeviceCount)) {
        vktrace_LogWarning("Number of physical devices mismatched in replay %u versus trace %u.", deviceCount,
                           *(pPacket->pPhysicalDeviceCount));
    } else if (deviceCount == 0) {
        vktrace_LogError("vkEnumeratePhysicalDevices number of gpus is zero.");
    } else if (pDevices != NULL) {
        vktrace_LogVerbose("Enumerated %d physical devices in the system.", deviceCount);
    }

    if (pDevices != NULL) {
        const uint32_t replay_device_count = deviceCount;
        uint64_t *replay_device_id = VKTRACE_NEW_ARRAY(uint64_t, replay_device_count);
        for (uint32_t i = 0; i < replay_device_count; ++i) {
            VkPhysicalDeviceProperties props;
            m_vkFuncs.GetPhysicalDeviceProperties(pDevices[i], &props);
            replay_device_id[i] = ((uint64_t)props.vendorID << 32) | (uint64_t)props.deviceID;
        }

        const uint32_t trace_device_count = *pPacket->pPhysicalDeviceCount;

        for (uint32_t i = 0; i < trace_device_count; i++) {
            // TODO: Pick a device based on matching properties. Might have to move this logic
            // First, check if device on the same index has matching vendor and device ID
            if (i < replay_device_count && m_pGpuinfo[i].gpu_id == replay_device_id[i]) {
                m_objMapper.add_to_physicaldevices_map(pPacket->pPhysicalDevices[i], pDevices[i]);
            } else {
                // Search the list for a matching device
                bool found = false;
                for (uint32_t j = 0; j < replay_device_count; ++j) {
                    if (j == i) {
                        continue;  // Already checked this
                    }
                    if (m_pGpuinfo[i].gpu_id == replay_device_id[j]) {
                        m_objMapper.add_to_physicaldevices_map(pPacket->pPhysicalDevices[i], pDevices[j]);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // If all else fails, just map the indices.
                    if (i >= replay_device_count) {
                        m_objMapper.add_to_physicaldevices_map(pPacket->pPhysicalDevices[i], pDevices[0]);
                    } else {
                        m_objMapper.add_to_physicaldevices_map(pPacket->pPhysicalDevices[i], pDevices[i]);
                    }
                }
            }
        }

        VKTRACE_DELETE(replay_device_id);
    }
    VKTRACE_DELETE(pDevices);
    return replayResult;
}

VkResult vkReplay::manually_replay_vkEnumeratePhysicalDeviceGroups(packet_vkEnumeratePhysicalDeviceGroups *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    uint32_t deviceGroupCount = *(pPacket->pPhysicalDeviceGroupCount);
    VkPhysicalDeviceGroupProperties *pDeviceGroupProperties = pPacket->pPhysicalDeviceGroupProperties;

    VkInstance remappedInstance = m_objMapper.remap_instances(pPacket->instance);
    if (remappedInstance == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkEnumeratePhysicalDevices() due to invalid remapped VkInstance.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    if (pPacket->pPhysicalDeviceGroupProperties != NULL) {
        // If we are querying for the list instead of the count, use a previously acquired count
        deviceGroupCount = m_gpu_group_count;
        pDeviceGroupProperties = VKTRACE_NEW_ARRAY(VkPhysicalDeviceGroupProperties, deviceGroupCount);
        memset(pDeviceGroupProperties, 0, sizeof(VkPhysicalDeviceGroupProperties) * deviceGroupCount);
    }
    replayResult = m_vkFuncs.EnumeratePhysicalDeviceGroups(remappedInstance, &deviceGroupCount, pDeviceGroupProperties);

    if (pDeviceGroupProperties == NULL) {
        // If we are querying for the count, store it for later
        m_gpu_group_count = deviceGroupCount;
    }

    if (deviceGroupCount != *(pPacket->pPhysicalDeviceGroupCount)) {
        vktrace_LogWarning("Number of physical device groups mismatched in replay %u versus trace %u.", deviceGroupCount,
                           *(pPacket->pPhysicalDeviceGroupCount));
    } else if (deviceGroupCount == 0) {
        vktrace_LogError("vkEnumeratePhysicalDeviceGroups number of gpu groups is zero.");
    } else if (pDeviceGroupProperties != NULL) {
        vktrace_LogVerbose("Enumerated %d physical device groups in the system.", deviceGroupCount);
    }

    if (pDeviceGroupProperties != NULL) {
        const uint32_t replay_device_group_count = deviceGroupCount;

        vector<VkPhysicalDevice> replay_device;
        vector<uint64_t> replay_device_id;
        for (uint32_t i = 0; i < replay_device_group_count; ++i) {
            for (uint32_t j = 0; j < pDeviceGroupProperties[i].physicalDeviceCount; ++j) {
                replay_device.push_back(pDeviceGroupProperties[i].physicalDevices[j]);
                VkPhysicalDeviceProperties props;
                m_vkFuncs.GetPhysicalDeviceProperties(pDeviceGroupProperties[i].physicalDevices[j], &props);
                replay_device_id.push_back(((uint64_t)props.vendorID << 32) | (uint64_t)props.deviceID);
            }
        }

        vector<VkPhysicalDevice> trace_device;
        for (uint32_t i = 0; i < *pPacket->pPhysicalDeviceGroupCount; ++i) {
            for (uint32_t j = 0; j < pPacket->pPhysicalDeviceGroupProperties[i].physicalDeviceCount; ++j) {
                trace_device.push_back(pPacket->pPhysicalDeviceGroupProperties[i].physicalDevices[j]);
            }
        }
        const uint32_t trace_device_count = trace_device.size();
        const uint32_t replay_device_count = replay_device_id.size();

        for (uint32_t i = 0; i < trace_device_count; i++) {
            // TODO: Pick a device based on matching properties. Might have to move this logic
            // First, check if device on the same index has matching vendor and device ID
            if (i < replay_device_count && m_pGpuinfo[i].gpu_id == replay_device_id[i]) {
                m_objMapper.add_to_physicaldevices_map(trace_device[i], replay_device[i]);
            } else {
                // Search the list for a matching device
                bool found = false;
                for (uint32_t j = 0; j < replay_device_count; ++j) {
                    if (j == i) {
                        continue;  // Already checked this
                    }
                    if (m_pGpuinfo[i].gpu_id == replay_device_id[j]) {
                        m_objMapper.add_to_physicaldevices_map(trace_device[i], replay_device[j]);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // If all else fails, just map the indices.
                    if (i >= replay_device_count) {
                        m_objMapper.add_to_physicaldevices_map(trace_device[i], replay_device[0]);
                    } else {
                        m_objMapper.add_to_physicaldevices_map(trace_device[i], replay_device[i]);
                    }
                }
            }
        }
    }
    VKTRACE_DELETE(pDeviceGroupProperties);
    return replayResult;
}

void vkReplay::manually_replay_vkDestroyBuffer(packet_vkDestroyBuffer *pPacket) {
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in vkDestroyBuffer() due to invalid remapped VkDevice.");
        return;
    }
    VkBuffer remappedBuffer = m_objMapper.remap_buffers(pPacket->buffer);
    if (pPacket->buffer != VK_NULL_HANDLE && remappedBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in vkDestroyBuffer() due to invalid remapped VkBuffer.");
        return;
    }
    m_vkDeviceFuncs.DestroyBuffer(remappedDevice, remappedBuffer, pPacket->pAllocator);
    m_objMapper.rm_from_buffers_map(pPacket->buffer);
    if (replayGetBufferMemoryRequirements.find(remappedBuffer) != replayGetBufferMemoryRequirements.end())
        replayGetBufferMemoryRequirements.erase(remappedBuffer);

    if (traceBufferToASBuildSizes.find(pPacket->buffer) != traceBufferToASBuildSizes.end())
        traceBufferToASBuildSizes.erase(pPacket->buffer);

    if (replayBufferToASBuildSizes.find(remappedBuffer) != replayBufferToASBuildSizes.end())
        replayBufferToASBuildSizes.erase(remappedBuffer);

    if (traceBufferToReplayMemory.find(pPacket->buffer) != traceBufferToReplayMemory.end())
        traceBufferToReplayMemory.erase(pPacket->buffer);

    if (g_hasAsApi) {
        if (replayBufferToReplayDeviceMemory.find(remappedBuffer) != replayBufferToReplayDeviceMemory.end())
            replayBufferToReplayDeviceMemory.erase(remappedBuffer);
    }

    for (auto it = traceDeviceAddrToReplayDeviceAddr4Buf.begin(); it != traceDeviceAddrToReplayDeviceAddr4Buf.end(); it++) {
        if (it->second.traceObjHandle == (uint64_t)(pPacket->buffer)) {
            rtHandler->delSbtBufferAddress(pPacket->buffer, it->first);
            traceDeviceAddrToReplayDeviceAddr4Buf.erase(it);
            break;
        }
    }

    if (replayBufferToUsageFlag.find(remappedBuffer) != replayBufferToUsageFlag.end())
        replayBufferToUsageFlag.erase(remappedBuffer);

    if (replayBufferToReplayDeviceMemoryOffset.find(remappedBuffer) != replayBufferToReplayDeviceMemoryOffset.end())
        replayBufferToReplayDeviceMemoryOffset.erase(remappedBuffer);
    return;
}

void vkReplay::manually_replay_vkDestroyImage(packet_vkDestroyImage *pPacket) {
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in vkDestroyImage() due to invalid remapped VkDevice.");
        return;
    }
    VkImage remappedImage = m_objMapper.remap_images(pPacket->image);
    if (pPacket->image != VK_NULL_HANDLE && remappedImage == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in vkDestroyImage() due to invalid remapped VkImage.");
        return;
    }
    m_vkDeviceFuncs.DestroyImage(remappedDevice, remappedImage, pPacket->pAllocator);
    m_objMapper.rm_from_images_map(pPacket->image);
    SwapchainImageState& curSwapchainImgStat = swapchainImageStates[curSwapchainHandle];
    if (curSwapchainImgStat.traceImageToImageIndex.find(pPacket->image) != curSwapchainImgStat.traceImageToImageIndex.end()) {
        curSwapchainImgStat.traceImageIndexToImage.erase(curSwapchainImgStat.traceImageToImageIndex[pPacket->image]);
        curSwapchainImgStat.traceImageToImageIndex.erase(pPacket->image);
    }
    if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() &&
        replayOptimalImageToDeviceMemory.find(remappedImage) != replayOptimalImageToDeviceMemory.end()) {
        m_vkDeviceFuncs.FreeMemory(remappedDevice, replayOptimalImageToDeviceMemory[remappedImage], NULL);
        replayOptimalImageToDeviceMemory.erase(remappedImage);
    }
    if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() &&
        replayImageToTiling.find(remappedImage) != replayImageToTiling.end()) {
        replayImageToTiling.erase(remappedImage);
    }
    if (replayGetImageMemoryRequirements.find(remappedImage) != replayGetImageMemoryRequirements.end())
        replayGetImageMemoryRequirements.erase(remappedImage);

    auto it = std::find(hardwarebufferImage.begin(), hardwarebufferImage.end(), remappedImage);
    if (it != hardwarebufferImage.end()) {
        hardwarebufferImage.erase(it);
    }
    return;
}

VkResult vkReplay::manually_replay_vkQueueSubmit(packet_vkQueueSubmit *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkQueue remappedQueue = m_objMapper.remap_queues(pPacket->queue);
    if (remappedQueue == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkQueueSubmit() due to invalid remapped VkQueue.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkFence remappedFence = m_objMapper.remap_fences(pPacket->fence);
    if (pPacket->fence != VK_NULL_HANDLE && remappedFence == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkQueueSubmit() due to invalid remapped VkPhysicalDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkSubmitInfo *remappedSubmits = (VkSubmitInfo *)pPacket->pSubmits;

    for (uint32_t submit_idx = 0; submit_idx < pPacket->submitCount; submit_idx++) {
        const VkSubmitInfo *submit = &pPacket->pSubmits[submit_idx];
        VkSubmitInfo *remappedSubmit = &remappedSubmits[submit_idx];
        // Remap Semaphores & CommandBuffers for this submit
        uint32_t i = 0;
        if (submit->pCommandBuffers != NULL) {
            VkCommandBuffer *pRemappedBuffers = (VkCommandBuffer *)remappedSubmit->pCommandBuffers;
            for (i = 0; i < submit->commandBufferCount; i++) {
                *(pRemappedBuffers + i) = m_objMapper.remap_commandbuffers(*(submit->pCommandBuffers + i));
                if (*(pRemappedBuffers + i) == VK_NULL_HANDLE) {
                    vktrace_LogError("Skipping vkQueueSubmit() due to invalid remapped VkCommandBuffer.");
                    return replayResult;
                }
            }
        }
        if (submit->pWaitSemaphores != NULL) {
            VkSemaphore *pRemappedWaitSems = (VkSemaphore *)remappedSubmit->pWaitSemaphores;
            for (i = 0; i < submit->waitSemaphoreCount; i++) {
                (*(pRemappedWaitSems + i)) = m_objMapper.remap_semaphores((*(submit->pWaitSemaphores + i)));
                if (g_pReplaySettings->forceSyncImgIdx) {
                    if (acquireSemaphoreToFSIISemaphore.find((*(pRemappedWaitSems + i))) != acquireSemaphoreToFSIISemaphore.end()) {
                        (*(pRemappedWaitSems + i)) = acquireSemaphoreToFSIISemaphore[(*(pRemappedWaitSems + i))];
                    }
                }
                if (*(pRemappedWaitSems + i) == VK_NULL_HANDLE) {
                    vktrace_LogError("Skipping vkQueueSubmit() due to invalid remapped wait VkSemaphore.");
                    return replayResult;
                }
            }
        }
        if (submit->pSignalSemaphores != NULL) {
            VkSemaphore *pRemappedSignalSems = (VkSemaphore *)remappedSubmit->pSignalSemaphores;
            for (i = 0; i < submit->signalSemaphoreCount; i++) {
                (*(pRemappedSignalSems + i)) = m_objMapper.remap_semaphores((*(submit->pSignalSemaphores + i)));
                if (*(pRemappedSignalSems + i) == VK_NULL_HANDLE) {
                    vktrace_LogError("Skipping vkQueueSubmit() due to invalid remapped signal VkSemaphore.");
                    return replayResult;
                }
            }
        }
    }
    replayResult = m_vkDeviceFuncs.QueueSubmit(remappedQueue, pPacket->submitCount, remappedSubmits, remappedFence);
#if VK_ANDROID_frame_boundary
    g_queue = remappedQueue;
#endif
    return replayResult;
}

VkResult vkReplay::manually_replay_vkQueueBindSparse(packet_vkQueueBindSparse *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkQueue remappedQueue = m_objMapper.remap_queues(pPacket->queue);
    if (remappedQueue == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkQueueBindSparse() due to invalid remapped VkQueue.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkFence remappedFence = m_objMapper.remap_fences(pPacket->fence);
    if (pPacket->fence != VK_NULL_HANDLE && remappedFence == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkQueueBindSparse() due to invalid remapped VkPhysicalDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkBindSparseInfo *remappedBindSparseInfos = (VkBindSparseInfo *)pPacket->pBindInfo;
    VkSparseImageMemoryBind *pRemappedImageMemories = NULL;
    VkSparseMemoryBind *pRemappedBufferMemories = NULL;
    VkSparseMemoryBind *pRemappedImageOpaqueMemories = NULL;
    VkSparseImageMemoryBindInfo *sIMBinf = NULL;
    VkSparseBufferMemoryBindInfo *sBMBinf = NULL;
    VkSparseImageOpaqueMemoryBindInfo *sIMOBinf = NULL;

    for (uint32_t bindInfo_idx = 0; bindInfo_idx < pPacket->bindInfoCount; bindInfo_idx++) {
        vkreplay_process_pnext_structs(pPacket->header, (void *)&remappedBindSparseInfos[bindInfo_idx]);

        if (remappedBindSparseInfos[bindInfo_idx].pBufferBinds) {
            remappedBindSparseInfos[bindInfo_idx].pBufferBinds =
                (const VkSparseBufferMemoryBindInfo *)(vktrace_trace_packet_interpret_buffer_pointer(
                    pPacket->header, (intptr_t)remappedBindSparseInfos[bindInfo_idx].pBufferBinds));

            sBMBinf = (VkSparseBufferMemoryBindInfo *)remappedBindSparseInfos[bindInfo_idx].pBufferBinds;
            sBMBinf->buffer = m_objMapper.remap_buffers(sBMBinf->buffer);

            if (sBMBinf->buffer == VK_NULL_HANDLE) {
                vktrace_LogError("Skipping vkQueueBindSparse() due to invalid remapped VkBuffer.");
                goto FAILURE;
            }

            if (sBMBinf->bindCount > 0 && sBMBinf->pBinds) {
                pRemappedBufferMemories = (VkSparseMemoryBind *)(vktrace_trace_packet_interpret_buffer_pointer(
                    pPacket->header, (intptr_t)remappedBindSparseInfos[bindInfo_idx].pBufferBinds->pBinds));
            }

            for (uint32_t bindCountIdx = 0; bindCountIdx < sBMBinf->bindCount; bindCountIdx++) {
                if (pRemappedBufferMemories[bindCountIdx].memory == VK_NULL_HANDLE) {
                    continue;
                }

                devicememoryObj local_mem = m_objMapper.find_devicememory(pRemappedBufferMemories[bindCountIdx].memory);
                VkDeviceMemory replay_mem = m_objMapper.remap_devicememorys(pRemappedBufferMemories[bindCountIdx].memory);

                if (replay_mem == VK_NULL_HANDLE || local_mem.pGpuMem == NULL) {
                    vktrace_LogError("Skipping vkQueueBindSparse() due to invalid remapped VkDeviceMemory.");
                    goto FAILURE;
                }
                pRemappedBufferMemories[bindCountIdx].memory = replay_mem;
            }
            sBMBinf->pBinds = pRemappedBufferMemories;
        }

        if (remappedBindSparseInfos[bindInfo_idx].pImageBinds) {
            remappedBindSparseInfos[bindInfo_idx].pImageBinds =
                (const VkSparseImageMemoryBindInfo *)(vktrace_trace_packet_interpret_buffer_pointer(
                    pPacket->header, (intptr_t)remappedBindSparseInfos[bindInfo_idx].pImageBinds));

            sIMBinf = (VkSparseImageMemoryBindInfo *)remappedBindSparseInfos[bindInfo_idx].pImageBinds;
            sIMBinf->image = m_objMapper.remap_images(sIMBinf->image);

            if (sIMBinf->image == VK_NULL_HANDLE) {
                vktrace_LogError("Skipping vkQueueBindSparse() due to invalid remapped VkImage.");
                goto FAILURE;
            }

            if (sIMBinf->bindCount > 0 && sIMBinf->pBinds) {
                pRemappedImageMemories = (VkSparseImageMemoryBind *)(vktrace_trace_packet_interpret_buffer_pointer(
                    pPacket->header, (intptr_t)remappedBindSparseInfos[bindInfo_idx].pImageBinds->pBinds));
            }
            for (uint32_t bindCountIdx = 0; bindCountIdx < sIMBinf->bindCount; bindCountIdx++) {
                if (pRemappedImageMemories[bindCountIdx].memory == VK_NULL_HANDLE) {
                    continue;
                }

                devicememoryObj local_mem = m_objMapper.find_devicememory(pRemappedImageMemories[bindCountIdx].memory);
                VkDeviceMemory replay_mem = m_objMapper.remap_devicememorys(pRemappedImageMemories[bindCountIdx].memory);

                if (replay_mem == VK_NULL_HANDLE || local_mem.pGpuMem == NULL) {
                    vktrace_LogError("Skipping vkQueueBindSparse() due to invalid remapped VkDeviceMemory.");
                    goto FAILURE;
                }
                pRemappedImageMemories[bindCountIdx].memory = replay_mem;
            }
            sIMBinf->pBinds = pRemappedImageMemories;
        }

        if (remappedBindSparseInfos[bindInfo_idx].pImageOpaqueBinds) {
            remappedBindSparseInfos[bindInfo_idx].pImageOpaqueBinds =
                (const VkSparseImageOpaqueMemoryBindInfo *)(vktrace_trace_packet_interpret_buffer_pointer(
                    pPacket->header, (intptr_t)remappedBindSparseInfos[bindInfo_idx].pImageOpaqueBinds));

            sIMOBinf = (VkSparseImageOpaqueMemoryBindInfo *)remappedBindSparseInfos[bindInfo_idx].pImageOpaqueBinds;
            sIMOBinf->image = m_objMapper.remap_images(sIMOBinf->image);

            if (sIMOBinf->image == VK_NULL_HANDLE) {
                vktrace_LogError("Skipping vkQueueBindSparse() due to invalid remapped VkImage.");
                goto FAILURE;
            }

            if (sIMOBinf->bindCount > 0 && sIMOBinf->pBinds) {
                pRemappedImageOpaqueMemories = (VkSparseMemoryBind *)(vktrace_trace_packet_interpret_buffer_pointer(
                    pPacket->header, (intptr_t)remappedBindSparseInfos[bindInfo_idx].pImageOpaqueBinds->pBinds));
            }
            for (uint32_t bindCountIdx = 0; bindCountIdx < sIMOBinf->bindCount; bindCountIdx++) {
                if (pRemappedImageOpaqueMemories[bindCountIdx].memory == VK_NULL_HANDLE) {
                    continue;
                }

                devicememoryObj local_mem =
                    m_objMapper.find_devicememory(pRemappedImageOpaqueMemories[bindCountIdx].memory);
                VkDeviceMemory replay_mem = m_objMapper.remap_devicememorys(pRemappedImageOpaqueMemories[bindCountIdx].memory);

                if (replay_mem == VK_NULL_HANDLE || local_mem.pGpuMem == NULL) {
                    vktrace_LogError("Skipping vkQueueBindSparse() due to invalid remapped VkDeviceMemory.");
                    goto FAILURE;
                }
                pRemappedImageOpaqueMemories[bindCountIdx].memory = replay_mem;
            }
            sIMOBinf->pBinds = pRemappedImageOpaqueMemories;
        }

        if (remappedBindSparseInfos[bindInfo_idx].pWaitSemaphores != NULL) {
            VkSemaphore *pRemappedWaitSems = (VkSemaphore *)remappedBindSparseInfos[bindInfo_idx].pWaitSemaphores;
            for (uint32_t i = 0; i < remappedBindSparseInfos[bindInfo_idx].waitSemaphoreCount; i++) {
                (*(pRemappedWaitSems + i)) =
                    m_objMapper.remap_semaphores((*(remappedBindSparseInfos[bindInfo_idx].pWaitSemaphores + i)));
                if (*(pRemappedWaitSems + i) == VK_NULL_HANDLE) {
                    vktrace_LogError("Skipping vkQueueBindSparse() due to invalid remapped wait VkSemaphore.");
                    goto FAILURE;
                }
            }
        }
        if (remappedBindSparseInfos[bindInfo_idx].pSignalSemaphores != NULL) {
            VkSemaphore *pRemappedSignalSems = (VkSemaphore *)remappedBindSparseInfos[bindInfo_idx].pSignalSemaphores;
            for (uint32_t i = 0; i < remappedBindSparseInfos[bindInfo_idx].signalSemaphoreCount; i++) {
                (*(pRemappedSignalSems + i)) =
                    m_objMapper.remap_semaphores((*(remappedBindSparseInfos[bindInfo_idx].pSignalSemaphores + i)));
                if (*(pRemappedSignalSems + i) == VK_NULL_HANDLE) {
                    vktrace_LogError("Skipping vkQueueBindSparse() due to invalid remapped signal VkSemaphore.");
                    goto FAILURE;
                }
            }
        }
    }

    replayResult = m_vkDeviceFuncs.QueueBindSparse(remappedQueue, pPacket->bindInfoCount, remappedBindSparseInfos, remappedFence);

FAILURE:
    return replayResult;
}

void vkReplay::manually_replay_vkUpdateDescriptorSets(packet_vkUpdateDescriptorSets *pPacket) {
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkUpdateDescriptorSets() due to invalid remapped VkDevice.");
        return;
    }

    VkWriteDescriptorSet *pRemappedWrites = (VkWriteDescriptorSet *)pPacket->pDescriptorWrites;
    VkCopyDescriptorSet *pRemappedCopies = (VkCopyDescriptorSet *)pPacket->pDescriptorCopies;

    bool errorBadRemap = false;
    VkWriteDescriptorSetAccelerationStructureKHR* pWriteDSAS = nullptr;
    for (uint32_t i = 0; i < pPacket->descriptorWriteCount && !errorBadRemap; i++) {
        VkDescriptorSet dstSet = m_objMapper.remap_descriptorsets(pPacket->pDescriptorWrites[i].dstSet);
        if (dstSet == VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkUpdateDescriptorSets() due to invalid remapped write VkDescriptorSet.");
            errorBadRemap = true;
            break;
        }

        pRemappedWrites[i].dstSet = dstSet;

        switch (pPacket->pDescriptorWrites[i].descriptorType) {
            case VK_DESCRIPTOR_TYPE_SAMPLER:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pImageInfo[j].sampler != VK_NULL_HANDLE) {
                        const_cast<VkDescriptorImageInfo *>(pRemappedWrites[i].pImageInfo)[j].sampler =
                            m_objMapper.remap_samplers(pPacket->pDescriptorWrites[i].pImageInfo[j].sampler);
                        if (pRemappedWrites[i].pImageInfo[j].sampler == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkUpdateDescriptorSets() due to invalid remapped VkSampler.");
                            errorBadRemap = true;
                            break;
                        }
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pImageInfo[j].imageView != VK_NULL_HANDLE) {
                        const_cast<VkDescriptorImageInfo *>(pRemappedWrites[i].pImageInfo)[j].imageView =
                            m_objMapper.remap_imageviews(pPacket->pDescriptorWrites[i].pImageInfo[j].imageView);
                        if (pRemappedWrites[i].pImageInfo[j].imageView == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkUpdateDescriptorSets() due to invalid remapped VkImageView.");
                            errorBadRemap = true;
                            break;
                        }
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pImageInfo[j].sampler != VK_NULL_HANDLE) {
                        const_cast<VkDescriptorImageInfo *>(pRemappedWrites[i].pImageInfo)[j].sampler =
                            m_objMapper.remap_samplers(pPacket->pDescriptorWrites[i].pImageInfo[j].sampler);
                        if (pRemappedWrites[i].pImageInfo[j].sampler == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkUpdateDescriptorSets() due to invalid remapped VkSampler.");
                            errorBadRemap = true;
                            break;
                        }
                    }
                    if (pPacket->pDescriptorWrites[i].pImageInfo[j].imageView != VK_NULL_HANDLE) {
                        const_cast<VkDescriptorImageInfo *>(pRemappedWrites[i].pImageInfo)[j].imageView =
                            m_objMapper.remap_imageviews(pPacket->pDescriptorWrites[i].pImageInfo[j].imageView);
                        if (pRemappedWrites[i].pImageInfo[j].imageView == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkUpdateDescriptorSets() due to invalid remapped VkImageView.");
                            errorBadRemap = true;
                            break;
                        }
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pTexelBufferView[j] != VK_NULL_HANDLE) {
                        const_cast<VkBufferView *>(pRemappedWrites[i].pTexelBufferView)[j] =
                            m_objMapper.remap_bufferviews(pPacket->pDescriptorWrites[i].pTexelBufferView[j]);
                        if (pRemappedWrites[i].pTexelBufferView[j] == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkUpdateDescriptorSets() due to invalid remapped VkBufferView.");
                            errorBadRemap = true;
                            break;
                        }
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pBufferInfo[j].buffer != VK_NULL_HANDLE) {
                        const_cast<VkDescriptorBufferInfo *>(pRemappedWrites[i].pBufferInfo)[j].buffer =
                            m_objMapper.remap_buffers(pPacket->pDescriptorWrites[i].pBufferInfo[j].buffer);
                        if (pRemappedWrites[i].pBufferInfo[j].buffer == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkUpdateDescriptorSets() due to invalid remapped VkBufferView.");
                            errorBadRemap = true;
                            break;
                        }
                    }
                }
                break;
            case  VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
                pWriteDSAS = (VkWriteDescriptorSetAccelerationStructureKHR*)find_ext_struct(
                                                                    (const vulkan_struct_header*)&pPacket->pDescriptorWrites[i],
                                                                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
                if (pWriteDSAS != nullptr) {
                    for (uint32_t j = 0; j < pWriteDSAS->accelerationStructureCount; j++) {
                        if (pWriteDSAS->pAccelerationStructures[j] != VK_NULL_HANDLE) {
                            const_cast<VkAccelerationStructureKHR*>(pWriteDSAS->pAccelerationStructures)[j] =
                                m_objMapper.remap_accelerationstructurekhrs(pWriteDSAS->pAccelerationStructures[j]);
                            if (pWriteDSAS->pAccelerationStructures[j] == VK_NULL_HANDLE) {
                                vktrace_LogError("Skipping vkUpdateDescriptorSets() due to invalid remapped VkAccelerationStructures.");
                                errorBadRemap = true;
                                break;
                            }
                        }
                    }
                }
                break;
            /* Nothing to do, already copied the constant values into the new descriptor info */
            default:
                vktrace_LogError("Can't find the correct type, i = %u, descriptorType = %d", i, pPacket->pDescriptorWrites[i].descriptorType);
                break;
        }
    }

    for (uint32_t i = 0; i < pPacket->descriptorCopyCount && !errorBadRemap; i++) {
        pRemappedCopies[i].dstSet = m_objMapper.remap_descriptorsets(pPacket->pDescriptorCopies[i].dstSet);
        if (pRemappedCopies[i].dstSet == VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkUpdateDescriptorSets() due to invalid remapped destination VkDescriptorSet.");
            errorBadRemap = true;
            break;
        }

        pRemappedCopies[i].srcSet = m_objMapper.remap_descriptorsets(pPacket->pDescriptorCopies[i].srcSet);
        if (pRemappedCopies[i].srcSet == VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkUpdateDescriptorSets() due to invalid remapped source VkDescriptorSet.");
            errorBadRemap = true;
            break;
        }
    }

    if (!errorBadRemap) {
        // If an error occurred, don't call the real function, but skip ahead so that memory is cleaned up!

        m_vkDeviceFuncs.UpdateDescriptorSets(remappedDevice, pPacket->descriptorWriteCount, pRemappedWrites,
                                             pPacket->descriptorCopyCount, pRemappedCopies);
    }
}

void vkReplay::manually_replay_vkUpdateDescriptorSetsPremapped(packet_vkUpdateDescriptorSets *pPacket) {
    VkDevice remappedDevice = *reinterpret_cast<VkDevice*>(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkUpdateDescriptorSetsPremapped() due to invalid remapped VkDevice.");
        return;
    }

    VkWriteDescriptorSet *pRemappedWrites = (VkWriteDescriptorSet *)pPacket->pDescriptorWrites;
    VkCopyDescriptorSet *pRemappedCopies = (VkCopyDescriptorSet *)pPacket->pDescriptorCopies;

    bool errorBadRemap = false;

    for (uint32_t i = 0; i < pPacket->descriptorWriteCount && !errorBadRemap; i++) {
        VkDescriptorSet dstSet = *reinterpret_cast<VkDescriptorSet*>(pPacket->pDescriptorWrites[i].dstSet);
        if (dstSet == VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkUpdateDescriptorSetsPremapped() due to invalid remapped write VkDescriptorSet.");
            errorBadRemap = true;
            break;
        }

        pRemappedWrites[i].dstSet = dstSet;

        switch (pPacket->pDescriptorWrites[i].descriptorType) {
            case VK_DESCRIPTOR_TYPE_SAMPLER:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pImageInfo[j].sampler != VK_NULL_HANDLE) {
                        VkSampler temp = *reinterpret_cast<VkSampler*>(pPacket->pDescriptorWrites[i].pImageInfo[j].sampler);
                        const_cast<VkDescriptorImageInfo *>(pRemappedWrites[i].pImageInfo)[j].sampler = temp;
                        if (pRemappedWrites[i].pImageInfo[j].sampler == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkUpdateDescriptorSetsPremapped() due to invalid remapped VkSampler.");
                            errorBadRemap = true;
                            break;
                        }
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pImageInfo[j].imageView != VK_NULL_HANDLE) {
                        VkImageView temp = *reinterpret_cast<VkImageView*>(pPacket->pDescriptorWrites[i].pImageInfo[j].imageView);
                        const_cast<VkDescriptorImageInfo *>(pRemappedWrites[i].pImageInfo)[j].imageView = temp;
                        if (pRemappedWrites[i].pImageInfo[j].imageView == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkUpdateDescriptorSetsPremapped() due to invalid remapped VkImageView.");
                            errorBadRemap = true;
                            break;
                        }
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pImageInfo[j].sampler != VK_NULL_HANDLE) {
                        VkSampler temp = *reinterpret_cast<VkSampler*>(pPacket->pDescriptorWrites[i].pImageInfo[j].sampler);
                        const_cast<VkDescriptorImageInfo *>(pRemappedWrites[i].pImageInfo)[j].sampler = temp;
                        if (pRemappedWrites[i].pImageInfo[j].sampler == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkUpdateDescriptorSetsPremapped() due to invalid remapped VkSampler.");
                            errorBadRemap = true;
                            break;
                        }
                    }
                    if (pPacket->pDescriptorWrites[i].pImageInfo[j].imageView != VK_NULL_HANDLE) {
                        VkImageView temp = *reinterpret_cast<VkImageView*>(pPacket->pDescriptorWrites[i].pImageInfo[j].imageView);
                        const_cast<VkDescriptorImageInfo *>(pRemappedWrites[i].pImageInfo)[j].imageView = temp;
                        if (pRemappedWrites[i].pImageInfo[j].imageView == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkUpdateDescriptorSetsPremapped() due to invalid remapped VkImageView.");
                            errorBadRemap = true;
                            break;
                        }
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pTexelBufferView[j] != VK_NULL_HANDLE) {
                        VkBufferView temp = *reinterpret_cast<VkBufferView*>(pPacket->pDescriptorWrites[i].pTexelBufferView[j]);
                        const_cast<VkBufferView *>(pRemappedWrites[i].pTexelBufferView)[j] = temp;
                        if (pRemappedWrites[i].pTexelBufferView[j] == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkUpdateDescriptorSetsPremapped() due to invalid remapped VkBufferView.");
                            errorBadRemap = true;
                            break;
                        }
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pBufferInfo[j].buffer != VK_NULL_HANDLE) {
                        bufferObj temp = *reinterpret_cast<bufferObj*>(pPacket->pDescriptorWrites[i].pBufferInfo[j].buffer);
                        const_cast<VkDescriptorBufferInfo *>(pRemappedWrites[i].pBufferInfo)[j].buffer = temp.replayBuffer;
                        if (pRemappedWrites[i].pBufferInfo[j].buffer == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkUpdateDescriptorSetsPremapped() due to invalid remapped VkBufferView.");
                            errorBadRemap = true;
                            break;
                        }
                    }
                }
            case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
                // Because AS handle can be 0x0, don't check the AS.
                break;
            /* Nothing to do, already copied the constant values into the new descriptor info */
            default:
                break;
        }
    }

    for (uint32_t i = 0; i < pPacket->descriptorCopyCount && !errorBadRemap; i++) {
        VkDescriptorSet temp = *reinterpret_cast<VkDescriptorSet*>(pPacket->pDescriptorCopies[i].dstSet);
        pRemappedCopies[i].dstSet = temp;
        if (pRemappedCopies[i].dstSet == VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkUpdateDescriptorSetsPremapped() due to invalid remapped destination VkDescriptorSet.");
            errorBadRemap = true;
            break;
        }

        temp = *reinterpret_cast<VkDescriptorSet*>(pPacket->pDescriptorCopies[i].srcSet);
        pRemappedCopies[i].srcSet = temp;
        if (pRemappedCopies[i].srcSet == VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkUpdateDescriptorSetsPremapped() due to invalid remapped source VkDescriptorSet.");
            errorBadRemap = true;
            break;
        }
    }

    if (!errorBadRemap) {
        // If an error occurred, don't call the real function, but skip ahead so that memory is cleaned up!

        m_vkDeviceFuncs.UpdateDescriptorSets(remappedDevice, pPacket->descriptorWriteCount, pRemappedWrites,
                                             pPacket->descriptorCopyCount, pRemappedCopies);
    }
}

VkResult vkReplay::manually_replay_vkCreateDescriptorSetLayout(packet_vkCreateDescriptorSetLayout *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateDescriptorSetLayout() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkDescriptorSetLayoutCreateInfo *pInfo = (VkDescriptorSetLayoutCreateInfo *)pPacket->pCreateInfo;
    if (pInfo != NULL) {
        if (pInfo->pBindings != NULL) {
            pInfo->pBindings = (VkDescriptorSetLayoutBinding *)vktrace_trace_packet_interpret_buffer_pointer(
                pPacket->header, (intptr_t)pInfo->pBindings);
            for (unsigned int i = 0; i < pInfo->bindingCount; i++) {
                VkDescriptorSetLayoutBinding *pBindings = (VkDescriptorSetLayoutBinding *)&pInfo->pBindings[i];
                if (pBindings->pImmutableSamplers != NULL &&
                    (pBindings->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
                     pBindings->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)) {
                    pBindings->pImmutableSamplers = (const VkSampler *)vktrace_trace_packet_interpret_buffer_pointer(
                        pPacket->header, (intptr_t)pBindings->pImmutableSamplers);
                    for (unsigned int j = 0; j < pBindings->descriptorCount; j++) {
                        VkSampler *pSampler = (VkSampler *)&pBindings->pImmutableSamplers[j];
                        *pSampler = m_objMapper.remap_samplers(pBindings->pImmutableSamplers[j]);
                        if (*pSampler == VK_NULL_HANDLE) {
                            vktrace_LogError("Skipping vkCreateDescriptorSetLayout() due to invalid remapped VkSampler.");
                            return VK_ERROR_VALIDATION_FAILED_EXT;
                        }
                    }
                }
            }
        }
    }
    VkDescriptorSetLayout setLayout;
    replayResult = m_vkDeviceFuncs.CreateDescriptorSetLayout(remappedDevice, pPacket->pCreateInfo, NULL, &setLayout);
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_descriptorsetlayouts_map(*(pPacket->pSetLayout), setLayout);
        replayDescriptorSetLayoutToDevice[setLayout] = remappedDevice;
    }
    return replayResult;
}

void vkReplay::manually_replay_vkDestroyDescriptorSetLayout(packet_vkDestroyDescriptorSetLayout *pPacket) {
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkDestroyDescriptorSetLayout() due to invalid remapped VkDevice.");
        return;
    }

    m_vkDeviceFuncs.DestroyDescriptorSetLayout(remappedDevice, pPacket->descriptorSetLayout, NULL);
    m_objMapper.rm_from_descriptorsetlayouts_map(pPacket->descriptorSetLayout);
}

VkResult vkReplay::manually_replay_vkFreeDescriptorSets(packet_vkFreeDescriptorSets *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkFreeDescriptorSets() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkDescriptorPool remappedDescriptorPool;
    remappedDescriptorPool = m_objMapper.remap_descriptorpools(pPacket->descriptorPool);
    if (remappedDescriptorPool == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkFreeDescriptorSets() due to invalid remapped VkDescriptorPool.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkDescriptorSet *localDSs = VKTRACE_NEW_ARRAY(VkDescriptorSet, pPacket->descriptorSetCount);
    uint32_t i;
    for (i = 0; i < pPacket->descriptorSetCount; ++i) {
        localDSs[i] = m_objMapper.remap_descriptorsets(pPacket->pDescriptorSets[i]);
        if (localDSs[i] == VK_NULL_HANDLE && pPacket->pDescriptorSets[i] != VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkFreeDescriptorSets() due to invalid remapped VkDescriptorSet.");
            VKTRACE_DELETE(localDSs);
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
    }

    replayResult =
        m_vkDeviceFuncs.FreeDescriptorSets(remappedDevice, remappedDescriptorPool, pPacket->descriptorSetCount, localDSs);
    if (replayResult == VK_SUCCESS) {
        for (i = 0; i < pPacket->descriptorSetCount; ++i) {
            m_objMapper.rm_from_descriptorsets_map(pPacket->pDescriptorSets[i]);
        }
    }
    VKTRACE_DELETE(localDSs);
    return replayResult;
}

void vkReplay::manually_replay_vkCmdBindDescriptorSets(packet_vkCmdBindDescriptorSets *pPacket) {
    VkCommandBuffer remappedCommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (remappedCommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBindDescriptorSets() due to invalid remapped VkCommandBuffer.");
        return;
    }

    VkPipelineLayout remappedLayout = m_objMapper.remap_pipelinelayouts(pPacket->layout);
    if (remappedLayout == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBindDescriptorSets() due to invalid remapped VkPipelineLayout.");
        return;
    }

    VkDescriptorSet *pRemappedSets = (VkDescriptorSet *)pPacket->pDescriptorSets;
    for (uint32_t idx = 0; idx < pPacket->descriptorSetCount && pPacket->pDescriptorSets != NULL; idx++) {
        pRemappedSets[idx] = m_objMapper.remap_descriptorsets(pPacket->pDescriptorSets[idx]);
        if (pRemappedSets[idx] == VK_NULL_HANDLE && pPacket->pDescriptorSets[idx] != VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCmdBindDescriptorSets() due to invalid remapped VkDescriptorSet.");
            return;
        }
    }

    m_vkDeviceFuncs.CmdBindDescriptorSets(remappedCommandBuffer, pPacket->pipelineBindPoint, remappedLayout, pPacket->firstSet,
                                          pPacket->descriptorSetCount, pRemappedSets, pPacket->dynamicOffsetCount,
                                          pPacket->pDynamicOffsets);
    return;
}

void vkReplay::manually_replay_vkCmdBindDescriptorSetsPremapped(packet_vkCmdBindDescriptorSets *pPacket) {
    VkCommandBuffer remappedCommandBuffer = *reinterpret_cast<VkCommandBuffer*>(pPacket->commandBuffer);
    if (remappedCommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBindDescriptorSetsPremapped() due to invalid remapped VkCommandBuffer.");
        return;
    }

    VkPipelineLayout remappedLayout = *reinterpret_cast<VkPipelineLayout*>(pPacket->layout);
    if (remappedLayout == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBindDescriptorSetsPremapped() due to invalid remapped VkPipelineLayout.");
        return;
    }

    VkDescriptorSet *pRemappedSets = (VkDescriptorSet *)pPacket->pDescriptorSets;
    for (uint32_t idx = 0; idx < pPacket->descriptorSetCount && pPacket->pDescriptorSets != NULL; idx++) {
        VkDescriptorSet temp = *reinterpret_cast<VkDescriptorSet*>(pPacket->pDescriptorSets[idx]);
        pRemappedSets[idx] = temp;
        if (pRemappedSets[idx] == VK_NULL_HANDLE && pPacket->pDescriptorSets[idx] != VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCmdBindDescriptorSetsPremapped() due to invalid remapped VkDescriptorSet.");
            return;
        }
    }

    m_vkDeviceFuncs.CmdBindDescriptorSets(remappedCommandBuffer, pPacket->pipelineBindPoint, remappedLayout, pPacket->firstSet,
                                          pPacket->descriptorSetCount, pRemappedSets, pPacket->dynamicOffsetCount,
                                          pPacket->pDynamicOffsets);
    return;
}

void vkReplay::manually_replay_vkCmdBindVertexBuffers(packet_vkCmdBindVertexBuffers *pPacket) {
    VkCommandBuffer remappedCommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (remappedCommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBindVertexBuffers() due to invalid remapped VkCommandBuffer.");
        return;
    }

    uint32_t i = 0;
    if (pPacket->pBuffers != NULL) {
        for (i = 0; i < pPacket->bindingCount; i++) {
            VkBuffer *pBuff = (VkBuffer *)&(pPacket->pBuffers[i]);
            *pBuff = m_objMapper.remap_buffers(pPacket->pBuffers[i]);
            if (*pBuff == VK_NULL_HANDLE) {
                vktrace_LogError("Skipping vkCmdBindVertexBuffers() due to invalid remapped VkBuffer.");
                return;
            }
        }
    }
    m_vkDeviceFuncs.CmdBindVertexBuffers(remappedCommandBuffer, pPacket->firstBinding, pPacket->bindingCount, pPacket->pBuffers,
                                         pPacket->pOffsets);
    return;
}

VkResult vkReplay::manually_replay_vkGetPipelineCacheData(packet_vkGetPipelineCacheData *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    size_t dataSize;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPipelineCacheData() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkPipelineCache remappedpipelineCache = m_objMapper.remap_pipelinecaches(pPacket->pipelineCache);
    if (pPacket->pipelineCache != VK_NULL_HANDLE && remappedpipelineCache == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPipelineCacheData() due to invalid remapped VkPipelineCache.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    // Since the returned data size may not be equal to size of the buffer in the trace packet allocate a local buffer as needed
    replayResult = m_vkDeviceFuncs.GetPipelineCacheData(remappeddevice, remappedpipelineCache, &dataSize, NULL);
    if (replayResult != VK_SUCCESS) return replayResult;
    if (pPacket->pData) {
        uint8_t *pData = VKTRACE_NEW_ARRAY(uint8_t, dataSize);
        replayResult = m_vkDeviceFuncs.GetPipelineCacheData(remappeddevice, remappedpipelineCache, &dataSize, pData);
        VKTRACE_DELETE(pData);
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateComputePipelines(packet_vkCreateComputePipelines *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    uint32_t i;

    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkPipelineCache pipelineCache;
    pipelineCache = m_objMapper.remap_pipelinecaches(pPacket->pipelineCache);

    VkComputePipelineCreateInfo *pLocalCIs = VKTRACE_NEW_ARRAY(VkComputePipelineCreateInfo, pPacket->createInfoCount);
    memcpy((void *)pLocalCIs, (void *)(pPacket->pCreateInfos), sizeof(VkComputePipelineCreateInfo) * pPacket->createInfoCount);

    // Fix up stage sub-elements
    for (i = 0; i < pPacket->createInfoCount; i++) {
        vkreplay_process_pnext_structs(pPacket->header, (void *)&pLocalCIs[i]);

        pLocalCIs[i].stage.module = m_objMapper.remap_shadermodules(pLocalCIs[i].stage.module);

        pLocalCIs[i].layout = m_objMapper.remap_pipelinelayouts(pLocalCIs[i].layout);
        pLocalCIs[i].basePipelineHandle = m_objMapper.remap_pipelines(pLocalCIs[i].basePipelineHandle);
    }

    VkPipeline *local_pPipelines = VKTRACE_NEW_ARRAY(VkPipeline, pPacket->createInfoCount);

    replayResult = m_vkDeviceFuncs.CreateComputePipelines(remappeddevice, pipelineCache, pPacket->createInfoCount, pLocalCIs, NULL,
                                                          local_pPipelines);

    if (replayResult == VK_SUCCESS) {
        for (i = 0; i < pPacket->createInfoCount; i++) {
            m_objMapper.add_to_pipelines_map(pPacket->pPipelines[i], local_pPipelines[i]);
            replayPipelineToDevice[local_pPipelines[i]] = remappeddevice;
        }
    }

    VKTRACE_DELETE(pLocalCIs);
    VKTRACE_DELETE(local_pPipelines);

    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateGraphicsPipelines(packet_vkCreateGraphicsPipelines *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateGraphicsPipelines() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    // Declaration of VRS ext struct to be added/substituded in the pnext chain of pipeline create info
    VkPipelineFragmentShadingRateStateCreateInfoKHR newPFSRStateCI = {};

    // remap shaders from each stage
    VkGraphicsPipelineCreateInfo *pCIs = (VkGraphicsPipelineCreateInfo *)pPacket->pCreateInfos;
    uint32_t i, j;
    for (i = 0; i < pPacket->createInfoCount; i++) {
        VkPipelineShaderStageCreateInfo *pRemappedStages = (VkPipelineShaderStageCreateInfo *)pCIs[i].pStages;

        for (j = 0; j < pPacket->pCreateInfos[i].stageCount; j++) {
            pRemappedStages[j].module = m_objMapper.remap_shadermodules(pRemappedStages[j].module);
            if (pRemappedStages[j].module == VK_NULL_HANDLE) {
                vktrace_LogError("Skipping vkCreateGraphicsPipelines() due to invalid remapped VkShaderModule.");
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }
        }

        vkreplay_process_pnext_structs(pPacket->header, (void *)&pCIs[i]);
        vkreplay_process_pnext_structs(pPacket->header, (void *)pCIs[i].pStages);
        vkreplay_process_pnext_structs(pPacket->header, (void *)pCIs[i].pVertexInputState);
        vkreplay_process_pnext_structs(pPacket->header, (void *)pCIs[i].pInputAssemblyState);
        vkreplay_process_pnext_structs(pPacket->header, (void *)pCIs[i].pTessellationState);
        vkreplay_process_pnext_structs(pPacket->header, (void *)pCIs[i].pViewportState);
        vkreplay_process_pnext_structs(pPacket->header, (void *)pCIs[i].pRasterizationState);
        vkreplay_process_pnext_structs(pPacket->header, (void *)pCIs[i].pMultisampleState);
        vkreplay_process_pnext_structs(pPacket->header, (void *)pCIs[i].pDepthStencilState);
        vkreplay_process_pnext_structs(pPacket->header, (void *)pCIs[i].pColorBlendState);
        vkreplay_process_pnext_structs(pPacket->header, (void *)pCIs[i].pDynamicState);

        pCIs[i].layout = m_objMapper.remap_pipelinelayouts(pPacket->pCreateInfos[i].layout);
        if (pCIs[i].layout == VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCreateGraphicsPipelines() due to invalid remapped VkPipelineLayout.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }

        pCIs[i].renderPass = m_objMapper.remap_renderpasss(pPacket->pCreateInfos[i].renderPass);
        if (pCIs[i].renderPass == VK_NULL_HANDLE) {
            vktrace_LogWarning("Remapped VkRenderPass is NULL in vkCreateGraphicsPipelines(). Ignore it if using dynamic rendering.");
        }

        pCIs[i].basePipelineHandle = m_objMapper.remap_pipelines(pPacket->pCreateInfos[i].basePipelineHandle);
        if (pCIs[i].basePipelineHandle == VK_NULL_HANDLE && pPacket->pCreateInfos[i].basePipelineHandle != VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCreateGraphicsPipelines() due to invalid remapped VkPipeline.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }

        ((VkPipelineViewportStateCreateInfo *)pCIs[i].pViewportState)->pViewports =
            (VkViewport *)vktrace_trace_packet_interpret_buffer_pointer(
                pPacket->header, (intptr_t)pPacket->pCreateInfos[i].pViewportState->pViewports);
        ((VkPipelineViewportStateCreateInfo *)pCIs[i].pViewportState)->pScissors =
            (VkRect2D *)vktrace_trace_packet_interpret_buffer_pointer(pPacket->header,
                                                                      (intptr_t)pPacket->pCreateInfos[i].pViewportState->pScissors);

        ((VkPipelineMultisampleStateCreateInfo *)pCIs[i].pMultisampleState)->pSampleMask =
            (VkSampleMask *)vktrace_trace_packet_interpret_buffer_pointer(
                pPacket->header, (intptr_t)pPacket->pCreateInfos[i].pMultisampleState->pSampleMask);

        // Manually add pnext struct for VRS.
        if (g_pReplaySettings->forceVariableRateShading != nullptr) {
            newPFSRStateCI = {VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR,
                              NULL,
                              {m_fragmentSize.width, m_fragmentSize.height},
                              {VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR, VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR}};
            VkPipelineFragmentShadingRateStateCreateInfoKHR *pPFSRStateCI =
                (VkPipelineFragmentShadingRateStateCreateInfoKHR *)find_ext_struct(
                    (vulkan_struct_header *)&pCIs[i], VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR);
            if (pPFSRStateCI) {
                // Override the fragment size and combiner if VRS is already enabled.
                pPFSRStateCI = &newPFSRStateCI;
            } else if (!m_fvrsOverrideOnly) {
                add_ext_struct((vulkan_struct_header *)&pCIs[i], (vulkan_struct_header *)&newPFSRStateCI);
            } else {
                vktrace_LogDebug("FVRS won't be applied to current pipeline in override only mode.");
            }
        }
    }

    VkPipelineCache remappedPipelineCache;
    remappedPipelineCache = m_objMapper.remap_pipelinecaches(pPacket->pipelineCache);
    if (remappedPipelineCache == VK_NULL_HANDLE && pPacket->pipelineCache != VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateGraphicsPipelines() due to invalid remapped VkPipelineCache.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    uint32_t createInfoCount = pPacket->createInfoCount;
    VkPipeline *local_pPipelines = VKTRACE_NEW_ARRAY(VkPipeline, pPacket->createInfoCount);

    replayResult = m_vkDeviceFuncs.CreateGraphicsPipelines(remappedDevice, remappedPipelineCache, createInfoCount, pCIs, NULL,
                                                           local_pPipelines);

    if (replayResult == VK_SUCCESS) {
        for (i = 0; i < pPacket->createInfoCount; i++) {
            m_objMapper.add_to_pipelines_map(pPacket->pPipelines[i], local_pPipelines[i]);
            replayPipelineToDevice[local_pPipelines[i]] = remappedDevice;
        }
    }

    VKTRACE_DELETE(local_pPipelines);

    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreatePipelineLayout(packet_vkCreatePipelineLayout *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) return VK_ERROR_VALIDATION_FAILED_EXT;

    uint32_t i = 0;
    for (i = 0; (i < pPacket->pCreateInfo->setLayoutCount) && (pPacket->pCreateInfo->pSetLayouts != NULL); i++) {
        VkDescriptorSetLayout *pSL = (VkDescriptorSetLayout *)&(pPacket->pCreateInfo->pSetLayouts[i]);
        *pSL = m_objMapper.remap_descriptorsetlayouts(pPacket->pCreateInfo->pSetLayouts[i]);
    }
    VkPipelineLayout localPipelineLayout;
    replayResult = m_vkDeviceFuncs.CreatePipelineLayout(remappedDevice, pPacket->pCreateInfo, NULL, &localPipelineLayout);
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_pipelinelayouts_map(*(pPacket->pPipelineLayout), localPipelineLayout);
        replayPipelineLayoutToDevice[localPipelineLayout] = remappedDevice;
    }
    return replayResult;
}

void vkReplay::manually_replay_vkCmdWaitEvents(packet_vkCmdWaitEvents *pPacket) {
    VkDevice traceDevice;
    VkDevice replayDevice;
    VkCommandBuffer remappedCommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (remappedCommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdWaitEvents() due to invalid remapped VkCommandBuffer.");
        return;
    }

    uint32_t idx = 0;
    for (idx = 0; idx < pPacket->eventCount; idx++) {
        VkEvent *pEvent = (VkEvent *)&(pPacket->pEvents[idx]);
        *pEvent = m_objMapper.remap_events(pPacket->pEvents[idx]);
        if (*pEvent == VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCmdWaitEvents() due to invalid remapped VkEvent.");
            return;
        }
    }

    for (idx = 0; idx < pPacket->bufferMemoryBarrierCount; idx++) {
        VkBufferMemoryBarrier *pNextBuf = (VkBufferMemoryBarrier *)&(pPacket->pBufferMemoryBarriers[idx]);
        traceDevice = traceBufferToDevice[pNextBuf->buffer];
        pNextBuf->buffer = m_objMapper.remap_buffers(pNextBuf->buffer);
        if (pNextBuf->buffer == VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCmdWaitEvents() due to invalid remapped VkBuffer.");
            return;
        }
        replayDevice = replayBufferToDevice[pNextBuf->buffer];
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pBufferMemoryBarriers[idx].srcQueueFamilyIndex);
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pBufferMemoryBarriers[idx].dstQueueFamilyIndex);
    }
    for (idx = 0; idx < pPacket->imageMemoryBarrierCount; idx++) {
        VkImageMemoryBarrier *pNextImg = (VkImageMemoryBarrier *)&(pPacket->pImageMemoryBarriers[idx]);
        SwapchainImageState& curSwapchainImgStat = swapchainImageStates[curSwapchainHandle];
        if (curSwapchainImgStat.traceImageToImageIndex.find(pNextImg->image) != curSwapchainImgStat.traceImageToImageIndex.end()
            && curSwapchainImgStat.traceImageIndexToImage.find(m_imageIndex) != curSwapchainImgStat.traceImageIndexToImage.end()
            && m_imageIndex != UINT32_MAX) {
            pNextImg->image = curSwapchainImgStat.traceImageIndexToImage[m_imageIndex];
        }
        traceDevice = traceImageToDevice[pNextImg->image];
        pNextImg->image = m_objMapper.remap_images(pNextImg->image);
        if (pNextImg->image == VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCmdWaitEvents() due to invalid remapped VkImage.");
            return;
        }
        replayDevice = replayImageToDevice[pNextImg->image];
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pImageMemoryBarriers[idx].srcQueueFamilyIndex);
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pImageMemoryBarriers[idx].dstQueueFamilyIndex);
    }
    m_vkDeviceFuncs.CmdWaitEvents(remappedCommandBuffer, pPacket->eventCount, pPacket->pEvents, pPacket->srcStageMask,
                                  pPacket->dstStageMask, pPacket->memoryBarrierCount, pPacket->pMemoryBarriers,
                                  pPacket->bufferMemoryBarrierCount, pPacket->pBufferMemoryBarriers,
                                  pPacket->imageMemoryBarrierCount, pPacket->pImageMemoryBarriers);
    return;
}

void vkReplay::manually_replay_vkCmdPipelineBarrier(packet_vkCmdPipelineBarrier *pPacket) {
    VkDevice traceDevice;
    VkDevice replayDevice;
    VkCommandBuffer remappedCommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (remappedCommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdPipelineBarrier() due to invalid remapped VkCommandBuffer.");
        return;
    }

    uint32_t idx = 0;
    for (idx = 0; idx < pPacket->bufferMemoryBarrierCount; idx++) {
        VkBufferMemoryBarrier *pNextBuf = (VkBufferMemoryBarrier *)&(pPacket->pBufferMemoryBarriers[idx]);
        VkBuffer saveBuf = pNextBuf->buffer;
        traceDevice = traceBufferToDevice[pNextBuf->buffer];
        pNextBuf->buffer = m_objMapper.remap_buffers(pNextBuf->buffer);
        if (pNextBuf->buffer == VK_NULL_HANDLE && saveBuf != VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCmdPipelineBarrier() due to invalid remapped VkBuffer.");
            return;
        }
        replayDevice = replayBufferToDevice[pNextBuf->buffer];
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pBufferMemoryBarriers[idx].srcQueueFamilyIndex);
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pBufferMemoryBarriers[idx].dstQueueFamilyIndex);
    }
    for (idx = 0; idx < pPacket->imageMemoryBarrierCount; idx++) {
        VkImageMemoryBarrier *pNextImg = (VkImageMemoryBarrier *)&(pPacket->pImageMemoryBarriers[idx]);
        SwapchainImageState& curSwapchainImgStat = swapchainImageStates[curSwapchainHandle];
        if (curSwapchainImgStat.traceImageToImageIndex.find(pNextImg->image) != curSwapchainImgStat.traceImageToImageIndex.end()
            && curSwapchainImgStat.traceImageIndexToImage.find(m_imageIndex) != curSwapchainImgStat.traceImageIndexToImage.end()
            && m_imageIndex != UINT32_MAX) {
            pNextImg->image = curSwapchainImgStat.traceImageIndexToImage[m_imageIndex];
        }
        VkImage saveImg = pNextImg->image;
        traceDevice = traceImageToDevice[pNextImg->image];
        if (traceDevice == NULL) vktrace_LogError("DEBUG: traceDevice is NULL");
        pNextImg->image = m_objMapper.remap_images(pNextImg->image);
        if (pNextImg->image == VK_NULL_HANDLE && saveImg != VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCmdPipelineBarrier() due to invalid remapped VkImage.");
            return;
        }
        replayDevice = replayImageToDevice[pNextImg->image];
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pImageMemoryBarriers[idx].srcQueueFamilyIndex);
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pImageMemoryBarriers[idx].dstQueueFamilyIndex);
    }
    m_vkDeviceFuncs.CmdPipelineBarrier(remappedCommandBuffer, pPacket->srcStageMask, pPacket->dstStageMask,
                                       pPacket->dependencyFlags, pPacket->memoryBarrierCount, pPacket->pMemoryBarriers,
                                       pPacket->bufferMemoryBarrierCount, pPacket->pBufferMemoryBarriers,
                                       pPacket->imageMemoryBarrierCount, pPacket->pImageMemoryBarriers);

    return;
}

void vkReplay::manually_replay_vkCmdPipelineBarrier2KHR(packet_vkCmdPipelineBarrier2KHR *pPacket) {
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdPipelineBarrier2KHR() due to invalid remapped VkCommandBuffer.");
        return ;
    }
    uint32_t idx = 0;
    VkDevice traceDevice;
    VkDevice replayDevice;
    for (idx = 0; idx < pPacket->pDependencyInfo->bufferMemoryBarrierCount; idx++) {
        VkBufferMemoryBarrier2 *pNextBuf = (VkBufferMemoryBarrier2 *)&(pPacket->pDependencyInfo->pBufferMemoryBarriers[idx]);
        VkBuffer saveBuf = pNextBuf->buffer;
        traceDevice = traceBufferToDevice[pNextBuf->buffer];
        pNextBuf->buffer = m_objMapper.remap_buffers(pNextBuf->buffer);
        if (pNextBuf->buffer == VK_NULL_HANDLE && saveBuf != VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCmdPipelineBarrier() due to invalid remapped VkBuffer.");
            return;
        }
        replayDevice = replayBufferToDevice[pNextBuf->buffer];
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pDependencyInfo->pBufferMemoryBarriers[idx].srcQueueFamilyIndex);
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pDependencyInfo->pBufferMemoryBarriers[idx].dstQueueFamilyIndex);
    }
    for (idx = 0; idx < pPacket->pDependencyInfo->imageMemoryBarrierCount; idx++) {
        VkImageMemoryBarrier2 *pNextImg = (VkImageMemoryBarrier2 *)&(pPacket->pDependencyInfo->pImageMemoryBarriers[idx]);
        SwapchainImageState& curSwapchainImgStat = swapchainImageStates[curSwapchainHandle];
        if (curSwapchainImgStat.traceImageToImageIndex.find(pNextImg->image) != curSwapchainImgStat.traceImageToImageIndex.end()
            && curSwapchainImgStat.traceImageIndexToImage.find(m_imageIndex) != curSwapchainImgStat.traceImageIndexToImage.end()
            && m_imageIndex != UINT32_MAX) {
            pNextImg->image = curSwapchainImgStat.traceImageIndexToImage[m_imageIndex];
        }
        VkImage saveImg = pNextImg->image;
        traceDevice = traceImageToDevice[pNextImg->image];
        if (traceDevice == NULL) vktrace_LogError("DEBUG: traceDevice is NULL");
        pNextImg->image = m_objMapper.remap_images(pNextImg->image);
        if (pNextImg->image == VK_NULL_HANDLE && saveImg != VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCmdPipelineBarrier() due to invalid remapped VkImage.");
            return;
        }
        replayDevice = replayImageToDevice[pNextImg->image];
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pDependencyInfo->pImageMemoryBarriers[idx].srcQueueFamilyIndex);
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pDependencyInfo->pImageMemoryBarriers[idx].dstQueueFamilyIndex);
    }

    // No need to remap pDependencyInfo
    m_vkDeviceFuncs.CmdPipelineBarrier2KHR(remappedcommandBuffer, pPacket->pDependencyInfo);
}

void vkReplay::manually_replay_vkCmdPipelineBarrier2(packet_vkCmdPipelineBarrier2 *pPacket) {
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdPipelineBarrier2KHR() due to invalid remapped VkCommandBuffer.");
        return ;
    }
    uint32_t idx = 0;
    VkDevice traceDevice;
    VkDevice replayDevice;
    for (idx = 0; idx < pPacket->pDependencyInfo->bufferMemoryBarrierCount; idx++) {
        VkBufferMemoryBarrier2 *pNextBuf = (VkBufferMemoryBarrier2 *)&(pPacket->pDependencyInfo->pBufferMemoryBarriers[idx]);
        VkBuffer saveBuf = pNextBuf->buffer;
        traceDevice = traceBufferToDevice[pNextBuf->buffer];
        pNextBuf->buffer = m_objMapper.remap_buffers(pNextBuf->buffer);
        if (pNextBuf->buffer == VK_NULL_HANDLE && saveBuf != VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCmdPipelineBarrier() due to invalid remapped VkBuffer.");
            return;
        }
        replayDevice = replayBufferToDevice[pNextBuf->buffer];
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pDependencyInfo->pBufferMemoryBarriers[idx].srcQueueFamilyIndex);
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pDependencyInfo->pBufferMemoryBarriers[idx].dstQueueFamilyIndex);
    }
    for (idx = 0; idx < pPacket->pDependencyInfo->imageMemoryBarrierCount; idx++) {
        VkImageMemoryBarrier2 *pNextImg = (VkImageMemoryBarrier2 *)&(pPacket->pDependencyInfo->pImageMemoryBarriers[idx]);
        SwapchainImageState& curSwapchainImgStat = swapchainImageStates[curSwapchainHandle];
        if (curSwapchainImgStat.traceImageToImageIndex.find(pNextImg->image) != curSwapchainImgStat.traceImageToImageIndex.end()
            && curSwapchainImgStat.traceImageIndexToImage.find(m_imageIndex) != curSwapchainImgStat.traceImageIndexToImage.end()
            && m_imageIndex != UINT32_MAX) {
            pNextImg->image = curSwapchainImgStat.traceImageIndexToImage[m_imageIndex];
        }
        VkImage saveImg = pNextImg->image;
        traceDevice = traceImageToDevice[pNextImg->image];
        if (traceDevice == NULL) vktrace_LogError("DEBUG: traceDevice is NULL");
        pNextImg->image = m_objMapper.remap_images(pNextImg->image);
        if (pNextImg->image == VK_NULL_HANDLE && saveImg != VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCmdPipelineBarrier() due to invalid remapped VkImage.");
            return;
        }
        replayDevice = replayImageToDevice[pNextImg->image];
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pDependencyInfo->pImageMemoryBarriers[idx].srcQueueFamilyIndex);
        getReplayQueueFamilyIdx(traceDevice, replayDevice, (uint32_t *)&pPacket->pDependencyInfo->pImageMemoryBarriers[idx].dstQueueFamilyIndex);
    }

    // No need to remap pDependencyInfo
    m_vkDeviceFuncs.CmdPipelineBarrier2(remappedcommandBuffer, pPacket->pDependencyInfo);
}

VkResult vkReplay::manually_replay_vkCreateFramebuffer(packet_vkCreateFramebuffer *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateFramebuffer() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkFramebufferCreateInfo *pInfo = (VkFramebufferCreateInfo *)pPacket->pCreateInfo;
    VkImageView *pAttachments = (VkImageView *)pInfo->pAttachments;
    if (pAttachments != NULL && !g_pReplaySettings->enableVirtualSwapchain) {
        for (uint32_t i = 0; i < pInfo->attachmentCount; i++) {
            VkImageView savedIV = pInfo->pAttachments[i];
            SwapchainImageState& curSwapchainImgStat = swapchainImageStates[curSwapchainHandle];
            if (curSwapchainImgStat.traceImageViewToImageIndex.find(pInfo->pAttachments[i]) != curSwapchainImgStat.traceImageViewToImageIndex.end()) {
                if (m_imageIndex != UINT32_MAX && m_imageIndex != m_pktImgIndex
                    && curSwapchainImgStat.traceImageIndexToImageViews[m_imageIndex].size() == 1) {
                    curSwapchainImgStat.traceFramebufferToImageIndex[*(pPacket->pFramebuffer)] = m_imageIndex;
                    if (curSwapchainImgStat.traceImageIndexToImageViews.find(m_imageIndex) != curSwapchainImgStat.traceImageIndexToImageViews.end()) {
                        savedIV = *curSwapchainImgStat.traceImageIndexToImageViews[m_imageIndex].begin();
                    }
                } else {
                    curSwapchainImgStat.traceFramebufferToImageIndex[*(pPacket->pFramebuffer)] = curSwapchainImgStat.traceImageViewToImageIndex[pInfo->pAttachments[i]];
                }
                curSwapchainImgStat.traceFramebufferToRenderpass[*(pPacket->pFramebuffer)] = pPacket->pCreateInfo->renderPass;
                curSwapchainImgStat.traceFramebufferToAttachmentCount[*(pPacket->pFramebuffer)] = pInfo->attachmentCount;
            }
            pAttachments[i] = m_objMapper.remap_imageviews(savedIV);
            if (pAttachments[i] == VK_NULL_HANDLE && savedIV != VK_NULL_HANDLE) {
                vktrace_LogError("Skipping vkCreateFramebuffer() due to invalid remapped VkImageView.");
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }
        }
    }
    if (pAttachments != NULL && g_pReplaySettings->enableVirtualSwapchain) {
        for (uint32_t i = 0; i < pInfo->attachmentCount; i++) {
            pAttachments[i] = m_objMapper.remap_imageviews(pInfo->pAttachments[i]);
            if (pAttachments[i] == VK_NULL_HANDLE && pInfo->pAttachments[i] != VK_NULL_HANDLE) {
                vktrace_LogError("Skipping vkCreateFramebuffer() due to invalid remapped VkImageView.");
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }
        }
    }
    VkRenderPass savedRP = pPacket->pCreateInfo->renderPass;
    pInfo->renderPass = m_objMapper.remap_renderpasss(pPacket->pCreateInfo->renderPass);
    if (pInfo->renderPass == VK_NULL_HANDLE && savedRP != VK_NULL_HANDLE) {
        vktrace_LogWarning("Remapped VkRenderPass is NULL in vkCreateFramebuffer(). Ignore it if using dynamic rendering.");
    }

#if VK_ANDROID_frame_boundary
    if(replaySettings.convertAndroidFrameBoundary) {
        if (!g_bCreateSurface) {
            // size check: 1k 2k 4k
            uint32_t width  = pPacket->pCreateInfo->width;
            uint32_t height = pPacket->pCreateInfo->height;
            if ((width == 1920 && height == 1080) || (width == 2560 && height == 1440) || (width == 3840 && height == 2160)) {
                g_windowWidth   = width;
                g_windowHeight  = height;

                // create android surface
                VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, nullptr, 0, nullptr};
                packet_vkCreateAndroidSurfaceKHR surfacePacket = {nullptr, g_instance, &surfaceCreateInfo, nullptr, &g_traceSurface, VK_SUCCESS};
                replayResult = manually_replay_vkCreateAndroidSurfaceKHR(&surfacePacket);
                if(replayResult != VK_SUCCESS) {
                    vktrace_LogError("manually CreateAndroidSurface failed when converting Android Frame Boundary(%d)", replayResult);
                }

                // create fence
                VkFenceCreateInfo fenceCreateInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
                replayResult = m_vkDeviceFuncs.CreateFence(remappedDevice, &fenceCreateInfo, nullptr, &g_fence);
                if(replayResult != VK_SUCCESS) {
                    vktrace_LogError("CreateFence failed when converting Android Frame Boundary(%d)", replayResult);
                }

                // create semaphore
                VkSemaphoreCreateInfo semaphoreCreateInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0};
                replayResult = m_vkDeviceFuncs.CreateSemaphore(remappedDevice, &semaphoreCreateInfo, nullptr, &g_semaphore);
                if(replayResult != VK_SUCCESS) {
                    vktrace_LogError("CreateSemaphore failed when converting Android Frame Boundary(%d)", replayResult);
                }

                // create swapchain
                VkSwapchainCreateInfoKHR scCreateInfo = {};
                scCreateInfo.sType              = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
                scCreateInfo.surface            = g_traceSurface;
                scCreateInfo.minImageCount      = g_scImgCount;
                scCreateInfo.imageFormat        = VK_FORMAT_R8G8B8A8_UNORM;
                scCreateInfo.imageColorSpace    = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                scCreateInfo.imageExtent        = {g_windowWidth, g_windowHeight};
                scCreateInfo.imageArrayLayers   = 1;
                scCreateInfo.imageUsage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                scCreateInfo.imageSharingMode   = VK_SHARING_MODE_EXCLUSIVE;
                scCreateInfo.preTransform       = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
                scCreateInfo.compositeAlpha     = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
                scCreateInfo.presentMode        = VK_PRESENT_MODE_FIFO_KHR;
                scCreateInfo.clipped            = 1;
                packet_vkCreateSwapchainKHR  scPacket = {nullptr, pPacket->device, &scCreateInfo, nullptr, &g_traceSc, VK_SUCCESS};
                replayResult = manually_replay_vkCreateSwapchainKHR(&scPacket);
                if(replayResult != VK_SUCCESS) {
                    vktrace_LogError("CreateSwapchain failed when converting Android Frame Boundary(%d)", replayResult);
                }

                // get swapchain images
                uint32_t scImageCount = 0;
                packet_vkGetSwapchainImagesKHR  scImagePacket = {nullptr, pPacket->device, g_traceSc, &scImageCount, nullptr, VK_SUCCESS};
                replayResult = manually_replay_vkGetSwapchainImagesKHR(&scImagePacket);
                if(replayResult != VK_SUCCESS) {
                    vktrace_LogError("manually GetSwapchainImages(1) failed when converting Android Frame Boundary(%d)", replayResult);
                }

                scImageVec.resize(scImageCount);
                scImagePacket.pSwapchainImages = scImageVec.data();
                replayResult = manually_replay_vkGetSwapchainImagesKHR(&scImagePacket);
                if(replayResult != VK_SUCCESS) {
                    vktrace_LogError("manually GetSwapchainImages(2) failed when converting Android Frame Boundary(%d)", replayResult);
                }

                // create image view
                scImageViewVec.resize(scImageCount);
                VkImageViewCreateInfo imageViewCreateInfo = {};
                imageViewCreateInfo.sType       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                imageViewCreateInfo.viewType    = VK_IMAGE_VIEW_TYPE_2D;
                imageViewCreateInfo.format      = VK_FORMAT_R8G8B8A8_UNORM;
                imageViewCreateInfo.components  = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY};
                imageViewCreateInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                for (int i = 0; i < scImageCount; i++) {
                    imageViewCreateInfo.image = scImageVec[i];
                    replayResult = m_vkDeviceFuncs.CreateImageView(remappedDevice, &imageViewCreateInfo, nullptr, &scImageViewVec[i]);
                    if(replayResult != VK_SUCCESS) {
                        vktrace_LogError("CreateImageView failed when converting Android Frame Boundary(%d)", replayResult);
                    }
                }

                g_bCreateSurface = true;
            }
        }
    }
#endif

    VkFramebuffer local_framebuffer;
    replayResult = m_vkDeviceFuncs.CreateFramebuffer(remappedDevice, pPacket->pCreateInfo, NULL, &local_framebuffer);
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_framebuffers_map(*(pPacket->pFramebuffer), local_framebuffer);
        replayFramebufferToDevice[local_framebuffer] = remappedDevice;
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateRenderPass(packet_vkCreateRenderPass *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateRenderPass() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkRenderPass local_renderpass;
    replayResult = m_vkDeviceFuncs.CreateRenderPass(remappedDevice, pPacket->pCreateInfo, NULL, &local_renderpass);
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_renderpasss_map(*(pPacket->pRenderPass), local_renderpass);
        replayRenderPassToDevice[local_renderpass] = remappedDevice;
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateRenderPass2(packet_vkCreateRenderPass2 *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateRenderPass2() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkRenderPass local_renderpass;
    replayResult = m_vkDeviceFuncs.CreateRenderPass2(remappedDevice, pPacket->pCreateInfo, NULL, &local_renderpass);
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_renderpasss_map(*(pPacket->pRenderPass), local_renderpass);
        replayRenderPassToDevice[local_renderpass] = remappedDevice;
    }
    return replayResult;
}

void vkReplay::manually_replay_vkCmdCopyBufferRemap(packet_vkCmdCopyBuffer *pPacket) {
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdCopyBuffer() due to invalid remapped VkCommandBuffer.");
        return;
    }
    VkBuffer remappedsrcBuffer = m_objMapper.remap_buffers(pPacket->srcBuffer);
    if (pPacket->srcBuffer != VK_NULL_HANDLE && remappedsrcBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdCopyBuffer() due to invalid remapped VkBuffer.");
        return;
    }
    VkBuffer remappeddstBuffer = m_objMapper.remap_buffers(pPacket->dstBuffer);
    if (pPacket->dstBuffer != VK_NULL_HANDLE && remappeddstBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdCopyBuffer() due to invalid remapped VkBuffer.");
        return;
    }
    // No need to remap regionCount
    // No need to remap pRegions

    if (g_hasAsApi) {
        VkDevice device = VK_NULL_HANDLE;
        auto it = replayCommandBufferToReplayDevice.find(remappedcommandBuffer);
        if (it == replayCommandBufferToReplayDevice.end()) {
            vktrace_LogError("Error detected in CmdCopyBuffer(), couldn't be able to find corresponding VkDevice for VkCommandBuffer 0x%llx.", remappedcommandBuffer);
            return;
        }
        device = it->second;

        bool supportASCaptureReplay = false;
        bool supportBufferCaptureReplay = false;
        auto it_fea = replayDeviceToFeatureSupport.find(device);
        if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.accelerationStructureCaptureReplay) {
            supportASCaptureReplay = true;
        }
        if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.bufferDeviceAddressCaptureReplay) {
            supportBufferCaptureReplay = true;
        }

        VkDeviceMemory mem = VK_NULL_HANDLE;
        auto it2 = replayBufferToReplayDeviceMemory.find(remappedsrcBuffer);
        if (it2 == replayBufferToReplayDeviceMemory.end()) {
            vktrace_LogError("Error detected in CmdCopyBuffer(), couldn't be able to find corresponding VkDeviceMemory for VkBuffer 0x%llx.", remappedsrcBuffer);
            return;
        }
        mem = it2->second;

        auto it_offset = replayBufferToReplayDeviceMemoryOffset.find(remappedsrcBuffer);
        if (it_offset == replayBufferToReplayDeviceMemoryOffset.end()) {
            vktrace_LogError("Error detected in CmdCopyBuffer(), couldn't be able to find corresponding offset for VkBuffer 0x%llx.", remappedsrcBuffer);
            return;
        }
        if (!(m_pFileHeader->bit_flags & VKTRACE_RQ_POSTPROCESSED_BIT)) {
            VkDeviceSize bufferOffset = it_offset->second;
            for (unsigned i = 0; i < pPacket->regionCount; ++i) {
                VkDeviceSize srcOffset = pPacket->pRegions[i].srcOffset;
                VkDeviceSize size = pPacket->pRegions[i].size;
                VkAccelerationStructureInstanceKHR* pAsInstance = nullptr;
                VkResult mapResult = VK_NOT_READY;
                auto it3 = replayMemoryToMapAddress.find(mem);
                if (it3 != replayMemoryToMapAddress.end()) {
                    char* tmp = (char*)it3->second;
                    tmp += bufferOffset + srcOffset;
                    pAsInstance = (VkAccelerationStructureInstanceKHR*)tmp;
                    mapResult = VK_SUCCESS;
                }
                else {
                    mapResult = m_vkDeviceFuncs.MapMemory(device, mem, bufferOffset + srcOffset, size, 0, (void**)&pAsInstance);
                }
                if (mapResult == VK_SUCCESS) {
                    if (!supportASCaptureReplay && (pPacket->header->packet_id == VKTRACE_TPI_VK_vkCmdCopyBufferRemapASandBuffer ||
                        pPacket->header->packet_id == VKTRACE_TPI_VK_vkCmdCopyBufferRemapAS)) {
                        // remap all VkAccelerationStructureInstanceKHRs from trace values to replay values
                        for (unsigned j = 0; j < size / sizeof(VkAccelerationStructureInstanceKHR); ++j) {
                            auto it = traceDeviceAddrToReplayDeviceAddr4AS.find(pAsInstance[j].accelerationStructureReference);
                            if (it != traceDeviceAddrToReplayDeviceAddr4AS.end()) {
                                pAsInstance[j].accelerationStructureReference = it->second.replayDeviceAddr;
                            }
                        }
                    }
                    if (!supportBufferCaptureReplay && (pPacket->header->packet_id == VKTRACE_TPI_VK_vkCmdCopyBufferRemapASandBuffer ||
                        pPacket->header->packet_id == VKTRACE_TPI_VK_vkCmdCopyBufferRemapBuffer)) {
                        // remap all VkDeviceAddress from trace values to replay values
                        VkDeviceAddress *pDeviceAddress = (VkDeviceAddress *)pAsInstance;
                        for (unsigned j = 0; j < size / sizeof(VkDeviceAddress); ++j) {
                            auto it0 = traceDeviceAddrToReplayDeviceAddr4Buf.find(pDeviceAddress[j]);
                            auto it1 = traceDeviceAddrToReplayDeviceAddr4AS.find(pDeviceAddress[j]);
                            if (it0 != traceDeviceAddrToReplayDeviceAddr4Buf.end() && it1 == traceDeviceAddrToReplayDeviceAddr4AS.end()) {
                                pDeviceAddress[j] = it0->second.replayDeviceAddr;
                            }
                        }
                    }
                    if (it3 == replayMemoryToMapAddress.end())
                        m_vkDeviceFuncs.UnmapMemory(device, mem);
                }
            }
        }
    }

    m_vkDeviceFuncs.CmdCopyBuffer(remappedcommandBuffer, remappedsrcBuffer, remappeddstBuffer, pPacket->regionCount, pPacket->pRegions);
    return;
}

VkResult vkReplay::manually_replay_vkCreateRenderPass2KHR(packet_vkCreateRenderPass2KHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateRenderPass2KHR() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkRenderPass local_renderpass;
    replayResult = m_vkDeviceFuncs.CreateRenderPass2KHR(remappedDevice, pPacket->pCreateInfo, NULL, &local_renderpass);
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_renderpasss_map(*(pPacket->pRenderPass), local_renderpass);
        replayRenderPassToDevice[local_renderpass] = remappedDevice;
    }
    return replayResult;
}

void vkReplay::manually_replay_vkCmdBeginRenderPass(packet_vkCmdBeginRenderPass *pPacket) {
    VkCommandBuffer remappedCommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);

    if (remappedCommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBeginRenderPass() due to invalid remapped VkCommandBuffer.");
        return;
    }

    VkRenderPassBeginInfo local_renderPassBeginInfo;
    SwapchainImageState& curSwapchainImgStat = swapchainImageStates[curSwapchainHandle];
    memcpy((void *)&local_renderPassBeginInfo, (void *)pPacket->pRenderPassBegin, sizeof(VkRenderPassBeginInfo));
    local_renderPassBeginInfo.pClearValues = (const VkClearValue *)pPacket->pRenderPassBegin->pClearValues;
    if (curSwapchainImgStat.traceFramebufferToImageIndex.find(pPacket->pRenderPassBegin->framebuffer) != curSwapchainImgStat.traceFramebufferToImageIndex.end() &&
        curSwapchainImgStat.traceFramebufferToRenderpass.find(pPacket->pRenderPassBegin->framebuffer) != curSwapchainImgStat.traceFramebufferToRenderpass.end() &&
        m_imageIndex != UINT32_MAX && m_imageIndex != m_pktImgIndex && !g_pReplaySettings->enableVirtualSwapchain) {
        // Use Framebuffer mapped to the image index returned by vkAcquireNextImage()
        bool bFind = false;
        for (auto &e : curSwapchainImgStat.traceFramebufferToImageIndex) {
            if (e.second == m_imageIndex &&
                curSwapchainImgStat.traceFramebufferToRenderpass[e.first] == pPacket->pRenderPassBegin->renderPass) {
                local_renderPassBeginInfo.framebuffer = m_objMapper.remap_framebuffers(e.first);
                bFind = true;
                break;
            }
        }
        if (!bFind) {
            for (auto &e : curSwapchainImgStat.traceFramebufferToImageIndex) {
                if (e.second == m_imageIndex &&
                    curSwapchainImgStat.traceFramebufferToAttachmentCount.at(e.first) ==
                        curSwapchainImgStat.traceFramebufferToAttachmentCount.at(pPacket->pRenderPassBegin->framebuffer)) {
                    local_renderPassBeginInfo.framebuffer = m_objMapper.remap_framebuffers(e.first);
                    bFind = true;
                    break;
                }
            }
        }
        if (!bFind) {
            local_renderPassBeginInfo.framebuffer = m_objMapper.remap_framebuffers(pPacket->pRenderPassBegin->framebuffer);
        }
    } else {
        local_renderPassBeginInfo.framebuffer = m_objMapper.remap_framebuffers(pPacket->pRenderPassBegin->framebuffer);
    }
    if (local_renderPassBeginInfo.framebuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBeginRenderPass() due to invalid remapped VkFramebuffer.");
        return;
    }
    local_renderPassBeginInfo.renderPass = m_objMapper.remap_renderpasss(pPacket->pRenderPassBegin->renderPass);
    if (local_renderPassBeginInfo.renderPass == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBeginRenderPass() due to invalid remapped VkRenderPass.");
        return;
    }
    m_vkDeviceFuncs.CmdBeginRenderPass(remappedCommandBuffer, &local_renderPassBeginInfo, pPacket->contents);
}

void vkReplay::manually_replay_vkCmdBeginRenderPass2(packet_vkCmdBeginRenderPass2 *pPacket) {
    packet_vkCmdBeginRenderPass2KHR packet = {pPacket->header, pPacket->commandBuffer, pPacket->pRenderPassBegin, pPacket->pSubpassBeginInfo};
    manually_replay_vkCmdBeginRenderPass2KHR(&packet);
}

void vkReplay::manually_replay_vkCmdBeginRenderPass2KHR(packet_vkCmdBeginRenderPass2KHR *pPacket) {
    VkCommandBuffer remappedCommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (remappedCommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBeginRenderPass2() due to invalid remapped VkCommandBuffer.");
        return;
    }
    VkRenderPassBeginInfo local_renderPassBeginInfo;
    SwapchainImageState& curSwapchainImgStat = swapchainImageStates[curSwapchainHandle];
    memcpy((void *)&local_renderPassBeginInfo, (void *)pPacket->pRenderPassBegin, sizeof(VkRenderPassBeginInfo));
    local_renderPassBeginInfo.pClearValues = (const VkClearValue *)pPacket->pRenderPassBegin->pClearValues;
    if (curSwapchainImgStat.traceFramebufferToImageIndex.find(pPacket->pRenderPassBegin->framebuffer) != curSwapchainImgStat.traceFramebufferToImageIndex.end() &&
        curSwapchainImgStat.traceFramebufferToRenderpass.find(pPacket->pRenderPassBegin->framebuffer) != curSwapchainImgStat.traceFramebufferToRenderpass.end() &&
        m_imageIndex != UINT32_MAX && m_imageIndex != m_pktImgIndex && !g_pReplaySettings->enableVirtualSwapchain) {
        // Use Framebuffer mapped to the image index returned by vkAcquireNextImage()
        bool bFind = false;
        for (auto &e : curSwapchainImgStat.traceFramebufferToImageIndex) {
            if (e.second == m_imageIndex &&
                curSwapchainImgStat.traceFramebufferToRenderpass[e.first] == pPacket->pRenderPassBegin->renderPass) {
                local_renderPassBeginInfo.framebuffer = m_objMapper.remap_framebuffers(e.first);
                bFind = true;
                break;
            }
        }
        if (!bFind) {
            for (auto &e : curSwapchainImgStat.traceFramebufferToImageIndex) {
                if (e.second == m_imageIndex &&
                    curSwapchainImgStat.traceFramebufferToAttachmentCount.at(e.first) ==
                        curSwapchainImgStat.traceFramebufferToAttachmentCount.at(pPacket->pRenderPassBegin->framebuffer)) {
                    local_renderPassBeginInfo.framebuffer = m_objMapper.remap_framebuffers(e.first);
                    bFind = true;
                    break;
                }
            }
        }
        if (!bFind) {
            local_renderPassBeginInfo.framebuffer = m_objMapper.remap_framebuffers(pPacket->pRenderPassBegin->framebuffer);
        }
    } else {
        local_renderPassBeginInfo.framebuffer = m_objMapper.remap_framebuffers(pPacket->pRenderPassBegin->framebuffer);
    }
    if (local_renderPassBeginInfo.framebuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBeginRenderPass2() due to invalid remapped VkFramebuffer.");
        return;
    }
    local_renderPassBeginInfo.renderPass = m_objMapper.remap_renderpasss(pPacket->pRenderPassBegin->renderPass);
    if (local_renderPassBeginInfo.renderPass == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBeginRenderPass2() due to invalid remapped VkRenderPass.");
        return;
    }

    if (pPacket->header->packet_id == VKTRACE_TPI_VK_vkCmdBeginRenderPass2KHR) {
        m_vkDeviceFuncs.CmdBeginRenderPass2KHR(remappedCommandBuffer, &local_renderPassBeginInfo, pPacket->pSubpassBeginInfo);
    } else {
        m_vkDeviceFuncs.CmdBeginRenderPass2(remappedCommandBuffer, &local_renderPassBeginInfo, pPacket->pSubpassBeginInfo);
    }
}

void vkReplay::manually_replay_vkCmdBeginRenderingKHR(packet_vkCmdBeginRenderingKHR *pPacket) {
    VkCommandBuffer remappedCommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (remappedCommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBeginRenderingKHR() due to invalid remapped VkCommandBuffer.");
        return;
    }
    VkRenderingInfoKHR local_renderingInfo;
    SwapchainImageState& curSwapchainImgStat = swapchainImageStates[curSwapchainHandle];
    memcpy((void *)&local_renderingInfo, (void *)pPacket->pRenderingInfo, sizeof(VkRenderingInfoKHR));
    local_renderingInfo.pColorAttachments = (const VkRenderingAttachmentInfoKHR *)pPacket->pRenderingInfo->pColorAttachments;

    // pColorAttachments
    VkRenderingAttachmentInfoKHR *local_colorAttachments = VKTRACE_NEW_ARRAY(VkRenderingAttachmentInfoKHR, pPacket->pRenderingInfo->colorAttachmentCount);
    uint32_t i;
    for(i = 0; i < pPacket->pRenderingInfo->colorAttachmentCount; i++) {
        VkRenderingAttachmentInfoKHR local_colorAttachmentInfo = local_renderingInfo.pColorAttachments[i];
        if (curSwapchainImgStat.traceImageViewToImageIndex.find(pPacket->pRenderingInfo->pColorAttachments[i].imageView) != curSwapchainImgStat.traceImageViewToImageIndex.end() &&
            curSwapchainImgStat.traceImageIndexToImageViews.find(m_imageIndex) != curSwapchainImgStat.traceImageIndexToImageViews.end() &&
            m_imageIndex != UINT32_MAX && m_imageIndex != m_pktImgIndex) {
            local_colorAttachmentInfo.imageView = m_objMapper.remap_imageviews(*curSwapchainImgStat.traceImageIndexToImageViews[m_imageIndex].begin());
        } else {
            local_colorAttachmentInfo.imageView = m_objMapper.remap_imageviews(pPacket->pRenderingInfo->pColorAttachments[i].imageView);
        }
        local_colorAttachments[i] = local_colorAttachmentInfo;
        if (local_colorAttachmentInfo.imageView == VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkCmdBeginRenderingKHR() due to invalid remapped VkImageView(color attachment [%d]).",  i);
            return;
        }
    }
    local_renderingInfo.pColorAttachments = local_colorAttachments;

    // pDepthAttachment
    VkRenderingAttachmentInfoKHR local_depthAttachmentInfo = *local_renderingInfo.pDepthAttachment;
    if (curSwapchainImgStat.traceImageViewToImageIndex.find(pPacket->pRenderingInfo->pDepthAttachment->imageView) != curSwapchainImgStat.traceImageViewToImageIndex.end() &&
        curSwapchainImgStat.traceImageIndexToImageViews.find(m_imageIndex) != curSwapchainImgStat.traceImageIndexToImageViews.end() &&
        m_imageIndex != UINT32_MAX && m_imageIndex != m_pktImgIndex) {
        local_depthAttachmentInfo.imageView = m_objMapper.remap_imageviews(*curSwapchainImgStat.traceImageIndexToImageViews[m_imageIndex].begin());
    } else {
        local_depthAttachmentInfo.imageView = m_objMapper.remap_imageviews(pPacket->pRenderingInfo->pDepthAttachment->imageView);
    }
    local_renderingInfo.pDepthAttachment = &local_depthAttachmentInfo;
    if (local_renderingInfo.pDepthAttachment->imageView == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBeginRenderingKHR() due to invalid remapped VkImageView(depth attachment).");
        return;
    }

    // pStencilAttachment
    VkRenderingAttachmentInfoKHR local_stencilAttachmentInfo = *local_renderingInfo.pStencilAttachment;
    if (curSwapchainImgStat.traceImageViewToImageIndex.find(pPacket->pRenderingInfo->pStencilAttachment->imageView) != curSwapchainImgStat.traceImageViewToImageIndex.end() &&
        curSwapchainImgStat.traceImageIndexToImageViews.find(m_imageIndex) != curSwapchainImgStat.traceImageIndexToImageViews.end() &&
        m_imageIndex != UINT32_MAX && m_imageIndex != m_pktImgIndex) {
        local_stencilAttachmentInfo.imageView = m_objMapper.remap_imageviews(*curSwapchainImgStat.traceImageIndexToImageViews[m_imageIndex].begin());
    } else {
        local_stencilAttachmentInfo.imageView = m_objMapper.remap_imageviews(pPacket->pRenderingInfo->pStencilAttachment->imageView);
    }
    local_renderingInfo.pStencilAttachment = &local_stencilAttachmentInfo;
    if (local_renderingInfo.pStencilAttachment->imageView == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdBeginRenderingKHR() due to invalid remapped VkImageView(stencil attachment).");
        return;
    }

    m_vkDeviceFuncs.CmdBeginRenderingKHR(remappedCommandBuffer, &local_renderingInfo);

    VKTRACE_DELETE(local_colorAttachments);
}

VkResult vkReplay::manually_replay_vkBeginCommandBuffer(packet_vkBeginCommandBuffer *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkCommandBuffer remappedCommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (remappedCommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkBeginCommandBuffer() due to invalid remapped VkCommandBuffer.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkCommandBufferBeginInfo *pInfo = (VkCommandBufferBeginInfo *)pPacket->pBeginInfo;
    VkCommandBufferInheritanceInfo *pHinfo = (VkCommandBufferInheritanceInfo *)((pInfo) ? pInfo->pInheritanceInfo : NULL);
    // Save the original RP & FB, then overwrite packet with remapped values
    VkRenderPass savedRP = VK_NULL_HANDLE, *pRP;
    VkFramebuffer savedFB = VK_NULL_HANDLE, *pFB;
    if (pInfo != NULL && pHinfo != NULL) {
        SwapchainImageState& curSwapchainImgStat = swapchainImageStates[curSwapchainHandle];
        savedRP = pHinfo->renderPass;
        savedFB = pHinfo->framebuffer;
        pRP = &(pHinfo->renderPass);
        pFB = &(pHinfo->framebuffer);
        *pRP = m_objMapper.remap_renderpasss(savedRP);
        if (curSwapchainImgStat.traceFramebufferToImageIndex.find(savedFB) != curSwapchainImgStat.traceFramebufferToImageIndex.end() &&
            curSwapchainImgStat.traceFramebufferToRenderpass.find(savedFB) != curSwapchainImgStat.traceFramebufferToRenderpass.end() &&
            m_imageIndex != UINT32_MAX && !g_pReplaySettings->enableVirtualSwapchain) {
            // Use Framebuffer mapped to the image index returned by vkAcquireNextImage()
            bool bFind = false;
            for (auto &e : curSwapchainImgStat.traceFramebufferToImageIndex) {
                if (e.second == m_imageIndex && curSwapchainImgStat.traceFramebufferToRenderpass[e.first] == savedRP) {
                    *pFB = m_objMapper.remap_framebuffers(e.first);
                    bFind = true;
                    break;
                }
            }
            if (!bFind) {
                for (auto &e : curSwapchainImgStat.traceFramebufferToImageIndex) {
                    if (e.second == m_imageIndex && curSwapchainImgStat.traceFramebufferToAttachmentCount.at(e.first) ==
                                                        curSwapchainImgStat.traceFramebufferToAttachmentCount.at(savedFB)) {
                        *pFB = m_objMapper.remap_framebuffers(e.first);
                        bFind = true;
                        break;
                    }
                }
            }
            if (!bFind) {
                *pFB = m_objMapper.remap_framebuffers(savedFB);
            }
        } else {
            *pFB = m_objMapper.remap_framebuffers(savedFB);
        }
    }
    replayResult = m_vkDeviceFuncs.BeginCommandBuffer(remappedCommandBuffer, pPacket->pBeginInfo);
    if (pInfo != NULL && pHinfo != NULL) {
        pHinfo->renderPass = savedRP;
        pHinfo->framebuffer = savedFB;
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkWaitForFences(packet_vkWaitForFences *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    uint32_t i;

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkWaitForFences() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkFence *pFence = (VkFence *)pPacket->pFences;
    for (i = 0; i < pPacket->fenceCount; i++) {
        (*(pFence + i)) = m_objMapper.remap_fences((*(pPacket->pFences + i)));
        if (*(pFence + i) == VK_NULL_HANDLE) {
            vktrace_LogError("Skipping vkWaitForFences() due to invalid remapped VkFence.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
    }

    if (g_pReplaySettings->forceSyncImgIdx) {
        for (i = 0; i < pPacket->fenceCount; i++) {
            if (acquireFenceToFSIIFence.find(*(pFence + i)) != acquireFenceToFSIIFence.end()) {
                *(pFence + i) = acquireFenceToFSIIFence[*(pFence + i)];
            }
        }
    }

    if (pPacket->result == VK_SUCCESS) {
        replayResult = m_vkDeviceFuncs.WaitForFences(remappedDevice, pPacket->fenceCount, pFence, pPacket->waitAll,
                                                     UINT64_MAX);  // mean as long as possible
    } else {
        if (pPacket->result == VK_TIMEOUT) {
            replayResult = m_vkDeviceFuncs.WaitForFences(remappedDevice, pPacket->fenceCount, pFence, pPacket->waitAll, 0);
        } else {
            replayResult =
                m_vkDeviceFuncs.WaitForFences(remappedDevice, pPacket->fenceCount, pFence, pPacket->waitAll, pPacket->timeout);
        }
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkWaitSemaphoresKHR(packet_vkWaitSemaphoresKHR* pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in WaitSemaphoresKHR() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    // No need to remap timeout
    for (int i = 0; i < pPacket->pWaitInfo->semaphoreCount; i++) {
        const_cast<VkSemaphore*>(pPacket->pWaitInfo->pSemaphores)[i] = m_objMapper.remap_semaphores(pPacket->pWaitInfo->pSemaphores[i]);
    }
    replayResult = m_vkDeviceFuncs.WaitSemaphoresKHR(remappeddevice, pPacket->pWaitInfo, pPacket->timeout);
    return replayResult;
}


bool vkReplay::getReplayMemoryTypeIdx(VkDevice traceDevice, VkDevice replayDevice, uint32_t traceIdx,
                                      VkMemoryRequirements *memRequirements, uint32_t *pReplayIdx) {
    VkPhysicalDevice tracePhysicalDevice;
    VkPhysicalDevice replayPhysicalDevice;
    uint32_t i;

    if (tracePhysicalDevices.find(traceDevice) == tracePhysicalDevices.end() ||
        replayPhysicalDevices.find(replayDevice) == replayPhysicalDevices.end()) {
        vktrace_LogError("Failed to find physical device for getReplayMemoryTypeIdx()");
        return false;
    }

    tracePhysicalDevice = tracePhysicalDevices[traceDevice];
    replayPhysicalDevice = replayPhysicalDevices[replayDevice];

    if (min(traceMemoryProperties[tracePhysicalDevice].memoryTypeCount,
            replayMemoryProperties[replayPhysicalDevice].memoryTypeCount) == 0) {
        vktrace_LogError("No cached memory types (%u replay, %u traced) for vkAllocateMemory",
                         (unsigned)traceMemoryProperties[tracePhysicalDevice].memoryTypeCount,
                         (unsigned)replayMemoryProperties[replayPhysicalDevice].memoryTypeCount);
        return false;
    }

    // Search for an exact match from set of bits in memoryRequirements->memoryTypeBits
    for (i = 0; i < min(traceMemoryProperties[tracePhysicalDevice].memoryTypeCount,
                        replayMemoryProperties[replayPhysicalDevice].memoryTypeCount);
         i++) {
        if (((1 << i) & memRequirements->memoryTypeBits) &&
            traceMemoryProperties[tracePhysicalDevice].memoryTypes[traceIdx].propertyFlags ==
                replayMemoryProperties[replayPhysicalDevice].memoryTypes[i].propertyFlags) {
            *pReplayIdx = i;
            return true;
        }
    }

    // Didn't find an exact match, search for a superset
    // from set of bits in memoryRequirements->memoryTypeBits
    for (i = 0; i < min(traceMemoryProperties[tracePhysicalDevice].memoryTypeCount,
                        replayMemoryProperties[replayPhysicalDevice].memoryTypeCount);
         i++) {
        if (((1 << i) & memRequirements->memoryTypeBits) &&
            traceMemoryProperties[tracePhysicalDevice].memoryTypes[traceIdx].propertyFlags ==
                (traceMemoryProperties[tracePhysicalDevice].memoryTypes[traceIdx].propertyFlags &
                 replayMemoryProperties[replayPhysicalDevice].memoryTypes[i].propertyFlags)) {
            *pReplayIdx = i;
            return true;
        }
    }

    // Didn't find a superset, search for mem type with both HOST_VISIBLE and HOST_COHERENT set
    // from set of bits in memoryRequirements->memoryTypeBits
    for (i = 0; i < replayMemoryProperties[replayPhysicalDevice].memoryTypeCount; i++) {
        if (((1 << i) & memRequirements->memoryTypeBits) &&
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &
                replayMemoryProperties[replayPhysicalDevice].memoryTypes[i].propertyFlags) {
            *pReplayIdx = i;
            return true;
        }
    }

    // At last, search for mem type with DEVICE_LOCAL set
    // from set of bits in memoryRequirements->memoryTypeBits
    for (i = 0; i < replayMemoryProperties[replayPhysicalDevice].memoryTypeCount; i++) {
        if (((1 << i) & memRequirements->memoryTypeBits) &&
            (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT & replayMemoryProperties[replayPhysicalDevice].memoryTypes[i].propertyFlags)) {
            *pReplayIdx = i;
            return true;
        }
    }

    vktrace_LogError("Could not determine memory type during vkAllocateMemory!");

    return false;
}

bool vkReplay::modifyMemoryTypeIndexInAllocateMemoryPacket(VkDevice remappedDevice, packet_vkAllocateMemory *pPacket) {
    bool rval = false;
    uint32_t replayMemTypeIndex;
    vktrace_trace_packet_header *pPacketHeader1;
    VkDeviceMemory traceAllocateMemoryRval = VK_NULL_HANDLE;
    bool foundBindMem;
    VkDeviceSize bimMemoryOffset;
    packet_vkFreeMemory *pFreeMemoryPacket;
    VkMemoryRequirements memRequirements;
    size_t n, amIdx;
    VkDeviceSize replayAllocationSize;
    static size_t amSearchPos = 0;
    VkImage remappedImage = VK_NULL_HANDLE;
    size_t bindMemIdx;
    VkImage bindMemImage = VK_NULL_HANDLE;
    bool doDestroyImage = false;

    // Should only be here if we are in compatibility mode, have a valid portability table,  and the replay platform
    // does not match the trace platform
    assert(g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch());

    // First find this vkAM call in portabilityTablePackets
    // In order to speed up the search, we start searching at amSearchPos,
    // wrapping the search back to 0.
    pPacket->header = (vktrace_trace_packet_header *)((PBYTE)pPacket - sizeof(vktrace_trace_packet_header));
    for (n = 0, amIdx = amSearchPos; n < portabilityTablePackets.size(); n++, amIdx++) {
        if (amIdx == portabilityTablePackets.size())
            // amIdx is past the end of the array, wrap back to the start of the array
            amIdx = 0;
        pPacketHeader1 = (vktrace_trace_packet_header *)portabilityTablePackets[amIdx];
        if (pPacketHeader1->global_packet_index == pPacket->header->global_packet_index &&
            pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkAllocateMemory) {
            // Found it
            // Save the vkAM return value from the trace file
            traceAllocateMemoryRval = *pPacket->pMemory;
            // Save the index where we will start the next search for vkAM
            amSearchPos = amIdx + 1;
            break;
        }
    }
    if (n == portabilityTablePackets.size()) {
        // Didn't find the current vkAM packet, something is wrong with the trace file.
        // Just use the index from the trace file and attempt to continue.
        vktrace_LogError("Replay of vkAllocateMemory() failed, trace file may be corrupt.");
        goto out;
    }

    // Search forward from amIdx for vkBIM/vkBBM/vkBIM2/vkBBM2 call that binds this memory.
    // If we don't find one, generate an error and do the best we can.
    foundBindMem = false;
    for (size_t i = amIdx + 1; !foundBindMem && i < portabilityTablePackets.size(); i++) {
        pPacketHeader1 = (vktrace_trace_packet_header *)portabilityTablePackets[i];
        if (pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkFreeMemory) {
            pFreeMemoryPacket = (packet_vkFreeMemory *)(pPacketHeader1->pBody);
            if (pFreeMemoryPacket->memory == traceAllocateMemoryRval) {
                // Found a free of this memory, end the forward search
                vktrace_LogWarning("Memory allocated by vkAllocateMemory is not used.");
                break;
            }
        }

        if (pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2KHR ||
            pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindBufferMemory2KHR ||
            pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2 ||
            pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindBufferMemory2 ||
            pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindTensorMemoryEXT) {
            // Search the memory bind list in vkBIM2/vkBBM2 packet for traceAllocateMemoryRval
            packet_vkBindImageMemory2KHR *pBim2Packet = (packet_vkBindImageMemory2KHR *)(pPacketHeader1->pBody);
            for (uint32_t bindCounter = 0; bindCounter < pBim2Packet->bindInfoCount; bindCounter++) {
                if (pBim2Packet->pBindInfos[bindCounter].memory == traceAllocateMemoryRval) {
                    // Found a bind
                    if (pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2KHR || pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2)
                        remappedImage = m_objMapper.remap_images(pBim2Packet->pBindInfos[bindCounter].image);
                    else
                        remappedImage = (VkImage)m_objMapper.remap_buffers((VkBuffer)pBim2Packet->pBindInfos[bindCounter].image);

                    if ((pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2KHR || pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2)
                        && remappedImage && replayImageToTiling[remappedImage] == VK_IMAGE_TILING_OPTIMAL) {
                        // Skip optimal tiling image
                        remappedImage = VK_NULL_HANDLE;
                    } else {
                        foundBindMem = true;
                        bindMemIdx = i;
                        bindMemImage = pBim2Packet->pBindInfos[bindCounter].image;
                        bimMemoryOffset = pBim2Packet->pBindInfos[bindCounter].memoryOffset;
                        break;
                    }
                }
            }
        } else if (pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory ||
                   pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindBufferMemory ||
                   pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindWeightsMemoryARM) {
            packet_vkBindImageMemory *bim = (packet_vkBindImageMemory *)(pPacketHeader1->pBody);
            if (traceAllocateMemoryRval == bim->memory) {
                // A vkBIM/vkBBM binds memory allocated by this vkAM call.
                if (pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory)
                    remappedImage = m_objMapper.remap_images(bim->image);
                else
                    remappedImage = (VkImage)m_objMapper.remap_buffers((VkBuffer)bim->image);

                if (pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory && remappedImage &&
                    replayImageToTiling[remappedImage] == VK_IMAGE_TILING_OPTIMAL) {
                    // Skip optimal tiling image
                    remappedImage = VK_NULL_HANDLE;
                } else {
                    foundBindMem = true;
                    bindMemIdx = i;
                    bindMemImage = bim->image;
                    bimMemoryOffset = bim->memoryOffset;
                }

                // Need to implement:
                // Check for uses of the memory returned by this vkAM call in vkQueueBindSparse calls to find
                // an image/buffer that uses the memory.
                // ....
            }
        }
    }

    if (!foundBindMem) {
        // Didn't find vkBind{Image|Buffer}Memory call for this vkAllocateMemory or the memory is allocated for optimal image(s)
        // only.
        // This isn't an error - the memory is either allocated but never used or needs to be skipped (optimal image(s) memory
        // allocation will be done in replaying vkBindImageMemory).
        // So just skip the memory allocation.
        goto out;
    }

    if (!remappedImage) {
        // The CreateImage/Buffer command after the AllocMem command, so the image/buffer hasn't
        // been created yet. Search backwards from the bindMem cmd for the CreateImage/Buffer
        // command and execute it
        // The newly created image/buffer needs to be destroyed after getting image/buffer memory requirements to keep the
        // sequence of API calls in the trace file. The destroy will prevent from creating a buffer too early which may be used
        // unexpectedly in a later call since two buffers may have the same handle if one of them is created after another one
        // being destroyed. e.g. Without destroy, a dstBuffer may be used as srcBuffer unexpectedly in vkCmdCopyBuffer if the
        // dstBuffer's memory is allocated before the creation of the expected srcBuffer with the same buffer handle. (The
        // srcBuffer is created and destroyed before the dstBuffer being created.)
        for (size_t i = bindMemIdx - 1; true; i--) {
            vktrace_trace_packet_header *pCreatePacketFull = (vktrace_trace_packet_header *)portabilityTablePackets[i];
            if (((pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory || pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2 || pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2KHR) && pCreatePacketFull->packet_id == VKTRACE_TPI_VK_vkCreateImage) ||
                ((pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindBufferMemory || pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindBufferMemory2 || pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindBufferMemory2KHR) && pCreatePacketFull->packet_id == VKTRACE_TPI_VK_vkCreateBuffer) ||
                (pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindWeightsMemoryARM && pCreatePacketFull->packet_id == VKTRACE_TPI_VK_vkCreateWeightsARM) ||
                (pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindTensorMemoryEXT && pCreatePacketFull->packet_id == VKTRACE_TPI_VK_vkCreateTensorEXT) ) {
                packet_vkCreateImage *pCreatePacket = (packet_vkCreateImage *)(pCreatePacketFull->pBody);
                if (*(pCreatePacket->pImage) == bindMemImage) {
                    VkResult replayResult;
                    // Create the image/buffer
                    if (pCreatePacketFull->packet_id == VKTRACE_TPI_VK_vkCreateBuffer)
                        replayResult = manually_replay_vkCreateBuffer((packet_vkCreateBuffer *)pCreatePacket);
                    else
                        replayResult = manually_replay_vkCreateImage((packet_vkCreateImage *)pCreatePacket);
                    if (replayResult != VK_SUCCESS) {
                        vktrace_LogError("vkCreateBuffer/Image/TensorEXT/WeightARM failed during vkAllocateMemory()");
                        return false;
                    }
                    if (pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory || pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2 || pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2KHR)
                        remappedImage = m_objMapper.remap_images(bindMemImage);
                    else
                        remappedImage = (VkImage)m_objMapper.remap_buffers((VkBuffer)bindMemImage);
                    doDestroyImage = true;
                    break;
                }
            }
            if (i == amIdx) {
                // This image/buffer is not created before it is bound
                vktrace_LogError("Bad buffer/image in call to vkBindImageMemory/vkBindBuffer");
                return false;
            }
        }
    }

    // Call GIMR/GBMR for the replay image/buffer
    if (pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory ||
        pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2KHR ||
        pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2) {
        if (replayGetImageMemoryRequirements.find(remappedImage) == replayGetImageMemoryRequirements.end()) {
            m_vkDeviceFuncs.GetImageMemoryRequirements(remappedDevice, remappedImage, &memRequirements);
            replayGetImageMemoryRequirements[remappedImage] = memRequirements;
        }
        memRequirements = replayGetImageMemoryRequirements[remappedImage];
    }
    else {
        if (replayGetBufferMemoryRequirements.find((VkBuffer)remappedImage) == replayGetBufferMemoryRequirements.end()) {
            m_vkDeviceFuncs.GetBufferMemoryRequirements(remappedDevice, (VkBuffer)remappedImage, &memRequirements);
            replayGetBufferMemoryRequirements[(VkBuffer)remappedImage] = memRequirements;
        }
        memRequirements = replayGetBufferMemoryRequirements[(VkBuffer)remappedImage];
    }

    replayAllocationSize = memRequirements.size;
    if (bimMemoryOffset > 0) {
        // Do alignment for allocationSize in traced vkBIM/vkBBM
        VkDeviceSize traceAllocationSize = *((VkDeviceSize *)&pPacket->pAllocateInfo->allocationSize);
        VkDeviceSize alignedAllocationSize =
            ((traceAllocationSize + memRequirements.alignment - 1) / memRequirements.alignment) * memRequirements.alignment;

        // Do alignment for memory offset
        replayAllocationSize +=
            ((bimMemoryOffset + memRequirements.alignment - 1) / memRequirements.alignment) * memRequirements.alignment;
        if (alignedAllocationSize != replayAllocationSize) {
            vktrace_LogWarning(
                "alignedAllocationSize: 0x%x does not match replayAllocationSize: 0x%x, traceAllocationSize: 0x%x, "
                "replayAlignment: 0x%x",
                alignedAllocationSize, replayAllocationSize, traceAllocationSize, memRequirements.alignment);
        }
    }

    if (getReplayMemoryTypeIdx(pPacket->device, remappedDevice, pPacket->pAllocateInfo->memoryTypeIndex, &memRequirements,
                               &replayMemTypeIndex)) {
        *((uint32_t *)&pPacket->pAllocateInfo->memoryTypeIndex) = replayMemTypeIndex;
        if (*((VkDeviceSize *)&pPacket->pAllocateInfo->allocationSize) < replayAllocationSize)
            *((VkDeviceSize *)&pPacket->pAllocateInfo->allocationSize) = replayAllocationSize;
        rval = true;
    } else {
        vktrace_LogError("vkAllocateMemory() failed, couldn't find memory type for memoryTypeIndex");
    }

out:
    if (doDestroyImage) {
        // Destroy temporarily created image/buffer and clean up obj map.
        if (pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory || pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory2) {
            m_vkDeviceFuncs.DestroyImage(remappedDevice, remappedImage, NULL);
            m_objMapper.rm_from_images_map(bindMemImage);
            if (replayGetImageMemoryRequirements.find(remappedImage) != replayGetImageMemoryRequirements.end())
                replayGetImageMemoryRequirements.erase(remappedImage);
        }
        else {
            m_vkDeviceFuncs.DestroyBuffer(remappedDevice, (VkBuffer)remappedImage, NULL);
            m_objMapper.rm_from_buffers_map((VkBuffer)bindMemImage);
            if (replayGetBufferMemoryRequirements.find((VkBuffer)remappedImage) != replayGetBufferMemoryRequirements.end())
                replayGetBufferMemoryRequirements.erase((VkBuffer)remappedImage);
        }
    }

    return rval;
}

extern vkReplay* g_pReplayer;

VkResult vkReplay::manually_replay_vkAllocateMemory(packet_vkAllocateMemory *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    devicememoryObj local_mem;

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkAllocateMemory() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (pPacket->result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
        /// ignore the recorded failed call
        vktrace_LogError("Skipping vkAllocateMemory() due to ignore the recorded failed call.");
        return VK_SUCCESS;
    }

    VkDeviceSize origSize = pPacket->pAllocateInfo->allocationSize;
    // VkMemoryAllocateInfo parameter of vkAllocateMemory contains a pNext member.
    // It might points to a VkMemoryDedicatedAllocateInfo structure:
    //     typedef struct VkMemoryDedicatedAllocateInfo {
    //         VkStructureType    sType;
    //         const void*        pNext;
    //         VkImage            image;
    //         VkBuffer           buffer;
    // } VkMemoryDedicatedAllocateInfo;
    // Both "image" and "buffer" need to be remapped here
    if (g_pReplayer != NULL) {
        g_pReplayer->interpret_pnext_handles((void *)pPacket->pAllocateInfo);
    }
    bool bModifyAllocateInfo = false;

#if defined(ANDROID)
    VkImportAndroidHardwareBufferInfoANDROID* importAHWBuf = (VkImportAndroidHardwareBufferInfoANDROID*)find_ext_struct(
                                                                    (const vulkan_struct_header*)pPacket->pAllocateInfo,
                                                                    VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID);
    if (importAHWBuf) {
        // If import android hardware buffer handle is not an offset, it must be associated with a exported memory
        // Consider the parameter as a handle if it is smaller than 4KB (size of the first page frame)
        if ((intptr_t)importAHWBuf->buffer >= (1 << 12)) {
            if (traceAHardwareBufferToReplayAHardwareBuffer.count(importAHWBuf->buffer) != 0) {
                importAHWBuf->buffer =  traceAHardwareBufferToReplayAHardwareBuffer[importAHWBuf->buffer];
            } else {
                vktrace_LogError("Error detected in AllocateMemory() due to invalid remapped AHardwareBuffer.");
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }
        } else {
            PBYTE importBufData =
                (PBYTE)vktrace_trace_packet_interpret_buffer_pointer(pPacket->header, (intptr_t)importAHWBuf->buffer);
            AHardwareBuffer_Desc *ahwbuf_desc = (AHardwareBuffer_Desc *)importBufData;
            ahwbuf_desc->usage = ahwbuf_desc->usage | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
            int ret = AHardwareBuffer_allocate(ahwbuf_desc, &importAHWBuf->buffer);
            if (ret) {
                vktrace_LogError("Allocate android hardware buffer failed with return %d!", ret);
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }
            uint32_t traceStride = ahwbuf_desc->stride;
            AHardwareBuffer_describe(importAHWBuf->buffer, ahwbuf_desc);
            if (traceStride != ahwbuf_desc->stride) {
                vktrace_LogDebug("The stride(%u) of hardware buffer in replayer does not match the stride(%u) in trace",
                                 ahwbuf_desc->stride, traceStride);
            }
            void *ahwbuf_wrt_ptr = NULL;
            ret = AHardwareBuffer_lock(importAHWBuf->buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, NULL, &ahwbuf_wrt_ptr);
            if (ret) {
                vktrace_LogError("AM: Lock the hardware buffer failed !");
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }
            PBYTE psrc = importBufData + sizeof(AHardwareBuffer_Desc);
            PBYTE pdst = (unsigned char *)ahwbuf_wrt_ptr;
            uint32_t trace_stride = traceStride * getAHardwareBufBPP(ahwbuf_desc->format);
            uint32_t retrace_stride = ahwbuf_desc->stride * getAHardwareBufBPP(ahwbuf_desc->format);
            uint32_t width = ahwbuf_desc->width * getAHardwareBufBPP(ahwbuf_desc->format);
            for (unsigned h = 0; h < ahwbuf_desc->height; h++) {
                memcpy(pdst + h * retrace_stride, psrc + h * trace_stride, width);
            }
            AHardwareBuffer_unlock(importAHWBuf->buffer, NULL);
            traceDeviceMemoryToAHWBuf[*pPacket->pMemory] = importAHWBuf->buffer;
            VkAndroidHardwareBufferPropertiesANDROID androidHardwareBufferPropertiesANDROID = {
                VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID, nullptr, 0, 0};
            m_vkDeviceFuncs.GetAndroidHardwareBufferPropertiesANDROID(remappedDevice, importAHWBuf->buffer,
                                                                      &androidHardwareBufferPropertiesANDROID);
            const_cast<VkMemoryAllocateInfo *>(pPacket->pAllocateInfo)->allocationSize =
                androidHardwareBufferPropertiesANDROID.allocationSize;
            bModifyAllocateInfo = true;
        }
    }
#endif
#if defined(PLATFORM_LINUX) && !defined(ANDROID)
    AHardwareBuffer_Desc ahwbuf_desc = {};
    PBYTE importBufData = nullptr;
    VkMemoryRequirements requirement = {};
    VkSubresourceLayout layout = {};
    VkImportMemoryFdInfoKHR importFdInfo = {};
    VkImportAndroidHardwareBufferInfoANDROID* importAHWBuf = (VkImportAndroidHardwareBufferInfoANDROID*)find_ext_struct(
                                                                    (const vulkan_struct_header*)pPacket->pAllocateInfo,
                                                                    VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID);
    if (importAHWBuf != nullptr) {
        if ((intptr_t)importAHWBuf->buffer < (1 << 12)) {
            VkMemoryDedicatedAllocateInfo *pDedicate = (VkMemoryDedicatedAllocateInfo *)find_ext_struct(
                (const vulkan_struct_header *)pPacket->pAllocateInfo, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
            importBufData = (PBYTE)vktrace_trace_packet_interpret_buffer_pointer(pPacket->header, (intptr_t)importAHWBuf->buffer);
            ahwbuf_desc = *((AHardwareBuffer_Desc *)importBufData);
            VkApplicationInfo *pNext = (VkApplicationInfo *)(pPacket->pAllocateInfo->pNext);
            VkApplicationInfo *pParent = pNext;
            while (pNext != nullptr) {
                if (pNext->pNext == (void *)importAHWBuf) {
                    pParent = pNext;
                    break;
                }
                pNext = (VkApplicationInfo *)pNext->pNext;
            }
            if (pParent == (VkApplicationInfo *)(pPacket->pAllocateInfo->pNext)) {
                const_cast<VkMemoryAllocateInfo *>(pPacket->pAllocateInfo)->pNext = nullptr;
            } else {
                pParent->pNext = importAHWBuf->pNext;
            }
            if (pDedicate != nullptr) {
                m_vkDeviceFuncs.GetImageMemoryRequirements(remappedDevice, pDedicate->image, &requirement);
                const_cast<VkMemoryAllocateInfo *>(pPacket->pAllocateInfo)->allocationSize = requirement.size;
                VkImageSubresource imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
                m_vkDeviceFuncs.GetImageSubresourceLayout(remappedDevice, pDedicate->image, &imageSubresource, &layout);
            }
            bModifyAllocateInfo = true;
        }
#if defined(ARM_LINUX_64)
        // Secondly import the created dma buffer fd when the content intends to import android hardware buffer
        else {
            // AHardwarebuffer will not be automatically remapped during interpration
            int fd = traceAHardwareBufferToDmaBufferFd.at((AHardwareBuffer *)importAHWBuf->buffer);
            openedDmaBufferFd.erase(fd);
            // Modify the pnext chain
            importFdInfo = {VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, NULL, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd};
            add_ext_struct((vulkan_struct_header *)pPacket->pAllocateInfo, (vulkan_struct_header *)&importFdInfo);
            delete_ext_struct((vulkan_struct_header *)pPacket->pAllocateInfo,
                              VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID);
            bModifyAllocateInfo = true;
        }
#endif
    }
#endif
#if defined(ARM_LINUX_64)
    VkMemoryDedicatedAllocateInfo *dedicatedAllocInfo = (VkMemoryDedicatedAllocateInfo *)find_ext_struct(
        (const vulkan_struct_header *)pPacket->pAllocateInfo, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
    // Do the first dma buffer import when the content intends to export android hardware buffer
    if (dedicatedAllocInfo != nullptr &&
        find_ext_struct((const vulkan_struct_header *)pPacket->pAllocateInfo,
                        VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID) == nullptr &&
        find_ext_struct((const vulkan_struct_header *)pPacket->pAllocateInfo, VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO) !=
            nullptr) {
        // No need to remap image handle in pnext chain because it is already remapped during interpration
        assert(std::find(hardwarebufferImage.begin(), hardwarebufferImage.end(), dedicatedAllocInfo->image) !=
               hardwarebufferImage.end());
        // Allocate a DMA buffer
        VkMemoryRequirements requirement = externalImageMemoryRequirements[dedicatedAllocInfo->image];
        int fd = allocateDmaBuffer(requirement.size, requirement.alignment);
        // DDK will close the original fd after allocation, so we only need to store the duplicated fd
        int dupFd = dup(fd);
        openedDmaBufferFd.insert(dupFd);
        traceDeviceMemoryToDmaBufferFd[*(pPacket->pMemory)] = dupFd;
        // Modify the pnext chain
        importFdInfo = {VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, NULL, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd};
        add_ext_struct((vulkan_struct_header *)pPacket->pAllocateInfo, (vulkan_struct_header *)&importFdInfo);
        delete_ext_struct((vulkan_struct_header *)pPacket->pAllocateInfo, VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO);
        bModifyAllocateInfo = true;
    }
#endif

    if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() && !bModifyAllocateInfo) {
        traceDeviceMemoryToMemoryTypeIndex[*(pPacket->pMemory)] = pPacket->pAllocateInfo->memoryTypeIndex;
    }

    static size_t amSearchPos = 0;
    // Map memory type index from trace platform to memory type index on replay platform.
    // Only do this if compatibility mode is enabled, we have a portability table, and
    // the trace and replay platforms are not identical.
    bool doAllocate = true;
    if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() && !bModifyAllocateInfo)
        doAllocate = modifyMemoryTypeIndexInAllocateMemoryPacket(remappedDevice, pPacket);
    if (doAllocate) {
        VkMemoryAllocateFlagsInfo *allocateFlagInfo = (VkMemoryAllocateFlagsInfo*)find_ext_struct((const vulkan_struct_header*)pPacket->pAllocateInfo->pNext, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO);
        if (allocateFlagInfo == nullptr) {
            allocateFlagInfo = (VkMemoryAllocateFlagsInfo*)find_ext_struct((const vulkan_struct_header*)pPacket->pAllocateInfo->pNext, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR);
        }

        VkMemoryOpaqueCaptureAddressAllocateInfo *pCaptureAddressAllocateInfo = (VkMemoryOpaqueCaptureAddressAllocateInfo*)find_ext_struct((const vulkan_struct_header*)pPacket->pAllocateInfo->pNext, VK_STRUCTURE_TYPE_MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO);
        if (pCaptureAddressAllocateInfo == nullptr) {
            pCaptureAddressAllocateInfo = (VkMemoryOpaqueCaptureAddressAllocateInfo*)find_ext_struct((const vulkan_struct_header*)pPacket->pAllocateInfo->pNext, VK_STRUCTURE_TYPE_MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO_KHR);
        }

        if (allocateFlagInfo != nullptr && (allocateFlagInfo->flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT)) {
            traceMemoryToAllocateFlag[*(pPacket->pMemory)] = allocateFlagInfo->flags;
        }

        if (allocateFlagInfo != nullptr && (allocateFlagInfo->flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_KHR)) {
            auto it = replayDeviceToFeatureSupport.find(remappedDevice);
            if (it != replayDeviceToFeatureSupport.end() && it->second.bufferDeviceAddressCaptureReplay == 0) {
                allocateFlagInfo->flags &= ~VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_KHR;
                vktrace_LogDebug("The device doesn't support bufferDeviceAddressCaptureReplay feature.");
                if (pCaptureAddressAllocateInfo != nullptr) {
                    pCaptureAddressAllocateInfo->opaqueCaptureAddress = 0;
                }
            }
        }
        // Search in the portability table to find the next vkBindBufferMemory whose memory is the same with pPacket->pMemory.
        // If the corresponding buffer is used to store an acceleration structure, we might need to adjust its size.
        if (g_hasAsApi) {
            for (unsigned amIdx = amSearchPos; amIdx < portabilityTablePackets.size(); amIdx++) {
                vktrace_trace_packet_header * pPacketHeader1 = (vktrace_trace_packet_header *)portabilityTablePackets[amIdx];
                if (pPacketHeader1->global_packet_index == pPacket->header->global_packet_index) {
                    amSearchPos = amIdx;
                }
                if (pPacketHeader1->global_packet_index > pPacket->header->global_packet_index && pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindBufferMemory) {
                    packet_vkBindBufferMemory *pBody = (packet_vkBindBufferMemory *)pPacketHeader1->pBody;
                    if (*pPacket->pMemory == pBody->memory) {
                        VkBuffer remappedBuffer = m_objMapper.remap_buffers(pBody->buffer);
                        auto it = replayGetBufferMemoryRequirements.find(remappedBuffer);
                        if (it != replayGetBufferMemoryRequirements.end() && it->second.size > pPacket->pAllocateInfo->allocationSize) {
                            const_cast<VkMemoryAllocateInfo*>(pPacket->pAllocateInfo)->allocationSize = it->second.size;
                        }
                        break;
                    }
                }
                else if (pPacketHeader1->global_packet_index > pPacket->header->global_packet_index && pPacketHeader1->packet_id == VKTRACE_TPI_VK_vkBindImageMemory) {
                    packet_vkBindImageMemory *pBody = (packet_vkBindImageMemory *)pPacketHeader1->pBody;
                    if (*pPacket->pMemory == pBody->memory) {
                        break;
                    }
                }
            }
        }
        rtHandler->addSbtCandidateMemory(*pPacket->pMemory, origSize);
        replayResult = m_vkDeviceFuncs.AllocateMemory(remappedDevice, pPacket->pAllocateInfo, NULL, &local_mem.replayDeviceMemory);
    }
    if (replayResult == VK_SUCCESS) {
        local_mem.pGpuMem = new (gpuMemory);
        if (local_mem.pGpuMem) local_mem.pGpuMem->setAllocInfo(pPacket->pAllocateInfo, false);
        local_mem.traceDeviceMemory = *(pPacket->pMemory);
        m_objMapper.add_to_devicememorys_map(*(pPacket->pMemory), local_mem);
        replayDeviceMemoryToDevice[local_mem.replayDeviceMemory] = remappedDevice;
#if defined(ANDROID)
        // Mark the memory object if it is exported to external memory
        VkExportMemoryAllocateInfo* exportInfo = (VkExportMemoryAllocateInfo*)find_ext_struct(
                                                (const vulkan_struct_header*)pPacket->pAllocateInfo,
                                                VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO);
        if (exportInfo) {
            // Since the OpenXR apps only run on Android platform, we only need to check android hardware buffer bit
            assert(exportInfo->handleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID);
            exportedReplayDeviceMemory.insert(local_mem.replayDeviceMemory);
        }
#endif
#if defined(PLATFORM_LINUX) && !defined(ANDROID)
        if (importAHWBuf != nullptr && (intptr_t)importAHWBuf->buffer < (1 << 12)) {
            uint32_t trace_stride = ahwbuf_desc.stride * getAHardwareBufBPP(ahwbuf_desc.format);
            uint32_t width = ahwbuf_desc.width * getAHardwareBufBPP(ahwbuf_desc.format);
            uint32_t retrace_stride = layout.rowPitch;
            void* pdata = nullptr;
            int res = m_vkDeviceFuncs.MapMemory(remappedDevice, local_mem.replayDeviceMemory, 0, requirement.size, 0, &pdata);

            if (res != VK_SUCCESS) {
                vktrace_LogError("MapMemory failed. res = %d", res);
            }
            PBYTE psrc = importBufData + sizeof(AHardwareBuffer_Desc);
            PBYTE pdst = (unsigned char*)pdata;
            memset(pdata, 0, requirement.size);
            for (unsigned h = 0; h < ahwbuf_desc.height ; h++) {
                memcpy(pdst + h * retrace_stride, psrc + h * trace_stride, width);
            }

            // synchronization
            VkMappedMemoryRange memoryRange{};
            memoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            memoryRange.memory = local_mem.replayDeviceMemory;
            memoryRange.offset = 0;
            memoryRange.size = requirement.size;
            m_vkDeviceFuncs.FlushMappedMemoryRanges(remappedDevice, 1, &memoryRange);

            m_vkDeviceFuncs.UnmapMemory(remappedDevice, local_mem.replayDeviceMemory);
        }
#endif
    } else {
        if (doAllocate) {
            vktrace_LogError("Allocate Memory 0x%llX failed with result = 0x%X", *(pPacket->pMemory), replayResult);
        } else {
            traceSkippedDeviceMemories.insert(*(pPacket->pMemory));
            vktrace_LogWarning("Skipping Allocate Memory 0x%llX, it is not bound to any image/buffer", *(pPacket->pMemory));
            return VK_SUCCESS;
        }
    }
    return replayResult;
}

void vkReplay::manually_replay_vkFreeMemory(packet_vkFreeMemory *pPacket) {
    if (pPacket->memory == VK_NULL_HANDLE) {
        return;
    }

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkFreeMemory() due to invalid remapped VkDevice.");
        return;
    }
    auto it = traceAddressToTraceMemoryMapInfo.begin();
    while (it != traceAddressToTraceMemoryMapInfo.end()) {
        if (it->second.traceMemory == pPacket->memory) {
            it = traceAddressToTraceMemoryMapInfo.erase(it);
        } else {
            it++;
        }
    }

    if (traceMemoryToAllocateFlag.find(pPacket->memory) != traceMemoryToAllocateFlag.end())
        traceMemoryToAllocateFlag.erase(pPacket->memory);

    if (!m_objMapper.devicememory_exists(pPacket->memory)) {
        if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() &&
            traceSkippedDeviceMemories.find(pPacket->memory) != traceSkippedDeviceMemories.end()) {
            traceSkippedDeviceMemories.erase(pPacket->memory);
            vktrace_LogDebug("Skipping vkFreeMemory() due to VkDeviceMemory 0x%llX skipped in vkAllocateMemory.", pPacket->memory);
            return;
        }
        vktrace_LogWarning("Skipping vkFreeMemory() due to invalid remapped VkDeviceMemory.");
        return;
    }

    devicememoryObj local_mem;
    local_mem = m_objMapper.find_devicememory(pPacket->memory);
    // TODO how/when to free pendingAlloc that did not use and existing devicememoryObj
    m_vkDeviceFuncs.FreeMemory(remappedDevice, local_mem.replayDeviceMemory, NULL);

    if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() &&
        traceDeviceMemoryToMemoryTypeIndex.find(pPacket->memory) != traceDeviceMemoryToMemoryTypeIndex.end()) {
        traceDeviceMemoryToMemoryTypeIndex.erase(pPacket->memory);
    }

    delete local_mem.pGpuMem;
    m_objMapper.rm_from_devicememorys_map(pPacket->memory);
#if defined(ANDROID)
    if (traceDeviceMemoryToAHWBuf.find(pPacket->memory) != traceDeviceMemoryToAHWBuf.end()) {
        AHardwareBuffer_release(traceDeviceMemoryToAHWBuf[pPacket->memory]);
        traceDeviceMemoryToAHWBuf.erase(pPacket->memory);
    }
#endif
}

VkResult vkReplay::manually_replay_vkMapMemory(packet_vkMapMemory *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkMapMemory() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (!m_objMapper.devicememory_exists(pPacket->memory)) {
        if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() &&
            traceSkippedDeviceMemories.find(pPacket->memory) != traceSkippedDeviceMemories.end()) {
            vktrace_LogDebug("Skipping vkMapMemory() due to VkDeviceMemory 0x%llX skipped in vkAllocateMemory.", pPacket->memory);
            return VK_SUCCESS;
        }
        vktrace_LogError("Skipping vkMapMemory() due to invalid remapped VkDeviceMemory.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    devicememoryObj local_mem = m_objMapper.find_devicememory(pPacket->memory);
    void *pData;
    if (!local_mem.pGpuMem->isPendingAlloc()) {
        replayResult = m_vkDeviceFuncs.MapMemory(remappedDevice, local_mem.replayDeviceMemory, pPacket->offset, pPacket->size,
                                                 pPacket->flags, &pData);
        if (replayResult == VK_SUCCESS) {
            if (local_mem.pGpuMem) {
                local_mem.pGpuMem->setMemoryMapRange(pData, pPacket->size, pPacket->offset, false);
            }
            if (g_hasAsApi) {
                traceMemoryMapInfo MapInfo = {.traceMemory = pPacket->memory, .offset = pPacket->offset, .size = pPacket->size, .flags = pPacket->flags};
                traceAddressToTraceMemoryMapInfo[*(pPacket->ppData)] = MapInfo;
                replayMemoryToMapAddress[local_mem.replayDeviceMemory] = pData;
            }
        }
    } else {
        if (local_mem.pGpuMem) {
            local_mem.pGpuMem->setMemoryMapRange(NULL, pPacket->size, pPacket->offset, true);
        }
    }
    return replayResult;
}

void vkReplay::remapInstanceBufferDeviceAddressInMapedMemory(VkDevice remappedDevice, VkDeviceMemory traceMemory, VkDeviceMemory remappedMemory, const void *pSrcData, VkDeviceSize size)
{
    bool supportASCaptureReplay = false;
    auto it_fea = replayDeviceToFeatureSupport.find(remappedDevice);
    if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.accelerationStructureCaptureReplay) {
        supportASCaptureReplay = true;
    }

    if (m_pFileHeader->bit_flags & VKTRACE_RQ_POSTPROCESSED_BIT)
        return;

    if (supportASCaptureReplay || !g_hasAsApi || !pSrcData) {
        return;
    }

    if (remapReplayPatternDeviceAddresssInMapedMemory(remappedDevice, pSrcData, size)) {
        return;
    }

    bool findAsReference = false;
    for (auto iter = replayBufferToReplayDeviceMemory.begin(); iter != replayBufferToReplayDeviceMemory.end(); iter++) {
        if (remappedMemory == iter->second) {
            if (replayBufferToUsageFlag.find(iter->first) != replayBufferToUsageFlag.end()) {
                    findAsReference = true;
                    break;
            }
        }
    }

    if (traceMemoryToAllocateFlag.find(traceMemory) != traceMemoryToAllocateFlag.end()) {
        findAsReference = true;
    }

    if (!findAsReference) {
        return;
    }

    bool remapAsReference = false;
    VkAccelerationStructureInstanceKHR* pAsInstance = (VkAccelerationStructureInstanceKHR *)pSrcData;
    for (unsigned j = 0; j < size / sizeof(VkAccelerationStructureInstanceKHR); ++j) {
        auto it = traceDeviceAddrToReplayDeviceAddr4AS.find(pAsInstance[j].accelerationStructureReference);
        if (it != traceDeviceAddrToReplayDeviceAddr4AS.end()) {
            pAsInstance[j].accelerationStructureReference = it->second.replayDeviceAddr;
            remapAsReference = true;
        }
    }

    if (remapAsReference) {
        VkMappedMemoryRange memoryRange = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, remappedMemory, 0, VK_WHOLE_SIZE};
        m_vkDeviceFuncs.FlushMappedMemoryRanges(remappedDevice, 1, &memoryRange);
    }
}

void vkReplay::manually_replay_vkUnmapMemory(packet_vkUnmapMemory *pPacket) {
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkUnmapMemory() due to invalid remapped VkDevice.");
        return;
    }

    if (!m_objMapper.devicememory_exists(pPacket->memory)) {
        if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() &&
            traceSkippedDeviceMemories.find(pPacket->memory) != traceSkippedDeviceMemories.end()) {
            vktrace_LogDebug("Skipping vkUnmapMemory() due to VkDeviceMemory 0x%llX skipped in vkAllocateMemory.", pPacket->memory);
            return;
        }
        vktrace_LogError("Skipping vkUnmapMemory() due to invalid remapped VkDeviceMemory.");
        return;
    }

    devicememoryObj local_mem = m_objMapper.find_devicememory(pPacket->memory);
    if (!local_mem.pGpuMem->isPendingAlloc()) {
        if (local_mem.pGpuMem) {
            remapInstanceBufferDeviceAddressInMapedMemory(remappedDevice, pPacket->memory, local_mem.replayDeviceMemory, pPacket->pData, local_mem.pGpuMem->getMemoryMapSize());
            if (pPacket->pData)
                local_mem.pGpuMem->copyMappingData(pPacket->pData, true, 0, 0);  // copies data from packet into memory buffer
        }
        m_vkDeviceFuncs.UnmapMemory(remappedDevice, local_mem.replayDeviceMemory);
        auto it = replayMemoryToMapAddress.find(local_mem.replayDeviceMemory);
        if (it != replayMemoryToMapAddress.end()) {
            replayMemoryToMapAddress.erase(it);
        }
    } else {
        if (local_mem.pGpuMem) {
            unsigned char *pBuf = (unsigned char *)vktrace_malloc(local_mem.pGpuMem->getMemoryMapSize());
            if (!pBuf) {
                vktrace_LogError("vkUnmapMemory() malloc failed.");
            }
            local_mem.pGpuMem->setMemoryDataAddr(pBuf);
            local_mem.pGpuMem->copyMappingData(pPacket->pData, true, 0, 0);
        }
    }
}

bool vkReplay::isMapMemoryAddress(const void* traceHostAddress) {
    bool isMapAddress = false;
    uint64_t address = (uint64_t)traceHostAddress;
    for(auto&it : traceAddressToTraceMemoryMapInfo) {
        uint64_t start = (uint64_t)(it.first);
        if (address >= start && address < start + it.second.size ) {
            isMapAddress = true;
            break;
        }
    }
    return isMapAddress;
}

void* vkReplay::fromTraceAddr2ReplayAddr(VkDevice replayDevice, const void* traceAddress) {
    traceMemoryMapInfo traceMemoryInfo = {};
    uint64_t address = (uint64_t)traceAddress;
    uint32_t gap = 0;
    for(auto&it : traceAddressToTraceMemoryMapInfo) {
        uint64_t start = (uint64_t)(it.first);
        if (address < start + it.second.size && address >= start) {
            traceMemoryInfo = it.second;
            gap = address - start;
            break;
        }
    }
    if (traceMemoryInfo.traceMemory == VK_NULL_HANDLE) {
        vktrace_LogError("Can't find the trace address %p.", traceAddress);
        return nullptr;
    }
    VkDeviceMemory replayMemory = m_objMapper.remap_devicememorys(traceMemoryInfo.traceMemory);
    if (replayMemory == VK_NULL_HANDLE) {
        vktrace_LogError("Can't find replay memory");
        return nullptr;
    }
    void* replayAddress = nullptr;
    auto it = replayMemoryToMapAddress.find(replayMemory);
    if (it != replayMemoryToMapAddress.end()) {
        replayAddress = (void*)((uint64_t)(it->second) + gap);
    } else {
        m_vkDeviceFuncs.MapMemory(replayDevice, replayMemory, traceMemoryInfo.offset, traceMemoryInfo.size, traceMemoryInfo.flags, &replayAddress);
        replayAddress = (void*)(uint64_t(replayAddress) + gap);
    }
    return replayAddress;
}

void vkReplay::manually_replay_vkGetAccelerationStructureBuildSizesKHR(packet_vkGetAccelerationStructureBuildSizesKHR *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetAccelerationStructureBuildSizesKHR() due to invalid remapped VkDevice.");
        return;
    }
    if (g_pReplaySettings->fDevBuild2HostBuild == TRUE) {
        pPacket->buildType = VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_OR_DEVICE_KHR;
    }
    VkAccelerationStructureKHR replaySrcAS = m_objMapper.remap_accelerationstructurekhrs(pPacket->pBuildInfo->srcAccelerationStructure);
    VkAccelerationStructureKHR replayDstAS = m_objMapper.remap_accelerationstructurekhrs(pPacket->pBuildInfo->dstAccelerationStructure);
    if (pPacket->pBuildInfo->srcAccelerationStructure != VK_NULL_HANDLE && replaySrcAS != VK_NULL_HANDLE) {
        const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pBuildInfo)->srcAccelerationStructure = replaySrcAS;
    }
    if (pPacket->pBuildInfo->dstAccelerationStructure != VK_NULL_HANDLE && replayDstAS != VK_NULL_HANDLE) {
        const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pBuildInfo)->dstAccelerationStructure = replayDstAS;
    }

    // No need to remap buildType
    // No need to remap pMaxPrimitiveCounts
    // No need to remap pSizeInfo
    VkAccelerationStructureBuildSizesInfoKHR traceASBuildSize = *(pPacket->pSizeInfo);
    m_vkDeviceFuncs.GetAccelerationStructureBuildSizesKHR(remappeddevice, pPacket->buildType, pPacket->pBuildInfo, pPacket->pMaxPrimitiveCounts, pPacket->pSizeInfo);

    if (traceASBuildSize.accelerationStructureSize) {
        auto it = traceASSizeToReplayASBuildSizes.find(traceASBuildSize.accelerationStructureSize);
        if (it != traceASSizeToReplayASBuildSizes.end()) {
            if (it->second.accelerationStructureSize < pPacket->pSizeInfo->accelerationStructureSize) {
                it->second.accelerationStructureSize = pPacket->pSizeInfo->accelerationStructureSize;
            }
            if (it->second.updateScratchSize < pPacket->pSizeInfo->updateScratchSize) {
                it->second.updateScratchSize = pPacket->pSizeInfo->updateScratchSize;
            }
            if (it->second.buildScratchSize < pPacket->pSizeInfo->buildScratchSize) {
                it->second.buildScratchSize = pPacket->pSizeInfo->buildScratchSize;
            }
        } else {
            traceASSizeToReplayASBuildSizes[traceASBuildSize.accelerationStructureSize] = *(pPacket->pSizeInfo);
        }
    }
    if (traceASBuildSize.updateScratchSize) {
        auto it2 = traceUpdateSizeToReplayASBuildSizes.find(traceASBuildSize.updateScratchSize);
        if (it2 != traceUpdateSizeToReplayASBuildSizes.end()) {
            if (it2->second.accelerationStructureSize < pPacket->pSizeInfo->accelerationStructureSize) {
                it2->second.accelerationStructureSize = pPacket->pSizeInfo->accelerationStructureSize;
            }
            if (it2->second.updateScratchSize < pPacket->pSizeInfo->updateScratchSize) {
                it2->second.updateScratchSize = pPacket->pSizeInfo->updateScratchSize;
            }
            if (it2->second.buildScratchSize < pPacket->pSizeInfo->buildScratchSize) {
                it2->second.buildScratchSize = pPacket->pSizeInfo->buildScratchSize;
            }
        } else {
            traceUpdateSizeToReplayASBuildSizes[traceASBuildSize.updateScratchSize] = *(pPacket->pSizeInfo);
        }
    }
    if (traceASBuildSize.buildScratchSize) {
        auto it3 = traceBuildSizeToReplayASBuildSizes.find(traceASBuildSize.buildScratchSize);
        if (it3 != traceBuildSizeToReplayASBuildSizes.end()) {
            if (it3->second.accelerationStructureSize < pPacket->pSizeInfo->accelerationStructureSize) {
                it3->second.accelerationStructureSize = pPacket->pSizeInfo->accelerationStructureSize;
            }
            if (it3->second.updateScratchSize < pPacket->pSizeInfo->updateScratchSize) {
                it3->second.updateScratchSize = pPacket->pSizeInfo->updateScratchSize;
            }
            if (it3->second.buildScratchSize < pPacket->pSizeInfo->buildScratchSize) {
                it3->second.buildScratchSize = pPacket->pSizeInfo->buildScratchSize;
            }
        } else {
            traceBuildSizeToReplayASBuildSizes[traceASBuildSize.buildScratchSize] = *(pPacket->pSizeInfo);
        }
    }
}

VkResult vkReplay::manually_replay_vkCreateAccelerationStructureKHR(packet_vkCreateAccelerationStructureKHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkAccelerationStructureKHR local_pAccelerationStructure;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CreateAccelerationStructureKHR() due to invalid remapped VkDevice.");
        return replayResult;
    }
    // No need to remap pAllocator
    VkBuffer remappedBuffer = m_objMapper.remap_buffers(pPacket->pCreateInfo->buffer);
    if (pPacket->pCreateInfo->buffer != VK_NULL_HANDLE && remappedBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CreateAccelerationStructureKHR() due to invalid remapped vkBuffer.");
        return replayResult;
    }
    const_cast<VkAccelerationStructureCreateInfoKHR*>(pPacket->pCreateInfo)->buffer = remappedBuffer;
    VkDeviceSize createInfoSize = pPacket->pCreateInfo->size;
    auto itAs = replayBufferToASBuildSizes.find(remappedBuffer);
    if (itAs != replayBufferToASBuildSizes.end()) {
        const_cast<VkAccelerationStructureCreateInfoKHR*>(pPacket->pCreateInfo)->size = std::max(itAs->second, pPacket->pCreateInfo->size);
    } else{
        vktrace_LogError("Error detected in CreateAccelerationStructureKHR() due to buffer find failed.");
        return replayResult;
    }

    if (pPacket->pCreateInfo->offset != 0) {
        vktrace_LogDebug(" The offset != 0, it is %u", pPacket->pCreateInfo->offset);
        const_cast<VkAccelerationStructureCreateInfoKHR*>(pPacket->pCreateInfo)->size = createInfoSize;
    }
    if (pPacket->pCreateInfo->deviceAddress != 0) {
        auto it = replayDeviceToFeatureSupport.find(remappeddevice);
        if (it != replayDeviceToFeatureSupport.end() && it->second.accelerationStructureCaptureReplay) {
            const_cast<VkAccelerationStructureCreateInfoKHR*>(pPacket->pCreateInfo)->createFlags |= VK_ACCELERATION_STRUCTURE_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_KHR;
        } else {
            const_cast<VkAccelerationStructureCreateInfoKHR*>(pPacket->pCreateInfo)->createFlags &= ~VK_ACCELERATION_STRUCTURE_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_KHR;
            const_cast<VkAccelerationStructureCreateInfoKHR*>(pPacket->pCreateInfo)->deviceAddress = 0;     // solve a ddk assertion
        }
    }
    replayResult = m_vkDeviceFuncs.CreateAccelerationStructureKHR(remappeddevice, pPacket->pCreateInfo, pPacket->pAllocator, &local_pAccelerationStructure);
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_accelerationstructurekhrs_map(*(pPacket->pAccelerationStructure), local_pAccelerationStructure);
        replayAccelerationStructureKHRToDevice[local_pAccelerationStructure] = remappeddevice;
        auto it = traceASSizeToReplayASBuildSizes.find(createInfoSize);
        if (it != traceASSizeToReplayASBuildSizes.end()) {
            replayASToASBuildSizes[local_pAccelerationStructure] = it->second;
        }
        else {
            vktrace_LogError("Cannot find accelerationSize from vkGetAccelerationStructureBuildSize() result in gid %lu!", (uint32_t)pPacket->header->global_packet_index);
        }
    }
    return replayResult;
}

void vkReplay::manually_replay_vkCmdWriteAccelerationStructuresPropertiesKHR(packet_vkCmdWriteAccelerationStructuresPropertiesKHR *pPacket) {
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdWriteAccelerationStructuresPropertiesKHR() due to invalid remapped VkCommandBuffer.");
        return;
    }
    // No need to remap accelerationStructureCount
    VkAccelerationStructureKHR *remappedpAccelerationStructures = new VkAccelerationStructureKHR[pPacket->accelerationStructureCount];
    for (uint32_t i = 0; i < pPacket->accelerationStructureCount; i++) {
        remappedpAccelerationStructures[i] = m_objMapper.remap_accelerationstructurekhrs(pPacket->pAccelerationStructures[i]);
        if (pPacket->pAccelerationStructures[i] != VK_NULL_HANDLE && remappedpAccelerationStructures[i] == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in CmdWriteAccelerationStructuresPropertiesKHR() due to invalid remapped VkAccelerationStructureKHR.");
            delete []remappedpAccelerationStructures;
            return;
        }
    }
    // No need to remap queryType
    VkQueryPool remappedqueryPool = m_objMapper.remap_querypools(pPacket->queryPool);
    if (pPacket->queryPool != VK_NULL_HANDLE && remappedqueryPool == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdWriteAccelerationStructuresPropertiesKHR() due to invalid remapped VkQueryPool.");
        delete []remappedpAccelerationStructures;
        return;
    }
    // No need to remap firstQuery
    m_vkDeviceFuncs.CmdWriteAccelerationStructuresPropertiesKHR(remappedcommandBuffer, pPacket->accelerationStructureCount, remappedpAccelerationStructures, pPacket->queryType, remappedqueryPool, pPacket->firstQuery);

    if (pPacket->queryType == VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR) {
        for (int i = 0; i < pPacket->accelerationStructureCount; ++i)
            replayQueryPoolASCompactSize[remappedqueryPool].insert(pPacket->firstQuery + i);
    }
    delete []remappedpAccelerationStructures;
}

VkResult vkReplay::manually_replay_vkGetQueryPoolResults(packet_vkGetQueryPoolResults *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    if (callFailedDuringTrace(pPacket->result, pPacket->header->packet_id)) {
        return replayResult;
    }
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetQueryPoolResults() due to invalid remapped VkDevice.");
        return replayResult;
    }
    VkQueryPool remappedqueryPool = m_objMapper.remap_querypools(pPacket->queryPool);
    if (pPacket->queryPool != VK_NULL_HANDLE && remappedqueryPool == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetQueryPoolResults() due to invalid remapped VkQueryPool.");
        return replayResult;
    }
    // No need to remap firstQuery
    // No need to remap queryCount
    // No need to remap dataSize
    // No need to remap pData
    char *pData = new char[pPacket->dataSize];
    // No need to remap stride
    // No need to remap flags
    uint32_t call_id = m_inFrameRange ? VKTRACE_TPI_VK_vkGetQueryPoolResults : 0;
    VkQueryType queryType = m_querypool_type[pPacket->queryPool];
    do {
    replayResult = m_vkDeviceFuncs.GetQueryPoolResults(remappeddevice, remappedqueryPool, pPacket->firstQuery, pPacket->queryCount, pPacket->dataSize, pData, pPacket->stride, pPacket->flags);
        m_CallStats[call_id].total++;
        m_CallStats[call_id].injectedCallCount++;
        if (call_id && replaySettings.perfMeasuringMode > 0) {
            VkPresentInfoKHR PresentInfo = {};
            PresentInfo.sType = VK_STRUCTURE_TYPE_MAX_ENUM;
            PresentInfo.waitSemaphoreCount = 1;
            m_vkDeviceFuncs.QueuePresentKHR(VK_NULL_HANDLE, &PresentInfo);
        }
    } while (pPacket->result == VK_SUCCESS && (pPacket->flags & VK_QUERY_RESULT_WAIT_BIT || queryType == VK_QUERY_TYPE_OCCLUSION) && replayResult != pPacket->result);
    m_CallStats[call_id].injectedCallCount--;
    if (call_id && replaySettings.perfMeasuringMode > 0) {
        VkPresentInfoKHR PresentInfo = {};
        PresentInfo.sType = VK_STRUCTURE_TYPE_MAX_ENUM;
        PresentInfo.waitSemaphoreCount = 0;
        m_vkDeviceFuncs.QueuePresentKHR(VK_NULL_HANDLE, &PresentInfo);
    }
    auto it = replayQueryPoolASCompactSize.find(remappedqueryPool);
    if (it != replayQueryPoolASCompactSize.end()) { // This query pool contains compacted acceleration structure sizes
        for (int i = pPacket->firstQuery; i < pPacket->firstQuery + pPacket->queryCount; ++i) {
            if (it->second.find(i) != it->second.end()) {
                int offset = i * pPacket->stride;
                if (pPacket->flags & VK_QUERY_RESULT_64_BIT) {
                    VkDeviceSize traceASCompactSize = *((uint64_t*)((char*)pPacket->pData + offset));
                    VkDeviceSize replayASCompactSize = *((u_int64_t*)(pData + offset));
                    traceASCompactSizeToReplayASCompactSize[traceASCompactSize] = replayASCompactSize;
                }
                else {
                    VkDeviceSize traceASCompactSize = *((int*)((char*)pPacket->pData + offset));
                    VkDeviceSize replayASCompactSize = *((int*)(pData + offset));
                    traceASCompactSizeToReplayASCompactSize[traceASCompactSize] = replayASCompactSize;
                }
            }
        }
    }
    delete []pData;
    return replayResult;
}

VkResult vkReplay::manually_replay_vkBuildAccelerationStructuresKHR(packet_vkBuildAccelerationStructuresKHR *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in BuildAccelerationStructuresKHR() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkDeferredOperationKHR remappeddeferredOperation = m_objMapper.remap_deferredoperationkhrs(pPacket->deferredOperation);
    if (pPacket->deferredOperation != VK_NULL_HANDLE && remappeddeferredOperation == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in BuildAccelerationStructuresKHR() due to invalid remapped VkDeferredOperationKHR.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    if (!replayDeviceToFeatureSupport[remappeddevice].accelerationStructureHostCommands) {
        vktrace_LogError("Error host build is not supported by the device.");
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    //interpret VB,IB,Transform and AABB buffer if the buffer is host buffer.
    char** hostAddressBit = (char**)malloc(pPacket->infoCount * sizeof(void*));
    for (uint32_t i = 0; i < pPacket->infoCount; ++i) {
        hostAddressBit[i] = (char*)malloc(pPacket->pInfos[i].geometryCount * sizeof(char));
        memset(hostAddressBit[i], 0, pPacket->pInfos[i].geometryCount * sizeof(char));
    }
    for (uint32_t i = 0; i < pPacket->infoCount; ++i) {
        for(uint32_t j = 0; j < pPacket->pInfos[i].geometryCount; ++j) {
            VkAccelerationStructureGeometryKHR* pGeometry = (VkAccelerationStructureGeometryKHR *)&(pPacket->pInfos[i].pGeometries[j]);
            if (pGeometry->geometryType == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
                if (pGeometry->geometry.triangles.vertexData.hostAddress != NULL && !isMapMemoryAddress(pGeometry->geometry.triangles.vertexData.hostAddress)) {
                    hostAddressBit[i][j] = hostAddressBit[i][j] | AS_GEOMETRY_TRIANGLES_VERTEXDATA_BIT;
                    void* temp = vktrace_trace_packet_interpret_buffer_pointer(pPacket->header, (intptr_t)pGeometry->geometry.triangles.vertexData.hostAddress);
                    int vbOffset = pPacket->ppBuildRangeInfos[i][j].primitiveOffset + pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexStride * pPacket->ppBuildRangeInfos[i][j].firstVertex;
                    if (vbOffset != 0) {
                        int vbUsedSize = pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexStride * (pPacket->pInfos[i].pGeometries[j].geometry.triangles.maxVertex + 1);
                        pGeometry->geometry.triangles.vertexData.hostAddress = malloc(vbOffset + vbUsedSize);
                        memcpy((char*)pGeometry->geometry.triangles.vertexData.hostAddress + vbOffset, temp, vbUsedSize);
                    } else {
                        pGeometry->geometry.triangles.vertexData.hostAddress = temp;
                    }
                }
                if (pGeometry->geometry.triangles.indexData.hostAddress != NULL && !isMapMemoryAddress(pGeometry->geometry.triangles.indexData.hostAddress)) {
                    hostAddressBit[i][j] = hostAddressBit[i][j] | AS_GEOMETRY_TRIANGLES_INDEXDATA_BIT;
                    void* temp = vktrace_trace_packet_interpret_buffer_pointer(pPacket->header, (intptr_t)pGeometry->geometry.triangles.indexData.hostAddress);
                    int ibOffset = pPacket->ppBuildRangeInfos[i][j].primitiveOffset;
                    if (ibOffset != 0) {
                        int ibUsedSize = pPacket->ppBuildRangeInfos[i][j].primitiveCount * getVertexIndexStride(pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexType) * 3;
                        pGeometry->geometry.triangles.indexData.hostAddress = malloc(ibOffset + ibUsedSize);
                        memcpy((char*)pGeometry->geometry.triangles.indexData.hostAddress + ibOffset, temp, ibUsedSize);
                    } else {
                        pGeometry->geometry.triangles.indexData.hostAddress = temp;
                    }

                }
                if (pGeometry->geometry.triangles.transformData.hostAddress && !isMapMemoryAddress(pGeometry->geometry.triangles.transformData.hostAddress)) {
                    hostAddressBit[i][j] = hostAddressBit[i][j] | AS_GEOMETRY_TRIANGLES_TRANSFORMDATA_BIT;
                    void* temp = vktrace_trace_packet_interpret_buffer_pointer(pPacket->header, (intptr_t)pGeometry->geometry.triangles.transformData.hostAddress);
                    int transOffset = pPacket->ppBuildRangeInfos[i][j].transformOffset;
                    if (transOffset != 0) {
                        int transUsedSize = sizeof(VkTransformMatrixKHR);
                        pGeometry->geometry.triangles.transformData.hostAddress = malloc(transOffset + transUsedSize);
                        memcpy((char*)pGeometry->geometry.triangles.transformData.hostAddress + transOffset, temp, transUsedSize);
                    } else {
                        pGeometry->geometry.triangles.transformData.hostAddress = temp;
                    }
                }
            }
            else if (pGeometry->geometryType == VK_GEOMETRY_TYPE_AABBS_KHR) {
                if (pGeometry->geometry.aabbs.data.hostAddress != NULL && !isMapMemoryAddress(pGeometry->geometry.aabbs.data.hostAddress)) {
                    hostAddressBit[i][j] = hostAddressBit[i][j] | AS_GEOMETRY_AABB_DATA_BIT;
                    void* temp = vktrace_trace_packet_interpret_buffer_pointer(pPacket->header, (intptr_t)pGeometry->geometry.aabbs.data.hostAddress);
                    int aabbOffset = pPacket->ppBuildRangeInfos[i][j].primitiveOffset;
                    if (aabbOffset != 0) {
                        int aabbUsedSize = pPacket->ppBuildRangeInfos[i][j].primitiveCount * pPacket->pInfos[i].pGeometries[j].geometry.aabbs.stride;
                        pGeometry->geometry.aabbs.data.hostAddress = malloc(aabbOffset + aabbUsedSize);
                        memcpy((char*)pGeometry->geometry.aabbs.data.hostAddress + aabbOffset, temp, aabbUsedSize);
                    } else {
                        pGeometry->geometry.aabbs.data.hostAddress = temp;
                    }
                }
            }
        }
    }

    // No need to remap infoCount
    // No need to remap ppBuildRangeInfos
    for (uint32_t i = 0; i < pPacket->infoCount; i++) {
        for (uint32_t j = 0; j < pPacket->pInfos[i].geometryCount; j++) {
            switch (pPacket->pInfos[i].pGeometries[j].geometryType) {
                case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
                    if (pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexData.hostAddress != nullptr && (hostAddressBit[i][j] & AS_GEOMETRY_TRIANGLES_VERTEXDATA_BIT) == 0) {
                        void** hostAddress = (void**)&(pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexData.hostAddress);
                        *hostAddress = fromTraceAddr2ReplayAddr(remappeddevice, pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexData.hostAddress);
                    }
                    if (pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexData.hostAddress != nullptr && (hostAddressBit[i][j] & AS_GEOMETRY_TRIANGLES_INDEXDATA_BIT) == 0) {
                        void** hostAddress = (void**)&(pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexData.hostAddress);
                        *hostAddress = fromTraceAddr2ReplayAddr(remappeddevice, pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexData.hostAddress);
                    }
                    if (pPacket->pInfos[i].pGeometries[j].geometry.triangles.transformData.hostAddress != nullptr && (hostAddressBit[i][j] & AS_GEOMETRY_TRIANGLES_TRANSFORMDATA_BIT) == 0) {
                        void** hostAddress = (void**)&(pPacket->pInfos[i].pGeometries[j].geometry.triangles.transformData.hostAddress);
                        *hostAddress = fromTraceAddr2ReplayAddr(remappeddevice, pPacket->pInfos[i].pGeometries[j].geometry.triangles.transformData.hostAddress);
                    }
                    break;
                case VK_GEOMETRY_TYPE_AABBS_KHR:
                    if (pPacket->pInfos[i].pGeometries[j].geometry.aabbs.data.hostAddress != nullptr && (hostAddressBit[i][j] & AS_GEOMETRY_AABB_DATA_BIT) == 0) {
                        void** hostAddress = (void**)&(pPacket->pInfos[i].pGeometries[j].geometry.aabbs.data.hostAddress);
                        *hostAddress = fromTraceAddr2ReplayAddr(remappeddevice, pPacket->pInfos[i].pGeometries[j].geometry.aabbs.data.hostAddress);
                    }
                    break;
                case VK_GEOMETRY_TYPE_INSTANCES_KHR:
                    if (pPacket->pInfos[i].pGeometries[j].geometry.instances.data.hostAddress != nullptr) {
                        if (VK_TRUE == pPacket->pInfos[i].pGeometries[j].geometry.instances.arrayOfPointers) {
                            VkAccelerationStructureInstanceKHR** pAsInstance = (VkAccelerationStructureInstanceKHR**)(pPacket->pInfos[i].pGeometries[j].geometry.instances.data.hostAddress);
                            for(uint32_t k = 0; k < pPacket->ppBuildRangeInfos[i][j].primitiveCount; k++) {
                                pAsInstance[k]->accelerationStructureReference = (uint64_t)m_objMapper.remap_accelerationstructurekhrs((VkAccelerationStructureKHR)(pAsInstance[k]->accelerationStructureReference));
                            }
                        } else {
                            VkAccelerationStructureInstanceKHR* pAsInstance = (VkAccelerationStructureInstanceKHR*)(pPacket->pInfos[i].pGeometries[j].geometry.instances.data.hostAddress);
                            for(uint32_t k = 0; k < pPacket->ppBuildRangeInfos[i][j].primitiveCount; k++) {
                                pAsInstance[k].accelerationStructureReference = (uint64_t)m_objMapper.remap_accelerationstructurekhrs((VkAccelerationStructureKHR)(pAsInstance[k].accelerationStructureReference));
                            }
                        }
                    }
                    break;
                default:
                    vktrace_LogError("Can't support the geometry type.");
            }
        }
    }
    for (uint32_t i = 0; i < pPacket->infoCount; i++) {
        VkAccelerationStructureKHR replaySrcAS = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfos[i].srcAccelerationStructure);
        VkAccelerationStructureKHR replayDstAS = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfos[i].dstAccelerationStructure);
        if (pPacket->pInfos[i].srcAccelerationStructure != VK_NULL_HANDLE && replaySrcAS != VK_NULL_HANDLE) {
            const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pInfos)[i].srcAccelerationStructure = replaySrcAS;
        } else if (pPacket->pInfos[i].srcAccelerationStructure != VK_NULL_HANDLE && replaySrcAS == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in BuildAccelerationStructuresIndirectKHR() due to invalid remapped VkAccelerationStructureKHR = %llu.", pPacket->pInfos[i].srcAccelerationStructure);
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
        if (pPacket->pInfos[i].dstAccelerationStructure != VK_NULL_HANDLE && replayDstAS != VK_NULL_HANDLE) {
            const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pInfos)[i].dstAccelerationStructure = replayDstAS;
        } else if (pPacket->pInfos[i].dstAccelerationStructure != VK_NULL_HANDLE && replayDstAS == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in BuildAccelerationStructuresIndirectKHR() due to invalid remapped VkAccelerationStructureKHR = %llu.", pPacket->pInfos[i].dstAccelerationStructure);
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }

#if defined(_DEBUG)
        VkAccelerationStructureBuildSizesInfoKHR asBuildSizeInfo = {};
        std::vector<uint32_t> maxPrimitiveCountsVec = {};
        for (uint32_t j = 0; j < pPacket->pInfos[i].geometryCount; j++) {
            maxPrimitiveCountsVec.push_back(pPacket->ppBuildRangeInfos[i][j].primitiveCount);
        }
        m_vkDeviceFuncs.GetAccelerationStructureBuildSizesKHR(remappeddevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_KHR, &(pPacket->pInfos[i]), maxPrimitiveCountsVec.data(), &asBuildSizeInfo);
        switch(pPacket->pInfos[i].mode) {
            case VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR:
                const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(&pPacket->pInfos[i])->scratchData.hostAddress = malloc(asBuildSizeInfo.buildScratchSize);
                break;
            case VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR:
                const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(&pPacket->pInfos[i])->scratchData.hostAddress = malloc(asBuildSizeInfo.updateScratchSize);
                break;
            default:
                vktrace_LogError("The mode type is error.");
        }
        if (pPacket->pInfos[i].scratchData.hostAddress == nullptr) {
            vktrace_LogError("Memory malloc for scratchData failed .");
        }
#else
        auto it = replayASToASBuildSizes.find(replayDstAS);
        if (it == replayASToASBuildSizes.end()) {
            vktrace_LogError("Can't find the dstAS.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
        switch(pPacket->pInfos[i].mode) {
            case VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR:
                const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(&pPacket->pInfos[i])->scratchData.hostAddress = malloc(it->second.buildScratchSize);
                break;
            case VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR:
                const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(&pPacket->pInfos[i])->scratchData.hostAddress = malloc(it->second.updateScratchSize);
                break;
            default:
                vktrace_LogError("The mode type is error.");
        }
        if (pPacket->pInfos[i].scratchData.hostAddress == nullptr) {
            vktrace_LogError("Memory malloc for scratchData failed .");
        }
#endif
    }

    if (pPacket->deferredOperation != VK_NULL_HANDLE) {
        vktrace_LogAlways("The deferredOperation is not VK_NULL_HANDLE, now, set it the VK_NULL_HANDLE.");
        pPacket->deferredOperation = VK_NULL_HANDLE;
    }
    VkResult replayResult = m_vkDeviceFuncs.BuildAccelerationStructuresKHR(remappeddevice, remappeddeferredOperation, pPacket->infoCount, pPacket->pInfos, pPacket->ppBuildRangeInfos);
    for (uint32_t i = 0; i < pPacket->infoCount; i++) {
        if (pPacket->pInfos[i].scratchData.hostAddress != nullptr) {
            free(pPacket->pInfos[i].scratchData.hostAddress);
        }
    }
    for (uint32_t i = 0; i < pPacket->infoCount; ++i) {
        free(hostAddressBit[i]);
    }
    free(hostAddressBit);
    return replayResult;
}

// Some buffer addresses are offsets from other buffer addresses
// So find the closest address of addr in traceDeviceAddrToReplayDeviceAddr4Buf
std::unordered_map<VkDeviceAddress, vkReplay::objDeviceAddr>::iterator vkReplay::findClosestAddress(VkDeviceAddress addr) {
    uint64_t delta_min = ULONG_LONG_MAX;
    auto it_result = traceDeviceAddrToReplayDeviceAddr4Buf.end();
    for (auto it = traceDeviceAddrToReplayDeviceAddr4Buf.begin(); it != traceDeviceAddrToReplayDeviceAddr4Buf.end(); ++it) {
        uint64_t delta = addr - it->first;
        if (addr >= it->first && delta >= 0 && delta < delta_min) {
            delta_min = delta;
            it_result = it;
        }
    }

    if (it_result != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
        auto iter = traceBufferToASBuildSizes.find((VkBuffer)it_result->second.traceObjHandle);
        if (iter != traceBufferToASBuildSizes.end()) {
            if (addr > it_result->first + iter->second)  {
                it_result = traceDeviceAddrToReplayDeviceAddr4Buf.end();
            }
        } else {
            it_result = traceDeviceAddrToReplayDeviceAddr4Buf.end();;
        }
    }

    return it_result;
}

void vkReplay::remapReplayInstanceBufferDeviceAddresss(VkDevice remappedDevice, VkDeviceMemory dataMemory, VkDeviceSize size, uint64_t offset)
{
    VkAccelerationStructureInstanceKHR* pAsInstance = nullptr;
    VkResult mapResult = VK_NOT_READY;
    auto it = replayMemoryToMapAddress.find(dataMemory);
    if (it != replayMemoryToMapAddress.end()) {
        pAsInstance = (VkAccelerationStructureInstanceKHR*)(it->second);
        mapResult = VK_SUCCESS;
    } else {
        mapResult = m_vkDeviceFuncs.MapMemory(remappedDevice, dataMemory, 0, VK_WHOLE_SIZE, 0, (void**)&pAsInstance);
        if (mapResult != VK_SUCCESS) {
            vktrace_LogError("Map memory failed for pAsInstance data of acceleration structure, mapResult = %d.", mapResult);
        }
    }

    if (mapResult == VK_SUCCESS) {
        pAsInstance += offset / sizeof(VkAccelerationStructureInstanceKHR);
        for(uint32_t k = 0; k < size / sizeof(VkAccelerationStructureInstanceKHR); k++) {
            if (pAsInstance[k].accelerationStructureReference == 0) {
                continue;
            }

            auto it0 = traceDeviceAddrToReplayDeviceAddr4AS.find((VkDeviceAddress)(pAsInstance[k].accelerationStructureReference));
            if (it0 != traceDeviceAddrToReplayDeviceAddr4AS.end()) {
                pAsInstance[k].accelerationStructureReference = it0->second.replayDeviceAddr;
            } else {
                vktrace_LogDebug("Can't find accelerationStructureReference %llu. If the data is written by vkCmdCopyBuffer, please ignore this warning.", pAsInstance[k].accelerationStructureReference);
            }
        }
        VkMappedMemoryRange memoryRange = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, dataMemory, 0, VK_WHOLE_SIZE};
        m_vkDeviceFuncs.FlushMappedMemoryRanges(remappedDevice, 1, &memoryRange);
        if (it == replayMemoryToMapAddress.end()) {
            m_vkDeviceFuncs.UnmapMemory(remappedDevice, dataMemory);
        }
    }
}

void vkReplay::remapInstanceBufferDeviceAddressInCopyBuffer(VkBuffer traceSrcBuffer, VkBuffer traceDstBuffer, VkDeviceSize srcOffset, VkDeviceSize copySize)
{
    VkDeviceMemory remappedSrcMemory = VK_NULL_HANDLE;
    VkDevice remappedDevice = VK_NULL_HANDLE;
    VkDeviceSize size = copySize;
    uint64_t offset = srcOffset;

    if (size % sizeof(VkAccelerationStructureInstanceKHR) != 0) {
        return;
    }

    if (m_pFileHeader->bit_flags & VKTRACE_RQ_POSTPROCESSED_BIT)
        return;

    auto itMem = traceBufferToReplayMemory.find(traceSrcBuffer);
    if (itMem != traceBufferToReplayMemory.end()) {
        remappedSrcMemory = itMem->second;
    } else {
        vktrace_LogError("Can't find the buffer memory for instances data of acceleration structure.");
    }

    auto itDevice = traceBufferToDevice.find(traceSrcBuffer);
    if (itDevice != traceBufferToDevice.end()) {
        remappedDevice = m_objMapper.remap_devices(itDevice->second);
    } else {
        vktrace_LogError("Can't find the device for instances data of acceleration structure.");
    }

    bool supportASCaptureReplay = false;
    auto it_fea = replayDeviceToFeatureSupport.find(remappedDevice);
    if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.accelerationStructureCaptureReplay) {
        supportASCaptureReplay = true;
    }

    if (supportASCaptureReplay || !g_hasAsApi) {
        return;
    }

    VkBuffer remappedSrcBuffer = m_objMapper.remap_buffers(traceSrcBuffer);
    VkBuffer remappedDstBuffer = m_objMapper.remap_buffers(traceDstBuffer);
    if (remappedSrcMemory == VK_NULL_HANDLE || remappedDevice == VK_NULL_HANDLE || remappedSrcBuffer == VK_NULL_HANDLE || remappedDstBuffer == VK_NULL_HANDLE ) {
        vktrace_LogError("remappedMemory is VK_NULL_HANDLE or remappedDevice is VK_NULL_HANDLE.");
        return;
    }

    bool findAsReference = false;
    if (replayBufferToUsageFlag.find(remappedDstBuffer) != replayBufferToUsageFlag.end()) {
        findAsReference = true;
    }

    if (!findAsReference) {
        return;
    }

    auto itOffset = replayBufferToReplayDeviceMemoryOffset.find(remappedSrcBuffer);
    if (itOffset != replayBufferToReplayDeviceMemoryOffset.end()) {
        offset += itOffset->second;
    }

    remapReplayInstanceBufferDeviceAddresss(remappedDevice, remappedSrcMemory, size, offset);
}

void vkReplay::deviceBuildToHostBuild(packet_vkCmdBuildAccelerationStructuresKHR* pPacket) {

    VkDevice        device = VK_NULL_HANDLE;
    VkResult        result = VK_SUCCESS;
    VkBuffer        buffer = VK_NULL_HANDLE;
    VkDeviceMemory  memory = VK_NULL_HANDLE;
    vector<void*>   malloc_mem;
    unordered_map<VkDeviceMemory, void*>  mapped_mem;

   for (uint32_t i = 0; i < pPacket->infoCount; i++) {
        VkAccelerationStructureKHR replaySrcAS = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfos[i].srcAccelerationStructure);
        VkAccelerationStructureKHR replayDstAS = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfos[i].dstAccelerationStructure);
        if (pPacket->pInfos[i].srcAccelerationStructure != VK_NULL_HANDLE && replaySrcAS != VK_NULL_HANDLE) {
            const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pInfos)[i].srcAccelerationStructure = replaySrcAS;
        } else if (pPacket->pInfos[i].srcAccelerationStructure != VK_NULL_HANDLE && replaySrcAS == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in deviceBuildToHostBuild() due to invalid remapped VkAccelerationStructureKHR = %llu.", pPacket->pInfos[i].srcAccelerationStructure);
        }
        if (pPacket->pInfos[i].dstAccelerationStructure != VK_NULL_HANDLE && replayDstAS != VK_NULL_HANDLE) {
            const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pInfos)[i].dstAccelerationStructure = replayDstAS;
        } else if (pPacket->pInfos[i].dstAccelerationStructure != VK_NULL_HANDLE && replayDstAS == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in deviceBuildToHostBuild() due to invalid remapped VkAccelerationStructureKHR = %llu.", pPacket->pInfos[i].dstAccelerationStructure);
        }
        auto it = replayASToASBuildSizes.find(replayDstAS);
        if (it == replayASToASBuildSizes.end()) {
            vktrace_LogError("deviceBuildToHostBuild: can't find the dstAS.");
            assert(0);
        }
        switch(pPacket->pInfos[i].mode) {
            case VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR:
                const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(&pPacket->pInfos[i])->scratchData.hostAddress = malloc(it->second.buildScratchSize);
                malloc_mem.push_back(const_cast<void*>(pPacket->pInfos[i].scratchData.hostAddress));
                break;
            case VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR:
                const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(&pPacket->pInfos[i])->scratchData.hostAddress = malloc(it->second.updateScratchSize);
                malloc_mem.push_back(const_cast<void*>(pPacket->pInfos[i].scratchData.hostAddress));
                break;
            default:
                vktrace_LogError("The mode type is error.");
        }
        if (pPacket->pInfos[i].scratchData.hostAddress == nullptr) {
            vktrace_LogError("Memory malloc for scratchData failed .");
        }
        for (uint32_t j = 0; j < pPacket->pInfos[i].geometryCount; j++) {
            switch (pPacket->pInfos[i].pGeometries[j].geometryType) {
                case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
                {
                    assert(pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexData.deviceAddress != 0);
                    buffer = traceDeviceAddrTotraceBuffer4Buf[pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexData.deviceAddress];
                    memory = traceBufferToReplayMemory[buffer];
                    device = m_objMapper.remap_devices(traceBufferToDevice[buffer]);
                    if (mapped_mem.find(memory) == mapped_mem.end()) {
                        result = m_vkDeviceFuncs.MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, (void**)&pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexData.hostAddress);
                        assert(result == VK_SUCCESS);
                        mapped_mem[memory] = const_cast<void*>(pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexData.hostAddress);
                    }
                    else {
                        void** hostAddress = const_cast<void**>(&(pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexData.hostAddress));
                        *hostAddress = mapped_mem[memory];;
                    }

                    if (pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexType != VK_INDEX_TYPE_NONE_KHR &&
                        pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexData.deviceAddress != 0) {
                        buffer = traceDeviceAddrTotraceBuffer4Buf[pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexData.deviceAddress];
                        memory = traceBufferToReplayMemory[buffer];
                        device = m_objMapper.remap_devices(traceBufferToDevice[buffer]);
                        if (mapped_mem.find(memory) == mapped_mem.end()) {
                            result = m_vkDeviceFuncs.MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, (void**)&pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexData.hostAddress);
                            assert(result == VK_SUCCESS);
                            mapped_mem[memory] = const_cast<void*>(pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexData.hostAddress);
                        }
                        else {
                             void** hostAddress = const_cast<void**>(&(pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexData.hostAddress));
                             *hostAddress = mapped_mem[memory];
                        }
                    }
                    break;
                }
                case VK_GEOMETRY_TYPE_AABBS_KHR:
                {
                    assert(pPacket->pInfos[i].pGeometries[j].geometry.aabbs.data.deviceAddress != 0);
                    buffer = traceDeviceAddrTotraceBuffer4Buf[pPacket->pInfos[i].pGeometries[j].geometry.aabbs.data.deviceAddress];
                    memory = traceBufferToReplayMemory[buffer];
                    device = m_objMapper.remap_devices(traceBufferToDevice[buffer]);
                    if (mapped_mem.find(memory) == mapped_mem.end()) {
                        result = m_vkDeviceFuncs.MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, (void**)&pPacket->pInfos[i].pGeometries[j].geometry.aabbs.data.hostAddress);
                        assert(result == VK_SUCCESS);
                        mapped_mem[memory] = const_cast<void*>(pPacket->pInfos[i].pGeometries[j].geometry.aabbs.data.hostAddress);
                    }
                    else {
                        void** hostAddress = const_cast<void**>(&(pPacket->pInfos[i].pGeometries[j].geometry.aabbs.data.hostAddress));
                        *hostAddress = mapped_mem[memory];
                    }
                    break;
                }
                case VK_GEOMETRY_TYPE_INSTANCES_KHR:
                {
                    assert(pPacket->pInfos[i].pGeometries[j].geometry.instances.data.deviceAddress != 0);
                    buffer = traceDeviceAddrTotraceBuffer4Buf[pPacket->pInfos[i].pGeometries[j].geometry.instances.data.deviceAddress];
                    memory = traceBufferToReplayMemory[buffer];
                    device = m_objMapper.remap_devices(traceBufferToDevice[buffer]);
                    if (mapped_mem.find(memory) == mapped_mem.end()) {
                        result = m_vkDeviceFuncs.MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, (void**)&pPacket->pInfos[i].pGeometries[j].geometry.instances.data.hostAddress);
                        assert(result == VK_SUCCESS);
                        mapped_mem[memory] = const_cast<void*>(pPacket->pInfos[i].pGeometries[j].geometry.instances.data.hostAddress);
                    }
                    else {
                        void** hostAddress = const_cast<void**>(&(pPacket->pInfos[i].pGeometries[j].geometry.instances.data.hostAddress));
                        *hostAddress = mapped_mem[memory];
                    }
                    char* instanceBuf =(char *)mapped_mem[memory]+pPacket->ppBuildRangeInfos[i][j].primitiveOffset;
                    VkAccelerationStructureInstanceKHR* data = NULL;
                    for (uint32_t instanceIndex = 0; instanceIndex < pPacket->ppBuildRangeInfos[i][j].primitiveCount; instanceIndex++) {
                        if (pPacket->pInfos[i].pGeometries[j].geometry.instances.arrayOfPointers == VK_TRUE) {
                            data = (VkAccelerationStructureInstanceKHR*)instanceBuf + instanceIndex;
                        }
                        else {
                            data = &((VkAccelerationStructureInstanceKHR*)instanceBuf)[instanceIndex];
                        }
                        if (data->accelerationStructureReference == 0) {
                            continue;
                        }
                        auto it = traceDeviceAddrToReplayDeviceAddr4AS.begin();
                        VkBool32 found = VK_FALSE;
                        while (it != traceDeviceAddrToReplayDeviceAddr4AS.end()) {
                             if (it->second.replayDeviceAddr == data->accelerationStructureReference) {
                                data->accelerationStructureReference =(uint64_t)m_objMapper.remap_accelerationstructurekhrs((const VkAccelerationStructureKHR)it->second.traceObjHandle);
                                found = VK_TRUE;
                                break;
                             }
                             it++;
                        }
                        if (found == VK_FALSE) {
                             vktrace_LogError("deviceBuildToHostBuild: cannot find the BLAS device address in traceDeviceAddrToReplayDeviceAddr4AS!");
                        }
                     }
                    break;
                }
                default:
                    vktrace_LogError("deviceBuildToHostBuild(): can't support the geometry type.");
            }
        }
    }

    assert(device != VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        vktrace_LogError("deviceBuildToHostBuild: MapMemory failed.");
    }

    m_vkDeviceFuncs.BuildAccelerationStructuresKHR(device, VK_NULL_HANDLE, pPacket->infoCount, pPacket->pInfos, pPacket->ppBuildRangeInfos);

    auto it_mem = mapped_mem.begin();
    while (it_mem != mapped_mem.end()) {
        m_vkDeviceFuncs.UnmapMemory(device, it_mem->first);
        it_mem++;
    }
    auto it_malloc = malloc_mem.begin();
    while(it_malloc != malloc_mem.end()) {
        free(*it_malloc);
        it_malloc++;
    }
}

void vkReplay::manually_replay_vkCmdBuildAccelerationStructuresKHR(packet_vkCmdBuildAccelerationStructuresKHR* pPacket) {
    if (g_devBuild2HostBuild_state == DEVBUILD_TO_HOSTBUILD_REQUESTED) {
        g_devBuild2HostBuild_state = DEVBUILD_TO_HOSTBUILD_RUNNING;
    }
    if (g_devBuild2HostBuild_state == DEVBUILD_TO_HOSTBUILD_RUNNING)
    {
        return deviceBuildToHostBuild(pPacket);
    }
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdBuildAccelerationStructuresKHR() due to invalid remapped VkCommandBuffer.");
        return ;
    }
    bool supportASCaptureReplay = false;
    bool supportBufferCaptureReplay = false;
    auto it_dev = replayCommandBufferToReplayDevice.find(remappedcommandBuffer);
    if (it_dev != replayCommandBufferToReplayDevice.end()) {
        auto it_fea = replayDeviceToFeatureSupport.find(it_dev->second);
        if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.accelerationStructureCaptureReplay) {
            supportASCaptureReplay = true;
        }
        if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.bufferDeviceAddressCaptureReplay) {
            supportBufferCaptureReplay = true;
        }
    }
    // No need to remap infoCount
    // No need to remap ppBuildRangeInfos
    for (uint32_t i = 0; i < pPacket->infoCount; i++) {
        VkAccelerationStructureKHR replaySrcAS = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfos[i].srcAccelerationStructure);
        VkAccelerationStructureKHR replayDstAS = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfos[i].dstAccelerationStructure);
        if (pPacket->pInfos[i].srcAccelerationStructure != VK_NULL_HANDLE && replaySrcAS != VK_NULL_HANDLE) {
            const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pInfos)[i].srcAccelerationStructure = replaySrcAS;
        } else if (pPacket->pInfos[i].srcAccelerationStructure != VK_NULL_HANDLE && replaySrcAS == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in CmdBuildAccelerationStructuresKHR() due to invalid remapped VkAccelerationStructureKHR = %llu.", pPacket->pInfos[i].srcAccelerationStructure);
            return ;
        }
        if (pPacket->pInfos[i].dstAccelerationStructure != VK_NULL_HANDLE && replayDstAS != VK_NULL_HANDLE) {
            const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pInfos)[i].dstAccelerationStructure = replayDstAS;
        } else if (pPacket->pInfos[i].dstAccelerationStructure != VK_NULL_HANDLE && replayDstAS == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in CmdBuildAccelerationStructuresKHR() due to invalid remapped VkAccelerationStructureKHR = %llu.", pPacket->pInfos[i].dstAccelerationStructure);
            return ;
        }

        auto it = traceDeviceAddrToReplayDeviceAddr4Buf.find(pPacket->pInfos[i].scratchData.deviceAddress);
        if (it !=  traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
            const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pInfos)[i].scratchData.deviceAddress = it->second.replayDeviceAddr;
        } else {
            it = findClosestAddress(pPacket->pInfos[i].scratchData.deviceAddress);
            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                vktrace_LogDebug("Found a closest one = %llu. We'll try to use the offset = %d of it.", it->first, pPacket->pInfos[i].scratchData.deviceAddress - it->first);
                const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pInfos)[i].scratchData.deviceAddress = it->second.replayDeviceAddr + (pPacket->pInfos[i].scratchData.deviceAddress - it->first);
            }
        }
        for(uint32_t j = 0; j < pPacket->pInfos[i].geometryCount; j++) {
            switch (pPacket->pInfos[i].pGeometries[j].geometryType) {
                case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
                    if (pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexData.deviceAddress != 0 && supportBufferCaptureReplay == false) {
                        VkDeviceAddress vertexAddr = pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexData.deviceAddress;
                        it = traceDeviceAddrToReplayDeviceAddr4Buf.find(vertexAddr);
                        if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                            const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.triangles.vertexData.deviceAddress = it->second.replayDeviceAddr;
                        } else {
                            vktrace_LogDebug("Can't find the replay device address for vertexData[%d][%d] = %p in packet = %d. Trying to find the closest address",
                                    i, j, vertexAddr, pPacket->header->global_packet_index);
                            it = findClosestAddress(vertexAddr);
                            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                                vktrace_LogDebug("Found a closest one = %p. We'll try to use the offset = %d of it.", it->first, vertexAddr - it->first);
                                const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.triangles.vertexData.deviceAddress = it->second.replayDeviceAddr + (vertexAddr - it->first);
                            }
                            else {
                                vktrace_LogError("Can't find the replay device address for vertexData[%d][%d] = %p in packet = %d", i, j, vertexAddr, pPacket->header->global_packet_index);
                            }
                        }
                    }
                    if (pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexData.deviceAddress != 0 && supportBufferCaptureReplay == false) {
                        VkDeviceAddress indexAddr = pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexData.deviceAddress;
                        it = traceDeviceAddrToReplayDeviceAddr4Buf.find(indexAddr);
                        if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                            const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.triangles.indexData.deviceAddress = it->second.replayDeviceAddr;
                        } else {
                            vktrace_LogDebug("Can't find the replay device address for indexData[%d][%d] = %p in packet = %d. Trying to find the closest address",
                                    i, j, indexAddr, pPacket->header->global_packet_index);
                            it = findClosestAddress(indexAddr);
                            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                                vktrace_LogDebug("Found a closest one = %p. We'll try to use the offset = %d of it.", it->first, indexAddr - it->first);
                                const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.triangles.indexData.deviceAddress = it->second.replayDeviceAddr + (indexAddr - it->first);
                            }
                            else {
                                vktrace_LogError("Can't find the replay device address for indexData[%d][%d] = %p in packet = %d", i, j, indexAddr, pPacket->header->global_packet_index);
                            }
                        }
                    }
                    if (pPacket->pInfos[i].pGeometries[j].geometry.triangles.transformData.deviceAddress != 0 && supportBufferCaptureReplay == false) {
                        VkDeviceAddress transformAddr = pPacket->pInfos[i].pGeometries[j].geometry.triangles.transformData.deviceAddress;
                        it = traceDeviceAddrToReplayDeviceAddr4Buf.find(transformAddr);
                        if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                            const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.triangles.transformData.deviceAddress = it->second.replayDeviceAddr;
                        } else {
                            vktrace_LogDebug("Can't find the replay device address for transformData[%d][%d] = %p in packet = %d. Trying to find the closest address",
                                    i, j, transformAddr, pPacket->header->global_packet_index);
                            it = findClosestAddress(transformAddr);
                            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                                vktrace_LogDebug("Found a closest one = %p. We'll try to use the offset = %d of it.", it->first, transformAddr - it->first);
                                const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.triangles.transformData.deviceAddress = it->second.replayDeviceAddr + (transformAddr - it->first);
                            }
                            else {
                                vktrace_LogError("Can't find the replay device address for transformData[%d][%d] = %p in packet = %d", i, j, transformAddr, pPacket->header->global_packet_index);
                            }
                        }
                    }
                    break;
                case VK_GEOMETRY_TYPE_AABBS_KHR:
                    if (pPacket->pInfos[i].pGeometries[j].geometry.aabbs.data.deviceAddress != 0 && supportBufferCaptureReplay == false) {
                        VkDeviceAddress aabbAddr = pPacket->pInfos[i].pGeometries[j].geometry.aabbs.data.deviceAddress;
                        it = traceDeviceAddrToReplayDeviceAddr4Buf.find(aabbAddr);
                        if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                            const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.aabbs.data.deviceAddress = it->second.replayDeviceAddr;
                        } else {
                            vktrace_LogDebug("Can't find the replay device address for aabbData[%d][%d] = %p in packet = %d. Trying to find the closest address",
                                    i, j, aabbAddr, pPacket->header->global_packet_index);
                            it = findClosestAddress(aabbAddr);
                            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                                vktrace_LogDebug("Found a closest one = %p. We'll try to use the offset = %d of it.", it->first, aabbAddr - it->first);
                                const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.aabbs.data.deviceAddress = it->second.replayDeviceAddr + (aabbAddr - it->first);
                            }
                            else {
                                vktrace_LogError("Can't find the replay device address for aabbData[%d][%d] = %p in packet = %d", i, j, aabbAddr, pPacket->header->global_packet_index);
                            }
                        }
                    }
                    break;
                case VK_GEOMETRY_TYPE_INSTANCES_KHR:
                    if (pPacket->pInfos[i].pGeometries[j].geometry.instances.data.deviceAddress != 0) {
                        VkDeviceAddress instanceAddr = pPacket->pInfos[i].pGeometries[j].geometry.instances.data.deviceAddress;
                        it = traceDeviceAddrToReplayDeviceAddr4Buf.find(instanceAddr);
                        bool foundAddr = false;
                        int offset = 0;
                        if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                            const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.instances.data.deviceAddress = it->second.replayDeviceAddr;
                            foundAddr = true;
                        } else {
                            vktrace_LogDebug("Can't find the replay device address for instanceData[%d][%d] = %p in packet = %d. Trying to find the closest address",
                                    i, j, instanceAddr, pPacket->header->global_packet_index);
                            it = findClosestAddress(instanceAddr);
                            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                                offset = instanceAddr - it->first;
                                vktrace_LogDebug("Found a closest one = %p. We'll try to use the offset = %d of it.", it->first, offset);
                                const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.instances.data.deviceAddress = it->second.replayDeviceAddr + (instanceAddr - it->first);
                                foundAddr = true;
                            }
                            else {
                                vktrace_LogError("Can't find the replay device address for instanceData[%d][%d] = %p in packet = %d", i, j, instanceAddr, pPacket->header->global_packet_index);
                            }
                            if (foundAddr && supportASCaptureReplay == false) {
                                if (pPacket->pInfos[i].pGeometries[j].geometry.instances.arrayOfPointers) {
                                    vktrace_LogAlways("NOTE: geometry.instances.arrayOfPointers is true, need implement.");
                                }
                            }
                        }
                    }
                    break;
                default:
                    vktrace_LogError("Can't support the geometry type.");
            }
        }
    }

    m_vkDeviceFuncs.CmdBuildAccelerationStructuresKHR(remappedcommandBuffer, pPacket->infoCount, pPacket->pInfos, pPacket->ppBuildRangeInfos);
}

void vkReplay::manually_replay_vkCmdBuildAccelerationStructuresIndirectKHR(packet_vkCmdBuildAccelerationStructuresIndirectKHR* pPacket) {
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdBuildAccelerationStructuresIndirectKHR() due to invalid remapped VkCommandBuffer.");
        return ;
    }
    bool supportASCaptureReplay = false;
    bool supportBufferCaptureReplay = false;
    auto it_dev = replayCommandBufferToReplayDevice.find(remappedcommandBuffer);
    if (it_dev != replayCommandBufferToReplayDevice.end()) {
        auto it_fea = replayDeviceToFeatureSupport.find(it_dev->second);
        if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.accelerationStructureCaptureReplay) {
            supportASCaptureReplay = true;
        }
        if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.bufferDeviceAddressCaptureReplay) {
            supportBufferCaptureReplay = true;
        }
    }
    // No need to remap infoCount
    // No need to remap pIndirectStrides
    // No need to remap ppMaxPrimitiveCounts
    for (uint32_t i = 0; i < pPacket->infoCount; i++) {
        auto dait = traceDeviceAddrToReplayDeviceAddr4Buf.find(pPacket->pIndirectDeviceAddresses[i]);
        if (dait != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
            const_cast<VkDeviceAddress*>(pPacket->pIndirectDeviceAddresses)[i] = dait->second.replayDeviceAddr;
        } else {
            dait = findClosestAddress(pPacket->pIndirectDeviceAddresses[i]);
            if (dait != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                vktrace_LogDebug("Found a closest one = %llu. We'll try to use the offset = %d of it.", dait->first, pPacket->pIndirectDeviceAddresses[i] - dait->first);
                const_cast<VkDeviceAddress*>(pPacket->pIndirectDeviceAddresses)[i] = dait->second.replayDeviceAddr + (pPacket->pIndirectDeviceAddresses[i] - dait->first);
            }
        }

        VkAccelerationStructureKHR replaySrcAS = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfos[i].srcAccelerationStructure);
        VkAccelerationStructureKHR replayDstAS = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfos[i].dstAccelerationStructure);
        if (pPacket->pInfos[i].srcAccelerationStructure != VK_NULL_HANDLE && replaySrcAS != VK_NULL_HANDLE) {
            const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pInfos)[i].srcAccelerationStructure = replaySrcAS;
        } else if (pPacket->pInfos[i].srcAccelerationStructure != VK_NULL_HANDLE && replaySrcAS == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in CmdBuildAccelerationStructuresIndirectKHR() due to invalid remapped VkAccelerationStructureKHR = %llu.", pPacket->pInfos[i].srcAccelerationStructure);
            return ;
        }
        if (pPacket->pInfos[i].dstAccelerationStructure != VK_NULL_HANDLE && replayDstAS != VK_NULL_HANDLE) {
            const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pInfos)[i].dstAccelerationStructure = replayDstAS;
        } else if (pPacket->pInfos[i].dstAccelerationStructure != VK_NULL_HANDLE && replayDstAS == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in CmdBuildAccelerationStructuresIndirectKHR() due to invalid remapped VkAccelerationStructureKHR = %llu.", pPacket->pInfos[i].dstAccelerationStructure);
            return ;
        }

        auto it = traceDeviceAddrToReplayDeviceAddr4Buf.find(pPacket->pInfos[i].scratchData.deviceAddress);
        if (it !=  traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
            const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pInfos)[i].scratchData.deviceAddress = it->second.replayDeviceAddr;
        } else {
            it = findClosestAddress(pPacket->pInfos[i].scratchData.deviceAddress);
            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                vktrace_LogDebug("Found a closest one = %llu. We'll try to use the offset = %d of it.", it->first, pPacket->pInfos[i].scratchData.deviceAddress - it->first);
                const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(pPacket->pInfos)[i].scratchData.deviceAddress = it->second.replayDeviceAddr + (pPacket->pInfos[i].scratchData.deviceAddress - it->first);
            }
        }

        for(uint32_t j = 0; j < pPacket->pInfos[i].geometryCount; j++) {
            switch (pPacket->pInfos[i].pGeometries[j].geometryType) {
                case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
                    if (pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexData.deviceAddress != 0 && supportBufferCaptureReplay == false) {
                        VkDeviceAddress vertexAddr = pPacket->pInfos[i].pGeometries[j].geometry.triangles.vertexData.deviceAddress;
                        it = traceDeviceAddrToReplayDeviceAddr4Buf.find(vertexAddr);
                        if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                            const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.triangles.vertexData.deviceAddress = it->second.replayDeviceAddr;
                        } else {
                            vktrace_LogDebug("Can't find the replay device address for vertexData[%d][%d] = %p in packet = %d. Trying to find the closest address",
                                    i, j, vertexAddr, pPacket->header->global_packet_index);
                            it = findClosestAddress(vertexAddr);
                            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                                vktrace_LogDebug("Found a closest one = %p. We'll try to use the offset = %d of it.", it->first, vertexAddr - it->first);
                                const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.triangles.vertexData.deviceAddress = it->second.replayDeviceAddr + (vertexAddr - it->first);
                            }
                            else {
                                vktrace_LogError("Can't find the replay device address for vertexData[%d][%d] = %p in packet = %d", i, j, vertexAddr, pPacket->header->global_packet_index);
                            }
                        }
                    }
                    if (pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexData.deviceAddress != 0 && supportBufferCaptureReplay == false) {
                        VkDeviceAddress indexAddr = pPacket->pInfos[i].pGeometries[j].geometry.triangles.indexData.deviceAddress;
                        it = traceDeviceAddrToReplayDeviceAddr4Buf.find(indexAddr);
                        if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                            const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.triangles.indexData.deviceAddress = it->second.replayDeviceAddr;
                        } else {
                            vktrace_LogDebug("Can't find the replay device address for indexData[%d][%d] = %p in packet = %d. Trying to find the closest address",
                                    i, j, indexAddr, pPacket->header->global_packet_index);
                            it = findClosestAddress(indexAddr);
                            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                                vktrace_LogDebug("Found a closest one = %p. We'll try to use the offset = %d of it.", it->first, indexAddr - it->first);
                                const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.triangles.indexData.deviceAddress = it->second.replayDeviceAddr + (indexAddr - it->first);
                            }
                            else {
                                vktrace_LogError("Can't find the replay device address for indexData[%d][%d] = %p in packet = %d", i, j, indexAddr, pPacket->header->global_packet_index);
                            }
                        }
                    }
                    if (pPacket->pInfos[i].pGeometries[j].geometry.triangles.transformData.deviceAddress != 0 && supportBufferCaptureReplay == false) {
                        VkDeviceAddress transformAddr = pPacket->pInfos[i].pGeometries[j].geometry.triangles.transformData.deviceAddress;
                        it = traceDeviceAddrToReplayDeviceAddr4Buf.find(transformAddr);
                        if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                            const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.triangles.transformData.deviceAddress = it->second.replayDeviceAddr;
                        } else {
                            vktrace_LogDebug("Can't find the replay device address for transformData[%d][%d] = %p in packet = %d. Trying to find the closest address",
                                    i, j, transformAddr, pPacket->header->global_packet_index);
                            it = findClosestAddress(transformAddr);
                            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                                vktrace_LogDebug("Found a closest one = %p. We'll try to use the offset = %d of it.", it->first, transformAddr - it->first);
                                const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.triangles.transformData.deviceAddress = it->second.replayDeviceAddr + (transformAddr - it->first);
                            }
                            else {
                                vktrace_LogError("Can't find the replay device address for transformData[%d][%d] = %p in packet = %d", i, j, transformAddr, pPacket->header->global_packet_index);
                            }
                        }
                    }
                    break;
                case VK_GEOMETRY_TYPE_AABBS_KHR:
                    if (pPacket->pInfos[i].pGeometries[j].geometry.aabbs.data.deviceAddress != 0 && supportBufferCaptureReplay == false) {
                        VkDeviceAddress aabbAddr = pPacket->pInfos[i].pGeometries[j].geometry.aabbs.data.deviceAddress;
                        it = traceDeviceAddrToReplayDeviceAddr4Buf.find(aabbAddr);
                        if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                            const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.aabbs.data.deviceAddress = it->second.replayDeviceAddr;
                        } else {
                            vktrace_LogDebug("Can't find the replay device address for aabbData[%d][%d] = %p in packet = %d. Trying to find the closest address",
                                    i, j, aabbAddr, pPacket->header->global_packet_index);
                            it = findClosestAddress(aabbAddr);
                            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                                vktrace_LogDebug("Found a closest one = %p. We'll try to use the offset = %d of it.", it->first, aabbAddr - it->first);
                                const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.aabbs.data.deviceAddress = it->second.replayDeviceAddr + (aabbAddr - it->first);
                            }
                            else {
                                vktrace_LogError("Can't find the replay device address for aabbData[%d][%d] = %p in packet = %d", i, j, aabbAddr, pPacket->header->global_packet_index);
                            }
                        }
                    }
                    break;
                case VK_GEOMETRY_TYPE_INSTANCES_KHR:
                    if (pPacket->pInfos[i].pGeometries[j].geometry.instances.data.deviceAddress != 0) {
                        VkDeviceAddress instanceAddr = pPacket->pInfos[i].pGeometries[j].geometry.instances.data.deviceAddress;
                        it = traceDeviceAddrToReplayDeviceAddr4Buf.find(instanceAddr);
                        bool foundAddr = false;
                        int offset = 0;
                        if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                            const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.instances.data.deviceAddress = it->second.replayDeviceAddr;
                            foundAddr = true;
                        } else {
                            vktrace_LogDebug("Can't find the replay device address for instanceData[%d][%d] = %p in packet = %d. Trying to find the closest address",
                                    i, j, instanceAddr, pPacket->header->global_packet_index);
                            it = findClosestAddress(instanceAddr);
                            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                                offset = instanceAddr - it->first;
                                vktrace_LogDebug("Found a closest one = %p. We'll try to use the offset = %d of it.", it->first, offset);
                                const_cast<VkAccelerationStructureGeometryKHR*>(pPacket->pInfos[i].pGeometries)[j].geometry.instances.data.deviceAddress = it->second.replayDeviceAddr + (instanceAddr - it->first);
                                foundAddr = true;
                            }
                            else {
                                vktrace_LogError("Can't find the replay device address for instanceData[%d][%d] = %p in packet = %d", i, j, instanceAddr, pPacket->header->global_packet_index);
                            }
                            if (foundAddr && supportASCaptureReplay == false) {
                                if (pPacket->pInfos[i].pGeometries[j].geometry.instances.arrayOfPointers) {
                                    vktrace_LogAlways("NOTE: geometry.instances.arrayOfPointers is true, need implement.");
                                }
                            }
                        }
                    }
                    break;
                default:
                    vktrace_LogError("Can't support the geometry type.");
            }
        }
    }

    m_vkDeviceFuncs.CmdBuildAccelerationStructuresIndirectKHR(remappedcommandBuffer, pPacket->infoCount, pPacket->pInfos, pPacket->pIndirectDeviceAddresses, pPacket->pIndirectStrides, pPacket->ppMaxPrimitiveCounts);
}

VkResult vkReplay::manually_replay_vkCopyAccelerationStructureToMemoryKHR(packet_vkCopyAccelerationStructureToMemoryKHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CopyAccelerationStructureToMemoryKHR() due to invalid remapped VkDevice.");
        return replayResult;
    }
    VkDeferredOperationKHR remappeddeferredOperation = m_objMapper.remap_deferredoperationkhrs(pPacket->deferredOperation);
    if (pPacket->deferredOperation != VK_NULL_HANDLE && remappeddeferredOperation == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CopyAccelerationStructureToMemoryKHR() due to invalid remapped VkDeferredOperationKHR.");
        return replayResult;
    }
    if (!replayDeviceToFeatureSupport[remappeddevice].accelerationStructureHostCommands) {
        vktrace_LogError("Error host build is not supported by the device.");
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    remappeddeferredOperation = VK_NULL_HANDLE;
    const_cast<VkCopyAccelerationStructureToMemoryInfoKHR*>(pPacket->pInfo)->src = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfo->src);
    assert(pPacket->pInfo->dst.hostAddress != nullptr);

    if (isMapMemoryAddress(pPacket->pInfo->dst.hostAddress)) {
        const_cast<VkCopyAccelerationStructureToMemoryInfoKHR*>(pPacket->pInfo)->dst.hostAddress = fromTraceAddr2ReplayAddr(remappeddevice, pPacket->pInfo->dst.hostAddress);
    } else {
        auto it = replayASToASBuildSizes.find(pPacket->pInfo->src);
        if (it != replayASToASBuildSizes.end()) {
            void* dst = malloc(it->second.accelerationStructureSize);
            const_cast<VkCopyAccelerationStructureToMemoryInfoKHR*>(pPacket->pInfo)->dst.hostAddress = dst;
            replayASToCopyAddress[pPacket->pInfo->src] = dst;
        } else {
            vktrace_LogError("The src AS is not exist.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
    }
    replayResult = m_vkDeviceFuncs.CopyAccelerationStructureToMemoryKHR(remappeddevice, remappeddeferredOperation, pPacket->pInfo);
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCopyMemoryToAccelerationStructureKHR(packet_vkCopyMemoryToAccelerationStructureKHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CopyMemoryToAccelerationStructureKHR() due to invalid remapped VkDevice.");
        return replayResult;
    }
    VkDeferredOperationKHR remappeddeferredOperation = m_objMapper.remap_deferredoperationkhrs(pPacket->deferredOperation);
    if (pPacket->deferredOperation != VK_NULL_HANDLE && remappeddeferredOperation == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CopyMemoryToAccelerationStructureKHR() due to invalid remapped VkDeferredOperationKHR.");
        return replayResult;
    }
    if (!replayDeviceToFeatureSupport[remappeddevice].accelerationStructureHostCommands) {
        vktrace_LogError("Error host build is not supported by the device.");
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    remappeddeferredOperation = VK_NULL_HANDLE;
    const_cast<VkCopyMemoryToAccelerationStructureInfoKHR*>(pPacket->pInfo)->dst = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfo->dst);

    assert(pPacket->pInfo->src.hostAddress != nullptr);
    if (isMapMemoryAddress(pPacket->pInfo->src.hostAddress)) {
        const_cast<VkCopyMemoryToAccelerationStructureInfoKHR*>(pPacket->pInfo)->src.hostAddress = fromTraceAddr2ReplayAddr(remappeddevice, pPacket->pInfo->src.hostAddress);
    } else {
        auto it = replayASToCopyAddress.find(pPacket->pInfo->dst);
        if (it != replayASToCopyAddress.end()) {
            const_cast<VkCopyMemoryToAccelerationStructureInfoKHR*>(pPacket->pInfo)->src.hostAddress = it->second;
        } else {
            vktrace_LogError("vkTrace doesn't support vkCopyMemoryToAccelerationStructureKHR.");
        }
    }
    replayResult = m_vkDeviceFuncs.CopyMemoryToAccelerationStructureKHR(remappeddevice, remappeddeferredOperation, pPacket->pInfo);
    return replayResult;
}

void vkReplay::manually_replay_vkCmdCopyAccelerationStructureToMemoryKHR(packet_vkCmdCopyAccelerationStructureToMemoryKHR* pPacket) {
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdCopyAccelerationStructureToMemoryKHR() due to invalid remapped VkCommandBuffer.");
        return ;
    }

    const_cast<VkCopyAccelerationStructureToMemoryInfoKHR*>(pPacket->pInfo)->src = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfo->src);
    auto it = traceDeviceAddrToReplayDeviceAddr4AS.find(pPacket->pInfo->dst.deviceAddress);
    if (it != traceDeviceAddrToReplayDeviceAddr4AS.end()) {
        const_cast<VkCopyAccelerationStructureToMemoryInfoKHR*>(pPacket->pInfo)->dst.deviceAddress = it->second.replayDeviceAddr;
    }
    m_vkDeviceFuncs.CmdCopyAccelerationStructureToMemoryKHR(remappedcommandBuffer, pPacket->pInfo);
}

void vkReplay::manually_replay_vkCmdCopyMemoryToAccelerationStructureKHR(packet_vkCmdCopyMemoryToAccelerationStructureKHR* pPacket) {
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdCopyMemoryToAccelerationStructureKHR() due to invalid remapped VkCommandBuffer.");
        return ;
    }

    const_cast<VkCopyMemoryToAccelerationStructureInfoKHR*>(pPacket->pInfo)->dst = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfo->dst);
    auto it = traceDeviceAddrToReplayDeviceAddr4AS.find(pPacket->pInfo->src.deviceAddress);
    if (it != traceDeviceAddrToReplayDeviceAddr4AS.end()) {
        const_cast<VkCopyMemoryToAccelerationStructureInfoKHR*>(pPacket->pInfo)->src.deviceAddress = it->second.replayDeviceAddr;
    }
    m_vkDeviceFuncs.CmdCopyMemoryToAccelerationStructureKHR(remappedcommandBuffer, pPacket->pInfo);
}

VkResult vkReplay::manually_replay_vkGetAccelerationStructureDeviceAddressKHR(packet_vkGetAccelerationStructureDeviceAddressKHR *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetAccelerationStructureDeviceAddressKHR() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    // No need to remap pInfo
    uint64_t traceASHandle = (uint64_t)(pPacket->pInfo->accelerationStructure);
    const_cast<VkAccelerationStructureDeviceAddressInfoKHR*>(pPacket->pInfo)->accelerationStructure = m_objMapper.remap_accelerationstructurekhrs(pPacket->pInfo->accelerationStructure);
    VkDeviceAddress replayDeviceAddr = pPacket->result;
    replayDeviceAddr = m_vkDeviceFuncs.GetAccelerationStructureDeviceAddressKHR(remappeddevice, pPacket->pInfo);
    objDeviceAddr objDeviceAddrInfo;
    objDeviceAddrInfo.replayDeviceAddr = replayDeviceAddr;
    objDeviceAddrInfo.traceObjHandle = traceASHandle;
    traceDeviceAddrToReplayDeviceAddr4AS[pPacket->result] = objDeviceAddrInfo;
    return VK_SUCCESS;
}

void vkReplay::manually_replay_vkDestroyAccelerationStructureKHR(packet_vkDestroyAccelerationStructureKHR *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in DestroyAccelerationStructureKHR() due to invalid remapped VkDevice.");
        return ;
    }
    VkAccelerationStructureKHR remappedaccelerationStructure = m_objMapper.remap_accelerationstructurekhrs(pPacket->accelerationStructure);
    if (pPacket->accelerationStructure != VK_NULL_HANDLE && remappedaccelerationStructure == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in DestroyAccelerationStructureKHR() due to invalid remapped VkAccelerationStructureKHR.");
        return ;
    }
    // No need to remap pAllocator
    m_vkDeviceFuncs.DestroyAccelerationStructureKHR(remappeddevice, remappedaccelerationStructure, pPacket->pAllocator);
    m_objMapper.rm_from_accelerationstructurekhrs_map(pPacket->accelerationStructure);

    if (replayASToASBuildSizes.find(remappedaccelerationStructure) != replayASToASBuildSizes.end())
        replayASToASBuildSizes.erase(remappedaccelerationStructure);

    for (auto it = traceDeviceAddrToReplayDeviceAddr4AS.begin(); it != traceDeviceAddrToReplayDeviceAddr4AS.end(); it++) {
        if (it->second.traceObjHandle == (uint64_t)(pPacket->accelerationStructure)) {
            traceDeviceAddrToReplayDeviceAddr4AS.erase(it);
            break;
        }
    }
}

BOOL isvkFlushMappedMemoryRangesSpecial(PBYTE pOPTPackageData) {
    BOOL bRet = FALSE;
    PageGuardChangedBlockInfo *pChangedInfoArray = (PageGuardChangedBlockInfo *)pOPTPackageData;
    if ((pOPTPackageData == nullptr) ||
        ((static_cast<uint64_t>(pChangedInfoArray[0].reserve0)) &
         PAGEGUARD_SPECIAL_FORMAT_PACKET_FOR_VKFLUSHMAPPEDMEMORYRANGES))  // TODO need think about 32bit
    {
        bRet = TRUE;
    }
    return bRet;
}

bool vkReplay::remapReplayPatternDeviceAddresssInMapedMemory(VkDevice remappedDevice, const void* pData, uint64_t size)
{
    bool ret = false;
    bool supportASCaptureReplay = false;
    bool supportBufferCaptureReplay = false;
    auto it_fea = replayDeviceToFeatureSupport.find(remappedDevice);
    if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.accelerationStructureCaptureReplay) {
        supportASCaptureReplay = true;
    }
    if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.bufferDeviceAddressCaptureReplay) {
        supportBufferCaptureReplay = true;
    }

    if ((supportASCaptureReplay && supportBufferCaptureReplay) || !g_hasAsApi || !pData) {
        return ret;
    }

    // set Special PatternA ro remap device address
    if (g_pReplaySettings->specialPatternConfig == 1) {
        // remap all VkDeviceAddress from trace values to replay values
        VkDeviceAddress *pDeviceAddress = (VkDeviceAddress *)(pData);
        for (unsigned j = 0; j < size / sizeof(VkDeviceAddress); ++j) {
            auto it1 = traceDeviceAddrToReplayDeviceAddr4AS.find(pDeviceAddress[j]);
            if (!supportASCaptureReplay && it1 != traceDeviceAddrToReplayDeviceAddr4AS.end()) {
                pDeviceAddress[j] = it1->second.replayDeviceAddr;
                ret = true;
                continue;
            }

            auto it0 = traceDeviceAddrToReplayDeviceAddr4Buf.find(pDeviceAddress[j]);
            if (!supportBufferCaptureReplay && it0 != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                pDeviceAddress[j] = it0->second.replayDeviceAddr;
                ret = true;
            } else if (!supportBufferCaptureReplay && it0 == traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                it0 = findClosestAddress(pDeviceAddress[j]);
                if (it0 != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                    vktrace_LogDebug("*Found a closest one = %llu. We'll try to use the offset = %d of it.", it0->first, pDeviceAddress[j] - it0->first);
                    pDeviceAddress[j] = it0->second.replayDeviceAddr + (pDeviceAddress[j] - it0->first);
                    ret = true;
                }
            }
        }
    }

    return ret;
}

// after OPT speed up, the format of this packet will be different with before, the packet now only include changed block(page).
//
VkResult vkReplay::manually_replay_vkFlushMappedMemoryRanges(packet_vkFlushMappedMemoryRanges *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkFlushMappedMemoryRanges() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkMappedMemoryRange *localRanges = (VkMappedMemoryRange *)pPacket->pMemoryRanges;

    VkDeviceMemory* flushedMems = NULL;
    uint32_t flushedCount = 0;
    if (!isvkFlushMappedMemoryRangesSpecial((PBYTE)pPacket->ppData[0])) {
        flushedMems = VKTRACE_NEW_ARRAY(VkDeviceMemory, pPacket->memoryRangeCount);
        memset(flushedMems, 0, sizeof(VkDeviceMemory) * pPacket->memoryRangeCount);
    }

    std::vector<VkDeviceMemory> traceMems;
    for (uint32_t i = 0; i < pPacket->memoryRangeCount; i++) {
        const devicememoryObj* localMem = nullptr;
        if (m_objMapper.devicememory_exists(pPacket->pMemoryRanges[i].memory)) {
            localMem = &(m_objMapper.find_devicememory(pPacket->pMemoryRanges[i].memory));
        } else if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() &&
                   traceSkippedDeviceMemories.find(pPacket->pMemoryRanges[i].memory) != traceSkippedDeviceMemories.end()) {
            vktrace_LogDebug("Skipping vkFlushMappedMemoryRanges() due to VkDeviceMemory 0x%llX skipped in vkAllocateMemory.",
                             pPacket->pMemoryRanges[i].memory);
            return VK_SUCCESS;
        }

        traceMems.push_back(pPacket->pMemoryRanges[i].memory);
        localRanges[i].memory = m_objMapper.remap_devicememorys(pPacket->pMemoryRanges[i].memory);
        if (localRanges[i].memory == VK_NULL_HANDLE || localMem->pGpuMem == NULL) {
            vktrace_LogError("Skipping vkFlushMappedMemoryRanges() due to invalid remapped VkDeviceMemory.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }

        if (flushedMems) {
            uint32_t j = 0;
            while (j < flushedCount) {
                if (flushedMems[j] == pPacket->pMemoryRanges[i].memory)
                    break;
                j++;
            }
            if (j < flushedCount)
                continue;
            else
                flushedMems[flushedCount++] = pPacket->pMemoryRanges[i].memory;
        }

#if defined(USE_PAGEGUARD_SPEEDUP)
        remapInstanceBufferDeviceAddressInFlushMemory(remappedDevice, traceMems[i], localRanges[i].memory, pPacket->ppData[i], false);
#endif

        if (!localMem->pGpuMem->isPendingAlloc()) {
            if (pPacket->pMemoryRanges[i].size != 0) {
#if defined(USE_PAGEGUARD_SPEEDUP)
                if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5))
                    localMem->pGpuMem->copyMappingDataPageGuard(pPacket->ppData[i]);
                else
                    localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                           pPacket->pMemoryRanges[i].offset);
#else
                localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                       pPacket->pMemoryRanges[i].offset);
#endif
            }
        } else {
            unsigned char *pBuf = (unsigned char *)vktrace_malloc(localMem->pGpuMem->getMemoryMapSize());
            if (!pBuf) {
                vktrace_LogError("vkFlushMappedMemoryRanges() malloc failed.");
            }
            localMem->pGpuMem->setMemoryDataAddr(pBuf);
#if defined(USE_PAGEGUARD_SPEEDUP)
            if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5))
                localMem->pGpuMem->copyMappingDataPageGuard(pPacket->ppData[i]);
            else
                localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                       pPacket->pMemoryRanges[i].offset);
#else
            localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                   pPacket->pMemoryRanges[i].offset);
#endif
        }
    }
#if defined(USE_PAGEGUARD_SPEEDUP)
    replayResult = pPacket->result;  // if this is a OPT refresh-all packet, we need avoid to call real api and return original
                                     // return to avoid error message;
    if (!vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5) || !isvkFlushMappedMemoryRangesSpecial((PBYTE)pPacket->ppData[0]))
#endif
    {
        if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_10) && m_inFrameRange) {
            m_CallStats[VKTRACE_TPI_VK_vkFlushMappedMemoryRanges].total++;
            if (vktrace_get_trace_packet_tag(pPacket->header) & PACKET_TAG__INJECTED) {
                m_CallStats[VKTRACE_TPI_VK_vkFlushMappedMemoryRanges].injectedCallCount++;
                if (replaySettings.perfMeasuringMode > 0) {
                    VkPresentInfoKHR PresentInfo = {};
                    PresentInfo.sType = VK_STRUCTURE_TYPE_MAX_ENUM;
                    PresentInfo.waitSemaphoreCount = 1;
                    m_vkDeviceFuncs.QueuePresentKHR(VK_NULL_HANDLE, &PresentInfo);
                }
            }
        }
        replayResult = m_vkDeviceFuncs.FlushMappedMemoryRanges(remappedDevice, pPacket->memoryRangeCount, localRanges);
        if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_10) && m_inFrameRange) {
            if ((vktrace_get_trace_packet_tag(pPacket->header) & PACKET_TAG__INJECTED) && replaySettings.perfMeasuringMode > 0) {
                VkPresentInfoKHR PresentInfo = {};
                PresentInfo.sType = VK_STRUCTURE_TYPE_MAX_ENUM;
                PresentInfo.waitSemaphoreCount = 0;
                m_vkDeviceFuncs.QueuePresentKHR(VK_NULL_HANDLE, &PresentInfo);
            }
        }
    }

    return replayResult;
}

typedef struct MemoryRangeBlockInfo {
    uint32_t offset;
    uint32_t length;
    uint32_t reserve0;
    uint32_t reserve1;
} MemoryRangeBlockInfo;

bool vkReplay::remapReplayPatternDeviceAddresssInFlushMemory(VkDevice remappedDevice, const void* pSrcData)
{
    bool ret = false;
    bool supportASCaptureReplay = false;
    bool supportBufferCaptureReplay = false;
    auto it_fea = replayDeviceToFeatureSupport.find(remappedDevice);
    if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.accelerationStructureCaptureReplay) {
        supportASCaptureReplay = true;
    }
    if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.bufferDeviceAddressCaptureReplay) {
        supportBufferCaptureReplay = true;
    }

    if ((supportASCaptureReplay && supportBufferCaptureReplay) || !g_hasAsApi || !pSrcData) {
        return ret;
    }

    // set Special PatternA ro remap device address
    if (g_pReplaySettings->specialPatternConfig == 1) {
        // remap all VkDeviceAddress from trace values to replay values
        MemoryRangeBlockInfo *pChangedInfoArray = (MemoryRangeBlockInfo *)pSrcData;
        if (pChangedInfoArray[0].length) {
            PBYTE pChangedData = (PBYTE)(pSrcData) + sizeof(MemoryRangeBlockInfo) * (pChangedInfoArray[0].offset + 1);
            size_t CurrentOffset = 0;
            for (size_t i = 0; i < pChangedInfoArray[0].offset; i++) {
                if (pChangedInfoArray[i + 1].length) {
                    PBYTE pData = pChangedData + CurrentOffset;
                    VkDeviceAddress* pDeviceAddress = (VkDeviceAddress *)pData;
                    for (unsigned j = 0; j < pChangedInfoArray[i + 1].length / sizeof(VkDeviceAddress); ++j) {
                        if (!supportASCaptureReplay) {
                            auto it1 = traceDeviceAddrToReplayDeviceAddr4AS.find(pDeviceAddress[j]);
                            if (it1 != traceDeviceAddrToReplayDeviceAddr4AS.end()) {
                                pDeviceAddress[j] = it1->second.replayDeviceAddr;
                                ret = true;
                                continue;
                            }
                        }

                        if (!supportBufferCaptureReplay) {
                            auto it0 = traceDeviceAddrToReplayDeviceAddr4Buf.find(pDeviceAddress[j]);
                            if (it0 != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                                pDeviceAddress[j] = it0->second.replayDeviceAddr;
                                ret = true;
                            } else {
                                it0 = findClosestAddress(pDeviceAddress[j]);
                                if (it0 != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                                    vktrace_LogDebug("*Found a closest one = %llu. We'll try to use the offset = %d of it.", it0->first, pDeviceAddress[j] - it0->first);
                                    pDeviceAddress[j] = it0->second.replayDeviceAddr + (pDeviceAddress[j] - it0->first);
                                    ret = true;
                                }
                            }
                        }
                    }
                }
                CurrentOffset += pChangedInfoArray[i + 1].length;
            }
        }
    }
    return ret;
}

VkResult vkReplay::manually_replay_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle(packet_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkFlushMappedMemoryRanges() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkMappedMemoryRange *localRanges = (VkMappedMemoryRange *)pPacket->pMemoryRanges;

    VkDeviceMemory* flushedMems = NULL;
    uint32_t flushedCount = 0;
    if (!isvkFlushMappedMemoryRangesSpecial((PBYTE)pPacket->ppData[0])) {
        flushedMems = VKTRACE_NEW_ARRAY(VkDeviceMemory, pPacket->memoryRangeCount);
        memset(flushedMems, 0, sizeof(VkDeviceMemory) * pPacket->memoryRangeCount);
    }

    std::vector<VkDeviceMemory> traceMems;
    for (uint32_t i = 0; i < pPacket->memoryRangeCount; i++) {
        const devicememoryObj* localMem = nullptr;
        if (m_objMapper.devicememory_exists(pPacket->pMemoryRanges[i].memory)) {
            localMem = &(m_objMapper.find_devicememory(pPacket->pMemoryRanges[i].memory));
        } else if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() &&
                   traceSkippedDeviceMemories.find(pPacket->pMemoryRanges[i].memory) != traceSkippedDeviceMemories.end()) {
            vktrace_LogDebug("Skipping vkFlushMappedMemoryRanges() due to VkDeviceMemory 0x%llX skipped in vkAllocateMemory.",
                             pPacket->pMemoryRanges[i].memory);
            return VK_SUCCESS;
        }

        traceMems.push_back(pPacket->pMemoryRanges[i].memory);
        localRanges[i].memory = m_objMapper.remap_devicememorys(pPacket->pMemoryRanges[i].memory);
        if (localRanges[i].memory == VK_NULL_HANDLE || localMem->pGpuMem == NULL) {
            vktrace_LogError("Skipping vkFlushMappedMemoryRanges() due to invalid remapped VkDeviceMemory.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }

        if (flushedMems) {
            uint32_t j = 0;
            while (j < flushedCount) {
                if (flushedMems[j] == pPacket->pMemoryRanges[i].memory)
                    break;
                j++;
            }
            if (j < flushedCount)
                continue;
            else
                flushedMems[flushedCount++] = pPacket->pMemoryRanges[i].memory;
        }

        int *pAsOffset = (int*)pPacket->ppDataAsOffset[i];
        MemoryRangeBlockInfo *pChangedInfoArray = (MemoryRangeBlockInfo *)pPacket->ppData[i];
        PBYTE pChangedData = (PBYTE)(pPacket->ppData[i]) + sizeof(MemoryRangeBlockInfo) * (pChangedInfoArray[0].offset + 1);
        int offsetCount = pAsOffset[0];
        for (int j = 1; j <= offsetCount; ++j) {
            uint64_t *addr = (uint64_t*)(pChangedData + pAsOffset[j]);

            auto it = traceDeviceAddrToReplayDeviceAddr4AS.find(*addr);
            if (it != traceDeviceAddrToReplayDeviceAddr4AS.end()) {
                *addr = it->second.replayDeviceAddr;
            } else {
                vktrace_LogError("0x%llx is not a valid acceleration structure reference!", *addr);
            }
        }

        int *pBufOffset = (int*)pPacket->ppDataBufferOffset[i];
        pChangedInfoArray = (MemoryRangeBlockInfo *)pPacket->ppData[i];
        pChangedData = (PBYTE)(pPacket->ppData[i]) + sizeof(MemoryRangeBlockInfo) * (pChangedInfoArray[0].offset + 1);
        offsetCount = pBufOffset[0];
        for (int j = 1; j <= offsetCount; ++j) {
            uint64_t *addr = (uint64_t*)(pChangedData + pBufOffset[j]);

            auto it = traceDeviceAddrToReplayDeviceAddr4Buf.find(*addr);
            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                *addr = it->second.replayDeviceAddr;
            } else {
                it = findClosestAddress(*addr);
                if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                    vktrace_LogDebug("*Found a closest one = %llu. We'll try to use the offset = %d of it.", it->first, *addr - it->first);
                    *addr = it->second.replayDeviceAddr + (*addr - it->first);
                } else {
                    vktrace_LogError("0x%llx is not a valid buffer device address!", *addr);
                }
            }
        }

        uint64_t *pSGHandleOffset = (uint64_t*)pPacket->ppDataSGHandleOffset[i];
        pChangedInfoArray = (MemoryRangeBlockInfo *)pPacket->ppData[i];
        pChangedData = (PBYTE)(pPacket->ppData[i]) + sizeof(MemoryRangeBlockInfo) * (pChangedInfoArray[0].offset + 1);
        offsetCount = pSGHandleOffset[0];
        for (int j = 0; j < offsetCount; ++j) {
            sbtPostprocessed = true;
            uint64_t combinedValue = pSGHandleOffset[j * 2 + 1];
            uint32_t offset = combinedValue >> 32;
            uint32_t sgHandleIdx = combinedValue & 0xFFFFFFFF;
            VkPipeline pipeline = (VkPipeline)pSGHandleOffset[j * 2 + 2];
            uint64_t *addr = (uint64_t*)(pChangedData + offset);

            VkPipeline remappedpipeline = m_objMapper.remap_pipelines(pipeline);
            uint32_t sgHandleSize = RayTracingPipelineShaderInfo::physicalRtProperties.shaderGroupHandleSize;
            uint8_t *sgData = (uint8_t*)rtHandler->getHandleData(remappedpipeline);
            memcpy(addr, sgData + sgHandleIdx * sgHandleSize, sgHandleSize);
        }

        if (!localMem->pGpuMem->isPendingAlloc()) {
            if (pPacket->pMemoryRanges[i].size != 0) {
#if defined(USE_PAGEGUARD_SPEEDUP)
                if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5))
                    localMem->pGpuMem->copyMappingDataPageGuard(pPacket->ppData[i]);
                else
                    localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                           pPacket->pMemoryRanges[i].offset);
#else
                localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                       pPacket->pMemoryRanges[i].offset);
#endif
            }
        } else {
            unsigned char *pBuf = (unsigned char *)vktrace_malloc(localMem->pGpuMem->getMemoryMapSize());
            if (!pBuf) {
                vktrace_LogError("vkFlushMappedMemoryRanges() malloc failed.");
            }
            localMem->pGpuMem->setMemoryDataAddr(pBuf);
#if defined(USE_PAGEGUARD_SPEEDUP)
            if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5))
                localMem->pGpuMem->copyMappingDataPageGuard(pPacket->ppData[i]);
            else
                localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                       pPacket->pMemoryRanges[i].offset);
#else
            localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                   pPacket->pMemoryRanges[i].offset);
#endif
        }
    }
#if defined(USE_PAGEGUARD_SPEEDUP)
    replayResult = pPacket->result;  // if this is a OPT refresh-all packet, we need avoid to call real api and return original
                                     // return to avoid error message;
    if (!vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5) || !isvkFlushMappedMemoryRangesSpecial((PBYTE)pPacket->ppData[0]))
#endif
    {
        if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_10) && m_inFrameRange) {
            m_CallStats[VKTRACE_TPI_VK_vkFlushMappedMemoryRanges].total++;
            if (vktrace_get_trace_packet_tag(pPacket->header) & PACKET_TAG__INJECTED) {
                m_CallStats[VKTRACE_TPI_VK_vkFlushMappedMemoryRanges].injectedCallCount++;
                if (replaySettings.perfMeasuringMode > 0) {
                    VkPresentInfoKHR PresentInfo = {};
                    PresentInfo.sType = VK_STRUCTURE_TYPE_MAX_ENUM;
                    PresentInfo.waitSemaphoreCount = 1;
                    m_vkDeviceFuncs.QueuePresentKHR(VK_NULL_HANDLE, &PresentInfo);
                }
            }
        }
        replayResult = m_vkDeviceFuncs.FlushMappedMemoryRanges(remappedDevice, pPacket->memoryRangeCount, localRanges);
        if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_10) && m_inFrameRange) {
            if ((vktrace_get_trace_packet_tag(pPacket->header) & PACKET_TAG__INJECTED) && replaySettings.perfMeasuringMode > 0) {
                VkPresentInfoKHR PresentInfo = {};
                PresentInfo.sType = VK_STRUCTURE_TYPE_MAX_ENUM;
                PresentInfo.waitSemaphoreCount = 0;
                m_vkDeviceFuncs.QueuePresentKHR(VK_NULL_HANDLE, &PresentInfo);
            }
        }
    }

    return replayResult;
}

VkResult vkReplay::manually_replay_vkUnmapMemoryRemapAsInstanceSGHandle(packet_vkUnmapMemoryRemapAsInstanceSGHandle* pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkUnmapMemory() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (!m_objMapper.devicememory_exists(pPacket->memory)) {
        if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() &&
            traceSkippedDeviceMemories.find(pPacket->memory) != traceSkippedDeviceMemories.end()) {
            vktrace_LogDebug("Skipping vkUnmapMemory() due to VkDeviceMemory 0x%llX skipped in vkAllocateMemory.", pPacket->memory);
            return VK_SUCCESS;
        }
        vktrace_LogError("Skipping vkUnmapMemory() due to invalid remapped VkDeviceMemory.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (pPacket->pData) {
        int *pAsOffset = (int*)pPacket->pDataAsOffset;
        PBYTE pChangedData = (PBYTE)(pPacket->pData);
        int offsetCount = pAsOffset[0];
        for (int j = 1; j <= offsetCount; ++j) {
            uint64_t *addr = (uint64_t*)(pChangedData + pAsOffset[j]);

            auto it = traceDeviceAddrToReplayDeviceAddr4AS.find(*addr);
            if (it != traceDeviceAddrToReplayDeviceAddr4AS.end()) {
                *addr = it->second.replayDeviceAddr;
            } else {
                vktrace_LogError("0x%llx is not a valid acceleration structure reference!", *addr);
            }
        }

        int *pBufOffset = (int*)pPacket->pDataBufferOffset;
        pChangedData = (PBYTE)(pPacket->pData);
        offsetCount = pBufOffset[0];
        for (int j = 1; j <= offsetCount; ++j) {
            uint64_t *addr = (uint64_t*)(pChangedData + pBufOffset[j]);

            auto it = traceDeviceAddrToReplayDeviceAddr4Buf.find(*addr);
            if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                *addr = it->second.replayDeviceAddr;
            } else {
                it = findClosestAddress(*addr);
                if (it != traceDeviceAddrToReplayDeviceAddr4Buf.end()) {
                    vktrace_LogDebug("*Found a closest one = %llu. We'll try to use the offset = %d of it.", it->first, *addr - it->first);
                    *addr = it->second.replayDeviceAddr + (*addr - it->first);
                } else {
                    vktrace_LogError("0x%llx is not a valid buffer device address!", *addr);
                }
            }
        }

        uint64_t *pSGHandleOffset = (uint64_t*)pPacket->pDataSGHandleOffset;
        pChangedData = (PBYTE)(pPacket->pData);
        offsetCount = pSGHandleOffset[0];
        for (int j = 1; j <= offsetCount; ++j) {
            sbtPostprocessed = true;
            uint64_t combinedValue = pSGHandleOffset[j * 2 + 1];
            uint32_t offset = combinedValue >> 32;
            uint32_t sgHandleIdx = combinedValue & 0xFFFFFFFF;
            VkPipeline pipeline = (VkPipeline)pSGHandleOffset[j * 2 + 2];
            uint64_t *addr = (uint64_t*)(pChangedData + offset);

            VkPipeline remappedpipeline = m_objMapper.remap_pipelines(pipeline);
            uint32_t sgHandleSize = RayTracingPipelineShaderInfo::physicalRtProperties.shaderGroupHandleSize;
            uint8_t *sgData = (uint8_t*)rtHandler->getHandleData(remappedpipeline);
            memcpy(addr, sgData + sgHandleIdx * sgHandleSize, sgHandleSize);
        }
    }

    devicememoryObj local_mem = m_objMapper.find_devicememory(pPacket->memory);
    if (!local_mem.pGpuMem->isPendingAlloc()) {
        if (local_mem.pGpuMem) {
            remapInstanceBufferDeviceAddressInMapedMemory(remappedDevice, pPacket->memory, local_mem.replayDeviceMemory, pPacket->pData, local_mem.pGpuMem->getMemoryMapSize());
            if (pPacket->pData)
                local_mem.pGpuMem->copyMappingData(pPacket->pData, true, 0, 0);  // copies data from packet into memory buffer
        }
        m_vkDeviceFuncs.UnmapMemory(remappedDevice, local_mem.replayDeviceMemory);
        auto it = replayMemoryToMapAddress.find(local_mem.replayDeviceMemory);
        if (it != replayMemoryToMapAddress.end()) {
            replayMemoryToMapAddress.erase(it);
        }
    } else {
        if (local_mem.pGpuMem) {
            unsigned char *pBuf = (unsigned char *)vktrace_malloc(local_mem.pGpuMem->getMemoryMapSize());
            if (!pBuf) {
                vktrace_LogError("vkUnmapMemory() malloc failed.");
            }
            local_mem.pGpuMem->setMemoryDataAddr(pBuf);
            local_mem.pGpuMem->copyMappingData(pPacket->pData, true, 0, 0);
        }
    }
    return replayResult;
}

void vkReplay::remapInstanceBufferDeviceAddressInFlushMemory(VkDevice remappedDevice, VkDeviceMemory traceMemory, VkDeviceMemory remappedMemory, const void *pSrcData, bool remapBufAddr)
{
    bool supportASCaptureReplay = false;
    bool supportBufferCaptureReplay = false;
    auto it_fea = replayDeviceToFeatureSupport.find(remappedDevice);

    if (m_pFileHeader->bit_flags & VKTRACE_RQ_POSTPROCESSED_BIT)
        return;

    if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.accelerationStructureCaptureReplay) {
        supportASCaptureReplay = true;
    }
    if (it_fea != replayDeviceToFeatureSupport.end() && it_fea->second.bufferDeviceAddressCaptureReplay) {
        supportBufferCaptureReplay = true;
    }

    if ((supportASCaptureReplay && supportBufferCaptureReplay) || !g_hasAsApi || !pSrcData) {
        return;
    }

    if (remapReplayPatternDeviceAddresssInFlushMemory(remappedDevice, pSrcData)) {
        return;
    }

    bool findAsReference = false;
    for (auto iter = replayBufferToReplayDeviceMemory.begin(); iter != replayBufferToReplayDeviceMemory.end(); iter++) {
        if (remappedMemory == iter->second) {
            if (replayBufferToUsageFlag.find(iter->first) != replayBufferToUsageFlag.end()) {
                    findAsReference = true;
                    break;
            }
        }
    }

    if (traceMemoryToAllocateFlag.find(traceMemory) != traceMemoryToAllocateFlag.end()) {
        findAsReference = true;
    }

    if (!findAsReference) {
        return;
    }

    bool remapAsReference = false;
    MemoryRangeBlockInfo *pChangedInfoArray = (MemoryRangeBlockInfo *)pSrcData;
    if (pChangedInfoArray[0].length) {
        PBYTE pChangedData = (PBYTE)(pSrcData) + sizeof(MemoryRangeBlockInfo) * (pChangedInfoArray[0].offset + 1);
        size_t CurrentOffset = 0;
        for (size_t i = 0; i < pChangedInfoArray[0].offset; i++) {
            if (pChangedInfoArray[i + 1].length) {
                PBYTE pData = pChangedData + CurrentOffset;
                VkAccelerationStructureInstanceKHR* pAsInstance = (VkAccelerationStructureInstanceKHR *)pData;
                for (unsigned j = 0; j < pChangedInfoArray[i + 1].length / sizeof(VkAccelerationStructureInstanceKHR); ++j) {
                    auto it = traceDeviceAddrToReplayDeviceAddr4AS.find(pAsInstance[j].accelerationStructureReference);
                    if (!supportASCaptureReplay && it != traceDeviceAddrToReplayDeviceAddr4AS.end()) {
                        pAsInstance[j].accelerationStructureReference = it->second.replayDeviceAddr;
                        remapAsReference = true;
                    }
                }

                if (remapBufAddr) {
                    VkDeviceAddress* pDeviceAddress = (VkDeviceAddress *)pData;
                    for (unsigned j = 0; j < pChangedInfoArray[i + 1].length / sizeof(VkDeviceAddress); ++j) {
                        auto it0 = traceDeviceAddrToReplayDeviceAddr4Buf.find(pDeviceAddress[j]);
                        auto it1 = traceDeviceAddrToReplayDeviceAddr4AS.find(pDeviceAddress[j]);
                        if (!supportBufferCaptureReplay && it0 != traceDeviceAddrToReplayDeviceAddr4Buf.end() && it1 == traceDeviceAddrToReplayDeviceAddr4AS.end()) {
                            pDeviceAddress[j] = it0->second.replayDeviceAddr;
                            remapAsReference = true;
                        }
                    }
                }
            }
            CurrentOffset += pChangedInfoArray[i + 1].length;
        }
    }

    if (remapAsReference) {
        VkMappedMemoryRange memoryRange = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, remappedMemory, 0, VK_WHOLE_SIZE};
        m_vkDeviceFuncs.FlushMappedMemoryRanges(remappedDevice, 1, &memoryRange);
    }
}

VkResult vkReplay::manually_replay_vkFlushMappedMemoryRangesRemap(packet_vkFlushMappedMemoryRanges *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkFlushMappedMemoryRanges() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkMappedMemoryRange *localRanges = (VkMappedMemoryRange *)pPacket->pMemoryRanges;

    VkDeviceMemory* flushedMems = NULL;
    uint32_t flushedCount = 0;
    if (!isvkFlushMappedMemoryRangesSpecial((PBYTE)pPacket->ppData[0])) {
        flushedMems = VKTRACE_NEW_ARRAY(VkDeviceMemory, pPacket->memoryRangeCount);
        memset(flushedMems, 0, sizeof(VkDeviceMemory) * pPacket->memoryRangeCount);
    }

    std::vector<VkDeviceMemory> traceMems;
    for (uint32_t i = 0; i < pPacket->memoryRangeCount; i++) {
        const devicememoryObj* localMem = nullptr;
        if (m_objMapper.devicememory_exists(pPacket->pMemoryRanges[i].memory)) {
            localMem = &(m_objMapper.find_devicememory(pPacket->pMemoryRanges[i].memory));
        } else if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() &&
                   traceSkippedDeviceMemories.find(pPacket->pMemoryRanges[i].memory) != traceSkippedDeviceMemories.end()) {
            vktrace_LogDebug("Skipping vkFlushMappedMemoryRanges() due to VkDeviceMemory 0x%llX skipped in vkAllocateMemory.",
                             pPacket->pMemoryRanges[i].memory);
            return VK_SUCCESS;
        }

        traceMems.push_back(pPacket->pMemoryRanges[i].memory);
        localRanges[i].memory = m_objMapper.remap_devicememorys(pPacket->pMemoryRanges[i].memory);
        if (localRanges[i].memory == VK_NULL_HANDLE || localMem->pGpuMem == NULL) {
            vktrace_LogError("Skipping vkFlushMappedMemoryRanges() due to invalid remapped VkDeviceMemory.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }

        if (flushedMems) {
            uint32_t j = 0;
            while (j < flushedCount) {
                if (flushedMems[j] == pPacket->pMemoryRanges[i].memory)
                    break;
                j++;
            }
            if (j < flushedCount)
                continue;
            else
                flushedMems[flushedCount++] = pPacket->pMemoryRanges[i].memory;
        }

#if defined(USE_PAGEGUARD_SPEEDUP)
        remapInstanceBufferDeviceAddressInFlushMemory(remappedDevice, traceMems[i], localRanges[i].memory, pPacket->ppData[i], true);
#endif

        if (!localMem->pGpuMem->isPendingAlloc()) {
            if (pPacket->pMemoryRanges[i].size != 0) {
#if defined(USE_PAGEGUARD_SPEEDUP)
                if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5))
                    localMem->pGpuMem->copyMappingDataPageGuard(pPacket->ppData[i]);
                else
                    localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                           pPacket->pMemoryRanges[i].offset);
#else
                localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                       pPacket->pMemoryRanges[i].offset);
#endif
            }
        } else {
            unsigned char *pBuf = (unsigned char *)vktrace_malloc(localMem->pGpuMem->getMemoryMapSize());
            if (!pBuf) {
                vktrace_LogError("vkFlushMappedMemoryRanges() malloc failed.");
            }
            localMem->pGpuMem->setMemoryDataAddr(pBuf);
#if defined(USE_PAGEGUARD_SPEEDUP)
            if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5))
                localMem->pGpuMem->copyMappingDataPageGuard(pPacket->ppData[i]);
            else
                localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                       pPacket->pMemoryRanges[i].offset);
#else
            localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                   pPacket->pMemoryRanges[i].offset);
#endif
        }
    }
#if defined(USE_PAGEGUARD_SPEEDUP)
    replayResult = pPacket->result;  // if this is a OPT refresh-all packet, we need avoid to call real api and return original
                                     // return to avoid error message;
    if (!vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5) || !isvkFlushMappedMemoryRangesSpecial((PBYTE)pPacket->ppData[0]))
#endif
    {
        if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_10) && m_inFrameRange) {
            m_CallStats[VKTRACE_TPI_VK_vkFlushMappedMemoryRanges].total++;
            if (vktrace_get_trace_packet_tag(pPacket->header) & PACKET_TAG__INJECTED) {
                m_CallStats[VKTRACE_TPI_VK_vkFlushMappedMemoryRanges].injectedCallCount++;
                if (replaySettings.perfMeasuringMode > 0) {
                    VkPresentInfoKHR PresentInfo = {};
                    PresentInfo.sType = VK_STRUCTURE_TYPE_MAX_ENUM;
                    PresentInfo.waitSemaphoreCount = 1;
                    m_vkDeviceFuncs.QueuePresentKHR(VK_NULL_HANDLE, &PresentInfo);
                }
            }
        }
        replayResult = m_vkDeviceFuncs.FlushMappedMemoryRanges(remappedDevice, pPacket->memoryRangeCount, localRanges);
        if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_10) && m_inFrameRange) {
            if ((vktrace_get_trace_packet_tag(pPacket->header) & PACKET_TAG__INJECTED) && replaySettings.perfMeasuringMode > 0) {
                VkPresentInfoKHR PresentInfo = {};
                PresentInfo.sType = VK_STRUCTURE_TYPE_MAX_ENUM;
                PresentInfo.waitSemaphoreCount = 0;
                m_vkDeviceFuncs.QueuePresentKHR(VK_NULL_HANDLE, &PresentInfo);
            }
        }
    }

    return replayResult;
}

// after OPT speed up, the format of this packet will be different with before, the packet now only include changed block(page).
//
VkResult vkReplay::manually_replay_vkFlushMappedMemoryRangesPremapped(packet_vkFlushMappedMemoryRanges *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkDevice remappedDevice = *(reinterpret_cast<VkDevice*>(pPacket->device));
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkFlushMappedMemoryRangesPremapped() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkMappedMemoryRange *localRanges = const_cast<VkMappedMemoryRange *>(pPacket->pMemoryRanges);

    VkDeviceMemory* flushedMems = NULL;
    uint32_t flushedCount = 0;
    if (!isvkFlushMappedMemoryRangesSpecial((PBYTE)pPacket->ppData[0])) {
        flushedMems = VKTRACE_NEW_ARRAY(VkDeviceMemory, pPacket->memoryRangeCount);
        memset(flushedMems, 0, sizeof(VkDeviceMemory) * pPacket->memoryRangeCount);
    }

    for (uint32_t i = 0; i < pPacket->memoryRangeCount; i++) {
        devicememoryObj* localMem = reinterpret_cast<devicememoryObj*>(pPacket->pMemoryRanges[i].memory);
        localRanges[i].memory = localMem->replayDeviceMemory;
        if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() &&
                   traceSkippedDeviceMemories.find(localMem->traceDeviceMemory) != traceSkippedDeviceMemories.end()) {
            vktrace_LogDebug("Skipping vkFlushMappedMemoryRangesPremapped() due to VkDeviceMemory 0x%llX skipped in vkAllocateMemory.",
                             pPacket->pMemoryRanges[i].memory);
            return VK_SUCCESS;
        }
        if (localRanges[i].memory == VK_NULL_HANDLE || localMem->pGpuMem == NULL) {
            vktrace_LogError("Skipping vkFlushMappedMemoryRangesPremapped() due to invalid remapped VkDeviceMemory.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }

        if (flushedMems) {
            uint32_t j = 0;
            while (j < flushedCount) {
                if (flushedMems[j] == pPacket->pMemoryRanges[i].memory)
                    break;
                j++;
            }
            if (j < flushedCount)
                continue;
            else
                flushedMems[flushedCount++] = pPacket->pMemoryRanges[i].memory;
        }

        if (!localMem->pGpuMem->isPendingAlloc()) {
            if (pPacket->pMemoryRanges[i].size != 0) {
#if defined(USE_PAGEGUARD_SPEEDUP)
                if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5))
                    localMem->pGpuMem->copyMappingDataPageGuard(pPacket->ppData[i]);
                else
                    localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                           pPacket->pMemoryRanges[i].offset);
#else
                localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                       pPacket->pMemoryRanges[i].offset);
#endif
            }
        } else {
            unsigned char *pBuf = (unsigned char *)vktrace_malloc(localMem->pGpuMem->getMemoryMapSize());
            if (!pBuf) {
                vktrace_LogError("vkFlushMappedMemoryRangesPremapped() malloc failed.");
            }
            localMem->pGpuMem->setMemoryDataAddr(pBuf);
#if defined(USE_PAGEGUARD_SPEEDUP)
            if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5))
                localMem->pGpuMem->copyMappingDataPageGuard(pPacket->ppData[i]);
            else
                localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                       pPacket->pMemoryRanges[i].offset);
#else
            localMem->pGpuMem->copyMappingData(pPacket->ppData[i], false, pPacket->pMemoryRanges[i].size,
                                                   pPacket->pMemoryRanges[i].offset);
#endif
        }
    }

#if defined(USE_PAGEGUARD_SPEEDUP)
    replayResult = pPacket->result;  // if this is a OPT refresh-all packet, we need avoid to call real api and return original
                                     // return to avoid error message;
    if (!vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5) || !isvkFlushMappedMemoryRangesSpecial((PBYTE)pPacket->ppData[0]))
#endif
    {
        replayResult = m_vkDeviceFuncs.FlushMappedMemoryRanges(remappedDevice, pPacket->memoryRangeCount, localRanges);
    }

    return replayResult;
}

void vkReplay::manually_replay_vkCmdDrawIndexedPremapped(packet_vkCmdDrawIndexed *pPacket)
{
    VkCommandBuffer remappedCommandBuffer = *reinterpret_cast<VkCommandBuffer*>(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedCommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdDrawIndexedPremapped() due to invalid remapped VkCommandBuffer.");
        return;
    }
    // No need to remap indexCount
    // No need to remap instanceCount
    // No need to remap firstIndex
    // No need to remap vertexOffset
    // No need to remap firstInstance
    m_vkDeviceFuncs.CmdDrawIndexed(remappedCommandBuffer, pPacket->indexCount, pPacket->instanceCount, pPacket->firstIndex, pPacket->vertexOffset, pPacket->firstInstance);
    return;
}

// InvalidateMappedMemory Ranges and flushMappedMemoryRanges are similar but keep it seperate until
// functionality tested fully
VkResult vkReplay::manually_replay_vkInvalidateMappedMemoryRanges(packet_vkInvalidateMappedMemoryRanges *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkInvalidateMappedMemoryRanges() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkMappedMemoryRange *localRanges = (VkMappedMemoryRange *)pPacket->pMemoryRanges;

    devicememoryObj *pLocalMems = VKTRACE_NEW_ARRAY(devicememoryObj, pPacket->memoryRangeCount);
    for (uint32_t i = 0; i < pPacket->memoryRangeCount; i++) {
        if (m_objMapper.devicememory_exists(pPacket->pMemoryRanges[i].memory)) {
            pLocalMems[i] = m_objMapper.find_devicememory(pPacket->pMemoryRanges[i].memory);
        }
        localRanges[i].memory = m_objMapper.remap_devicememorys(pPacket->pMemoryRanges[i].memory);
        if (localRanges[i].memory == VK_NULL_HANDLE || pLocalMems[i].pGpuMem == NULL) {
            vktrace_LogError("Skipping vkInvalidsateMappedMemoryRanges() due to invalid remapped VkDeviceMemory.");
            VKTRACE_DELETE(pLocalMems);
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
    }

    replayResult = m_vkDeviceFuncs.InvalidateMappedMemoryRanges(remappedDevice, pPacket->memoryRangeCount, localRanges);

    VKTRACE_DELETE(pLocalMems);

    return replayResult;
}

void vkReplay::manually_replay_vkGetPhysicalDeviceProperties(packet_vkGetPhysicalDeviceProperties *pPacket) {
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (pPacket->physicalDevice != VK_NULL_HANDLE && remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetPhysicalDeviceProperties() due to invalid remapped VkPhysicalDevice.");
        return;
    }
    char traceDeviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "";
    memcpy(traceDeviceName, pPacket->pProperties->deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
    m_vkFuncs.GetPhysicalDeviceProperties(remappedphysicalDevice, pPacket->pProperties);
    m_replay_gpu = ((uint64_t)pPacket->pProperties->vendorID << 32) | (uint64_t)pPacket->pProperties->deviceID;
    m_replay_drv_vers = (uint64_t)pPacket->pProperties->driverVersion;
    memcpy(m_replay_pipelinecache_uuid, pPacket->pProperties->pipelineCacheUUID, VK_UUID_SIZE);
    char replayDeviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "";
    memcpy(replayDeviceName, pPacket->pProperties->deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
    g_TraceDeviceNameToReplayDeviceName[traceDeviceName] = replayDeviceName;
    forceDisableCaptureReplayFeature();
}

void vkReplay::manually_replay_vkGetPhysicalDeviceProperties2(packet_vkGetPhysicalDeviceProperties2 *pPacket) {
    manually_replay_vkGetPhysicalDeviceProperties2KHR((packet_vkGetPhysicalDeviceProperties2KHR *)pPacket);
}

void vkReplay::manually_replay_vkGetPhysicalDeviceProperties2KHR(packet_vkGetPhysicalDeviceProperties2KHR *pPacket) {
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (pPacket->physicalDevice != VK_NULL_HANDLE && remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetPhysicalDeviceProperties2KHR() due to invalid remapped VkPhysicalDevice.");
        return;
    }
    char traceDeviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "";
    memcpy(traceDeviceName, pPacket->pProperties->properties.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);

    for (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *p = (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *)pPacket->pProperties->pNext;
            p != NULL; p = (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *)p->pNext) {
        if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR) {
            RayTracingPipelineShaderInfo::physicalRtPropertiesTrace = *p;
        }
    }

    if (pPacket->header->packet_id == VKTRACE_TPI_VK_vkGetPhysicalDeviceProperties2KHR) {
        m_vkFuncs.GetPhysicalDeviceProperties2KHR(remappedphysicalDevice, pPacket->pProperties);
    } else {
        m_vkFuncs.GetPhysicalDeviceProperties2(remappedphysicalDevice, pPacket->pProperties);
    }

    for (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *p = (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *)pPacket->pProperties->pNext;
            p != NULL; p = (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *)p->pNext) {
        if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR) {
            RayTracingPipelineShaderInfo::physicalRtProperties = *p;
        }
    }

    m_replay_gpu =
        ((uint64_t)pPacket->pProperties->properties.vendorID << 32) | (uint64_t)pPacket->pProperties->properties.deviceID;
    m_replay_drv_vers = (uint64_t)pPacket->pProperties->properties.driverVersion;
    memcpy(m_replay_pipelinecache_uuid, pPacket->pProperties->properties.pipelineCacheUUID, VK_UUID_SIZE);
    char replayDeviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "";
    memcpy(replayDeviceName, pPacket->pProperties->properties.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
    forceDisableCaptureReplayFeature();
}

VkResult vkReplay::manually_replay_vkGetPhysicalDeviceSurfaceSupportKHR(packet_vkGetPhysicalDeviceSurfaceSupportKHR *pPacket) {
    if (g_pReplaySettings->headless == TRUE) {
        return VK_SUCCESS;
    }
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceSurfaceSupportKHR() due to invalid remapped VkPhysicalDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkSurfaceKHR remappedSurfaceKHR = m_objMapper.remap_surfacekhrs(pPacket->surface);
    if (remappedSurfaceKHR == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceSurfaceSupportKHR() due to invalid remapped VkSurfaceKHR.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    replayResult = m_vkFuncs.GetPhysicalDeviceSurfaceSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                remappedSurfaceKHR, pPacket->pSupported);

    return replayResult;
}

void vkReplay::manually_replay_vkGetPhysicalDeviceMemoryProperties(packet_vkGetPhysicalDeviceMemoryProperties *pPacket) {
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceMemoryProperties() due to invalid remapped VkPhysicalDevice.");
        return;
    }

    traceMemoryProperties[pPacket->physicalDevice] = *(pPacket->pMemoryProperties);
    m_vkFuncs.GetPhysicalDeviceMemoryProperties(remappedphysicalDevice, pPacket->pMemoryProperties);
    replayMemoryProperties[remappedphysicalDevice] = *(pPacket->pMemoryProperties);
    return;
}

void vkReplay::manually_replay_vkGetPhysicalDeviceMemoryProperties2KHR(packet_vkGetPhysicalDeviceMemoryProperties2KHR *pPacket) {
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceMemoryProperties2KHR() due to invalid remapped VkPhysicalDevice.");
        return;
    }

    traceMemoryProperties[pPacket->physicalDevice] = pPacket->pMemoryProperties->memoryProperties;
    m_vkFuncs.GetPhysicalDeviceMemoryProperties2KHR(remappedphysicalDevice, pPacket->pMemoryProperties);
    replayMemoryProperties[remappedphysicalDevice] = pPacket->pMemoryProperties->memoryProperties;
    return;
}

void vkReplay::manually_replay_vkGetPhysicalDeviceMemoryProperties2(packet_vkGetPhysicalDeviceMemoryProperties2 *pPacket) {
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceMemoryProperties2() due to invalid remapped VkPhysicalDevice.");
        return;
    }

    traceMemoryProperties[pPacket->physicalDevice] = pPacket->pMemoryProperties->memoryProperties;
    m_vkFuncs.GetPhysicalDeviceMemoryProperties2(remappedphysicalDevice, pPacket->pMemoryProperties);
    replayMemoryProperties[remappedphysicalDevice] = pPacket->pMemoryProperties->memoryProperties;
    return;
}

void vkReplay::manually_replay_vkGetPhysicalDeviceQueueFamilyProperties(packet_vkGetPhysicalDeviceQueueFamilyProperties *pPacket) {
    static std::unordered_map<VkPhysicalDevice, uint32_t> queueFamPropCnt;  // count returned when pQueueFamilyProperties is NULL
    VkQueueFamilyProperties *savepQueueFamilyProperties = NULL;

    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);

    if (pPacket->physicalDevice != VK_NULL_HANDLE && remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceQueueFamilyProperties() due to invalid remapped VkPhysicalDevice.");
        return;
    }

    if (queueFamPropCnt.find(pPacket->physicalDevice) != queueFamPropCnt.end()) {
        // This query was previously done with pQueueFamilyProperties set to null. It was a query
        // to determine the size of data to be returned. We saved the size returned during
        // that api call. Use that count instead to prevent a VK_INCOMPLETE error.
        if (queueFamPropCnt[pPacket->physicalDevice] > *pPacket->pQueueFamilyPropertyCount) {
            *pPacket->pQueueFamilyPropertyCount = queueFamPropCnt[pPacket->physicalDevice];
            savepQueueFamilyProperties = pPacket->pQueueFamilyProperties;
            pPacket->pQueueFamilyProperties = VKTRACE_NEW_ARRAY(VkQueueFamilyProperties, *pPacket->pQueueFamilyPropertyCount);
        }
    }

    // If we haven't previously allocated queueFamilyProperties for the trace physical device, allocate it.
    // If we previously allocated queueFamilyProperities for the trace physical device and the size of this
    // query is larger than what we saved last time, then free the last properties map and allocate a new map.
    if (traceQueueFamilyProperties.find(pPacket->physicalDevice) == traceQueueFamilyProperties.end() ||
        *pPacket->pQueueFamilyPropertyCount > traceQueueFamilyProperties[pPacket->physicalDevice].count) {
        if (traceQueueFamilyProperties.find(pPacket->physicalDevice) != traceQueueFamilyProperties.end()) {
            free(traceQueueFamilyProperties[pPacket->physicalDevice].queueFamilyProperties);
            traceQueueFamilyProperties.erase(pPacket->physicalDevice);
        }
        if (pPacket->pQueueFamilyProperties) {
            traceQueueFamilyProperties[pPacket->physicalDevice].queueFamilyProperties =
                (VkQueueFamilyProperties *)malloc(*pPacket->pQueueFamilyPropertyCount * sizeof(VkQueueFamilyProperties));
            memcpy(traceQueueFamilyProperties[pPacket->physicalDevice].queueFamilyProperties, pPacket->pQueueFamilyProperties,
                   *pPacket->pQueueFamilyPropertyCount * sizeof(VkQueueFamilyProperties));
            traceQueueFamilyProperties[pPacket->physicalDevice].count = *pPacket->pQueueFamilyPropertyCount;
        }
    }

    m_vkFuncs.GetPhysicalDeviceQueueFamilyProperties(remappedphysicalDevice, pPacket->pQueueFamilyPropertyCount,
                                                     pPacket->pQueueFamilyProperties);

    // If we haven't previously allocated queueFamilyProperties for the replay physical device, allocate it.
    // If we previously allocated queueFamilyProperities for the replay physical device and the size of this
    // query is larger than what we saved last time, then free the last properties map and allocate a new map.
    if (replayQueueFamilyProperties.find(remappedphysicalDevice) == replayQueueFamilyProperties.end() ||
        *pPacket->pQueueFamilyPropertyCount > replayQueueFamilyProperties[remappedphysicalDevice].count) {
        if (replayQueueFamilyProperties.find(remappedphysicalDevice) != replayQueueFamilyProperties.end()) {
            free(replayQueueFamilyProperties[remappedphysicalDevice].queueFamilyProperties);
            replayQueueFamilyProperties.erase(remappedphysicalDevice);
        }
        if (pPacket->pQueueFamilyProperties) {
            replayQueueFamilyProperties[remappedphysicalDevice].queueFamilyProperties =
                (VkQueueFamilyProperties *)malloc(*pPacket->pQueueFamilyPropertyCount * sizeof(VkQueueFamilyProperties));
            memcpy(replayQueueFamilyProperties[remappedphysicalDevice].queueFamilyProperties, pPacket->pQueueFamilyProperties,
                   *pPacket->pQueueFamilyPropertyCount * sizeof(VkQueueFamilyProperties));
            replayQueueFamilyProperties[remappedphysicalDevice].count = *pPacket->pQueueFamilyPropertyCount;
        }
    }

    if (!pPacket->pQueueFamilyProperties) {
        // This was a query to determine size. Save the returned size so we can use that size next time
        // we're called with pQueueFamilyProperties not null. This is to prevent a VK_INCOMPLETE error.
        queueFamPropCnt[pPacket->physicalDevice] = *pPacket->pQueueFamilyPropertyCount;
    }

    if (savepQueueFamilyProperties) {
        // Restore pPacket->pQueueFamilyProperties. We do this because the replay will free the memory.
        // Note that we don't copy the queried data - it wouldn't fit, and it's not used by the replayer anyway.
        VKTRACE_DELETE(pPacket->pQueueFamilyProperties);
        pPacket->pQueueFamilyProperties = savepQueueFamilyProperties;
    }

    return;
}

void vkReplay::manually_replay_vkGetPhysicalDeviceQueueFamilyProperties2(packet_vkGetPhysicalDeviceQueueFamilyProperties2 *pPacket) {
    manually_replay_vkGetPhysicalDeviceQueueFamilyProperties2KHR((packet_vkGetPhysicalDeviceQueueFamilyProperties2KHR *)pPacket);
}

void vkReplay::manually_replay_vkGetPhysicalDeviceQueueFamilyProperties2KHR(
    packet_vkGetPhysicalDeviceQueueFamilyProperties2KHR *pPacket) {
    static std::unordered_map<VkPhysicalDevice, uint32_t>
        queueFamProp2KHRCnt;  // count returned when pQueueFamilyProperties is NULL
    VkQueueFamilyProperties2KHR *savepQueueFamilyProperties = NULL;

    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);

    if (pPacket->physicalDevice != VK_NULL_HANDLE && remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceQueueFamilyProperties() due to invalid remapped VkPhysicalDevice.");
        return;
    }

    if (queueFamProp2KHRCnt.find(pPacket->physicalDevice) != queueFamProp2KHRCnt.end()) {
        // This query was previously done with pQueueFamilyProperties set to null. It was a query
        // to determine the size of data to be returned. We saved the size returned during
        // that api call. Use that count instead to prevent a VK_INCOMPLETE error.
        if (queueFamProp2KHRCnt[pPacket->physicalDevice] > *pPacket->pQueueFamilyPropertyCount) {
            *pPacket->pQueueFamilyPropertyCount = queueFamProp2KHRCnt[pPacket->physicalDevice];
            savepQueueFamilyProperties = pPacket->pQueueFamilyProperties;
            pPacket->pQueueFamilyProperties = VKTRACE_NEW_ARRAY(VkQueueFamilyProperties2KHR, *pPacket->pQueueFamilyPropertyCount);
        }
    }

    // If we haven't previously allocated queueFamilyProperties for the trace physical device, allocate it.
    // If we previously allocated queueFamilyProperities for the trace physical device and the size of this
    // query is larger than what we saved last time, then free the last properties map and allocate a new map.
    if (traceQueueFamilyProperties.find(pPacket->physicalDevice) == traceQueueFamilyProperties.end() ||
        *pPacket->pQueueFamilyPropertyCount > traceQueueFamilyProperties[pPacket->physicalDevice].count) {
        if (traceQueueFamilyProperties.find(pPacket->physicalDevice) != traceQueueFamilyProperties.end()) {
            free(traceQueueFamilyProperties[pPacket->physicalDevice].queueFamilyProperties);
            traceQueueFamilyProperties.erase(pPacket->physicalDevice);
        }
        if (pPacket->pQueueFamilyProperties) {
            traceQueueFamilyProperties[pPacket->physicalDevice].queueFamilyProperties =
                (VkQueueFamilyProperties *)malloc(*pPacket->pQueueFamilyPropertyCount * sizeof(VkQueueFamilyProperties));
            memcpy(traceQueueFamilyProperties[pPacket->physicalDevice].queueFamilyProperties, pPacket->pQueueFamilyProperties,
                   *pPacket->pQueueFamilyPropertyCount * sizeof(VkQueueFamilyProperties));
            traceQueueFamilyProperties[pPacket->physicalDevice].count = *pPacket->pQueueFamilyPropertyCount;
        }
    }

    if (pPacket->header->packet_id == VKTRACE_TPI_VK_vkGetPhysicalDeviceQueueFamilyProperties2KHR) {
        m_vkFuncs.GetPhysicalDeviceQueueFamilyProperties2KHR(remappedphysicalDevice, pPacket->pQueueFamilyPropertyCount,
                                                            pPacket->pQueueFamilyProperties);
    } else {
        m_vkFuncs.GetPhysicalDeviceQueueFamilyProperties2(remappedphysicalDevice, pPacket->pQueueFamilyPropertyCount,
                                                            pPacket->pQueueFamilyProperties);
    }

    // If we haven't previously allocated queueFamilyProperties for the replay physical device, allocate it.
    // If we previously allocated queueFamilyProperities for the replay physical device and the size of this
    // query is larger than what we saved last time, then free the last properties map and allocate a new map.
    if (replayQueueFamilyProperties.find(remappedphysicalDevice) == replayQueueFamilyProperties.end() ||
        *pPacket->pQueueFamilyPropertyCount > replayQueueFamilyProperties[remappedphysicalDevice].count) {
        if (replayQueueFamilyProperties.find(remappedphysicalDevice) != replayQueueFamilyProperties.end()) {
            free(replayQueueFamilyProperties[remappedphysicalDevice].queueFamilyProperties);
            replayQueueFamilyProperties.erase(remappedphysicalDevice);
        }
        if (pPacket->pQueueFamilyProperties) {
            replayQueueFamilyProperties[remappedphysicalDevice].queueFamilyProperties =
                (VkQueueFamilyProperties *)malloc(*pPacket->pQueueFamilyPropertyCount * sizeof(VkQueueFamilyProperties));
            memcpy(replayQueueFamilyProperties[remappedphysicalDevice].queueFamilyProperties, pPacket->pQueueFamilyProperties,
                   *pPacket->pQueueFamilyPropertyCount * sizeof(VkQueueFamilyProperties));
            replayQueueFamilyProperties[remappedphysicalDevice].count = *pPacket->pQueueFamilyPropertyCount;
        }
    }

    if (!pPacket->pQueueFamilyProperties) {
        // This was a query to determine size. Save the returned size so we can use that size next time
        // we're called with pQueueFamilyProperties not null. This is to prevent a VK_INCOMPLETE error.
        queueFamProp2KHRCnt[pPacket->physicalDevice] = *pPacket->pQueueFamilyPropertyCount;
    }

    if (savepQueueFamilyProperties) {
        // Restore pPacket->pQueueFamilyProperties. We do this because the replay will free the memory.
        // Note that we don't copy the queried data - it wouldn't fit, and it's not used by the replayer anyway.
        VKTRACE_DELETE(pPacket->pQueueFamilyProperties);
        pPacket->pQueueFamilyProperties = savepQueueFamilyProperties;
    }

    return;
}

void vkReplay::manually_replay_vkGetPhysicalDeviceSparseImageFormatProperties(
    packet_vkGetPhysicalDeviceSparseImageFormatProperties *pPacket) {
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    static std::unordered_map<VkPhysicalDevice, uint32_t> propCnt;  // count returned when pProperties is NULL
    VkSparseImageFormatProperties *savepProperties = NULL;

    if (pPacket->physicalDevice != VK_NULL_HANDLE && remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceSparseImageFormatProperties() due to invalid remapped VkPhysicalDevice.");
        return;
    }

    if (propCnt.find(pPacket->physicalDevice) != propCnt.end()) {
        // This query was previously done with pProperties set to null. It was a query
        // to determine the size of data to be returned. We saved the size returned during
        // that api call. Use that count instead to prevent a VK_INCOMPLETE error.
        if (propCnt[pPacket->physicalDevice] > *pPacket->pPropertyCount) {
            *pPacket->pPropertyCount = propCnt[pPacket->physicalDevice];
            savepProperties = pPacket->pProperties;
            pPacket->pProperties = VKTRACE_NEW_ARRAY(VkSparseImageFormatProperties, *pPacket->pPropertyCount);
        }
    }

    m_vkFuncs.GetPhysicalDeviceSparseImageFormatProperties(remappedphysicalDevice, pPacket->format, pPacket->type, pPacket->samples,
                                                           pPacket->usage, pPacket->tiling, pPacket->pPropertyCount,
                                                           pPacket->pProperties);

    if (!pPacket->pProperties) {
        // This was a query to determine size. Save the returned size so we can use that size next time
        // we're called with pProperties not null. This is to prevent a VK_INCOMPLETE error.
        propCnt[pPacket->physicalDevice] = *pPacket->pPropertyCount;
    }

    if (savepProperties) {
        // Restore pPacket->pProperties. We do this because the replay will free the memory.
        // Note that we don't copy the queried data - it wouldn't fit, and it's not used by the replayer anyway.
        VKTRACE_DELETE(pPacket->pProperties);
        pPacket->pProperties = savepProperties;
    }

    return;
}

void vkReplay::manually_replay_vkGetPhysicalDeviceSparseImageFormatProperties2(
    packet_vkGetPhysicalDeviceSparseImageFormatProperties2 *pPacket) {
    manually_replay_vkGetPhysicalDeviceSparseImageFormatProperties2KHR((packet_vkGetPhysicalDeviceSparseImageFormatProperties2KHR *)pPacket);
}

void vkReplay::manually_replay_vkGetPhysicalDeviceSparseImageFormatProperties2KHR(
    packet_vkGetPhysicalDeviceSparseImageFormatProperties2KHR *pPacket) {
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    static std::unordered_map<VkPhysicalDevice, uint32_t> prop2KHRCnt;  // count returned when pProperties is NULL
    VkSparseImageFormatProperties2KHR *savepProperties = NULL;

    if (pPacket->physicalDevice != VK_NULL_HANDLE && remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceSparseImageFormatProperties() due to invalid remapped VkPhysicalDevice.");
        return;
    }

    if (prop2KHRCnt.find(pPacket->physicalDevice) != prop2KHRCnt.end()) {
        // This query was previously done with pProperties set to null. It was a query
        // to determine the size of data to be returned. We saved the size returned during
        // that api call. Use that count instead to prevent a VK_INCOMPLETE error.
        if (prop2KHRCnt[pPacket->physicalDevice] > *pPacket->pPropertyCount) {
            *pPacket->pPropertyCount = prop2KHRCnt[pPacket->physicalDevice];
            savepProperties = pPacket->pProperties;
            pPacket->pProperties = VKTRACE_NEW_ARRAY(VkSparseImageFormatProperties2KHR, *pPacket->pPropertyCount);
        }
    }

    if (pPacket->header->packet_id == VKTRACE_TPI_VK_vkGetPhysicalDeviceSparseImageFormatProperties2KHR) {
        m_vkFuncs.GetPhysicalDeviceSparseImageFormatProperties2KHR(remappedphysicalDevice, pPacket->pFormatInfo,
                                                                pPacket->pPropertyCount, pPacket->pProperties);
    } else {
        m_vkFuncs.GetPhysicalDeviceSparseImageFormatProperties2(remappedphysicalDevice, pPacket->pFormatInfo,
                                                                pPacket->pPropertyCount, pPacket->pProperties);
    }

    if (!pPacket->pProperties) {
        // This was a query to determine size. Save the returned size so we can use that size next time
        // we're called with pProperties not null. This is to prevent a VK_INCOMPLETE error.
        prop2KHRCnt[pPacket->physicalDevice] = *pPacket->pPropertyCount;
    }

    if (savepProperties) {
        // Restore pPacket->pProperties. We do this because the replay will free the memory.
        // Note that we don't copy the queried data - it wouldn't fit, and it's not used by the replayer anyway.
        VKTRACE_DELETE(pPacket->pProperties);
        pPacket->pProperties = savepProperties;
    }

    return;
}

VkResult vkReplay::manually_replay_vkBindBufferMemory(packet_vkBindBufferMemory *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in BindBufferMemory() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkBuffer traceBuffer = pPacket->buffer;
    VkBuffer remappedbuffer = m_objMapper.remap_buffers(pPacket->buffer);
    if (pPacket->buffer != VK_NULL_HANDLE && remappedbuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in BindBufferMemory() due to invalid remapped VkBuffer.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkDeviceMemory remappedmemory = m_objMapper.remap_devicememorys(pPacket->memory);
    if (pPacket->memory != VK_NULL_HANDLE && remappedmemory == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in BindBufferMemory() due to invalid remapped VkDeviceMemory.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch()) {
        uint64_t memOffsetTemp;
        if (replayGetBufferMemoryRequirements.find(remappedbuffer) == replayGetBufferMemoryRequirements.end()) {
            // vkBindBufferMemory is being called on a buffer for which vkGetBufferMemoryRequirements
            // was not called. This might be violation of the spec on the part of the app, but seems to
            // be done in many apps.  Call vkGetBufferMemoryRequirements for this buffer and add result to
            // replayGetBufferMemoryRequirements map.
            VkMemoryRequirements mem_reqs;
            m_vkDeviceFuncs.GetBufferMemoryRequirements(remappeddevice, remappedbuffer, &mem_reqs);
            replayGetBufferMemoryRequirements[remappedbuffer] = mem_reqs;
        }
        assert(replayGetBufferMemoryRequirements[remappedbuffer].alignment);
        memOffsetTemp = pPacket->memoryOffset + replayGetBufferMemoryRequirements[remappedbuffer].alignment - 1;
        memOffsetTemp = memOffsetTemp / replayGetBufferMemoryRequirements[remappedbuffer].alignment;
        memOffsetTemp = memOffsetTemp * replayGetBufferMemoryRequirements[remappedbuffer].alignment;
        replayResult = m_vkDeviceFuncs.BindBufferMemory(remappeddevice, remappedbuffer, remappedmemory, memOffsetTemp);
        replayBufferToReplayDeviceMemoryOffset[remappedbuffer] = memOffsetTemp;
    } else {
        replayResult = m_vkDeviceFuncs.BindBufferMemory(remappeddevice, remappedbuffer, remappedmemory, pPacket->memoryOffset);
        replayBufferToReplayDeviceMemoryOffset[remappedbuffer] = pPacket->memoryOffset;
    }
    traceBufferToReplayMemory[traceBuffer] = remappedmemory;
    if (g_hasAsApi) {
        replayBufferToReplayDeviceMemory[remappedbuffer] = remappedmemory;
    }
    rtHandler->addSbtBufferMemory(pPacket->buffer, pPacket->memory);
    return replayResult;
}

VkResult vkReplay::manually_replay_vkBindImageMemory(packet_vkBindImageMemory *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in BindImageMemory() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkImage remappedimage = m_objMapper.remap_images(pPacket->image);
    if (pPacket->image != VK_NULL_HANDLE && remappedimage == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in BindImageMemory() due to invalid remapped VkImage.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkDeviceMemory remappedmemory = VK_NULL_HANDLE;
    uint64_t memoryOffset = 0;
    bool bAllocateMemory = true;
    auto it = std::find(hardwarebufferImage.begin(), hardwarebufferImage.end(), remappedimage);
    if (it != hardwarebufferImage.end()) {
        bAllocateMemory = false;
    }
    if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() && bAllocateMemory) {
        if (replayImageToTiling.find(remappedimage) == replayImageToTiling.end()) {
            vktrace_LogError("Error detected in BindImageMemory() due to invalid remapped image tiling.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }

        if (replayGetImageMemoryRequirements.find(remappedimage) == replayGetImageMemoryRequirements.end()) {
            // vkBindImageMemory is being called with an image for which vkGetImageMemoryRequirements
            // was not called. This might be violation of the spec on the part of the app, but seems to
            // be done in many apps.  Call vkGetImageMemoryRequirements for this image and add result to
            // replayGetImageMemoryRequirements map.
            VkMemoryRequirements mem_reqs;
            m_vkDeviceFuncs.GetImageMemoryRequirements(remappeddevice, remappedimage, &mem_reqs);
            replayGetImageMemoryRequirements[remappedimage] = mem_reqs;
        }

        if (replayImageToTiling[remappedimage] == VK_IMAGE_TILING_OPTIMAL) {
            uint32_t replayMemTypeIndex;
            if (!getReplayMemoryTypeIdx(pPacket->device, remappeddevice, traceDeviceMemoryToMemoryTypeIndex[pPacket->memory],
                                        &replayGetImageMemoryRequirements[remappedimage], &replayMemTypeIndex)) {
                vktrace_LogError("Error detected in BindImageMemory() due to invalid remapped memory type index.");
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }

            VkMemoryAllocateInfo memoryAllocateInfo = {
                VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                NULL,
                replayGetImageMemoryRequirements[remappedimage].size,
                replayMemTypeIndex,
            };
            replayResult = m_vkDeviceFuncs.AllocateMemory(remappeddevice, &memoryAllocateInfo, NULL, &remappedmemory);

            if (replayResult == VK_SUCCESS) {
                replayOptimalImageToDeviceMemory[remappedimage] = remappedmemory;
            }
        } else {
            remappedmemory = m_objMapper.remap_devicememorys(pPacket->memory);
            assert(replayGetImageMemoryRequirements[remappedimage].alignment);
            memoryOffset = pPacket->memoryOffset + replayGetImageMemoryRequirements[remappedimage].alignment - 1;
            memoryOffset = memoryOffset / replayGetImageMemoryRequirements[remappedimage].alignment;
            memoryOffset = memoryOffset * replayGetImageMemoryRequirements[remappedimage].alignment;
        }
    } else {
        remappedmemory = m_objMapper.remap_devicememorys(pPacket->memory);
        memoryOffset = pPacket->memoryOffset;
    }

    if (pPacket->memory != VK_NULL_HANDLE && remappedmemory == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in BindImageMemory() due to invalid remapped VkDeviceMemory.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    replayResult = m_vkDeviceFuncs.BindImageMemory(remappeddevice, remappedimage, remappedmemory, memoryOffset);

    return replayResult;
}

void vkReplay::manually_replay_vkGetImageMemoryRequirements(packet_vkGetImageMemoryRequirements *pPacket) {
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in vkGetImageMemoryRequirements() due to invalid remapped VkDevice.");
        return;
    }

    VkImage remappedImage = m_objMapper.remap_images(pPacket->image);
    if (pPacket->image != VK_NULL_HANDLE && remappedImage == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetImageMemoryRequirements() due to invalid remapped VkImage.");
        return;
    }

    m_vkDeviceFuncs.GetImageMemoryRequirements(remappedDevice, remappedImage, pPacket->pMemoryRequirements);
    replayGetImageMemoryRequirements[remappedImage] = *(pPacket->pMemoryRequirements);
#if defined(ARM_LINUX_64)
    if (std::find(hardwarebufferImage.begin(), hardwarebufferImage.end(), remappedImage) != hardwarebufferImage.end()) {
        externalImageMemoryRequirements[remappedImage] = *(pPacket->pMemoryRequirements);
    }
#endif
    return;
}

void vkReplay::manually_replay_vkGetImageMemoryRequirements2(packet_vkGetImageMemoryRequirements2 *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    VkImage remappedimage = VK_NULL_HANDLE;

    if ((pPacket->device != VK_NULL_HANDLE) && (remappeddevice == VK_NULL_HANDLE)) {
        vktrace_LogError("Error detected in GetImageMemoryRequirements2KHR() due to invalid remapped VkDevice.");
        return;
    }

    if (pPacket->pInfo != nullptr) {
        remappedimage = m_objMapper.remap_images(pPacket->pInfo->image);

        if ((pPacket->pInfo->image != VK_NULL_HANDLE) && (remappedimage == VK_NULL_HANDLE)) {
            vktrace_LogError("Error detected in GetImageMemoryRequirements2KHR() due to invalid remapped VkImage.");
            return;
        }

        (const_cast<VkImageMemoryRequirementsInfo2 *>(pPacket->pInfo))->image = remappedimage;
    }

    m_vkDeviceFuncs.GetImageMemoryRequirements2(remappeddevice, pPacket->pInfo, pPacket->pMemoryRequirements);

    replayGetImageMemoryRequirements[remappedimage] = pPacket->pMemoryRequirements->memoryRequirements;
}

void vkReplay::manually_replay_vkGetImageMemoryRequirements2KHR(packet_vkGetImageMemoryRequirements2KHR *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    VkImage remappedimage = VK_NULL_HANDLE;

    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetImageMemoryRequirements2KHR() due to invalid remapped VkDevice.");
        return;
    }

    if (pPacket->pInfo != nullptr) {
        remappedimage = m_objMapper.remap_images(pPacket->pInfo->image);

        if ((pPacket->pInfo->image != VK_NULL_HANDLE) && (remappedimage == VK_NULL_HANDLE)) {
            vktrace_LogError("Error detected in GetImageMemoryRequirements2KHR() due to invalid remapped VkImage.");
            return;
        }

        ((VkImageMemoryRequirementsInfo2KHR *)pPacket->pInfo)->image = remappedimage;
    }
    vkreplay_process_pnext_structs(pPacket->header, (void *)pPacket->pInfo);
    m_vkDeviceFuncs.GetImageMemoryRequirements2KHR(remappeddevice, pPacket->pInfo, pPacket->pMemoryRequirements);

    replayGetImageMemoryRequirements[remappedimage] = pPacket->pMemoryRequirements->memoryRequirements;
}

void vkReplay::manually_replay_vkGetBufferMemoryRequirements(packet_vkGetBufferMemoryRequirements *pPacket) {
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in vkGetBufferMemoryRequirements() due to invalid remapped VkDevice.");
        return;
    }

    VkBuffer remappedBuffer = m_objMapper.remap_buffers(pPacket->buffer);
    if (pPacket->buffer != VK_NULL_HANDLE && remappedBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetBufferMemoryRequirements() due to invalid remapped VkBuffer.");
        return;
    }

    rtHandler->addSbtBufferSize(pPacket->buffer, pPacket->pMemoryRequirements->size);
    m_vkDeviceFuncs.GetBufferMemoryRequirements(remappedDevice, remappedBuffer, pPacket->pMemoryRequirements);
    replayGetBufferMemoryRequirements[remappedBuffer] = *(pPacket->pMemoryRequirements);
    return;
}

void vkReplay::manually_replay_vkGetBufferMemoryRequirements2(packet_vkGetBufferMemoryRequirements2 *pPacket) {
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in vkGetBufferMemoryRequirements2() due to invalid remapped VkDevice.");
        return;
    }

    VkBuffer remappedBuffer = m_objMapper.remap_buffers(pPacket->pInfo->buffer);
    if ((pPacket->pInfo->buffer != VK_NULL_HANDLE) && (remappedBuffer == VK_NULL_HANDLE)) {
        vktrace_LogError("Error detected in GetBufferMemoryRequirements2() due to invalid remapped VkBuffer.");
        return;
    }
    rtHandler->addSbtBufferSize(pPacket->pInfo->buffer, pPacket->pMemoryRequirements->memoryRequirements.size);
    *(const_cast<VkBuffer *>(&pPacket->pInfo->buffer)) = remappedBuffer;

    m_vkDeviceFuncs.GetBufferMemoryRequirements2(remappedDevice, pPacket->pInfo, pPacket->pMemoryRequirements);
    replayGetBufferMemoryRequirements[pPacket->pInfo->buffer] = pPacket->pMemoryRequirements->memoryRequirements;
    return;
}

void vkReplay::manually_replay_vkGetBufferMemoryRequirements2KHR(packet_vkGetBufferMemoryRequirements2KHR *pPacket) {
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in vkGetBufferMemoryRequirements2() due to invalid remapped VkDevice.");
        return;
    }

    VkBuffer remappedBuffer = m_objMapper.remap_buffers(pPacket->pInfo->buffer);
    if (pPacket->pInfo->buffer != VK_NULL_HANDLE && remappedBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetBufferMemoryRequirements2() due to invalid remapped VkBuffer.");
        return;
    }
    rtHandler->addSbtBufferSize(pPacket->pInfo->buffer, pPacket->pMemoryRequirements->memoryRequirements.size);
    *(const_cast<VkBuffer *>(&pPacket->pInfo->buffer)) = remappedBuffer;

    vkreplay_process_pnext_structs(pPacket->header, (void *)pPacket->pInfo);
    m_vkDeviceFuncs.GetBufferMemoryRequirements2KHR(remappedDevice, pPacket->pInfo, pPacket->pMemoryRequirements);
    replayGetBufferMemoryRequirements[pPacket->pInfo->buffer] = pPacket->pMemoryRequirements->memoryRequirements;
    return;
}

VkResult vkReplay::manually_replay_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    packet_vkGetPhysicalDeviceSurfaceCapabilitiesKHR *pPacket) {
    if (g_pReplaySettings->headless == TRUE) {
        return VK_SUCCESS;
    }
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceSurfaceCapabilitiesKHR() due to invalid remapped VkPhysicalDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkSurfaceKHR remappedSurfaceKHR = m_objMapper.remap_surfacekhrs(pPacket->surface);
    if (remappedSurfaceKHR == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceSurfaceCapabilitiesKHR() due to invalid remapped VkSurfaceKHR.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

#if defined(PLATFORM_LINUX)
    if (m_displayServer == VK_DISPLAY_XCB) {
        VkSurfaceCapabilitiesKHR sc;     // sc won't be used in the following function, so its initial value doesn't matter
        m_display->resize_window(pPacket->pSurfaceCapabilities->currentExtent.width,
                                 pPacket->pSurfaceCapabilities->currentExtent.height,
                                 VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, sc);
    }
#endif

    VkSurfaceTransformFlagBitsKHR trace_currentTransform = pPacket->pSurfaceCapabilities->currentTransform;
    replayResult = m_vkFuncs.GetPhysicalDeviceSurfaceCapabilitiesKHR(remappedphysicalDevice, remappedSurfaceKHR,
                                                                     pPacket->pSurfaceCapabilities);

    replaySurfaceCapabilities[remappedSurfaceKHR] = *(pPacket->pSurfaceCapabilities);

    if (replaySettings.useTraceSurfaceTransformFlagBit) {
        replaySurfaceCapabilities[remappedSurfaceKHR].currentTransform = trace_currentTransform;
    }

    return replayResult;
}

VkResult vkReplay::manually_replay_vkGetPhysicalDeviceSurfaceFormatsKHR(packet_vkGetPhysicalDeviceSurfaceFormatsKHR *pPacket) {
    if (g_pReplaySettings->headless == TRUE) {
        return VK_SUCCESS;
    }
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    static std::unordered_map<VkPhysicalDevice, uint32_t> surfFmtCnt;  // count returned when pSurfaceFormats is NULL
    VkSurfaceFormatKHR *savepSurfaceFormats = NULL;

    if (remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceSurfaceFormatsKHR() due to invalid remapped VkPhysicalDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkSurfaceKHR remappedSurfaceKHR = m_objMapper.remap_surfacekhrs(pPacket->surface);
    if (remappedSurfaceKHR == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceSurfaceFormatsKHR() due to invalid remapped VkSurfaceKHR.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (surfFmtCnt.find(pPacket->physicalDevice) != surfFmtCnt.end()) {
        // This query was previously done with pSurfaceFormats set to null. It was a query
        // to determine the size of data to be returned. We saved the size returned during
        // that api call. Use that count instead to prevent a VK_INCOMPLETE error.
        if (surfFmtCnt[pPacket->physicalDevice] > *pPacket->pSurfaceFormatCount) {
            *pPacket->pSurfaceFormatCount = surfFmtCnt[pPacket->physicalDevice];
            savepSurfaceFormats = pPacket->pSurfaceFormats;
            pPacket->pSurfaceFormats = VKTRACE_NEW_ARRAY(VkSurfaceFormatKHR, *pPacket->pSurfaceFormatCount);
        }
    }

    replayResult = m_vkFuncs.GetPhysicalDeviceSurfaceFormatsKHR(remappedphysicalDevice, remappedSurfaceKHR,
                                                                pPacket->pSurfaceFormatCount, pPacket->pSurfaceFormats);

    if (!pPacket->pSurfaceFormats) {
        // This was a query to determine size. Save the returned size so we can use that size next time
        // we're called with pSurfaceFormats not null. This is to prevent a VK_INCOMPLETE error.
        surfFmtCnt[pPacket->physicalDevice] = *pPacket->pSurfaceFormatCount;
    }

    if (savepSurfaceFormats) {
        // Restore pPacket->pSurfaceFormats. We do this because the replay will free the memory.
        // Note that we don't copy the queried data - it wouldn't fit, and it's not used by the replayer anyway.
        VKTRACE_DELETE(pPacket->pSurfaceFormats);
        pPacket->pSurfaceFormats = savepSurfaceFormats;
    }

    return replayResult;
}

VkResult vkReplay::manually_replay_vkGetPhysicalDeviceSurfaceFormats2KHR(packet_vkGetPhysicalDeviceSurfaceFormats2KHR *pPacket) {
    if (g_pReplaySettings->headless == TRUE) {
        return VK_SUCCESS;
    }
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    static std::unordered_map<VkPhysicalDevice, uint32_t> surfFmtCnt;  // count returned when pSurfaceFormats is NULL
    VkSurfaceFormat2KHR *savepSurfaceFormats = NULL;

    if (remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceSurfaceFormatsKHR() due to invalid remapped VkPhysicalDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    *((VkSurfaceKHR*)&pPacket->pSurfaceInfo->surface) = m_objMapper.remap_surfacekhrs(pPacket->pSurfaceInfo->surface);
    if (pPacket->pSurfaceInfo->surface == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceSurfaceFormats2KHR() due to invalid remapped VkSurfaceKHR.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (surfFmtCnt.find(pPacket->physicalDevice) != surfFmtCnt.end()) {
        // This query was previously done with pSurfaceFormats set to null. It was a query
        // to determine the size of data to be returned. We saved the size returned during
        // that api call. Use that count instead to prevent a VK_INCOMPLETE error.
        if (surfFmtCnt[pPacket->physicalDevice] > *pPacket->pSurfaceFormatCount) {
            *pPacket->pSurfaceFormatCount = surfFmtCnt[pPacket->physicalDevice];
            savepSurfaceFormats = pPacket->pSurfaceFormats;
            pPacket->pSurfaceFormats = VKTRACE_NEW_ARRAY(VkSurfaceFormat2KHR, *pPacket->pSurfaceFormatCount);
        }
    }

    replayResult = m_vkFuncs.GetPhysicalDeviceSurfaceFormats2KHR(remappedphysicalDevice, pPacket->pSurfaceInfo,
                                                                pPacket->pSurfaceFormatCount, pPacket->pSurfaceFormats);

    if (!pPacket->pSurfaceFormats) {
        // This was a query to determine size. Save the returned size so we can use that size next time
        // we're called with pSurfaceFormats not null. This is to prevent a VK_INCOMPLETE error.
        surfFmtCnt[pPacket->physicalDevice] = *pPacket->pSurfaceFormatCount;
    }

    if (savepSurfaceFormats) {
        // Restore pPacket->pSurfaceFormats. We do this because the replay will free the memory.
        // Note that we don't copy the queried data - it wouldn't fit, and it's not used by the replayer anyway.
        VKTRACE_DELETE(pPacket->pSurfaceFormats);
        pPacket->pSurfaceFormats = savepSurfaceFormats;
    }

    return replayResult;
}

VkResult vkReplay::manually_replay_vkGetPhysicalDeviceSurfacePresentModesKHR(
    packet_vkGetPhysicalDeviceSurfacePresentModesKHR *pPacket) {
    if (g_pReplaySettings->headless == TRUE) {
        return VK_SUCCESS;
    }
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    static std::unordered_map<VkPhysicalDevice, uint32_t> presModeCnt;  // count returned when pPrsentModes is NULL
    VkPresentModeKHR *savepPresentModes = NULL;

    if (remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceSurfacePresentModesKHR() due to invalid remapped VkPhysicalDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkSurfaceKHR remappedSurfaceKHR = m_objMapper.remap_surfacekhrs(pPacket->surface);
    if (remappedSurfaceKHR == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetPhysicalDeviceSurfacePresentModesKHR() due to invalid remapped VkSurfaceKHR.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (presModeCnt.find(pPacket->physicalDevice) != presModeCnt.end()) {
        // This query was previously done with pSurfaceFormats set to null. It was a query
        // to determine the size of data to be returned. We saved the size returned during
        // that api call. Use that count instead to prevent a VK_INCOMPLETE error.
        if (presModeCnt[pPacket->physicalDevice] > *pPacket->pPresentModeCount) {
            *pPacket->pPresentModeCount = presModeCnt[pPacket->physicalDevice];
            savepPresentModes = pPacket->pPresentModes;
            pPacket->pPresentModes = VKTRACE_NEW_ARRAY(VkPresentModeKHR, *pPacket->pPresentModeCount);
        }
    }

    replayResult = m_vkFuncs.GetPhysicalDeviceSurfacePresentModesKHR(remappedphysicalDevice, remappedSurfaceKHR,
                                                                     pPacket->pPresentModeCount, pPacket->pPresentModes);

    if (!pPacket->pPresentModes) {
        // This was a query to determine size. Save the returned size so we can use that size next time
        // we're called with pPresentModes not null. This is to prevent a VK_INCOMPLETE error.
        presModeCnt[pPacket->physicalDevice] = *pPacket->pPresentModeCount;
    }

    if (savepPresentModes) {
        // Restore pPacket->pPresentModes. We do this because the replay will free the memory.
        // Note that we don't copy the queried data - it wouldn't fit, and it's not used by the replayer anyway.
        VKTRACE_DELETE(pPacket->pPresentModes);
        pPacket->pPresentModes = savepPresentModes;
    }

    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateSampler(packet_vkCreateSampler *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkSampler local_pSampler;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in vkCreateSampler() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    // No need to remap pCreateInfo
    // No need to remap pAllocator
    if (g_pReplaySettings->forceUseFilter != UINT_MAX) {
        bool change_all_filter = (g_pReplaySettings->forceUseFilter & 256) ? true : false;
        if (change_all_filter) {
            uint32_t forceUseFilter = g_pReplaySettings->forceUseFilter & (~256);
            if (forceUseFilter == 2) {
                const_cast<VkSamplerCreateInfo*>(pPacket->pCreateInfo)->magFilter = VK_FILTER_CUBIC_EXT;
                const_cast<VkSamplerCreateInfo*>(pPacket->pCreateInfo)->minFilter = VK_FILTER_CUBIC_EXT;
            } else {
                const_cast<VkSamplerCreateInfo*>(pPacket->pCreateInfo)->magFilter = (VkFilter)forceUseFilter;
                const_cast<VkSamplerCreateInfo*>(pPacket->pCreateInfo)->minFilter = (VkFilter)forceUseFilter;
            }
        } else {
            uint32_t forceUseFilter = g_pReplaySettings->forceUseFilter;
            if (pPacket->pCreateInfo->magFilter == VK_FILTER_LINEAR) {
                if (forceUseFilter == 2) {
                    const_cast<VkSamplerCreateInfo*>(pPacket->pCreateInfo)->magFilter = VK_FILTER_CUBIC_EXT;
                } else {
                    const_cast<VkSamplerCreateInfo*>(pPacket->pCreateInfo)->magFilter = (VkFilter)forceUseFilter;
                }
            }
            if (pPacket->pCreateInfo->minFilter == VK_FILTER_LINEAR) {
                if (forceUseFilter == 2) {
                    const_cast<VkSamplerCreateInfo*>(pPacket->pCreateInfo)->minFilter = VK_FILTER_CUBIC_EXT;
                } else {
                    const_cast<VkSamplerCreateInfo*>(pPacket->pCreateInfo)->minFilter = (VkFilter)forceUseFilter;
                }
            }
        }
    }
    replayResult = m_vkDeviceFuncs.CreateSampler(remappeddevice, pPacket->pCreateInfo, pPacket->pAllocator, &local_pSampler);
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_samplers_map(*(pPacket->pSampler), local_pSampler);
        replaySamplerToDevice[local_pSampler] = remappeddevice;
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateSwapchainKHR(packet_vkCreateSwapchainKHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkSwapchainKHR local_pSwapchain;
    VkSwapchainKHR save_oldSwapchain, *pSC;
    VkSurfaceKHR save_surface;
    pSC = (VkSwapchainKHR *)&pPacket->pCreateInfo->oldSwapchain;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateSwapchainKHR() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    save_oldSwapchain = pPacket->pCreateInfo->oldSwapchain;
    (*pSC) = m_objMapper.remap_swapchainkhrs(save_oldSwapchain);
    if ((*pSC) == VK_NULL_HANDLE && save_oldSwapchain != VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateSwapchainKHR() due to invalid remapped VkSwapchainKHR.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    save_surface = pPacket->pCreateInfo->surface;
    VkSurfaceKHR *pSurf = (VkSurfaceKHR *)&(pPacket->pCreateInfo->surface);
    *pSurf = m_objMapper.remap_surfacekhrs(*pSurf);
    if (*pSurf == VK_NULL_HANDLE && pPacket->pCreateInfo->surface != VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateSwapchainKHR() due to invalid remapped VkSurfaceKHR.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (replaySurfToSwapchain.find(*pSurf) != replaySurfToSwapchain.end()) {
        local_pSwapchain = replaySurfToSwapchain[*pSurf];
        m_objMapper.add_to_swapchainkhrs_map(*(pPacket->pSwapchain), local_pSwapchain);
        replaySwapchainKHRToDevice[local_pSwapchain] = remappeddevice;
        savedSwapchainImgStates.push(swapchainImageStates[curSwapchainHandle]);
        swapchainImageStates[curSwapchainHandle].reset();
        swapchainRefCount++;
        return VK_SUCCESS;
    }

    VkSurfaceCapabilitiesKHR surfCap;
    auto it = replaySurfaceCapabilities.find(*pSurf);
    if (it != replaySurfaceCapabilities.end()) {
        surfCap = it->second;
    } else {
        replayResult = m_vkFuncs.GetPhysicalDeviceSurfaceCapabilitiesKHR(replayPhysicalDevices[remappeddevice], *pSurf, &surfCap);
        if (replayResult != VK_SUCCESS) {
            vktrace_LogError("Get surface capabilities failed when creating swapchain !");
            return replayResult;
        }
        replaySurfaceCapabilities[*pSurf] = surfCap;
    }
    const_cast<VkSwapchainCreateInfoKHR*>(pPacket->pCreateInfo)->imageExtent.width = std::max(surfCap.minImageExtent.width, pPacket->pCreateInfo->imageExtent.width);
    const_cast<VkSwapchainCreateInfoKHR*>(pPacket->pCreateInfo)->imageExtent.width = std::min(surfCap.maxImageExtent.width, pPacket->pCreateInfo->imageExtent.width);
    const_cast<VkSwapchainCreateInfoKHR*>(pPacket->pCreateInfo)->imageExtent.height = std::max(surfCap.minImageExtent.height, pPacket->pCreateInfo->imageExtent.height);
    const_cast<VkSwapchainCreateInfoKHR*>(pPacket->pCreateInfo)->imageExtent.height = std::min(surfCap.maxImageExtent.height, pPacket->pCreateInfo->imageExtent.height);

    m_display->resize_window(pPacket->pCreateInfo->imageExtent.width, pPacket->pCreateInfo->imageExtent.height, pPacket->pCreateInfo->preTransform, surfCap);

    // Convert queueFamilyIndices
    if (pPacket->pCreateInfo && pPacket->pCreateInfo->pQueueFamilyIndices) {
        for (uint32_t i = 0; i < pPacket->pCreateInfo->queueFamilyIndexCount; i++) {
            getReplayQueueFamilyIdx(pPacket->device, remappeddevice, (uint32_t *)&pPacket->pCreateInfo->pQueueFamilyIndices[i]);
        }
    }

    // Get the list of VkFormats that are supported:
    VkPhysicalDevice remappedPhysicalDevice = replayPhysicalDevices[remappeddevice];
    uint32_t formatCount;
    VkResult U_ASSERT_ONLY res;
    // Note that pPacket->pCreateInfo->surface has been remapped above
    res = vkGetPhysicalDeviceSurfaceFormatsKHR(remappedPhysicalDevice, pPacket->pCreateInfo->surface, &formatCount, NULL);
    assert(!res);
    VkSurfaceFormatKHR *surfFormats = (VkSurfaceFormatKHR *)malloc(formatCount * sizeof(VkSurfaceFormatKHR));
    assert(surfFormats);
    res = vkGetPhysicalDeviceSurfaceFormatsKHR(remappedPhysicalDevice, pPacket->pCreateInfo->surface, &formatCount, surfFormats);
    assert(!res);
    // If the format list includes just one entry of VK_FORMAT_UNDEFINED,
    // the surface has no preferred format.  Otherwise, at least one
    // supported format will be returned.
    if (!(formatCount == 1 && surfFormats[0].format == VK_FORMAT_UNDEFINED)) {
        bool found = false;
        for (uint32_t i = 0; i < formatCount; i++) {
            vktrace_LogAlways("Record swap chain fmt %d, local surface fmt[%d] = %d !",
                                pPacket->pCreateInfo->imageFormat,
                                i,
                                surfFormats[i].format);
            if (pPacket->pCreateInfo->imageFormat == surfFormats[i].format) {
                found = true;
                break;
            }
        }
        if (!found) {
            vktrace_LogWarning("Format %d is not supported for presentable images, using format %d",
                               pPacket->pCreateInfo->imageFormat, surfFormats[0].format);
            VkFormat *pFormat = (VkFormat *)&(pPacket->pCreateInfo->imageFormat);
            *pFormat = surfFormats[0].format;
        }
    }
    free(surfFormats);

    // Force a present mode via the _VKREPLAY_CREATESWAPCHAIN_PRESENTMODE
    // environment variable. This is sometimes needed for compatibility when replaying
    // old traces on a newer version of a driver.
    VkPresentModeKHR vkreplay_createswapchain_presentmode = VK_PRESENT_MODE_MAX_ENUM_KHR;
    char *vkreplay_createswapchain_presentmode_env;
    vkreplay_createswapchain_presentmode_env = vktrace_get_global_var("_VKREPLAY_CREATESWAPCHAIN_PRESENTMODE");
    if (vkreplay_createswapchain_presentmode_env && *vkreplay_createswapchain_presentmode_env) {
        if (0 == strcmp(vkreplay_createswapchain_presentmode_env, "VK_PRESENT_MODE_IMMEDIATE_KHR"))
            vkreplay_createswapchain_presentmode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        else if (0 == strcmp(vkreplay_createswapchain_presentmode_env, "VK_PRESENT_MODE_MAILBOX_KHR"))
            vkreplay_createswapchain_presentmode = VK_PRESENT_MODE_MAILBOX_KHR;
        else if (0 == strcmp(vkreplay_createswapchain_presentmode_env, "VK_PRESENT_MODE_FIFO_KHR"))
            vkreplay_createswapchain_presentmode = VK_PRESENT_MODE_FIFO_KHR;
        else if (0 == strcmp(vkreplay_createswapchain_presentmode_env, "VK_PRESENT_MODE_FIFO_RELAXED_KHR"))
            vkreplay_createswapchain_presentmode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        else if (0 == strcmp(vkreplay_createswapchain_presentmode_env, "VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR"))
            vkreplay_createswapchain_presentmode = VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR;
        else if (0 == strcmp(vkreplay_createswapchain_presentmode_env, "VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR"))
            vkreplay_createswapchain_presentmode = VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR;
        if (vkreplay_createswapchain_presentmode != VK_PRESENT_MODE_MAX_ENUM_KHR)
            *((VkPresentModeKHR *)(&pPacket->pCreateInfo->presentMode)) = vkreplay_createswapchain_presentmode;
    }

#if !defined(ANDROID) && defined(ARM_ARCH)
    if (g_pReplaySettings->headless == FALSE) {
        // Check if vkSwapchainCreateInfoKHR.imageExtent is between VkSurfaceCapabilitiesKHR.minImageExtent and
        // VkSurfaceCapabilitiesKHR.maxImageExtent, inclusive, where VkSurfaceCapabilitiesKHR is returned by
        // vkGetPhysicalDeviceSurfaceCapabilitiesKHR for the surface.
        if (replaySurfaceCapabilities.find(pPacket->pCreateInfo->surface) == replaySurfaceCapabilities.end()) {
            vktrace_LogWarning(
                "Failed to verify vkSwapchainCreateInfoKHR.imageExtent, vkGetPhysicalDeviceSurfaceCapabilitiesKHR is not called before "
                "vkCreateSwapchainKHR.");
        } else {
            VkSurfaceCapabilitiesKHR surfaceCapabilities = replaySurfaceCapabilities[pPacket->pCreateInfo->surface];
            if (pPacket->pCreateInfo->imageExtent.width < surfaceCapabilities.minImageExtent.width ||
                pPacket->pCreateInfo->imageExtent.height < surfaceCapabilities.minImageExtent.height ||
                pPacket->pCreateInfo->imageExtent.width > surfaceCapabilities.maxImageExtent.width ||
                pPacket->pCreateInfo->imageExtent.height > surfaceCapabilities.maxImageExtent.height) {
                vktrace_LogWarning(
                    "Invalid vkSwapchainCreateInfoKHR.imageExtent %ux%u. It is NOT between "
                    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR's minImageExtent %ux%u and maxImageExtent %ux%u.",
                    pPacket->pCreateInfo->imageExtent.width, pPacket->pCreateInfo->imageExtent.height,
                    surfaceCapabilities.minImageExtent.width, surfaceCapabilities.minImageExtent.height,
                    surfaceCapabilities.maxImageExtent.width, surfaceCapabilities.maxImageExtent.height);
            }
        }
    }
#else
    // Turn off vsync
    if (g_pReplaySettings->vsyncOff == TRUE) {
        VkPresentModeKHR *overwritePresentMode = (VkPresentModeKHR *)&pPacket->pCreateInfo->presentMode;
        *overwritePresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
#endif

    // If the present mode is not FIFO and the present mode requested is not supported by the
    // replay device, then change the present mode to FIFO
    if (pPacket->pCreateInfo->presentMode != VK_PRESENT_MODE_FIFO_KHR &&
        replayPhysicalDevices.find(remappeddevice) != replayPhysicalDevices.end()) {
        // Call GetPhysicalDeviceSurfacePresentModesKHR to get the list of supported present modes
        uint32_t presentModeCount;
        VkPresentModeKHR *pPresentModes;
        VkResult result;
        uint32_t i;
        result = m_vkFuncs.GetPhysicalDeviceSurfacePresentModesKHR(replayPhysicalDevices[remappeddevice],
                                                                   pPacket->pCreateInfo->surface, &presentModeCount, NULL);
        if (result == VK_SUCCESS) {
            pPresentModes = VKTRACE_NEW_ARRAY(VkPresentModeKHR, presentModeCount);
            result = m_vkFuncs.GetPhysicalDeviceSurfacePresentModesKHR(
                replayPhysicalDevices[remappeddevice], pPacket->pCreateInfo->surface, &presentModeCount, pPresentModes);
            if (result == VK_SUCCESS && presentModeCount) {
                for (i = 0; i < presentModeCount; i++) {
                    if (pPacket->pCreateInfo->presentMode == pPresentModes[i])
                        // Found matching present mode
                        break;
                }
                if (i == presentModeCount) {
                    if (g_pReplaySettings->vsyncOff == TRUE) {
                        vktrace_LogError("Present mode %s not supported! vsyncoff option does not work!",
                                         string_VkPresentModeKHR(pPacket->pCreateInfo->presentMode));
                        VKTRACE_DELETE(pPresentModes);
                        return replayResult;
                    } else {
                        vktrace_LogWarning("Present mode %s not supported! Using FIFO instead!",
                                           string_VkPresentModeKHR(pPacket->pCreateInfo->presentMode));
                        // Didn't find a matching present mode, so use FIFO instead.
                        *((VkPresentModeKHR *)(&pPacket->pCreateInfo->presentMode)) = VK_PRESENT_MODE_FIFO_KHR;
                    }
                }
            }
            VKTRACE_DELETE(pPresentModes);
        }
    }

    uint32_t maxImageCount = surfCap.maxImageCount;
    uint32_t old_minImageCount = pPacket->pCreateInfo->minImageCount;
    uint32_t assigned_minImageCount = replaySettings.swapChainMinImageCount;
    if(assigned_minImageCount < old_minImageCount || assigned_minImageCount > maxImageCount){
        assigned_minImageCount = old_minImageCount;
    }
    vktrace_LogDebug("Using swapchain min image count %llu now! Valid min image count: (%llu ~ %llu).", assigned_minImageCount, old_minImageCount, maxImageCount);
    *((uint32_t *)(&pPacket->pCreateInfo->minImageCount)) = assigned_minImageCount;
    if (g_pReplaySettings->enableVirtualSwapchain) {
        const_cast<VkSwapchainCreateInfoKHR*>(pPacket->pCreateInfo)->imageUsage = pPacket->pCreateInfo->imageUsage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    if (g_pReplaySettings->scCompressFlag != UINT_MAX) {
        VkSwapchainCreateInfoKHR *pTemp = const_cast<VkSwapchainCreateInfoKHR*>(pPacket->pCreateInfo);
        static VkImageCompressionControlEXT sccompressionInfo = {};
        sccompressionInfo.sType = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT;
        sccompressionInfo.flags = g_pReplaySettings->scCompressFlag;
        sccompressionInfo.pNext = const_cast<void*>(pTemp->pNext);
        pTemp->pNext = &sccompressionInfo;
        sccompressionInfo.compressionControlPlaneCount = getImagePlaneCount(pPacket->pCreateInfo->imageFormat);
        static VkImageCompressionFixedRateFlagsEXT sccompressFixedRateFlags[3] = {g_pReplaySettings->scCompressRate, g_pReplaySettings->scCompressRate, g_pReplaySettings->scCompressRate};
        sccompressionInfo.pFixedRateFlags = sccompressFixedRateFlags;
    }

    replayResult = m_vkDeviceFuncs.CreateSwapchainKHR(remappeddevice, pPacket->pCreateInfo, pPacket->pAllocator, &local_pSwapchain);
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_swapchainkhrs_map(*(pPacket->pSwapchain), local_pSwapchain);
        replaySwapchainKHRToDevice[local_pSwapchain] = remappeddevice;
        if (g_pReplaySettings->forceSingleWindow) {
            replaySurfToSwapchain[*pSurf] = local_pSwapchain;
            swapchainRefCount++;
        }
        traceSwapchainToCreateInfo[*(pPacket->pSwapchain)] = *(pPacket->pCreateInfo);
        if (pPacket->pCreateInfo->queueFamilyIndexCount && pPacket->pCreateInfo->pQueueFamilyIndices != nullptr) {
            traceSwapchainToCreateInfo[*(pPacket->pSwapchain)].pQueueFamilyIndices = (uint32_t*)malloc(sizeof(uint32_t) * pPacket->pCreateInfo->queueFamilyIndexCount);
            memcpy((void*)traceSwapchainToCreateInfo[*(pPacket->pSwapchain)].pQueueFamilyIndices,  (void*)pPacket->pCreateInfo->pQueueFamilyIndices, sizeof(uint32_t) * pPacket->pCreateInfo->queueFamilyIndexCount);
        }
        curSwapchainHandle = local_pSwapchain;
    }

    (*pSC) = save_oldSwapchain;
    *pSurf = save_surface;

    m_objMapper.m_pImageIndex.clear();

    return replayResult;
}

void vkReplay::deleteVirtualObject(VkDevice remappeddevice, VkImage image) {
    if (traceRealImageToVirtualImage.find(image) != traceRealImageToVirtualImage.end()) {
        VkImage virtualImage = traceRealImageToVirtualImage[image];
        m_vkDeviceFuncs.DestroyImage(remappeddevice, virtualImage, nullptr);
        traceRealImageToVirtualImage.erase(image);
        if (virtualImageToVirtualMemory.find(virtualImage) != virtualImageToVirtualMemory.end()) {
            m_vkDeviceFuncs.FreeMemory(remappeddevice, virtualImageToVirtualMemory[virtualImage], nullptr);
            virtualImageToVirtualMemory.erase(virtualImage);
        }
        if (replayImageToDevice.find(virtualImage) != replayImageToDevice.end()) {
            replayImageToDevice.erase(virtualImage);
        }
        auto fit = virtualImageToVirtualFence.find(virtualImage);
        if (fit != virtualImageToVirtualFence.end()) {
            m_vkDeviceFuncs.DestroyFence(remappeddevice, fit->second, nullptr);
            virtualImageToVirtualFence.erase(fit);
        }
        auto sit = virtualImageToVirtualSemaphore.find(virtualImage);
        if (sit != virtualImageToVirtualSemaphore.end()) {
            m_vkDeviceFuncs.DestroySemaphore(remappeddevice, sit->second, nullptr);
            virtualImageToVirtualSemaphore.erase(sit);
        }
        auto cbit = virtualImageToVirtualCommandBuffer.find(virtualImage);
        if (cbit != virtualImageToVirtualCommandBuffer.end()) {
            auto cpit = virtualImageToVirtualCommandPool.find(virtualImage);
            m_vkDeviceFuncs.FreeCommandBuffers(remappeddevice, cpit->second, 1, &cbit->second);
            virtualImageToVirtualCommandBuffer.erase(cbit);
        }
        auto cpit = virtualImageToVirtualCommandPool.find(virtualImage);
        if (cpit != virtualImageToVirtualCommandPool.end()) {
            m_vkDeviceFuncs.DestroyCommandPool(remappeddevice, cpit->second, nullptr);
            virtualImageToVirtualCommandPool.erase(cpit);
        }
        auto dfit = virtualImageToDeviceFence.find(virtualImage);
        if (dfit != virtualImageToDeviceFence.end()) {
            virtualImageToDeviceFence.erase(dfit);
        }
    }
}

void vkReplay::manually_replay_vkDestroySwapchainKHR(packet_vkDestroySwapchainKHR *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);

    if (g_pReplaySettings->forceSingleWindow && swapchainRefCount > 1) {
        swapchainRefCount--;
        swapchainImageStates[curSwapchainHandle].reset();
        swapchainImageStates[curSwapchainHandle] = savedSwapchainImgStates.top();
        savedSwapchainImgStates.pop();
        return;
    }

    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkDestroySwapchainKHR() due to invalid remapped VkDevice.");
        return;
    }

    VkSwapchainKHR remappedswapchain = m_objMapper.remap_swapchainkhrs(pPacket->swapchain);
    if (pPacket->swapchain != VK_NULL_HANDLE && remappedswapchain == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkDestroySwapchainKHR() due to invalid remapped VkSwapchainKHR.");
        return;
    }

    bool find = findImageFromOtherSwapchain(pPacket->swapchain);
    // Need to unmap images obtained with vkGetSwapchainImagesKHR
    while (!traceSwapchainToImages[pPacket->swapchain].empty()) {
        VkImage image = traceSwapchainToImages[pPacket->swapchain].back();
        if (!find) {
            m_objMapper.rm_from_images_map(image);
            deleteVirtualObject(remappeddevice, image);
        }
        traceSwapchainToImages[pPacket->swapchain].pop_back();
    }

    auto crit = traceSwapchainToVirtualImageCopyRegion.find(pPacket->swapchain);
    if (crit != traceSwapchainToVirtualImageCopyRegion.end()) {
        traceSwapchainToVirtualImageCopyRegion.erase(crit);
    }
    auto scit = traceSwapchainToReplayImages.find(pPacket->swapchain);
        if (scit != traceSwapchainToReplayImages.end()) {
        traceSwapchainToReplayImages.erase(scit);
    }
    m_vkDeviceFuncs.DestroySwapchainKHR(remappeddevice, remappedswapchain, pPacket->pAllocator);
    m_objMapper.rm_from_swapchainkhrs_map(pPacket->swapchain);

    if (!find) {
        swapchainImageStates[remappedswapchain].reset();
    }
    auto it = g_TraceScToScImageCount.find(pPacket->swapchain);
    if (it != g_TraceScToScImageCount.end()) {
        g_TraceScToScImageCount.erase(it);
    }

    auto it2 = traceSwapchainToCreateInfo.find(pPacket->swapchain);
    if (it2 != traceSwapchainToCreateInfo.end()) {
        if (it2->second.queueFamilyIndexCount && it2->second.pQueueFamilyIndices != nullptr) {
            free((void*)it2->second.pQueueFamilyIndices);
        }
        traceSwapchainToCreateInfo.erase(it2);
    }
    m_imageIndex = UINT32_MAX;
    m_pktImgIndex = UINT32_MAX;
}

bool vkReplay::createVirtualObject(VkDevice traceDevice, VkSwapchainKHR traceSwapchain, VkImage* pRealImages, uint32_t traceImageCount) {
    VkDevice device = m_objMapper.remap_devices(traceDevice);
    VkSwapchainCreateInfoKHR* scCreateInfo = &traceSwapchainToCreateInfo[traceSwapchain];
    VkImageCreateInfo pinfo = {};
    pinfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    pinfo.flags = (scCreateInfo->flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR);
    pinfo.imageType = VK_IMAGE_TYPE_2D;
    pinfo.extent.height = scCreateInfo->imageExtent.height;
    pinfo.extent.width = scCreateInfo->imageExtent.width;
    pinfo.extent.depth = 1;
    pinfo.format = scCreateInfo->imageFormat;
    pinfo.mipLevels = 1;
    pinfo.arrayLayers = scCreateInfo->imageArrayLayers;
    pinfo.samples = VK_SAMPLE_COUNT_1_BIT;
    pinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    pinfo.usage = scCreateInfo->imageUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    pinfo.sharingMode = scCreateInfo->imageSharingMode;
    assert(pinfo.sharingMode == VK_SHARING_MODE_EXCLUSIVE); // TBD
    pinfo.queueFamilyIndexCount = scCreateInfo->queueFamilyIndexCount;
    pinfo.pQueueFamilyIndices = scCreateInfo->pQueueFamilyIndices;
    pinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    for (uint32_t i = 0; i < traceImageCount; i++) {
        VkImage virtualImage = VK_NULL_HANDLE;
        auto it = traceRealImageToVirtualImage.find(pRealImages[i]);
        if (it == traceRealImageToVirtualImage.end()) {
            if (g_pReplaySettings->scCompressFlag != UINT_MAX) {
                static VkImageCompressionControlEXT vicompressionInfo = {};
                vicompressionInfo.sType = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT;
                vicompressionInfo.flags = g_pReplaySettings->scCompressFlag;
                pinfo.pNext = &vicompressionInfo;
                vicompressionInfo.compressionControlPlaneCount = getImagePlaneCount(pinfo.format);
                static VkImageCompressionFixedRateFlagsEXT vicompressFixedRateFlags[3] = {g_pReplaySettings->scCompressRate, g_pReplaySettings->scCompressRate, g_pReplaySettings->scCompressRate};
                vicompressionInfo.pFixedRateFlags = vicompressFixedRateFlags;
            }
            VkResult result = m_vkDeviceFuncs.CreateImage(device, &pinfo, nullptr, &virtualImage);
            if (result != VK_SUCCESS) {
                vktrace_LogError("The virtual CreateImage failed.");
                return false;
            }
            VkMemoryRequirements req = {};
            m_vkDeviceFuncs.GetImageMemoryRequirements(device, virtualImage, &req);
            VkMemoryAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = req.size;
            auto mit = traceDeviceMemoryToMemoryTypeIndex.begin();
            if (mit != traceDeviceMemoryToMemoryTypeIndex.end()) {
                 if (!getReplayMemoryTypeIdx(traceDevice, device, mit->second, &req, &allocInfo.memoryTypeIndex)) {
                    vktrace_LogDebug("allocInfo.memoryTypeIndex = %d is error.", allocInfo.memoryTypeIndex);
                    allocInfo.memoryTypeIndex = 0;
                }
            } else {
                allocInfo.memoryTypeIndex = 0;
            }
            VkDeviceMemory virtualMemory;
            result = m_vkDeviceFuncs.AllocateMemory(device, &allocInfo, nullptr, &virtualMemory);
            if (result != VK_SUCCESS) {
                vktrace_LogError("The virtual AllocateMemory create failed.");
                return false;
            }
            result = m_vkDeviceFuncs.BindImageMemory(device, virtualImage, virtualMemory, 0);
            if (result != VK_SUCCESS) {
                vktrace_LogError("The virtual BindImageMemory create failed.");
                return false;
            }
            traceRealImageToVirtualImage[pRealImages[i]] = virtualImage;
            virtualImageToVirtualMemory[virtualImage] = virtualMemory;
            replayImageToDevice[virtualImage] = device;
        }

        VkFence virtualFence = VK_NULL_HANDLE;
        VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0 };
        VkResult result = m_vkDeviceFuncs.CreateFence(device, &fenceInfo, nullptr, &virtualFence);
        if (result != VK_SUCCESS) {
            vktrace_LogError("The virtual CreateFence create failed.");
            return false;
        }
        virtualImageToVirtualFence[virtualImage] = virtualFence;

        VkSemaphoreCreateInfo semaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
        VkSemaphore virtualSemaphore = VK_NULL_HANDLE;
        result = m_vkDeviceFuncs.CreateSemaphore(device, &semaInfo, nullptr, &virtualSemaphore);
        if (result != VK_SUCCESS) {
            vktrace_LogError("The virtual CreateSemaphore create failed.");
            return false;
        }
        virtualImageToVirtualSemaphore[virtualImage] = virtualSemaphore;

        VkCommandPoolCreateInfo cpCreateInfo = {};
        VkCommandPool virtualCommandPool = VK_NULL_HANDLE;
        cpCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (scCreateInfo->queueFamilyIndexCount && scCreateInfo->pQueueFamilyIndices != nullptr) {
            cpCreateInfo.queueFamilyIndex = scCreateInfo->pQueueFamilyIndices[0]; // TBD
        } else {
            cpCreateInfo.queueFamilyIndex = 0;
        }
        result = m_vkDeviceFuncs.CreateCommandPool(device, &cpCreateInfo, nullptr, &virtualCommandPool);
        if (result != VK_SUCCESS) {
            vktrace_LogError("The virtual CreateCommandPool create failed.");
            return false;
        }
        virtualImageToVirtualCommandPool[virtualImage] = virtualCommandPool;

        VkCommandBufferAllocateInfo cbAllocateInfo = {};
        VkCommandBuffer virtualCommandBuffer = VK_NULL_HANDLE;
        cbAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbAllocateInfo.commandPool = virtualCommandPool;
        cbAllocateInfo.commandBufferCount = 1;
        result = m_vkDeviceFuncs.AllocateCommandBuffers(device, &cbAllocateInfo, &virtualCommandBuffer);
        if (result != VK_SUCCESS) {
            vktrace_LogError("virtual AllocateCommandBuffers create failed.");
            return false;
        }
        virtualImageToVirtualCommandBuffer[virtualImage] = virtualCommandBuffer;
    }
    VkImageCopy virtualImageCopyRegion = {};
    virtualImageCopyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    virtualImageCopyRegion.srcOffset = { 0, 0, 0 };
    virtualImageCopyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    virtualImageCopyRegion.dstOffset = { 0, 0, 0 };
    virtualImageCopyRegion.extent.width = scCreateInfo->imageExtent.width;
    virtualImageCopyRegion.extent.height = scCreateInfo->imageExtent.height;
    virtualImageCopyRegion.extent.depth = 1;
    traceSwapchainToVirtualImageCopyRegion[traceSwapchain] = virtualImageCopyRegion;
    return true;
}

VkImage vkReplay::getVirtualSwapchainImage(VkImage traceImage) {
    auto it = traceRealImageToVirtualImage.find(traceImage);
    if (it != traceRealImageToVirtualImage.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

VkResult vkReplay::manually_replay_vkGetSwapchainImagesKHR(packet_vkGetSwapchainImagesKHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;

    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetSwapchainImagesKHR() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkSwapchainKHR remappedswapchain;
    remappedswapchain = m_objMapper.remap_swapchainkhrs(pPacket->swapchain);
    if (remappedswapchain == VK_NULL_HANDLE && pPacket->swapchain != VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkGetSwapchainImagesKHR() due to invalid remapped VkSwapchainKHR.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkImage packetImage[128] = {};
    uint32_t numImages = 0;
    if (pPacket->pSwapchainImages != NULL) {
        // Need to store the images and then add to map after we get actual image handles back
        VkImage *pPacketImages = (VkImage *)pPacket->pSwapchainImages;
        numImages = *(pPacket->pSwapchainImageCount);
        for (uint32_t i = 0; i < numImages; i++) {
            packetImage[i] = pPacketImages[i];
            swapchainImageStates[remappedswapchain].traceImageIndexToImage[i] = packetImage[i];
            swapchainImageStates[remappedswapchain].traceImageToImageIndex[packetImage[i]] = i;
            traceImageToDevice[packetImage[i]] = pPacket->device;
        }
    }
    VkImage* pOldImage = nullptr;
    VkImage* pNewImage = nullptr;
    uint32_t traceImageCount = *(pPacket->pSwapchainImageCount);
    auto it = g_TraceScToScImageCount.find(pPacket->swapchain);
    if (it != g_TraceScToScImageCount.end() && pPacket->pSwapchainImages != nullptr && traceImageCount != it->second)  {
        *pPacket->pSwapchainImageCount = it->second;
        pOldImage = pPacket->pSwapchainImages;
        pNewImage = new VkImage[it->second];
        pPacket->pSwapchainImages = pNewImage;
    }
    if (numImages) {
        vktrace_LogAlways("Swapchain image count = %d in trace file, the current swapchain image count = %d.",numImages, *pPacket->pSwapchainImageCount);
    }
    replayResult = m_vkDeviceFuncs.GetSwapchainImagesKHR(remappeddevice, remappedswapchain, pPacket->pSwapchainImageCount,
                                                         pPacket->pSwapchainImages);
    if (pPacket->pSwapchainImages == nullptr && traceImageCount != *pPacket->pSwapchainImageCount) {
        g_TraceScToScImageCount[pPacket->swapchain] = *pPacket->pSwapchainImageCount;
    }
    if (replayResult == VK_SUCCESS) {
        if (numImages != 0) {
            VkImage *pReplayImages = (VkImage *)pPacket->pSwapchainImages;
            if (g_pReplaySettings->enableVirtualSwapchain) {
                bool bFlag = createVirtualObject(pPacket->device, pPacket->swapchain, packetImage, numImages);
                if (!bFlag) {
                    vktrace_LogError("There is some virtual object created failed.");
                    return VK_ERROR_VALIDATION_FAILED_EXT;
                }
                for (uint32_t i = 0; i < numImages; i++) {
                    imageObj local_imageObj;
                    local_imageObj.replayImage = traceRealImageToVirtualImage[packetImage[i]];
                    m_objMapper.add_to_images_map(packetImage[i], local_imageObj);
                }
            } else {
                if (*pPacket->pSwapchainImageCount != numImages) {
                    for (uint32_t i = 0; i < numImages; i++) {
                        uint32_t replayImageIdx = i % *pPacket->pSwapchainImageCount;
                        imageObj local_imageObj;
                        local_imageObj.replayImage = pReplayImages[replayImageIdx];
                        m_objMapper.add_to_images_map(packetImage[i], local_imageObj);
                        replayImageToDevice[pReplayImages[replayImageIdx]] = remappeddevice;
                    }
                } else {
                    for (uint32_t i = 0; i < numImages; i++) {
                        imageObj local_imageObj;
                        local_imageObj.replayImage = pReplayImages[i];
                        m_objMapper.add_to_images_map(packetImage[i], local_imageObj);
                        replayImageToDevice[pReplayImages[i]] = remappeddevice;
                    }
                }
            }
            for (uint32_t i = 0; i < numImages; i++) {
                traceSwapchainToImages[pPacket->swapchain].push_back(packetImage[i]);
            }
            for (uint32_t i = 0; i < *pPacket->pSwapchainImageCount; i++) {
                traceSwapchainToReplayImages[pPacket->swapchain].push_back(pReplayImages[i]);
                replaySwapchainImageToDevice[pReplayImages[i]] = remappeddevice;
            }
            swapchainImageAcquireStatus[pPacket->swapchain].resize(*pPacket->pSwapchainImageCount, false);
        }
    }
    if (pNewImage != nullptr) {
        pPacket->pSwapchainImages = pOldImage;
        delete []pNewImage;
    }
    return replayResult;
}

VkResult vkReplay::copyFromVirtualImageToReplayImage(VkQueue traceQueue, VkImage virtualImage, VkImage replayImage, VkImageCopy* pCopyReg, uint32_t queueFamilyIndex, std::vector<VkSemaphore>& semaphores) {
    VkQueue queue = m_objMapper.remap_queues(traceQueue);
    VkCommandBufferBeginInfo command_buffer_begin_info = {};
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VkResult result = m_vkDeviceFuncs.BeginCommandBuffer(virtualImageToVirtualCommandBuffer[virtualImage], &command_buffer_begin_info);
    assert(result == VK_SUCCESS);
    std::vector<VkImageMemoryBarrier> image_barriers(2);
    VkImageMemoryBarrier& image_barrier_src = image_barriers.at(0);
    VkImageMemoryBarrier& image_barrier_dst = image_barriers.at(1);

    image_barrier_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier_src.pNext = nullptr;
    image_barrier_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    image_barrier_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_barrier_src.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // TBD
    image_barrier_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // TBD
    image_barrier_src.image = virtualImage;
    image_barrier_src.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    image_barrier_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier_dst.pNext = nullptr;
    image_barrier_dst.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier_dst.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    image_barrier_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // TBD
    image_barrier_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // TBD
    image_barrier_dst.image = replayImage;
    image_barrier_dst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    m_vkDeviceFuncs.CmdPipelineBarrier(virtualImageToVirtualCommandBuffer[virtualImage], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image_barrier_src);
    m_vkDeviceFuncs.CmdPipelineBarrier(virtualImageToVirtualCommandBuffer[virtualImage], VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image_barrier_dst);
    m_vkDeviceFuncs.CmdCopyImage(virtualImageToVirtualCommandBuffer[virtualImage], virtualImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        replayImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, pCopyReg);

    image_barrier_src.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_barrier_src.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    image_barrier_src.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier_src.newLayout = VK_IMAGE_LAYOUT_UNDEFINED; // TBD could also be VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR
    image_barrier_dst.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier_src.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier_dst.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier_dst.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // TBD could also be VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR
    m_vkDeviceFuncs.CmdPipelineBarrier(virtualImageToVirtualCommandBuffer[virtualImage], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, image_barriers.size(), image_barriers.data());

    result = m_vkDeviceFuncs.EndCommandBuffer(virtualImageToVirtualCommandBuffer[virtualImage]);
    assert(result == VK_SUCCESS);
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &virtualImageToVirtualCommandBuffer[virtualImage];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &virtualImageToVirtualSemaphore[virtualImage];
    result = m_vkDeviceFuncs.QueueSubmit(queue, 1, &submitInfo,virtualImageToVirtualFence[virtualImage]);
    assert(result == VK_SUCCESS);
    semaphores.push_back(virtualImageToVirtualSemaphore[virtualImage]);
    auto it = traceQueueToDevice.find(traceQueue);
    assert(it != traceQueueToDevice.end());
    virtualFence obj = {m_objMapper.remap_devices(it->second), virtualImageToVirtualFence[virtualImage]};
    virtualImageToDeviceFence[virtualImage] = obj;
    if (m_bScreenshotLayer) {
        m_vkDeviceFuncs.WaitForFences(obj.device, 1, &(obj.fence), VK_TRUE, UINT64_MAX);
    }
    return result;
}

VkResult vkReplay::manually_replay_vkQueuePresentKHR(packet_vkQueuePresentKHR *pPacket) {
    VkResult replayResult = VK_SUCCESS;
    uint32_t remappedImageIndex = m_objMapper.remap_pImageIndex(*pPacket->pPresentInfo->pImageIndices);
    std::vector<VkSemaphore> semaphores;
    if (g_pReplaySettings->enableVirtualSwapchain && g_pReplaySettings->enableVscPerfMode == FALSE) {
        for (uint32_t i = 0; i < pPacket->pPresentInfo->swapchainCount; i++) {
            VkImage traceImage = traceSwapchainToImages[pPacket->pPresentInfo->pSwapchains[i]][pPacket->pPresentInfo->pImageIndices[0]];
            VkImage virtualImage = getVirtualSwapchainImage(traceImage);
            if (virtualImage != VK_NULL_HANDLE) {
                auto it = virtualImageToDeviceFence.find(virtualImage);
                if (it != virtualImageToDeviceFence.end()) {
                    replayResult = m_vkDeviceFuncs.WaitForFences(it->second.device, 1, &it->second.fence, VK_TRUE, 0);
                    if (replayResult != VK_SUCCESS) {
                        replayResult = m_vkDeviceFuncs.WaitForFences(it->second.device, 1, &it->second.fence, VK_TRUE, UINT64_MAX);
                    }
                    m_vkDeviceFuncs.ResetFences(it->second.device, 1, &it->second.fence);
                    virtualImageToDeviceFence.erase(it);
                }
                VkImage replayImage = traceSwapchainToReplayImages[pPacket->pPresentInfo->pSwapchains[i]][remappedImageIndex];
                VkImageCopy copyRegion = traceSwapchainToVirtualImageCopyRegion[pPacket->pPresentInfo->pSwapchains[i]];
                VkSwapchainCreateInfoKHR* scCreateInfo = &traceSwapchainToCreateInfo[pPacket->pPresentInfo->pSwapchains[i]];
                uint32_t queueFamilyIndex = 0;
                if (scCreateInfo->queueFamilyIndexCount && scCreateInfo->pQueueFamilyIndices != nullptr) {
                    queueFamilyIndex = scCreateInfo->pQueueFamilyIndices[0];
                } else {
                    queueFamilyIndex = 0;
                }
                copyFromVirtualImageToReplayImage(pPacket->queue, virtualImage, replayImage, &copyRegion, queueFamilyIndex, semaphores);
            }
        }
    }
    VkQueue remappedQueue = m_objMapper.remap_queues(pPacket->queue);
    if (remappedQueue == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkQueuePresentKHR() due to invalid remapped VkQueue.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkResult localResults[5];
    VkSemaphore *pRemappedWaitSems = (VkSemaphore *)pPacket->pPresentInfo->pWaitSemaphores;
    VkSwapchainKHR *pRemappedSwapchains = (VkSwapchainKHR *)pPacket->pPresentInfo->pSwapchains;
    VkResult *pResults = localResults;
    VkPresentInfoKHR present;
    uint32_t i = 0;

    if (pPacket->pPresentInfo->swapchainCount > 5 && pPacket->pPresentInfo->pResults != NULL) {
        pResults = VKTRACE_NEW_ARRAY(VkResult, pPacket->pPresentInfo->swapchainCount);
    }

    if (pResults == NULL) {
        replayResult = VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    if (replayResult == VK_SUCCESS) {
        for (i = 0; i < pPacket->pPresentInfo->swapchainCount; i++) {
            pRemappedSwapchains[i] = m_objMapper.remap_swapchainkhrs(pPacket->pPresentInfo->pSwapchains[i]);
            if (pRemappedSwapchains[i] == VK_NULL_HANDLE) {
                vktrace_LogError("Skipping vkQueuePresentKHR() due to invalid remapped VkSwapchainKHR.");
                replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
                goto out;
            }
        }

        assert(pPacket->pPresentInfo->swapchainCount == 1 && "Multiple swapchain images not supported yet");

        if (remappedImageIndex == UINT32_MAX) {
            vktrace_LogError("Skipping vkQueuePresentKHR() due to invalid remapped pImageIndices.");
            replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
            goto out;
        }

        present.sType = pPacket->pPresentInfo->sType;
        present.pNext = pPacket->pPresentInfo->pNext;
        present.swapchainCount = pPacket->pPresentInfo->swapchainCount;
        present.pSwapchains = pRemappedSwapchains;
        present.pImageIndices = &remappedImageIndex;
        present.waitSemaphoreCount = pPacket->pPresentInfo->waitSemaphoreCount;
        present.pWaitSemaphores = NULL;
        if (present.waitSemaphoreCount != 0) {
            present.pWaitSemaphores = pRemappedWaitSems;
            for (i = 0; i < pPacket->pPresentInfo->waitSemaphoreCount; i++) {
                (*(pRemappedWaitSems + i)) = m_objMapper.remap_semaphores((*(pPacket->pPresentInfo->pWaitSemaphores + i)));
                if (*(pRemappedWaitSems + i) == VK_NULL_HANDLE) {
                    vktrace_LogError("Skipping vkQueuePresentKHR() due to invalid remapped wait VkSemaphore.");
                    replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
                    goto out;
                }
            }
        }
        present.pResults = NULL;
    }

    if (replayResult == VK_SUCCESS) {
        // If the application requested per-swapchain results, set up to get the results from the replay.
        if (pPacket->pPresentInfo->pResults != NULL) {
            present.pResults = pResults;
        }
        if (g_pReplaySettings->finishBeforeSwap)  {
            auto it = traceQueueToDevice.find(pPacket->queue);
            if (it != traceQueueToDevice.end()) {
                VkDevice remappeddevice = m_objMapper.remap_devices(it->second);
                if (remappeddevice != VK_NULL_HANDLE) {
                   m_vkDeviceFuncs.DeviceWaitIdle(remappeddevice);
                }
            }
        }
        std::vector<VkSemaphore> newSemaphores;
        for (uint32_t i = 0; i < present.waitSemaphoreCount; i++) {
            newSemaphores.push_back(present.pWaitSemaphores[i]);
        }
        for (uint32_t i = 0; i < semaphores.size(); i++) {
            newSemaphores.push_back(semaphores[i]);
        }

        present.waitSemaphoreCount = newSemaphores.size();
        present.pWaitSemaphores = newSemaphores.data();
        replayResult = m_vkDeviceFuncs.QueuePresentKHR(remappedQueue, &present);
        m_frameNumber++;

        // Compare the results from the trace file with those just received from the replay.  Report any differences.
        if (present.pResults != NULL) {
            for (i = 0; i < pPacket->pPresentInfo->swapchainCount; i++) {
                if (present.pResults[i] != pPacket->pPresentInfo->pResults[i]) {
                    vktrace_LogError(
                        "Return value %s from API call (VkQueuePresentKHR) does not match return value from trace file %s for "
                        "swapchain %d.",
                        string_VkResult(present.pResults[i]), string_VkResult(pPacket->pPresentInfo->pResults[i]), i);
                }
            }
        }
    }

out:

    if (pResults != NULL && pResults != localResults) {
        VKTRACE_DELETE(pResults);
    }

    if (pPacket->result != VK_SUCCESS) {
        // The call failed during tracing, probably because the window was resized by the app.
        // We'll return the result from the trace file, even though the call succeeded.
        return pPacket->result;
    }

    return replayResult;
}

VkResult vkReplay::manually_replay_vkAcquireNextImageKHR(packet_vkAcquireNextImageKHR* pPacket) {
    VkResult replayResult = VK_SUCCESS;
    uint32_t local_pImageIndex;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in AcquireNextImageKHR() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkSwapchainKHR remappedswapchain = m_objMapper.remap_swapchainkhrs(pPacket->swapchain);
    if (pPacket->swapchain != VK_NULL_HANDLE && remappedswapchain == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in AcquireNextImageKHR() due to invalid remapped VkSwapchainKHR.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    // No need to remap timeout
    VkSemaphore remappedsemaphore = m_objMapper.remap_semaphores(pPacket->semaphore);
    if (pPacket->semaphore != VK_NULL_HANDLE && remappedsemaphore == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in AcquireNextImageKHR() due to invalid remapped VkSemaphore.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkFence remappedfence = m_objMapper.remap_fences(pPacket->fence);
    if (pPacket->fence != VK_NULL_HANDLE && remappedfence == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in AcquireNextImageKHR() due to invalid remapped VkFence.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    if (g_pReplaySettings->forceSyncImgIdx) {
        uint32_t swapchain_img_count = swapchainImageAcquireStatus[pPacket->swapchain].size();
        local_pImageIndex = *(pPacket->pImageIndex);
        acquireSemaphoreToFSIISemaphore.clear();
        if (*(pPacket->pImageIndex) < swapchain_img_count && swapchainImageAcquireStatus[pPacket->swapchain][*(pPacket->pImageIndex)]) {
            swapchainImageAcquireStatus[pPacket->swapchain][*(pPacket->pImageIndex)] = false;
            acquireSemaphoreToFSIISemaphore[remappedsemaphore] = swapchainImgIdxToAcquireSemaphoreAndFence[local_pImageIndex].first;  // remapping the semaphore
            acquireFenceToFSIIFence[remappedfence] = swapchainImgIdxToAcquireSemaphoreAndFence[local_pImageIndex].second;
        } else {
            while(true) {
                const static uint32_t maxFSIIsemaphoresCount = 6;
                VkSemaphoreCreateInfo semaInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0};
                VkSemaphore tmpSema = 0;
                VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, 0};
                VkFence tmpFence = 0;
                if (fsiiSemaphoresAndFences.size() < maxFSIIsemaphoresCount) {
                    if (m_vkDeviceFuncs.CreateSemaphore(remappeddevice, &semaInfo, NULL, &tmpSema) == VK_SUCCESS
                        && m_vkDeviceFuncs.CreateFence(remappeddevice, &fenceInfo, NULL, &tmpFence) == VK_SUCCESS) {
                        fsiiSemaphoresAndFences.push({tmpSema, tmpFence});
                    } else {
                        vktrace_LogError("vkAcquireNextImage - fsii create semaphore / fence failed!");
                        break;
                    }
                } else {
                    // reuse the oldest semaphore.
                    tmpSema = fsiiSemaphoresAndFences.front().first;
                    tmpFence = fsiiSemaphoresAndFences.front().second;
                    fsiiSemaphoresAndFences.pop();
                    fsiiSemaphoresAndFences.push({tmpSema, tmpFence});
                }
                replayResult = m_vkDeviceFuncs.AcquireNextImageKHR(remappeddevice, remappedswapchain, pPacket->timeout, tmpSema, tmpFence, &local_pImageIndex);
                if (replayResult == VK_SUCCESS) {
                    swapchainImgIdxToAcquireSemaphoreAndFence[local_pImageIndex] = {tmpSema, tmpFence};
                    if (local_pImageIndex != *(pPacket->pImageIndex) && *(pPacket->pImageIndex) < swapchain_img_count) {
                        swapchainImageAcquireStatus[pPacket->swapchain][local_pImageIndex] = true;
                        continue;
                    } else {
                        // remapping the semaphore
                        acquireSemaphoreToFSIISemaphore[remappedsemaphore] = tmpSema;
                        acquireFenceToFSIIFence[remappedfence] = tmpFence;
                    }
                }
                break;
            }
        }
    } else {
        replayResult = m_vkDeviceFuncs.AcquireNextImageKHR(remappeddevice, remappedswapchain, pPacket->timeout, remappedsemaphore, remappedfence, &local_pImageIndex);
    }
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_pImageIndex_map(*(pPacket->pImageIndex), local_pImageIndex);
        m_pktImgIndex = *(pPacket->pImageIndex);
        m_imageIndex = local_pImageIndex;
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateXcbSurfaceKHR(packet_vkCreateXcbSurfaceKHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkInstance remappedInstance = m_objMapper.remap_instances(pPacket->instance);
    if (remappedInstance == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateXcbSurfaceKHR() due to invalid remapped VkInstance.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

#if defined(PLATFORM_LINUX) && !defined(ANDROID)
#if defined(VK_USE_PLATFORM_XCB_KHR)
    if (m_displayServer == VK_DISPLAY_XCB) {
        VkIcdSurfaceXcb *pSurf = (VkIcdSurfaceXcb *)m_display->get_surface();
        VkXcbSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.connection = pSurf->connection;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateXcbSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (m_displayServer == VK_DISPLAY_XLIB) {
        VkIcdSurfaceXlib *pSurf = (VkIcdSurfaceXlib *)m_display->get_surface();
        VkXlibSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.dpy = pSurf->dpy;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateXlibSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (m_displayServer == VK_DISPLAY_WAYLAND) {
        VkIcdSurfaceWayland *pSurf = (VkIcdSurfaceWayland *)m_display->get_surface();
        VkWaylandSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.display = pSurf->display;
        createInfo.surface = pSurf->surface;
        replayResult = m_vkFuncs.CreateWaylandSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
    if (m_displayServer == VK_DISPLAY_NONE) {
        if (g_pReplaySettings->headless == TRUE) {
            replayResult = create_headless_surface_ext(remappedInstance, &local_pSurface);
        } else {
            VkIcdSurfaceDisplay *pSurf = (VkIcdSurfaceDisplay *)m_display->get_surface();
            VkDisplaySurfaceCreateInfoKHR createInfo;
            createInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
            createInfo.pNext = NULL;
            createInfo.flags = 0;
            createInfo.displayMode = pSurf->displayMode;
            createInfo.planeIndex = pSurf->planeIndex;
            createInfo.planeStackIndex = pSurf->planeStackIndex;
            createInfo.transform = pSurf->transform;
            createInfo.globalAlpha = pSurf->globalAlpha;
            createInfo.alphaMode = pSurf->alphaMode;
            createInfo.imageExtent = pSurf->imageExtent;
            replayResult = m_vkFuncs.CreateDisplayPlaneSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
        }
    }
#elif defined(WIN32)
    VkIcdSurfaceWin32 *pSurf = (VkIcdSurfaceWin32 *)m_display->get_surface();
    VkWin32SurfaceCreateInfoKHR createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = pPacket->pCreateInfo->pNext;
    createInfo.flags = pPacket->pCreateInfo->flags;
    createInfo.hinstance = pSurf->hinstance;
    createInfo.hwnd = pSurf->hwnd;
    replayResult = m_vkFuncs.CreateWin32SurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
#elif defined(ANDROID)
    VkIcdSurfaceAndroid *pSurf = (VkIcdSurfaceAndroid *)m_display->get_surface();
    if (local_pSurface == VK_NULL_HANDLE || !g_pReplaySettings->forceSingleWindow) {
        VkAndroidSurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateAndroidSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    } else {
        replayResult = VK_SUCCESS;
    }
#else
    vktrace_LogError("manually_replay_vkCreateXcbSurfaceKHR not implemented on this vkreplay platform");
    replayResult = VK_ERROR_FEATURE_NOT_PRESENT;
#endif

    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_surfacekhrs_map(*(pPacket->pSurface), local_pSurface);
        if (g_pReplaySettings->forceSingleWindow)
            surfRefCount++;
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateXlibSurfaceKHR(packet_vkCreateXlibSurfaceKHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkInstance remappedinstance = m_objMapper.remap_instances(pPacket->instance);

    if (pPacket->instance != VK_NULL_HANDLE && remappedinstance == VK_NULL_HANDLE) {
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

#if defined(PLATFORM_LINUX) && defined(VK_USE_PLATFORM_ANDROID_KHR)
    VkIcdSurfaceAndroid *pSurf = (VkIcdSurfaceAndroid *)m_display->get_surface();
    if (local_pSurface == VK_NULL_HANDLE || !g_pReplaySettings->forceSingleWindow) {
        VkAndroidSurfaceCreateInfoKHR createInfo;
        createInfo.sType = pPacket->pCreateInfo->sType;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateAndroidSurfaceKHR(remappedinstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    } else {
        replayResult = VK_SUCCESS;
    }
#elif defined(PLATFORM_LINUX)
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (m_displayServer == VK_DISPLAY_XLIB) {
        VkIcdSurfaceXlib *pSurf = (VkIcdSurfaceXlib *)m_display->get_surface();
        VkXlibSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.dpy = pSurf->dpy;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateXlibSurfaceKHR(remappedinstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
    if (m_displayServer == VK_DISPLAY_XCB) {
        VkIcdSurfaceXcb *pSurf = (VkIcdSurfaceXcb *)m_display->get_surface();
        VkXcbSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.connection = pSurf->connection;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateXcbSurfaceKHR(remappedinstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (m_displayServer == VK_DISPLAY_WAYLAND) {
        VkIcdSurfaceWayland *pSurf = (VkIcdSurfaceWayland *)m_display->get_surface();
        VkWaylandSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.display = pSurf->display;
        createInfo.surface = pSurf->surface;
        replayResult = m_vkFuncs.CreateWaylandSurfaceKHR(remappedinstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
    if (m_displayServer == VK_DISPLAY_NONE) {
        if (g_pReplaySettings->headless == TRUE) {
            replayResult = create_headless_surface_ext(remappedinstance, &local_pSurface);
        } else {
            VkIcdSurfaceDisplay *pSurf = (VkIcdSurfaceDisplay *)m_display->get_surface();
            VkDisplaySurfaceCreateInfoKHR createInfo;
            createInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
            createInfo.pNext = NULL;
            createInfo.flags = 0;
            createInfo.displayMode = pSurf->displayMode;
            createInfo.planeIndex = pSurf->planeIndex;
            createInfo.planeStackIndex = pSurf->planeStackIndex;
            createInfo.transform = pSurf->transform;
            createInfo.globalAlpha = pSurf->globalAlpha;
            createInfo.alphaMode = pSurf->alphaMode;
            createInfo.imageExtent = pSurf->imageExtent;
            replayResult = m_vkFuncs.CreateDisplayPlaneSurfaceKHR(remappedinstance, &createInfo, pPacket->pAllocator, &local_pSurface);
        }
    }
#elif defined(WIN32)
    VkIcdSurfaceWin32 *pSurf = (VkIcdSurfaceWin32 *)m_display->get_surface();
    VkWin32SurfaceCreateInfoKHR createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = pPacket->pCreateInfo->pNext;
    createInfo.flags = pPacket->pCreateInfo->flags;
    createInfo.hinstance = pSurf->hinstance;
    createInfo.hwnd = pSurf->hwnd;
    replayResult = m_vkFuncs.CreateWin32SurfaceKHR(remappedinstance, &createInfo, pPacket->pAllocator, &local_pSurface);
#elif defined(ANDROID)
    VkIcdSurfaceAndroid *pSurf = (VkIcdSurfaceAndroid *)m_display->get_surface();
    if (local_pSurface == VK_NULL_HANDLE || !g_pReplaySettings->forceSingleWindow) {
        VkAndroidSurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateAndroidSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    } else {
        replayResult = VK_SUCCESS;
    }
#else
    vktrace_LogError("manually_replay_vkCreateXlibSurfaceKHR not implemented on this playback platform");
    replayResult = VK_ERROR_FEATURE_NOT_PRESENT;
#endif
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_surfacekhrs_map(*(pPacket->pSurface), local_pSurface);
        if (g_pReplaySettings->forceSingleWindow)
            surfRefCount++;
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateWaylandSurfaceKHR(packet_vkCreateWaylandSurfaceKHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkInstance remappedinstance = m_objMapper.remap_instances(pPacket->instance);

    if (pPacket->instance != VK_NULL_HANDLE && remappedinstance == VK_NULL_HANDLE) {
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

#if defined(PLATFORM_LINUX) && defined(VK_USE_PLATFORM_ANDROID_KHR)
// TODO
#elif defined(PLATFORM_LINUX)
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (m_displayServer == VK_DISPLAY_WAYLAND) {
        VkIcdSurfaceWayland *pSurf = (VkIcdSurfaceWayland *)m_display->get_surface();
        VkWaylandSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.display = pSurf->display;
        createInfo.surface = pSurf->surface;
        replayResult = m_vkFuncs.CreateWaylandSurfaceKHR(remappedinstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
    if (m_displayServer == VK_DISPLAY_XCB) {
        VkIcdSurfaceXcb *pSurf = (VkIcdSurfaceXcb *)m_display->get_surface();
        VkXcbSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.connection = pSurf->connection;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateXcbSurfaceKHR(remappedinstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (m_displayServer == VK_DISPLAY_XCB) {
        VkIcdSurfaceXlib *pSurf = (VkIcdSurfaceXlib *)m_display->get_surface();
        VkXlibSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.dpy = pSurf->dpy;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateXlibSurfaceKHR(remappedinstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
    if (m_displayServer == VK_DISPLAY_NONE) {
        if (g_pReplaySettings->headless == TRUE) {
            replayResult = create_headless_surface_ext(remappedinstance, &local_pSurface);
        } else {
            VkIcdSurfaceDisplay *pSurf = (VkIcdSurfaceDisplay *)m_display->get_surface();
            VkDisplaySurfaceCreateInfoKHR createInfo;
            createInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
            createInfo.pNext = NULL;
            createInfo.flags = 0;
            createInfo.displayMode = pSurf->displayMode;
            createInfo.planeIndex = pSurf->planeIndex;
            createInfo.planeStackIndex = pSurf->planeStackIndex;
            createInfo.transform = pSurf->transform;
            createInfo.globalAlpha = pSurf->globalAlpha;
            createInfo.alphaMode = pSurf->alphaMode;
            createInfo.imageExtent = pSurf->imageExtent;
            replayResult = m_vkFuncs.CreateDisplayPlaneSurfaceKHR(remappedinstance, &createInfo, pPacket->pAllocator, &local_pSurface);
        }
    }
#elif defined(WIN32)
    VkIcdSurfaceWin32 *pSurf = (VkIcdSurfaceWin32 *)m_display->get_surface();
    VkWin32SurfaceCreateInfoKHR createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = pPacket->pCreateInfo->pNext;
    createInfo.flags = pPacket->pCreateInfo->flags;
    createInfo.hinstance = pSurf->hinstance;
    createInfo.hwnd = pSurf->hwnd;
    replayResult = m_vkFuncs.CreateWin32SurfaceKHR(remappedinstance, &createInfo, pPacket->pAllocator, &local_pSurface);
#elif defined(ANDROID)
    VkIcdSurfaceAndroid *pSurf = (VkIcdSurfaceAndroid *)m_display->get_surface();
    if (local_pSurface == VK_NULL_HANDLE || !g_pReplaySettings->forceSingleWindow) {
        VkAndroidSurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateAndroidSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    } else {
        replayResult = VK_SUCCESS;
    }
#else
    vktrace_LogError("manually_replay_vkCreateWaylandSurfaceKHR not implemented on this playback platform");
    replayResult = VK_ERROR_FEATURE_NOT_PRESENT;
#endif
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_surfacekhrs_map(*(pPacket->pSurface), local_pSurface);
        if (g_pReplaySettings->forceSingleWindow)
            surfRefCount++;
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateWin32SurfaceKHR(packet_vkCreateWin32SurfaceKHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkInstance remappedInstance = m_objMapper.remap_instances(pPacket->instance);
    if (remappedInstance == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateWin32SurfaceKHR() due to invalid remapped VkInstance.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

#if defined(WIN32)
    VkIcdSurfaceWin32 *pSurf = (VkIcdSurfaceWin32 *)m_display->get_surface();
    VkWin32SurfaceCreateInfoKHR createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = pPacket->pCreateInfo->pNext;
    createInfo.flags = pPacket->pCreateInfo->flags;
    createInfo.hinstance = pSurf->hinstance;
    createInfo.hwnd = pSurf->hwnd;
    replayResult = m_vkFuncs.CreateWin32SurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
#elif defined(PLATFORM_LINUX) && !defined(ANDROID)
#if defined(VK_USE_PLATFORM_XCB_KHR)
    if (m_displayServer == VK_DISPLAY_XCB) {
        VkIcdSurfaceXcb *pSurf = (VkIcdSurfaceXcb *)m_display->get_surface();
        VkXcbSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.connection = pSurf->connection;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateXcbSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (m_displayServer == VK_DISPLAY_XLIB) {
        VkIcdSurfaceXlib *pSurf = (VkIcdSurfaceXlib *)m_display->get_surface();
        VkXlibSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.dpy = pSurf->dpy;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateXlibSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (m_displayServer == VK_DISPLAY_WAYLAND) {
        VkIcdSurfaceWayland *pSurf = (VkIcdSurfaceWayland *)m_display->get_surface();
        VkWaylandSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.display = pSurf->display;
        createInfo.surface = pSurf->surface;
        replayResult = m_vkFuncs.CreateWaylandSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
    if (m_displayServer == VK_DISPLAY_NONE) {
        if (g_pReplaySettings->headless == TRUE) {
            replayResult = create_headless_surface_ext(remappedInstance, &local_pSurface);
        } else {
            VkIcdSurfaceDisplay *pSurf = (VkIcdSurfaceDisplay *)m_display->get_surface();
            VkDisplaySurfaceCreateInfoKHR createInfo;
            createInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
            createInfo.pNext = NULL;
            createInfo.flags = 0;
            createInfo.displayMode = pSurf->displayMode;
            createInfo.planeIndex = pSurf->planeIndex;
            createInfo.planeStackIndex = pSurf->planeStackIndex;
            createInfo.transform = pSurf->transform;
            createInfo.globalAlpha = pSurf->globalAlpha;
            createInfo.alphaMode = pSurf->alphaMode;
            createInfo.imageExtent = pSurf->imageExtent;
            replayResult = m_vkFuncs.CreateDisplayPlaneSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
        }
    }
#elif defined(ANDROID)
    VkIcdSurfaceAndroid *pSurf = (VkIcdSurfaceAndroid *)m_display->get_surface();
    if (local_pSurface == VK_NULL_HANDLE || !g_pReplaySettings->forceSingleWindow) {
        VkAndroidSurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateAndroidSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    } else {
        replayResult = VK_SUCCESS;
    }
#else
    vktrace_LogError("manually_replay_vkCreateWin32SurfaceKHR not implemented on this playback platform");
    replayResult = VK_ERROR_FEATURE_NOT_PRESENT;
#endif
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_surfacekhrs_map(*(pPacket->pSurface), local_pSurface);
        if (g_pReplaySettings->forceSingleWindow)
            surfRefCount++;
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateAndroidSurfaceKHR(packet_vkCreateAndroidSurfaceKHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkInstance remappedInstance = m_objMapper.remap_instances(pPacket->instance);
    if (remappedInstance == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateAndroidSurfaceKHR() due to invalid remapped VkInstance.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
#if defined(WIN32)
    VkIcdSurfaceWin32 *pSurf = (VkIcdSurfaceWin32 *)m_display->get_surface();
    VkWin32SurfaceCreateInfoKHR createInfo;
    createInfo.sType = pPacket->pCreateInfo->sType;
    createInfo.pNext = pPacket->pCreateInfo->pNext;
    createInfo.flags = pPacket->pCreateInfo->flags;
    createInfo.hinstance = pSurf->hinstance;
    createInfo.hwnd = pSurf->hwnd;
    replayResult = m_vkFuncs.CreateWin32SurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
#elif defined(PLATFORM_LINUX)
#if !defined(ANDROID)
#if defined(VK_USE_PLATFORM_XCB_KHR)
    if (m_displayServer == VK_DISPLAY_XCB) {
        VkIcdSurfaceXcb *pSurf = (VkIcdSurfaceXcb *)m_display->get_surface();
        VkXcbSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.connection = pSurf->connection;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateXcbSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (m_displayServer == VK_DISPLAY_XLIB) {
        VkIcdSurfaceXlib *pSurf = (VkIcdSurfaceXlib *)m_display->get_surface();
        VkXlibSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.dpy = pSurf->dpy;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateXlibSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (m_displayServer == VK_DISPLAY_WAYLAND) {
        VkIcdSurfaceWayland *pSurf = (VkIcdSurfaceWayland *)m_display->get_surface();
        VkWaylandSurfaceCreateInfoKHR createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.display = pSurf->display;
        createInfo.surface = pSurf->surface;
        replayResult = m_vkFuncs.CreateWaylandSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    }
#endif
    if (m_displayServer == VK_DISPLAY_NONE) {
        if (g_pReplaySettings->headless == TRUE) {
            replayResult = create_headless_surface_ext(remappedInstance, &local_pSurface);
        } else {
            VkIcdSurfaceDisplay *pSurf = (VkIcdSurfaceDisplay *)m_display->get_surface();
            VkDisplaySurfaceCreateInfoKHR createInfo;
            createInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
            createInfo.pNext = NULL;
            createInfo.flags = 0;
            createInfo.displayMode = pSurf->displayMode;
            createInfo.planeIndex = pSurf->planeIndex;
            createInfo.planeStackIndex = pSurf->planeStackIndex;
            createInfo.transform = pSurf->transform;
            createInfo.globalAlpha = pSurf->globalAlpha;
            createInfo.alphaMode = pSurf->alphaMode;
            createInfo.imageExtent = pSurf->imageExtent;
            replayResult = m_vkFuncs.CreateDisplayPlaneSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
        }
    }
#else
    VkIcdSurfaceAndroid *pSurf = (VkIcdSurfaceAndroid *)m_display->get_surface();
    if (local_pSurface == VK_NULL_HANDLE || !g_pReplaySettings->forceSingleWindow) {
        VkAndroidSurfaceCreateInfoKHR createInfo;
        createInfo.sType = pPacket->pCreateInfo->sType;
        createInfo.pNext = pPacket->pCreateInfo->pNext;
        createInfo.flags = pPacket->pCreateInfo->flags;
        createInfo.window = pSurf->window;
        replayResult = m_vkFuncs.CreateAndroidSurfaceKHR(remappedInstance, &createInfo, pPacket->pAllocator, &local_pSurface);
    } else {
        replayResult = VK_SUCCESS;
    }
#endif  // ANDROID
#else
    vktrace_LogError("manually_replay_vkCreateAndroidSurfaceKHR not implemented on this playback platform");
    replayResult = VK_ERROR_FEATURE_NOT_PRESENT;
#endif
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_surfacekhrs_map(*(pPacket->pSurface), local_pSurface);
        if (g_pReplaySettings->forceSingleWindow)
            surfRefCount++;
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateDisplayPlaneSurfaceKHR(packet_vkCreateDisplayPlaneSurfaceKHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkSurfaceKHR local_pSurface;
    VkInstance remappedinstance = m_objMapper.remap_instances(pPacket->instance);
    if (pPacket->instance != VK_NULL_HANDLE && remappedinstance == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CreateDisplayPlaneSurfaceKHR() due to invalid remapped VkInstance.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
#if defined(PLATFORM_LINUX) && !defined(ANDROID)
    if (m_displayServer == VK_DISPLAY_NONE) {
        if (g_pReplaySettings->headless == TRUE) {
            replayResult = create_headless_surface_ext(remappedinstance, &local_pSurface);
        } else {
            VkIcdSurfaceDisplay *pSurf = (VkIcdSurfaceDisplay *)m_display->get_surface();
            VkDisplaySurfaceCreateInfoKHR createInfo;
            createInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
            createInfo.pNext = NULL;
            createInfo.flags = 0;
            createInfo.displayMode = pSurf->displayMode;
            createInfo.planeIndex = pSurf->planeIndex;
            createInfo.planeStackIndex = pSurf->planeStackIndex;
            createInfo.transform = pSurf->transform;
            createInfo.globalAlpha = pSurf->globalAlpha;
            createInfo.alphaMode = pSurf->alphaMode;
            createInfo.imageExtent = pSurf->imageExtent;
            replayResult = m_vkFuncs.CreateDisplayPlaneSurfaceKHR(remappedinstance, &createInfo, pPacket->pAllocator, &local_pSurface);
        }
    } else {
        replayResult = m_vkFuncs.CreateDisplayPlaneSurfaceKHR(remappedinstance, pPacket->pCreateInfo, pPacket->pAllocator, &local_pSurface);
    }
#else
    replayResult = m_vkFuncs.CreateDisplayPlaneSurfaceKHR(remappedinstance, pPacket->pCreateInfo, pPacket->pAllocator, &local_pSurface);
#endif
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_surfacekhrs_map(*(pPacket->pSurface), local_pSurface);
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateDebugReportCallbackEXT(packet_vkCreateDebugReportCallbackEXT *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDebugReportCallbackEXT local_msgCallback;
    VkInstance remappedInstance = m_objMapper.remap_instances(pPacket->instance);
    if (remappedInstance == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateDebugReportCallbackEXT() due to invalid remapped VkInstance.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (!g_fpDbgMsgCallback || !m_vkFuncs.CreateDebugReportCallbackEXT) {
        // just eat this call as we don't have local call back function defined
        return VK_SUCCESS;
    } else {
        VkDebugReportCallbackCreateInfoEXT dbgCreateInfo;
        memset(&dbgCreateInfo, 0, sizeof(dbgCreateInfo));
        dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
        dbgCreateInfo.flags = pPacket->pCreateInfo->flags;
        dbgCreateInfo.pfnCallback = g_fpDbgMsgCallback;
        dbgCreateInfo.pUserData = NULL;
        replayResult = m_vkFuncs.CreateDebugReportCallbackEXT(remappedInstance, &dbgCreateInfo, NULL, &local_msgCallback);
        if (replayResult == VK_SUCCESS) {
            m_objMapper.add_to_debugreportcallbackexts_map(*(pPacket->pCallback), local_msgCallback);
        }
    }
    return replayResult;
}

void vkReplay::manually_replay_vkDestroyDebugReportCallbackEXT(packet_vkDestroyDebugReportCallbackEXT *pPacket) {
    VkInstance remappedInstance = m_objMapper.remap_instances(pPacket->instance);
    if (remappedInstance == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkDestroyDebugReportCallbackEXT() due to invalid remapped VkInstance.");
        return;
    }

    if (!g_fpDbgMsgCallback || !m_vkFuncs.DestroyDebugReportCallbackEXT) {
        // just eat this call as we don't have local call back function defined
        return;
    }

    VkDebugReportCallbackEXT remappedMsgCallback;
    remappedMsgCallback = m_objMapper.remap_debugreportcallbackexts(pPacket->callback);
    if (remappedMsgCallback == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkDestroyDebugReportCallbackEXT() due to invalid remapped VkDebugReportCallbackEXT.");
        return;
    }

    m_vkFuncs.DestroyDebugReportCallbackEXT(remappedInstance, remappedMsgCallback, NULL);
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                              const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
                                                              void                                       *userData) {
    // Log debug message
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        vktrace_LogDebug("{%d} - {%s}: {%s}", callbackData->messageIdNumber, callbackData->pMessageIdName, callbackData->pMessage);
    }
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        vktrace_LogDebug("{%d} - {%s}: {%s}", callbackData->messageIdNumber, callbackData->pMessageIdName, callbackData->pMessage);
    }
    return VK_FALSE;
}

VkResult vkReplay::manually_replay_vkCreateDebugUtilsMessengerEXT(packet_vkCreateDebugUtilsMessengerEXT *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDebugUtilsMessengerEXT local_pMessenger;
    VkInstance remappedInstance = m_objMapper.remap_instances(pPacket->instance);
    if (remappedInstance == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCreateDebugUtilsMessengerEXT() due to invalid remapped VkInstance.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (!m_vkFuncs.CreateDebugUtilsMessengerEXT) {
        vktrace_LogError("Skipping vkCreateDebugUtilsMessengerEXT() due to NULL function potiner.");
        // just eat this call as we don't get the address of debug commands
        return VK_ERROR_VALIDATION_FAILED_EXT;
    } else {
        VkDebugUtilsMessengerCreateInfoEXT dbgCreateInfo;
        memset(&dbgCreateInfo, 0, sizeof(dbgCreateInfo));
        dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgCreateInfo.flags = pPacket->pCreateInfo->flags;
        dbgCreateInfo.messageSeverity = pPacket->pCreateInfo->messageSeverity;
        dbgCreateInfo.messageType = pPacket->pCreateInfo->messageType;
        dbgCreateInfo.pfnUserCallback = debugUtilsCallback;  // The callback function is defined above and probably different from the traced app's function.
        dbgCreateInfo.pUserData = NULL;  // vktrace could not capture the size of UserData

        replayResult = m_vkFuncs.CreateDebugUtilsMessengerEXT(remappedInstance, &dbgCreateInfo, NULL, &local_pMessenger);
        if (replayResult == VK_SUCCESS) {
            m_objMapper.add_to_debugutilsmessengerexts_map(*(pPacket->pMessenger), local_pMessenger);
        }
    }
    return replayResult;
}

void vkReplay::manually_replay_vkDestroyDebugUtilsMessengerEXT(packet_vkDestroyDebugUtilsMessengerEXT *pPacket) {
    VkInstance remappedInstance = m_objMapper.remap_instances(pPacket->instance);
    if (remappedInstance == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkDestroyDebugUtilsMessengerEXT() due to invalid remapped VkInstance.");
        return;
    }

    if (!m_vkFuncs.DestroyDebugUtilsMessengerEXT) {
        vktrace_LogError("Skipping vkDestroyDebugUtilsMessengerEXT() due to NULL function potiner.");
        // just eat this call as we don't get the address of debug commands
        return;
    }

    VkDebugUtilsMessengerEXT remapped_pMessenger;
    remapped_pMessenger= m_objMapper.remap_debugutilsmessengerexts(pPacket->messenger);
    if (remapped_pMessenger == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkDestroyDebugUtilsMessengerEXT() due to invalid remapped VkDebugUtilsMessengerEXT.");
        return;
    }

    m_vkFuncs.DestroyDebugUtilsMessengerEXT(remappedInstance, remapped_pMessenger, NULL);
}

uint64_t vkReplay::remapObjectHandleWithVkObjectType(uint64_t objectHandle, VkObjectType objectType) {
    switch (objectType) {
        case VK_OBJECT_TYPE_IMAGE: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_images(reinterpret_cast<VkImage>(objectHandle)));
        }
        case VK_OBJECT_TYPE_IMAGE_VIEW: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_imageviews(reinterpret_cast<VkImageView>(objectHandle)));
        }
        case VK_OBJECT_TYPE_SAMPLER: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_samplers(reinterpret_cast<VkSampler>(objectHandle)));
        }
        case VK_OBJECT_TYPE_BUFFER: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_buffers(reinterpret_cast<VkBuffer>(objectHandle)));
        }
        case VK_OBJECT_TYPE_SHADER_MODULE: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_shadermodules(reinterpret_cast<VkShaderModule>(objectHandle)));
        }
        case VK_OBJECT_TYPE_PIPELINE: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_pipelines(reinterpret_cast<VkPipeline>(objectHandle)));
        }
        case VK_OBJECT_TYPE_PIPELINE_LAYOUT: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_pipelinelayouts(reinterpret_cast<VkPipelineLayout>(objectHandle)));
        }
        case VK_OBJECT_TYPE_DESCRIPTOR_SET: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_descriptorsets(reinterpret_cast<VkDescriptorSet>(objectHandle)));
        }
        case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_descriptorsetlayouts(reinterpret_cast<VkDescriptorSetLayout>(objectHandle)));
        }
        case VK_OBJECT_TYPE_RENDER_PASS: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_renderpasss(reinterpret_cast<VkRenderPass>(objectHandle)));
        }
        case VK_OBJECT_TYPE_INSTANCE: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_instances(reinterpret_cast<VkInstance>(objectHandle)));
        }
        case VK_OBJECT_TYPE_PHYSICAL_DEVICE: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_physicaldevices(reinterpret_cast<VkPhysicalDevice>(objectHandle)));
        }
        case VK_OBJECT_TYPE_DEVICE: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_devices(reinterpret_cast<VkDevice>(objectHandle)));
        }
        case VK_OBJECT_TYPE_QUEUE: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_queues(reinterpret_cast<VkQueue>(objectHandle)));
        }
        case VK_OBJECT_TYPE_SEMAPHORE: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_semaphores(reinterpret_cast<VkSemaphore>(objectHandle)));
        }
        case VK_OBJECT_TYPE_COMMAND_BUFFER: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_commandbuffers(reinterpret_cast<VkCommandBuffer>(objectHandle)));
        }
        case VK_OBJECT_TYPE_FENCE: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_fences(reinterpret_cast<VkFence>(objectHandle)));
        }
        case VK_OBJECT_TYPE_DEVICE_MEMORY: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_devicememorys(reinterpret_cast<VkDeviceMemory>(objectHandle)));
        }
        case VK_OBJECT_TYPE_EVENT: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_events(reinterpret_cast<VkEvent>(objectHandle)));
        }
        case VK_OBJECT_TYPE_QUERY_POOL: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_querypools(reinterpret_cast<VkQueryPool>(objectHandle)));
        }
        case VK_OBJECT_TYPE_BUFFER_VIEW: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_bufferviews(reinterpret_cast<VkBufferView>(objectHandle)));
        }
        case VK_OBJECT_TYPE_PIPELINE_CACHE: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_pipelinecaches(reinterpret_cast<VkPipelineCache>(objectHandle)));
        }
        case VK_OBJECT_TYPE_DESCRIPTOR_POOL: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_descriptorpools(reinterpret_cast<VkDescriptorPool>(objectHandle)));
        }
        case VK_OBJECT_TYPE_FRAMEBUFFER: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_framebuffers(reinterpret_cast<VkFramebuffer>(objectHandle)));
        }
        case VK_OBJECT_TYPE_COMMAND_POOL: {
            return reinterpret_cast<uint64_t>(m_objMapper.remap_commandpools(reinterpret_cast<VkCommandPool>(objectHandle)));
        }
        default: {
            return VK_OBJECT_TYPE_UNKNOWN;
        }
    }
}

void vkReplay::manually_replay_vkSubmitDebugUtilsMessageEXT(packet_vkSubmitDebugUtilsMessageEXT *pPacket) {
    VkInstance remappedinstance = m_objMapper.remap_instances(pPacket->instance);
    if (pPacket->instance != VK_NULL_HANDLE && remappedinstance == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in SubmitDebugUtilsMessageEXT() due to invalid remapped VkInstance.");
        return;
    }
    // No need to remap messageSeverity
    // No need to remap messageTypes
    // Remap object handles in pCallbackData
    VkDebugUtilsMessengerCallbackDataEXT *pCallbackData = const_cast<VkDebugUtilsMessengerCallbackDataEXT*>(pPacket->pCallbackData);
    if (pCallbackData != NULL) {
        uint32_t i = 0;
        for (i = 0; i < pCallbackData->objectCount; i++) {
            uint64_t remappedHandle = remapObjectHandleWithVkObjectType(pCallbackData->pObjects[i].objectHandle, pCallbackData->pObjects[i].objectType);
            if (remappedHandle == VK_OBJECT_TYPE_UNKNOWN) {
                vktrace_LogError("The object %s is currently unsupported.", pCallbackData->pObjects[i].pObjectName);
                return;
            }
            VkDebugUtilsObjectNameInfoEXT* pObjects = const_cast<VkDebugUtilsObjectNameInfoEXT*>(pCallbackData->pObjects);
            pObjects[i].objectHandle = remappedHandle;
        }
    }
    m_vkFuncs.SubmitDebugUtilsMessageEXT(remappedinstance, pPacket->messageSeverity, pPacket->messageTypes, pPacket->pCallbackData);
}

VkResult vkReplay::manually_replay_vkSetDebugUtilsObjectNameEXT(packet_vkSetDebugUtilsObjectNameEXT *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in SetDebugUtilsObjectNameEXT() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkDebugUtilsObjectNameInfoEXT *pNameInfo = const_cast<VkDebugUtilsObjectNameInfoEXT*>(pPacket->pNameInfo);
    if (pNameInfo != NULL) {
        uint64_t remappedHandle = remapObjectHandleWithVkObjectType(pNameInfo->objectHandle, pNameInfo->objectType);
        if (remappedHandle == VK_OBJECT_TYPE_UNKNOWN) {
            vktrace_LogError("The object %s is currently unsupported.", pNameInfo->pObjectName);
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
        pNameInfo->objectHandle = remappedHandle;
    }
    vkreplay_process_pnext_structs(pPacket->header, (void*)pNameInfo); // Todo: It seems that this function could not deal with VkDebugUtilsObjectNameInfoEXT's pnext pointer.
    replayResult = m_vkDeviceFuncs.SetDebugUtilsObjectNameEXT(remappeddevice, pNameInfo);
    return replayResult;
}

VkResult vkReplay::manually_replay_vkSetDebugUtilsObjectTagEXT(packet_vkSetDebugUtilsObjectTagEXT *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in SetDebugUtilsObjectTagEXT() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkDebugUtilsObjectTagInfoEXT *pTagInfo = const_cast<VkDebugUtilsObjectTagInfoEXT*>(pPacket->pTagInfo);
    if (pTagInfo != NULL) {
        uint64_t remappedHandle = remapObjectHandleWithVkObjectType(pTagInfo->objectHandle, pTagInfo->objectType);
        if (remappedHandle == VK_OBJECT_TYPE_UNKNOWN) {
            vktrace_LogError("The object " PRIu64 " is currently unsupported.", pTagInfo->tagName);
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
        pTagInfo->objectHandle = remappedHandle;
    }
    vkreplay_process_pnext_structs(pPacket->header, (void*)pTagInfo); // Todo: It seems that this function could not deal with VkDebugUtilsObjectTagInfoEXT's pnext pointer.
    replayResult = m_vkDeviceFuncs.SetDebugUtilsObjectTagEXT(remappeddevice, pTagInfo);
    return replayResult;
}

VkResult vkReplay::manually_replay_vkAllocateCommandBuffers(packet_vkAllocateCommandBuffers *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappedDevice = m_objMapper.remap_devices(pPacket->device);
    if (remappedDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkAllocateCommandBuffers() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkCommandBuffer *local_pCommandBuffers = new VkCommandBuffer[pPacket->pAllocateInfo->commandBufferCount];
    VkCommandPool local_CommandPool;
    local_CommandPool = pPacket->pAllocateInfo->commandPool;
    ((VkCommandBufferAllocateInfo *)pPacket->pAllocateInfo)->commandPool =
        m_objMapper.remap_commandpools(pPacket->pAllocateInfo->commandPool);
    if (pPacket->pAllocateInfo->commandPool == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkAllocateCommandBuffers() due to invalid remapped VkCommandPool.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    replayResult = m_vkDeviceFuncs.AllocateCommandBuffers(remappedDevice, pPacket->pAllocateInfo, local_pCommandBuffers);
    ((VkCommandBufferAllocateInfo *)pPacket->pAllocateInfo)->commandPool = local_CommandPool;

    if (replayResult == VK_SUCCESS) {
        for (uint32_t i = 0; i < pPacket->pAllocateInfo->commandBufferCount; i++) {
            m_objMapper.add_to_commandbuffers_map(pPacket->pCommandBuffers[i], local_pCommandBuffers[i]);
            if(g_hasAsApi) {
                replayCommandBufferToReplayDevice[local_pCommandBuffers[i]] = remappedDevice;
            }
        }
    }
    delete[] local_pCommandBuffers;
    return replayResult;
}

VkBool32 vkReplay::manually_replay_vkGetPhysicalDeviceXcbPresentationSupportKHR(
    packet_vkGetPhysicalDeviceXcbPresentationSupportKHR *pPacket) {
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError(
            "Error detected in vkGetPhysicalDeviceXcbPresentationSupportKHR() due to invalid remapped VkPhysicalDevice.");
        return VK_FALSE;
    }

    // Convert the queue family index
    getReplayQueueFamilyIdx(pPacket->physicalDevice, remappedphysicalDevice, &pPacket->queueFamilyIndex);

#if defined(PLATFORM_LINUX) && defined(VK_USE_PLATFORM_ANDROID_KHR)
    // This is not defined for Android
    return VK_TRUE;
#elif defined(PLATFORM_LINUX)
#if defined(VK_USE_PLATFORM_XCB_KHR)
    if (m_displayServer == VK_DISPLAY_XCB) {
        vkDisplayXcb *pDisp = (vkDisplayXcb *)m_display;
        return (m_vkFuncs.GetPhysicalDeviceXcbPresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                     pDisp->get_connection_handle(),
                                                                     pDisp->get_screen_handle()->root_visual));
    }
#endif
/*#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (m_displayServer == VK_DISPLAY_XLIB) {
        VkIcdSurfaceXlib *pSurf = (VkIcdSurfaceXlib *)m_display->get_surface();
        return (m_vkFuncs.GetPhysicalDeviceXlibPresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                      pSurf->dpy,
                                                                      m_display->get_screen_handle()->root_visual));
    }
#endif*/
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (m_displayServer == VK_DISPLAY_WAYLAND) {
        vkDisplayWayland *pDisp = (vkDisplayWayland *)m_display;
        return (m_vkFuncs.GetPhysicalDeviceWaylandPresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                         pDisp->get_display_handle()));
    }
#endif
    if (m_displayServer == VK_DISPLAY_NONE) {
        return VK_TRUE;
    }
#elif defined(WIN32)
    return (m_vkFuncs.GetPhysicalDeviceWin32PresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex));
#else
    vktrace_LogError("manually_replay_vkGetPhysicalDeviceXcbPresentationSupportKHR not implemented on this playback platform");
#endif
    return VK_FALSE;
}

VkBool32 vkReplay::manually_replay_vkGetPhysicalDeviceXlibPresentationSupportKHR(
    packet_vkGetPhysicalDeviceXlibPresentationSupportKHR *pPacket) {
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError(
            "Error detected in vkGetPhysicalDeviceXlibPresentationSupportKHR() due to invalid remapped VkPhysicalDevice.");
        return VK_FALSE;
    }

    // Convert the queue family index
    getReplayQueueFamilyIdx(pPacket->physicalDevice, remappedphysicalDevice, &pPacket->queueFamilyIndex);

#if defined(PLATFORM_LINUX) && defined(VK_USE_PLATFORM_ANDROID_KHR)
    // This is not defined for Android
    return VK_TRUE;
#elif defined(PLATFORM_LINUX)
#if defined(VK_USE_PLATFORM_XCB_KHR)
    if (m_displayServer == VK_DISPLAY_XCB) {
        vkDisplayXcb *pDisp = (vkDisplayXcb *)m_display;
        return (m_vkFuncs.GetPhysicalDeviceXcbPresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                     pDisp->get_connection_handle(),
                                                                     pDisp->get_screen_handle()->root_visual));
    }
#endif
/*#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (m_displayServer == VK_DISPLAY_XLIB) {
        VkIcdSurfaceXlib *pSurf = (VkIcdSurfaceXlib *)m_display->get_surface();
        return (m_vkFuncs.GetPhysicalDeviceXlibPresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                      pSurf->dpy,
                                                                      m_display->get_screen_handle()->root_visual));
    }
#endif*/
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (m_displayServer == VK_DISPLAY_WAYLAND) {
        vkDisplayWayland *pDisp = (vkDisplayWayland *)m_display;
        return (m_vkFuncs.GetPhysicalDeviceWaylandPresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                         pDisp->get_display_handle()));
    }
#endif
    if (m_displayServer == VK_DISPLAY_NONE) {
        return VK_TRUE;
    }
#elif defined(WIN32)
    return (m_vkFuncs.GetPhysicalDeviceWin32PresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex));
#else
    vktrace_LogError("manually_replay_vkGetPhysicalDeviceXlibPresentationSupportKHR not implemented on this playback platform");
#endif
    return VK_FALSE;
}

VkBool32 vkReplay::manually_replay_vkGetPhysicalDeviceWaylandPresentationSupportKHR(
    packet_vkGetPhysicalDeviceWaylandPresentationSupportKHR *pPacket) {
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError(
            "Error detected in vkGetPhysicalDeviceWaylandPresentationSupportKHR() due to invalid remapped VkPhysicalDevice.");
        return VK_FALSE;
    }

    // Convert the queue family index
    getReplayQueueFamilyIdx(pPacket->physicalDevice, remappedphysicalDevice, &pPacket->queueFamilyIndex);

#if defined(PLATFORM_LINUX) && defined(VK_USE_PLATFORM_ANDROID_KHR)
    // This is not defined for Android
    return VK_TRUE;
#elif defined(PLATFORM_LINUX)
#if defined(VK_USE_PLATFORM_XCB_KHR)
    if (m_displayServer == VK_DISPLAY_XCB) {
        vkDisplayXcb *pDisp = (vkDisplayXcb *)m_display;
        return (m_vkFuncs.GetPhysicalDeviceXcbPresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                     pDisp->get_connection_handle(),
                                                                     pDisp->get_screen_handle()->root_visual));
    }
#endif
/*#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (m_displayServer == VK_DISPLAY_XLIB) {
        VkIcdSurfaceXlib *pSurf = (VkIcdSurfaceXlib *)m_display->get_surface();
        return (m_vkFuncs.GetPhysicalDeviceXlibPresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                      pSurf->dpy,
                                                                      m_display->get_screen_handle()->root_visual));
    }
#endif*/
#if VK_USE_PLATFORM_WAYLAND_KHR
    if (m_displayServer == VK_DISPLAY_WAYLAND) {
        vkDisplayWayland *pDisp = (vkDisplayWayland *)m_display;
        return (m_vkFuncs.GetPhysicalDeviceWaylandPresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                         pDisp->get_display_handle()));
    }
#endif
    if (m_displayServer == VK_DISPLAY_NONE) {
        return VK_TRUE;
    }
#elif defined(WIN32)
    return (m_vkFuncs.GetPhysicalDeviceWin32PresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex));
#else
    vktrace_LogError("manually_replay_vkGetPhysicalDeviceWaylandPresentationSupportKHR not implemented on this playback platform");
#endif
    return VK_FALSE;
}

VkBool32 vkReplay::manually_replay_vkGetPhysicalDeviceWin32PresentationSupportKHR(
    packet_vkGetPhysicalDeviceWin32PresentationSupportKHR *pPacket) {
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (pPacket->physicalDevice != VK_NULL_HANDLE && remappedphysicalDevice == VK_NULL_HANDLE) {
        return VK_FALSE;
    }

    // Convert the queue family index
    getReplayQueueFamilyIdx(pPacket->physicalDevice, remappedphysicalDevice, &pPacket->queueFamilyIndex);

#if defined(WIN32)
    return (m_vkFuncs.GetPhysicalDeviceWin32PresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex));
#elif defined(PLATFORM_LINUX) && defined(VK_USE_PLATFORM_ANDROID_KHR)
    // This is not defined for Android
    return VK_TRUE;
#elif defined(PLATFORM_LINUX)
#if defined(VK_USE_PLATFORM_XCB_KHR)
    if (m_displayServer == VK_DISPLAY_XCB) {
        vkDisplayXcb *pDisp = (vkDisplayXcb *)m_display;
        return (m_vkFuncs.GetPhysicalDeviceXcbPresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                     pDisp->get_connection_handle(),
                                                                     pDisp->get_screen_handle()->root_visual));
    }
#endif
/*#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (m_displayServer == VK_DISPLAY_XLIB) {
        VkIcdSurfaceXlib *pSurf = (VkIcdSurfaceXlib *)m_display->get_surface();
        return (m_vkFuncs.GetPhysicalDeviceXlibPresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                         pSurf->dpy, m_display->get_screen_handle()->root_visual));
    }
#endif*/
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (m_displayServer == VK_DISPLAY_WAYLAND) {
        vkDisplayWayland *pDisp = (vkDisplayWayland *)m_display;
        return (m_vkFuncs.GetPhysicalDeviceWaylandPresentationSupportKHR(remappedphysicalDevice, pPacket->queueFamilyIndex,
                                                                         pDisp->get_display_handle()));
    }
#endif
    if (m_displayServer == VK_DISPLAY_NONE) {
        return VK_TRUE;
    }
#else
    vktrace_LogError("manually_replay_vkGetPhysicalDeviceWin32PresentationSupportKHR not implemented on this playback platform");
#endif
    return VK_FALSE;
}

static std::unordered_map<VkDescriptorUpdateTemplateKHR, VkDescriptorUpdateTemplateCreateInfoKHR *>
    descriptorUpdateTemplateCreateInfo;

VkResult vkReplay::manually_replay_vkCreateDescriptorUpdateTemplate(packet_vkCreateDescriptorUpdateTemplate *pPacket) {
    VkResult replayResult;
    VkDescriptorUpdateTemplate local_pDescriptorUpdateTemplate;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CreateDescriptorUpdateTemplate() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    *(const_cast<VkDescriptorSetLayout *>(&pPacket->pCreateInfo->descriptorSetLayout)) =
        m_objMapper.remap_descriptorsetlayouts(pPacket->pCreateInfo->descriptorSetLayout);

    replayResult = m_vkDeviceFuncs.CreateDescriptorUpdateTemplate(remappeddevice, pPacket->pCreateInfo, pPacket->pAllocator,
                                                                  &local_pDescriptorUpdateTemplate);

    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_descriptorupdatetemplates_map(*(pPacket->pDescriptorUpdateTemplate), local_pDescriptorUpdateTemplate);
        descriptorUpdateTemplateCreateInfo[local_pDescriptorUpdateTemplate] =
            reinterpret_cast<VkDescriptorUpdateTemplateCreateInfo *>(malloc(sizeof(VkDescriptorUpdateTemplateCreateInfo)));
        memcpy(descriptorUpdateTemplateCreateInfo[local_pDescriptorUpdateTemplate], pPacket->pCreateInfo,
               sizeof(VkDescriptorUpdateTemplateCreateInfo));
        descriptorUpdateTemplateCreateInfo[local_pDescriptorUpdateTemplate]->pDescriptorUpdateEntries =
            reinterpret_cast<VkDescriptorUpdateTemplateEntry *>(
                malloc(sizeof(VkDescriptorUpdateTemplateEntry) * pPacket->pCreateInfo->descriptorUpdateEntryCount));
        memcpy(const_cast<VkDescriptorUpdateTemplateEntry *>(
                   descriptorUpdateTemplateCreateInfo[local_pDescriptorUpdateTemplate]->pDescriptorUpdateEntries),
               pPacket->pCreateInfo->pDescriptorUpdateEntries,
               sizeof(VkDescriptorUpdateTemplateEntry) * pPacket->pCreateInfo->descriptorUpdateEntryCount);
    }
    return replayResult;
}

VkResult vkReplay::manually_replay_vkCreateDescriptorUpdateTemplateKHR(packet_vkCreateDescriptorUpdateTemplateKHR *pPacket) {
    VkResult replayResult;
    VkDescriptorUpdateTemplateKHR local_pDescriptorUpdateTemplate;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CreateDescriptorUpdateTemplateKHR() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    *((VkDescriptorSetLayout *)&pPacket->pCreateInfo->descriptorSetLayout) =
        m_objMapper.remap_descriptorsetlayouts(pPacket->pCreateInfo->descriptorSetLayout);

    replayResult = m_vkDeviceFuncs.CreateDescriptorUpdateTemplateKHR(remappeddevice, pPacket->pCreateInfo, pPacket->pAllocator,
                                                                     &local_pDescriptorUpdateTemplate);
    if (replayResult == VK_SUCCESS) {
        m_objMapper.add_to_descriptorupdatetemplates_map(*(pPacket->pDescriptorUpdateTemplate), local_pDescriptorUpdateTemplate);
        descriptorUpdateTemplateCreateInfo[local_pDescriptorUpdateTemplate] =
            (VkDescriptorUpdateTemplateCreateInfoKHR *)malloc(sizeof(VkDescriptorUpdateTemplateCreateInfoKHR));
        memcpy(descriptorUpdateTemplateCreateInfo[local_pDescriptorUpdateTemplate], pPacket->pCreateInfo,
               sizeof(VkDescriptorUpdateTemplateCreateInfoKHR));
        descriptorUpdateTemplateCreateInfo[local_pDescriptorUpdateTemplate]->pDescriptorUpdateEntries =
            (VkDescriptorUpdateTemplateEntryKHR *)malloc(sizeof(VkDescriptorUpdateTemplateEntryKHR) *
                                                         pPacket->pCreateInfo->descriptorUpdateEntryCount);
        memcpy((void *)descriptorUpdateTemplateCreateInfo[local_pDescriptorUpdateTemplate]->pDescriptorUpdateEntries,
               pPacket->pCreateInfo->pDescriptorUpdateEntries,
               sizeof(VkDescriptorUpdateTemplateEntryKHR) * pPacket->pCreateInfo->descriptorUpdateEntryCount);
    }
    return replayResult;
}

void vkReplay::manually_replay_vkDestroyDescriptorUpdateTemplate(packet_vkDestroyDescriptorUpdateTemplate *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in DestroyDescriptorUpdateTemplate() due to invalid remapped VkDevice.");
        return;
    }
    VkDescriptorUpdateTemplate remappedDescriptorUpdateTemplate =
        m_objMapper.remap_descriptorupdatetemplates(pPacket->descriptorUpdateTemplate);
    if (pPacket->descriptorUpdateTemplate != VK_NULL_HANDLE && remappedDescriptorUpdateTemplate == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in DestroyDescriptorUpdateTemplate() due to invalid remapped VkDescriptorUpdateTemplate.");
        return;
    }
    m_vkDeviceFuncs.DestroyDescriptorUpdateTemplate(remappeddevice, remappedDescriptorUpdateTemplate, pPacket->pAllocator);
    m_objMapper.rm_from_descriptorupdatetemplates_map(pPacket->descriptorUpdateTemplate);

    if (descriptorUpdateTemplateCreateInfo.find(remappedDescriptorUpdateTemplate) != descriptorUpdateTemplateCreateInfo.end()) {
        if (descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]) {
            if (descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]->pDescriptorUpdateEntries)
                free((void *)descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]->pDescriptorUpdateEntries);
            free(descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]);
        }
        descriptorUpdateTemplateCreateInfo.erase(remappedDescriptorUpdateTemplate);
    }
}

void vkReplay::manually_replay_vkDestroyDescriptorUpdateTemplateKHR(packet_vkDestroyDescriptorUpdateTemplateKHR *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in DestroyDescriptorUpdateTemplateKHR() due to invalid remapped VkDevice.");
        return;
    }
    VkDescriptorUpdateTemplateKHR remappedDescriptorUpdateTemplate =
        m_objMapper.remap_descriptorupdatetemplates(pPacket->descriptorUpdateTemplate);
    if (pPacket->descriptorUpdateTemplate != VK_NULL_HANDLE && remappedDescriptorUpdateTemplate == VK_NULL_HANDLE) {
        vktrace_LogError(
            "Error detected in DestroyDescriptorUpdateTemplateKHR() due to invalid remapped VkDescriptorUpdateTemplateKHR.");
        return;
    }
    m_vkDeviceFuncs.DestroyDescriptorUpdateTemplateKHR(remappeddevice, remappedDescriptorUpdateTemplate, pPacket->pAllocator);
    m_objMapper.rm_from_descriptorupdatetemplates_map(pPacket->descriptorUpdateTemplate);

    if (descriptorUpdateTemplateCreateInfo.find(remappedDescriptorUpdateTemplate) != descriptorUpdateTemplateCreateInfo.end()) {
        if (descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]) {
            if (descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]->pDescriptorUpdateEntries)
                free((void *)descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]->pDescriptorUpdateEntries);
            free(descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]);
        }
        descriptorUpdateTemplateCreateInfo.erase(remappedDescriptorUpdateTemplate);
    }
}

void vkReplay::remapHandlesInDescriptorSetWithTemplateData(VkDescriptorUpdateTemplateKHR remappedDescriptorUpdateTemplate,
                                                           char *pData) {
    if (VK_NULL_HANDLE == remappedDescriptorUpdateTemplate) {
        vktrace_LogError(
            "Error detected in remapHandlesInDescriptorSetWithTemplateData() due to invalid remapped VkDescriptorUpdateTemplate.");
        return;
    }

    for (uint32_t i = 0; i < descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]->descriptorUpdateEntryCount;
         i++) {
        for (uint32_t j = 0;
             j < descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]->pDescriptorUpdateEntries[i].descriptorCount;
             j++) {
            size_t offset =
                descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]->pDescriptorUpdateEntries[i].offset +
                j * descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]->pDescriptorUpdateEntries[i].stride;
            char *update_entry = pData + offset;
            switch (
                descriptorUpdateTemplateCreateInfo[remappedDescriptorUpdateTemplate]->pDescriptorUpdateEntries[i].descriptorType) {
                case VK_DESCRIPTOR_TYPE_SAMPLER: {
                    auto image_entry = reinterpret_cast<VkDescriptorImageInfo *>(update_entry);
                    image_entry->sampler = m_objMapper.remap_samplers(image_entry->sampler);
                    break;
                }
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
                    auto image_entry = reinterpret_cast<VkDescriptorImageInfo *>(update_entry);
                    image_entry->sampler = m_objMapper.remap_samplers(image_entry->sampler);
                    image_entry->imageView = m_objMapper.remap_imageviews(image_entry->imageView);
                    break;
                }
                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
                    auto image_entry = reinterpret_cast<VkDescriptorImageInfo *>(update_entry);
                    image_entry->imageView = m_objMapper.remap_imageviews(image_entry->imageView);
                    break;
                }
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
                    auto buffer_entry = reinterpret_cast<VkDescriptorBufferInfo *>(update_entry);
                    buffer_entry->buffer = m_objMapper.remap_buffers(buffer_entry->buffer);
                    break;
                }
                case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
                    auto buffer_view_handle = reinterpret_cast<VkBufferView *>(update_entry);
                    *buffer_view_handle = m_objMapper.remap_bufferviews(*buffer_view_handle);
                    break;
                }
                case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: {
                    // don't support VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET when premap
                    break;
                }
                default:
                    assert(0);
            }
        }
    }
}

void vkReplay::manually_replay_vkUpdateDescriptorSetWithTemplate(packet_vkUpdateDescriptorSetWithTemplate *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in UpdateDescriptorSetWithTemplate() due to invalid remapped VkDevice.");
        return;
    }

    VkDescriptorSet remappedDescriptorSet = m_objMapper.remap_descriptorsets(pPacket->descriptorSet);
    if (pPacket->descriptorSet != VK_NULL_HANDLE && remappedDescriptorSet == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in UpdateDescriptorSetWithTemplate() due to invalid remapped VkDescriptorSet.");
        return;
    }

    VkDescriptorUpdateTemplate remappedDescriptorUpdateTemplate =
        m_objMapper.remap_descriptorupdatetemplates(pPacket->descriptorUpdateTemplate);
    if (pPacket->descriptorUpdateTemplate != VK_NULL_HANDLE && remappedDescriptorUpdateTemplate == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in UpdateDescriptorSetWithTemplate() due to invalid remapped VkDescriptorUpdateTemplate.");
        return;
    }

    // Map handles inside of pData
    remapHandlesInDescriptorSetWithTemplateData(remappedDescriptorUpdateTemplate,
                                                const_cast<char *>(reinterpret_cast<const char *>(pPacket->pData)));

    m_vkDeviceFuncs.UpdateDescriptorSetWithTemplate(remappeddevice, remappedDescriptorSet, remappedDescriptorUpdateTemplate,
                                                    pPacket->pData);
}

void vkReplay::manually_replay_vkUpdateDescriptorSetWithTemplateKHR(packet_vkUpdateDescriptorSetWithTemplateKHR *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in UpdateDescriptorSetWithTemplateKHR() due to invalid remapped VkDevice.");
        return;
    }

    VkDescriptorSet remappedDescriptorSet = m_objMapper.remap_descriptorsets(pPacket->descriptorSet);
    if (pPacket->descriptorSet != VK_NULL_HANDLE && remappedDescriptorSet == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in UpdateDescriptorSetWithTemplateKHR() due to invalid remapped VkDescriptorSet.");
        return;
    }

    VkDescriptorUpdateTemplateKHR remappedDescriptorUpdateTemplate =
        m_objMapper.remap_descriptorupdatetemplates(pPacket->descriptorUpdateTemplate);
    if (pPacket->descriptorUpdateTemplate != VK_NULL_HANDLE && remappedDescriptorUpdateTemplate == VK_NULL_HANDLE) {
        vktrace_LogError(
            "Error detected in UpdateDescriptorSetWithTemplateKHR() due to invalid remapped VkDescriptorUpdateTemplateKHR.");
        return;
    }

    // Map handles inside of pData
    remapHandlesInDescriptorSetWithTemplateData(remappedDescriptorUpdateTemplate,
                                                const_cast<char *>(reinterpret_cast<const char *>(pPacket->pData)));

    m_vkDeviceFuncs.UpdateDescriptorSetWithTemplateKHR(remappeddevice, remappedDescriptorSet, remappedDescriptorUpdateTemplate,
                                                       pPacket->pData);
}

void vkReplay::manually_replay_vkCmdPushDescriptorSetKHR(packet_vkCmdPushDescriptorSetKHR *pPacket) {
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdPushDescriptorSetKHR() due to invalid remapped VkCommandBuffer.");
        return;
    }

    VkWriteDescriptorSet *pRemappedWrites = (VkWriteDescriptorSet *)pPacket->pDescriptorWrites;

    // No need to remap pipelineBindPoint

    VkPipelineLayout remappedlayout = m_objMapper.remap_pipelinelayouts(pPacket->layout);
    if (pPacket->layout != VK_NULL_HANDLE && remappedlayout == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdPushDescriptorSetKHR() due to invalid remapped VkPipelineLayout.");
        return;
    }

    bool errorBadRemap = false;

    if (pPacket->pDescriptorWrites != NULL) {
        for (uint32_t i = 0; i < pPacket->descriptorWriteCount && !errorBadRemap; i++) {
            pRemappedWrites[i].dstSet = 0;  // Ignored

            switch (pPacket->pDescriptorWrites[i].descriptorType) {
                case VK_DESCRIPTOR_TYPE_SAMPLER:
                    for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                        if (pPacket->pDescriptorWrites[i].pImageInfo[j].sampler != VK_NULL_HANDLE) {
                            const_cast<VkDescriptorImageInfo *>(pRemappedWrites[i].pImageInfo)[j].sampler =
                                m_objMapper.remap_samplers(pPacket->pDescriptorWrites[i].pImageInfo[j].sampler);
                            if (pRemappedWrites[i].pImageInfo[j].sampler == VK_NULL_HANDLE) {
                                vktrace_LogError("Skipping vkCmdPushDescriptorSet() due to invalid remapped VkSampler.");
                                errorBadRemap = true;
                                break;
                            }
                        }
                    }
                    break;
                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                    for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                        if (pPacket->pDescriptorWrites[i].pImageInfo[j].imageView != VK_NULL_HANDLE) {
                            const_cast<VkDescriptorImageInfo *>(pRemappedWrites[i].pImageInfo)[j].imageView =
                                m_objMapper.remap_imageviews(pPacket->pDescriptorWrites[i].pImageInfo[j].imageView);
                            if (pRemappedWrites[i].pImageInfo[j].imageView == VK_NULL_HANDLE) {
                                vktrace_LogError("Skipping vkCmdPushDescriptorSet() due to invalid remapped VkImageView.");
                                errorBadRemap = true;
                                break;
                            }
                        }
                    }
                    break;
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                        if (pPacket->pDescriptorWrites[i].pImageInfo[j].sampler != VK_NULL_HANDLE) {
                            const_cast<VkDescriptorImageInfo *>(pRemappedWrites[i].pImageInfo)[j].sampler =
                                m_objMapper.remap_samplers(pPacket->pDescriptorWrites[i].pImageInfo[j].sampler);
                            if (pRemappedWrites[i].pImageInfo[j].sampler == VK_NULL_HANDLE) {
                                vktrace_LogError("Skipping vkCmdPushDescriptorSet() due to invalid remapped VkSampler.");
                                errorBadRemap = true;
                                break;
                            }
                        }
                        if (pPacket->pDescriptorWrites[i].pImageInfo[j].imageView != VK_NULL_HANDLE) {
                            const_cast<VkDescriptorImageInfo *>(pRemappedWrites[i].pImageInfo)[j].imageView =
                                m_objMapper.remap_imageviews(pPacket->pDescriptorWrites[i].pImageInfo[j].imageView);
                            if (pRemappedWrites[i].pImageInfo[j].imageView == VK_NULL_HANDLE) {
                                vktrace_LogError("Skipping vkCmdPushDescriptorSet() due to invalid remapped VkImageView.");
                                errorBadRemap = true;
                                break;
                            }
                        }
                    }
                    break;
                case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                    for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                        if (pPacket->pDescriptorWrites[i].pTexelBufferView[j] != VK_NULL_HANDLE) {
                            const_cast<VkBufferView *>(pRemappedWrites[i].pTexelBufferView)[j] =
                                m_objMapper.remap_bufferviews(pPacket->pDescriptorWrites[i].pTexelBufferView[j]);
                            if (pRemappedWrites[i].pTexelBufferView[j] == VK_NULL_HANDLE) {
                                vktrace_LogError("Skipping vkCmdPushDescriptorSet() due to invalid remapped VkBufferView.");
                                errorBadRemap = true;
                                break;
                            }
                        }
                    }
                    break;
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                    for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                        if (pPacket->pDescriptorWrites[i].pBufferInfo[j].buffer != VK_NULL_HANDLE) {
                            const_cast<VkDescriptorBufferInfo *>(pRemappedWrites[i].pBufferInfo)[j].buffer =
                                m_objMapper.remap_buffers(pPacket->pDescriptorWrites[i].pBufferInfo[j].buffer);
                            if (pRemappedWrites[i].pBufferInfo[j].buffer == VK_NULL_HANDLE) {
                                vktrace_LogError("Skipping vkCmdPushDescriptorSet() due to invalid remapped VkBufferView.");
                                errorBadRemap = true;
                                break;
                            }
                        }
                    }
                case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
                    // Because AS handle can be 0x0, don't check the as.
                    break;
                /* Nothing to do, already copied the constant values into the new descriptor info */
                default:
                    break;
            }
        }
    }

    if (!errorBadRemap) {
        // If an error occurred, don't call the real function, but skip ahead so that memory is cleaned up!
        m_vkDeviceFuncs.CmdPushDescriptorSetKHR(remappedcommandBuffer, pPacket->pipelineBindPoint, remappedlayout, pPacket->set,
                                                pPacket->descriptorWriteCount, pRemappedWrites);
    }
}

void vkReplay::manually_replay_vkCmdPushDescriptorSetWithTemplateKHR(packet_vkCmdPushDescriptorSetWithTemplateKHR *pPacket) {
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdPushDescriptorSetWithTemplateKHR() due to invalid remapped VkCommandBuffer.");
        return;
    }

    VkDescriptorUpdateTemplateKHR remappedDescriptorUpdateTemplate =
        m_objMapper.remap_descriptorupdatetemplates(pPacket->descriptorUpdateTemplate);
    if (pPacket->descriptorUpdateTemplate != VK_NULL_HANDLE && remappedDescriptorUpdateTemplate == VK_NULL_HANDLE) {
        vktrace_LogError(
            "Error detected in CmdPushDescriptorSetWithTemplateKHR() due to invalid remapped VkDescriptorUpdateTemplateKHR.");
        return;
    }

    VkPipelineLayout remappedlayout = m_objMapper.remap_pipelinelayouts(pPacket->layout);
    if (pPacket->layout != VK_NULL_HANDLE && remappedlayout == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdPushDescriptorSetWithTemplateKHR() due to invalid remapped VkPipelineLayout.");
        return;
    }

    // Map handles inside of pData
    remapHandlesInDescriptorSetWithTemplateData(remappedDescriptorUpdateTemplate, (char *)pPacket->pData);

    m_vkDeviceFuncs.CmdPushDescriptorSetWithTemplateKHR(remappedcommandBuffer, remappedDescriptorUpdateTemplate, remappedlayout,
                                                        pPacket->set, pPacket->pData);
}

VkResult vkReplay::manually_replay_vkRegisterDeviceEventEXT(packet_vkRegisterDeviceEventEXT *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in RegisterDeviceEventEXT() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    // No need to remap pDeviceEventInfo
    // No need to remap pAllocator
    VkFence fence;
    auto result = m_vkDeviceFuncs.RegisterDeviceEventEXT(remappeddevice, pPacket->pDeviceEventInfo, pPacket->pAllocator, &fence);
    if (result == VK_SUCCESS) {
        m_objMapper.add_to_fences_map(*pPacket->pFence, fence);
    }
    return result;
}

VkResult vkReplay::manually_replay_vkRegisterDisplayEventEXT(packet_vkRegisterDisplayEventEXT *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in RegisterDisplayEventEXT() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkDisplayKHR remappeddisplay = m_objMapper.remap_displaykhrs(pPacket->display);
    if (pPacket->display != VK_NULL_HANDLE && remappeddisplay == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in RegisterDisplayEventEXT() due to invalid remapped VkDisplayKHR.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    // No need to remap pDisplayEventInfo
    // No need to remap pAllocator
    VkFence fence;
    auto result = m_vkDeviceFuncs.RegisterDisplayEventEXT(remappeddevice, remappeddisplay, pPacket->pDisplayEventInfo,
                                                          pPacket->pAllocator, &fence);
    if (result == VK_SUCCESS) {
        m_objMapper.add_to_fences_map(*pPacket->pFence, fence);
    }
    return result;
}

VkResult vkReplay::manually_replay_vkBindBufferMemory2KHR(packet_vkBindBufferMemory2KHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in BindBufferMemory2KHR() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    for (size_t i = 0; i < pPacket->bindInfoCount; i++) {
        VkBuffer traceBuffer = pPacket->pBindInfos[i].buffer;
        VkBuffer remappedBuffer = m_objMapper.remap_buffers(pPacket->pBindInfos[i].buffer);
        if (traceBuffer != VK_NULL_HANDLE && remappedBuffer == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in BindBufferMemory2KHR() due to invalid remapped VkBuffer.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
        rtHandler->addSbtBufferMemory(traceBuffer, pPacket->pBindInfos[i].memory);
        *((VkBuffer *)&pPacket->pBindInfos[i].buffer) = remappedBuffer;
        *((VkDeviceMemory *)&pPacket->pBindInfos[i].memory) = m_objMapper.remap_devicememorys(pPacket->pBindInfos[i].memory);
        if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch()) {
            uint64_t memOffsetTemp;
            if (replayGetBufferMemoryRequirements.find(remappedBuffer) == replayGetBufferMemoryRequirements.end()) {
                // vkBindBufferMemory2KHR is being called with a buffer for which vkGetBufferMemoryRequirements
                // was not called. This might be violation of the spec on the part of the app, but seems to
                // be done in many apps.  Call vkGetBufferMemoryRequirements for this buffer and add result to
                // replayGetBufferMemoryRequirements map.
                VkMemoryRequirements mem_reqs;
                m_vkDeviceFuncs.GetBufferMemoryRequirements(remappeddevice, remappedBuffer, &mem_reqs);
                replayGetBufferMemoryRequirements[remappedBuffer] = mem_reqs;
            }

            assert(replayGetBufferMemoryRequirements[remappedBuffer].alignment);
            memOffsetTemp = pPacket->pBindInfos[i].memoryOffset + replayGetBufferMemoryRequirements[remappedBuffer].alignment - 1;
            memOffsetTemp = memOffsetTemp / replayGetBufferMemoryRequirements[remappedBuffer].alignment;
            memOffsetTemp = memOffsetTemp * replayGetBufferMemoryRequirements[remappedBuffer].alignment;
            *((VkDeviceSize *)&pPacket->pBindInfos[i].memoryOffset) = memOffsetTemp;
        }
        traceBufferToReplayMemory[traceBuffer] = pPacket->pBindInfos[i].memory;
        replayBufferToReplayDeviceMemoryOffset[remappedBuffer] = pPacket->pBindInfos[i].memoryOffset;
        if (g_hasAsApi) {
            replayBufferToReplayDeviceMemory[remappedBuffer] = pPacket->pBindInfos[i].memory;
        }
    }
    replayResult = m_vkDeviceFuncs.BindBufferMemory2KHR(remappeddevice, pPacket->bindInfoCount, pPacket->pBindInfos);
    return replayResult;
}

VkResult vkReplay::manually_replay_vkBindImageMemory2KHR(packet_vkBindImageMemory2KHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in BindImageMemory2KHR() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    for (size_t i = 0; i < pPacket->bindInfoCount; i++) {
        VkImage traceImage = pPacket->pBindInfos[i].image;
        VkImage remappedImage = m_objMapper.remap_images(pPacket->pBindInfos[i].image);
        if (traceImage != VK_NULL_HANDLE && remappedImage == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in BindImageMemory2KHR() due to invalid remapped VkImage.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
        *((VkImage *)&pPacket->pBindInfos[i].image) = remappedImage;

        bool bAllocateMemory = true;
        auto it = std::find(hardwarebufferImage.begin(), hardwarebufferImage.end(), remappedImage);
        if (it != hardwarebufferImage.end()) {
            bAllocateMemory = false;
        }
        if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() && bAllocateMemory) {
            if (replayImageToTiling.find(remappedImage) == replayImageToTiling.end()) {
                vktrace_LogError("Error detected in BindImageMemory2KHR() due to invalid remapped image tiling.");
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }

            if (replayGetImageMemoryRequirements.find(remappedImage) == replayGetImageMemoryRequirements.end()) {
                // vkBindImageMemory2KHR is being called with an image for which vkGetImageMemoryRequirements
                // was not called. This might be violation of the spec on the part of the app, but seems to
                // be done in many apps.  Call vkGetImageMemoryRequirements for this image and add result to
                // replayGetImageMemoryRequirements map.
                VkMemoryRequirements mem_reqs;
                m_vkDeviceFuncs.GetImageMemoryRequirements(remappeddevice, remappedImage, &mem_reqs);
                replayGetImageMemoryRequirements[remappedImage] = mem_reqs;
            }

            if (replayImageToTiling[remappedImage] == VK_IMAGE_TILING_OPTIMAL) {
                uint32_t replayMemTypeIndex;
                if (!getReplayMemoryTypeIdx(pPacket->device, remappeddevice,
                                            traceDeviceMemoryToMemoryTypeIndex[pPacket->pBindInfos[i].memory],
                                            &replayGetImageMemoryRequirements[remappedImage], &replayMemTypeIndex)) {
                    vktrace_LogError("Error detected in BindImageMemory2KHR() due to invalid remapped memory type index.");
                    return VK_ERROR_VALIDATION_FAILED_EXT;
                }

                VkMemoryAllocateInfo memoryAllocateInfo = {
                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    NULL,
                    replayGetImageMemoryRequirements[remappedImage].size,
                    replayMemTypeIndex,
                };
                VkDeviceMemory remappedMemory = VK_NULL_HANDLE;
                replayResult = m_vkDeviceFuncs.AllocateMemory(remappeddevice, &memoryAllocateInfo, NULL, &remappedMemory);

                if (replayResult == VK_SUCCESS) {
                    *((VkDeviceMemory *)&pPacket->pBindInfos[i].memory) = remappedMemory;
                    replayOptimalImageToDeviceMemory[remappedImage] = remappedMemory;
                } else {
                    *((VkDeviceMemory *)&pPacket->pBindInfos[i].memory) = VK_NULL_HANDLE;
                }
                *((VkDeviceSize *)&pPacket->pBindInfos[i].memoryOffset) = 0;
            } else {
                *((VkDeviceMemory *)&pPacket->pBindInfos[i].memory) =
                    m_objMapper.remap_devicememorys(pPacket->pBindInfos[i].memory);
                assert(replayGetImageMemoryRequirements[remappedImage].alignment);
                uint64_t memoryOffset = 0;
                memoryOffset = pPacket->pBindInfos[i].memoryOffset + replayGetImageMemoryRequirements[remappedImage].alignment - 1;
                memoryOffset = memoryOffset / replayGetImageMemoryRequirements[remappedImage].alignment;
                memoryOffset = memoryOffset * replayGetImageMemoryRequirements[remappedImage].alignment;
                *((VkDeviceSize *)&pPacket->pBindInfos[i].memoryOffset) = memoryOffset;
            }
        } else {
            *((VkDeviceMemory *)&pPacket->pBindInfos[i].memory) = m_objMapper.remap_devicememorys(pPacket->pBindInfos[i].memory);
        }
    }
    replayResult = m_vkDeviceFuncs.BindImageMemory2KHR(remappeddevice, pPacket->bindInfoCount, pPacket->pBindInfos);
    return replayResult;
}

VkResult vkReplay::manually_replay_vkBindBufferMemory2(packet_vkBindBufferMemory2 *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in BindBufferMemory2() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    for (size_t i = 0; i < pPacket->bindInfoCount; i++) {
        VkBuffer traceBuffer = pPacket->pBindInfos[i].buffer;
        VkBuffer remappedBuffer = m_objMapper.remap_buffers(pPacket->pBindInfos[i].buffer);
        if (traceBuffer != VK_NULL_HANDLE && remappedBuffer == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in BindBufferMemory2() due to invalid remapped VkBuffer.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
        rtHandler->addSbtBufferMemory(traceBuffer, pPacket->pBindInfos[i].memory);
        *(reinterpret_cast<VkBuffer *>(&(const_cast<VkBindBufferMemoryInfo *>(pPacket->pBindInfos)[i]).buffer)) = remappedBuffer;
        *(reinterpret_cast<VkDeviceMemory *>(&(const_cast<VkBindBufferMemoryInfo *>(pPacket->pBindInfos)[i]).memory)) =
            m_objMapper.remap_devicememorys(pPacket->pBindInfos[i].memory);
        if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch()) {
            uint64_t memOffsetTemp;
            if (replayGetBufferMemoryRequirements.find(remappedBuffer) == replayGetBufferMemoryRequirements.end()) {
                // vkBindBufferMemory2 is being called with a buffer for which vkGetBufferMemoryRequirements
                // was not called. This might be violation of the spec on the part of the app, but seems to
                // be done in many apps.  Call vkGetBufferMemoryRequirements for this buffer and add result to
                // replayGetBufferMemoryRequirements map.
                VkMemoryRequirements mem_reqs;
                m_vkDeviceFuncs.GetBufferMemoryRequirements(remappeddevice, remappedBuffer, &mem_reqs);
                replayGetBufferMemoryRequirements[remappedBuffer] = mem_reqs;
            }

            assert(replayGetBufferMemoryRequirements[remappedBuffer].alignment);
            memOffsetTemp = pPacket->pBindInfos[i].memoryOffset + replayGetBufferMemoryRequirements[remappedBuffer].alignment - 1;
            memOffsetTemp = memOffsetTemp / replayGetBufferMemoryRequirements[remappedBuffer].alignment;
            memOffsetTemp = memOffsetTemp * replayGetBufferMemoryRequirements[remappedBuffer].alignment;
            *(reinterpret_cast<VkDeviceSize *>(&(const_cast<VkBindBufferMemoryInfo *>(pPacket->pBindInfos)[i]).memoryOffset)) =
                memOffsetTemp;
        }
        replayBufferToReplayDeviceMemoryOffset[remappedBuffer] = pPacket->pBindInfos[i].memoryOffset;
        traceBufferToReplayMemory[traceBuffer] = pPacket->pBindInfos[i].memory;
        if (g_hasAsApi) {
            replayBufferToReplayDeviceMemory[remappedBuffer] = pPacket->pBindInfos[i].memory;
        }
    }
    replayResult = m_vkDeviceFuncs.BindBufferMemory2(remappeddevice, pPacket->bindInfoCount, pPacket->pBindInfos);
    return replayResult;
}

VkResult vkReplay::manually_replay_vkBindImageMemory2(packet_vkBindImageMemory2 *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in BindImageMemory2() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    for (size_t i = 0; i < pPacket->bindInfoCount; i++) {
        VkImage traceImage = pPacket->pBindInfos[i].image;
        VkImage remappedImage = m_objMapper.remap_images(pPacket->pBindInfos[i].image);
        if (traceImage != VK_NULL_HANDLE && remappedImage == VK_NULL_HANDLE) {
            vktrace_LogError("Error detected in BindImageMemory2() due to invalid remapped VkImage.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
        *(reinterpret_cast<VkImage *>(&(const_cast<VkBindImageMemoryInfo *>(pPacket->pBindInfos)[i]).image)) = remappedImage;

        bool bAllocateMemory = true;
        auto it = std::find(hardwarebufferImage.begin(), hardwarebufferImage.end(), remappedImage);
        if (it != hardwarebufferImage.end()) {
            bAllocateMemory = false;
        }
        if (g_pReplaySettings->compatibilityMode && m_pFileHeader->portability_table_valid && !platformMatch() && bAllocateMemory) {
            if (replayImageToTiling.find(remappedImage) == replayImageToTiling.end()) {
                vktrace_LogError("Error detected in BindImageMemory2() due to invalid remapped image tiling.");
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }

            if (replayGetImageMemoryRequirements.find(remappedImage) == replayGetImageMemoryRequirements.end()) {
                // vkBindImageMemory2 is being called with an image for which vkGetImageMemoryRequirements
                // was not called. This might be violation of the spec on the part of the app, but seems to
                // be done in many apps.  Call vkGetImageMemoryRequirements for this image and add result to
                // replayGetImageMemoryRequirements map.
                VkMemoryRequirements mem_reqs;
                m_vkDeviceFuncs.GetImageMemoryRequirements(remappeddevice, remappedImage, &mem_reqs);
                replayGetImageMemoryRequirements[remappedImage] = mem_reqs;
            }

            if (replayImageToTiling[remappedImage] == VK_IMAGE_TILING_OPTIMAL) {
                uint32_t replayMemTypeIndex;
                if (!getReplayMemoryTypeIdx(pPacket->device, remappeddevice,
                                            traceDeviceMemoryToMemoryTypeIndex[pPacket->pBindInfos[i].memory],
                                            &replayGetImageMemoryRequirements[remappedImage], &replayMemTypeIndex)) {
                    vktrace_LogError("Error detected in BindImageMemory2() due to invalid remapped memory type index.");
                    return VK_ERROR_VALIDATION_FAILED_EXT;
                }

                VkMemoryAllocateInfo memoryAllocateInfo = {
                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    NULL,
                    replayGetImageMemoryRequirements[remappedImage].size,
                    replayMemTypeIndex,
                };
                VkDeviceMemory remappedMemory = VK_NULL_HANDLE;
                replayResult = m_vkDeviceFuncs.AllocateMemory(remappeddevice, &memoryAllocateInfo, NULL, &remappedMemory);

                if (replayResult == VK_SUCCESS) {
                    *(reinterpret_cast<VkDeviceMemory *>(&(const_cast<VkBindImageMemoryInfo *>(pPacket->pBindInfos)[i]).memory)) =
                        remappedMemory;
                    replayOptimalImageToDeviceMemory[remappedImage] = remappedMemory;
                } else {
                    *(reinterpret_cast<VkDeviceMemory *>(&(const_cast<VkBindImageMemoryInfo *>(pPacket->pBindInfos)[i]).memory)) =
                        VK_NULL_HANDLE;
                }
                *(reinterpret_cast<VkDeviceSize *>(&(const_cast<VkBindImageMemoryInfo *>(pPacket->pBindInfos)[i]).memoryOffset)) =
                    0;
            } else {
                *(reinterpret_cast<VkDeviceMemory *>(&(const_cast<VkBindImageMemoryInfo *>(pPacket->pBindInfos)[i]).memory)) =
                    m_objMapper.remap_devicememorys(pPacket->pBindInfos[i].memory);
                assert(replayGetImageMemoryRequirements[remappedImage].alignment);
                uint64_t memoryOffset = 0;
                memoryOffset = pPacket->pBindInfos[i].memoryOffset + replayGetImageMemoryRequirements[remappedImage].alignment - 1;
                memoryOffset = memoryOffset / replayGetImageMemoryRequirements[remappedImage].alignment;
                memoryOffset = memoryOffset * replayGetImageMemoryRequirements[remappedImage].alignment;
                *(reinterpret_cast<VkDeviceSize *>(&(const_cast<VkBindImageMemoryInfo *>(pPacket->pBindInfos)[i]).memoryOffset)) =
                    memoryOffset;
            }
        } else {
            *(reinterpret_cast<VkDeviceMemory *>(&(const_cast<VkBindImageMemoryInfo *>(pPacket->pBindInfos)[i]).memory)) =
                m_objMapper.remap_devicememorys(pPacket->pBindInfos[i].memory);
        }
    }
    replayResult = m_vkDeviceFuncs.BindImageMemory2(remappeddevice, pPacket->bindInfoCount, pPacket->pBindInfos);
    return replayResult;
}

void vkReplay::manually_replay_vkCmdSetFragmentShadingRateKHR(packet_vkCmdSetFragmentShadingRateKHR* pPacket) {
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Skipping vkCmdSetFragmentShadingRateKHR due to invalid remapped VkCommandBuffer.");
        return;
    }
    if (g_pReplaySettings->forceVariableRateShading != nullptr) {
        vktrace_LogWarning("Overriding fragment size of vkCmdSetFragmentShadingRateKHR.");
        VkExtent2D *pFragmentSize = const_cast<VkExtent2D *>(pPacket->pFragmentSize);
        VkFragmentShadingRateCombinerOpKHR *combinerOps = const_cast<VkFragmentShadingRateCombinerOpKHR *>(pPacket->combinerOps);
        pFragmentSize->width = m_fragmentSize.width;
        pFragmentSize->height = m_fragmentSize.height;
        combinerOps[0] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
        combinerOps[1] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
    }
    m_vkDeviceFuncs.CmdSetFragmentShadingRateKHR(remappedcommandBuffer, pPacket->pFragmentSize, pPacket->combinerOps);
}

VkResult vkReplay::manually_replay_vkAcquireProfilingLockKHR(packet_vkAcquireProfilingLockKHR* pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkAcquireProfilingLockInfoKHR local_pInfo;
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in AcquireProfilingLockKHR() due to invalid remapped VkDevice.");
        return replayResult;
    }
    replayResult = m_vkDeviceFuncs.AcquireProfilingLockKHR(remappeddevice, &local_pInfo);

    return replayResult;
}

VkResult vkReplay::manually_replay_vkGetDisplayPlaneSupportedDisplaysKHR(packet_vkGetDisplayPlaneSupportedDisplaysKHR *pPacket) {
    VkResult replayResult = VK_ERROR_VALIDATION_FAILED_EXT;
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (pPacket->physicalDevice != VK_NULL_HANDLE && remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetDisplayPlaneSupportedDisplaysKHR() due to invalid remapped VkPhysicalDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    // No need to remap planeIndex
    // No need to remap pDisplayCount
    // No need to remap pDisplays

    VkDisplayKHR *pDisplays = pPacket->pDisplays;
    if (pPacket->pDisplays != NULL) {
        pDisplays = VKTRACE_NEW_ARRAY(VkDisplayKHR, *pPacket->pDisplayCount);
    }
    replayResult = m_vkFuncs.GetDisplayPlaneSupportedDisplaysKHR(remappedphysicalDevice, pPacket->planeIndex,
                                                                 pPacket->pDisplayCount, pDisplays);

    if (pPacket->pDisplays != NULL) {
        for (uint32_t i = 0; i < *pPacket->pDisplayCount; ++i) {
            m_objMapper.add_to_displaykhrs_map(pPacket->pDisplays[i], pDisplays[i]);
        }
        VKTRACE_DELETE(pDisplays);
    }

    return replayResult;
}

void vkReplay::manually_replay_vkCmdPushConstants(packet_vkCmdPushConstants *pPacket) {
    //packet_vkCmdPushConstants* pPacket = (packet_vkCmdPushConstants*)(packet->pBody);
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdPushConstants() due to invalid remapped VkCommandBuffer.");
        return;
    }
    VkPipelineLayout remappedlayout = m_objMapper.remap_pipelinelayouts(pPacket->layout);
    if (pPacket->layout != VK_NULL_HANDLE && remappedlayout == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdPushConstants() due to invalid remapped VkPipelineLayout.");
        return;
    }
    // No need to remap stageFlags
    // No need to remap offset
    // No need to remap size
    // No need to remap pValues
    m_vkDeviceFuncs.CmdPushConstants(remappedcommandBuffer, remappedlayout, pPacket->stageFlags, pPacket->offset, pPacket->size, pPacket->pValues);
}

void vkReplay::manually_replay_vkCmdPushConstantsRemap(packet_vkCmdPushConstants *pPacket) {
    //packet_vkCmdPushConstants* pPacket = (packet_vkCmdPushConstants*)(packet->pBody);
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdPushConstants() due to invalid remapped VkCommandBuffer.");
        return;
    }
    VkPipelineLayout remappedlayout = m_objMapper.remap_pipelinelayouts(pPacket->layout);
    if (pPacket->layout != VK_NULL_HANDLE && remappedlayout == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdPushConstants() due to invalid remapped VkPipelineLayout.");
        return;
    }
    // No need to remap stageFlags
    // No need to remap offset
    // No need to remap size
    // No need to remap pValues
    VkDeviceAddress *pDeviceAddress = (VkDeviceAddress *)pPacket->pValues;
    for (unsigned j = 0; j < pPacket->size / sizeof(VkDeviceAddress); ++j) {
        auto it0 = traceDeviceAddrToReplayDeviceAddr4Buf.find(pDeviceAddress[j]);
        auto it1 = traceDeviceAddrToReplayDeviceAddr4AS.find(pDeviceAddress[j]);
        if (it0 != traceDeviceAddrToReplayDeviceAddr4Buf.end() && it1 == traceDeviceAddrToReplayDeviceAddr4AS.end()) {
            pDeviceAddress[j] = it0->second.replayDeviceAddr;
        }
    }
    m_vkDeviceFuncs.CmdPushConstants(remappedcommandBuffer, remappedlayout, pPacket->stageFlags, pPacket->offset, pPacket->size, pPacket->pValues);
}

VkResult vkReplay::manually_replay_vkEnumerateDeviceExtensionProperties(packet_vkEnumerateDeviceExtensionProperties *pPacket) {
    VkPhysicalDevice remappedphysicalDevice = m_objMapper.remap_physicaldevices(pPacket->physicalDevice);
    if (pPacket->physicalDevice != VK_NULL_HANDLE && remappedphysicalDevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in EnumerateDeviceExtensionProperties() due to invalid remapped VkPhysicalDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    uint32_t propertyCount = *pPacket->pPropertyCount;
    auto pProperties = pPacket->pProperties;
    // Get mapped pPropertyCount and alloc new array if querying for pProperties
    if (pProperties != nullptr) {
        if (replayDeviceExtensionPropertyCount.find(pPacket->physicalDevice) == replayDeviceExtensionPropertyCount.end()) {
            vktrace_LogError("Error detected in EnumerateDeviceExtensionProperties() due to invalid remapped pPropertyCount.");
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
        propertyCount = replayDeviceExtensionPropertyCount[pPacket->physicalDevice];
        pProperties = VKTRACE_NEW_ARRAY(VkExtensionProperties, propertyCount);
    }

    auto result =
        m_vkFuncs.EnumerateDeviceExtensionProperties(remappedphysicalDevice, pPacket->pLayerName, &propertyCount, pProperties);

    // Map physical device to property count
    if (result == VK_SUCCESS && pPacket->pProperties == nullptr) {
        replayDeviceExtensionPropertyCount[pPacket->physicalDevice] = propertyCount;
    }

    // Clean up properties array. For portability, we will want to compare this to what is in the packet.
    if (pProperties != nullptr) {
        VKTRACE_DELETE(pProperties);
    }

    return result;
}

VkResult vkReplay::manually_replay_vkGetMemoryAndroidHardwareBufferANDROID(packet_vkGetMemoryAndroidHardwareBufferANDROID *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetMemoryAndroidHardwareBufferANDROID() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkDeviceMemory remappedMemory = m_objMapper.remap_devicememorys(pPacket->pInfo->memory);
    if (pPacket->pInfo->memory != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetMemoryAndroidHardwareBufferANDROID() due to invalid remapped VkDeviceMemory.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
#if defined(ANDROID)
    VkMemoryGetAndroidHardwareBufferInfoANDROID info = {VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID, NULL, remappedMemory};
    AHardwareBuffer* replayAHardwareBuffer;
    VkResult result = m_vkDeviceFuncs.GetMemoryAndroidHardwareBufferANDROID(remappeddevice, &info, &replayAHardwareBuffer);
    if (result == VK_SUCCESS && exportedReplayDeviceMemory.count(remappedMemory) != 0) {
        traceAHardwareBufferToReplayAHardwareBuffer[*pPacket->pBuffer] = replayAHardwareBuffer;
    }
    return result;
#elif defined(ARM_LINUX_64)
    // Mark the traced android hardware buffer to track the coresponding allocation
    traceAHardwareBufferToDmaBufferFd[*(pPacket->pBuffer)] = traceDeviceMemoryToDmaBufferFd[pPacket->pInfo->memory];
    return VK_SUCCESS;
#endif
    vktrace_LogError("Skipping GetMemoryAndroidHardwareBufferANDROID() because it is not support on current platform.");
    return VK_ERROR_VALIDATION_FAILED_EXT;
}

bool vkReplay::premap_UpdateDescriptorSets(vktrace_trace_packet_header* pHeader)
{
    packet_vkUpdateDescriptorSets* pPacket = reinterpret_cast<packet_vkUpdateDescriptorSets*>(pHeader->pBody);
    VkDevice *pDevice = m_objMapper.add_null_to_devices_map(pPacket->device);
    pPacket->device = reinterpret_cast<VkDevice>(pDevice);

    for (uint32_t i = 0; i < pPacket->descriptorWriteCount; i++) {
        VkDescriptorSet *pDescriptorSet = m_objMapper.add_null_to_descriptorsets_map(pPacket->pDescriptorWrites[i].dstSet);
        const_cast<VkWriteDescriptorSet *>(pPacket->pDescriptorWrites)[i].dstSet = reinterpret_cast<VkDescriptorSet>(pDescriptorSet);

        switch (pPacket->pDescriptorWrites[i].descriptorType) {
            case VK_DESCRIPTOR_TYPE_SAMPLER:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pImageInfo[j].sampler != VK_NULL_HANDLE) {
                        VkSampler *pSampler = m_objMapper.add_null_to_samplers_map(pPacket->pDescriptorWrites[i].pImageInfo[j].sampler);
                        const_cast<VkDescriptorImageInfo *>(pPacket->pDescriptorWrites[i].pImageInfo)[j].sampler = reinterpret_cast<VkSampler>(pSampler);
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pImageInfo[j].imageView != VK_NULL_HANDLE) {
                        VkImageView *pImageView = m_objMapper.add_null_to_imageviews_map(pPacket->pDescriptorWrites[i].pImageInfo[j].imageView);
                        const_cast<VkDescriptorImageInfo *>(pPacket->pDescriptorWrites[i].pImageInfo)[j].imageView = reinterpret_cast<VkImageView>(pImageView);
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pImageInfo[j].sampler != VK_NULL_HANDLE) {
                        VkSampler *pSampler = m_objMapper.add_null_to_samplers_map(pPacket->pDescriptorWrites[i].pImageInfo[j].sampler);
                        const_cast<VkDescriptorImageInfo *>(pPacket->pDescriptorWrites[i].pImageInfo)[j].sampler = reinterpret_cast<VkSampler>(pSampler);
                    }
                    if (pPacket->pDescriptorWrites[i].pImageInfo[j].imageView != VK_NULL_HANDLE) {
                        VkImageView *pImageView = m_objMapper.add_null_to_imageviews_map(pPacket->pDescriptorWrites[i].pImageInfo[j].imageView);
                        const_cast<VkDescriptorImageInfo *>(pPacket->pDescriptorWrites[i].pImageInfo)[j].imageView = reinterpret_cast<VkImageView>(pImageView);
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pTexelBufferView[j] != VK_NULL_HANDLE) {
                        VkBufferView *pBufferView = m_objMapper.add_null_to_bufferviews_map(pPacket->pDescriptorWrites[i].pTexelBufferView[j]);
                        const_cast<VkBufferView*>(pPacket->pDescriptorWrites[i].pTexelBufferView)[j] = reinterpret_cast<VkBufferView>(pBufferView);
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pBufferInfo[j].buffer != VK_NULL_HANDLE) {
                        bufferObj *pBufferObj = m_objMapper.add_null_to_buffers_map(pPacket->pDescriptorWrites[i].pBufferInfo[j].buffer);
                        const_cast<VkDescriptorBufferInfo*>(pPacket->pDescriptorWrites[i].pBufferInfo)[j].buffer = reinterpret_cast<VkBuffer>(pBufferObj);
                    }
                }
                break;
            case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
                for (uint32_t j = 0; j < pPacket->pDescriptorWrites[i].descriptorCount; j++) {
                    if (pPacket->pDescriptorWrites[i].pNext != nullptr) {
                        VkWriteDescriptorSetAccelerationStructureKHR *pWriteDSAS = (VkWriteDescriptorSetAccelerationStructureKHR*)(pPacket->pDescriptorWrites[i].pNext);
                        for (unsigned asi = 0; asi < pWriteDSAS->accelerationStructureCount; asi++) {
                            vktrace_LogError("Don't support VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET premap.");
                        }
                    }
                }
            break;
            /* Nothing to do, already copied the constant values into the new descriptor info */
            default:
                break;
        }
    }

    for (uint32_t i = 0; i < pPacket->descriptorCopyCount; i++) {
        VkDescriptorSet *pDescriptorSet = m_objMapper.add_null_to_descriptorsets_map(pPacket->pDescriptorCopies[i].dstSet);
        const_cast<VkCopyDescriptorSet *>(pPacket->pDescriptorCopies)[i].dstSet = reinterpret_cast<VkDescriptorSet>(pDescriptorSet);
        pDescriptorSet = m_objMapper.add_null_to_descriptorsets_map(pPacket->pDescriptorCopies[i].srcSet);
        const_cast<VkCopyDescriptorSet *>(pPacket->pDescriptorCopies)[i].srcSet = reinterpret_cast<VkDescriptorSet>(pDescriptorSet);
    }

    return true;
}

bool vkReplay::premap_FlushMappedMemoryRanges(vktrace_trace_packet_header* pHeader)
{
    packet_vkFlushMappedMemoryRanges* pPacket = reinterpret_cast<packet_vkFlushMappedMemoryRanges*>(pHeader->pBody);
    VkDevice *pDevice = m_objMapper.add_null_to_devices_map(pPacket->device);
    pPacket->device = reinterpret_cast<VkDevice>(pDevice);

    VkMappedMemoryRange* localRanges = const_cast<VkMappedMemoryRange*>(pPacket->pMemoryRanges);
    for (uint32_t i = 0; i < pPacket->memoryRangeCount; i++) {
        devicememoryObj *pDevicememoryObj = m_objMapper.add_null_to_devicememorys_map(pPacket->pMemoryRanges[i].memory);
        localRanges[i].memory = reinterpret_cast<VkDeviceMemory>(pDevicememoryObj);
    }

    return true;
}

bool vkReplay::premap_CmdBindDescriptorSets(vktrace_trace_packet_header* pHeader)
{
    packet_vkCmdBindDescriptorSets* pPacket = reinterpret_cast<packet_vkCmdBindDescriptorSets*>(pHeader->pBody);
    VkCommandBuffer *pCommandBuffer = m_objMapper.add_null_to_commandbuffers_map(pPacket->commandBuffer);
    pPacket->commandBuffer = reinterpret_cast<VkCommandBuffer>(pCommandBuffer);

    VkPipelineLayout *pPipelineLayout = m_objMapper.add_null_to_pipelinelayouts_map(pPacket->layout);
    pPacket->layout = reinterpret_cast<VkPipelineLayout>(pPipelineLayout);

    for (uint32_t idx = 0; idx < pPacket->descriptorSetCount && pPacket->pDescriptorSets != NULL; idx++) {
        VkDescriptorSet *pDescriptorSet = m_objMapper.add_null_to_descriptorsets_map(pPacket->pDescriptorSets[idx]);
        const_cast<VkDescriptorSet*>(pPacket->pDescriptorSets)[idx] = reinterpret_cast<VkDescriptorSet>(pDescriptorSet);
    }

    return true;
}

bool vkReplay::premap_CmdDrawIndexed(vktrace_trace_packet_header* pHeader)
{
    packet_vkCmdDrawIndexed* pPacket = reinterpret_cast<packet_vkCmdDrawIndexed*>(pHeader->pBody);
    VkCommandBuffer *pCommandBuffer = m_objMapper.add_null_to_commandbuffers_map(pPacket->commandBuffer);
    pPacket->commandBuffer = reinterpret_cast<VkCommandBuffer>(pCommandBuffer);

    return true;
}

void vkReplay::post_interpret(vktrace_trace_packet_header* pHeader) {
    if (nullptr == pHeader) {
        return;
    }

    switch (pHeader->packet_id) {
        case VKTRACE_TPI_VK_vkCreateSampler: {
            packet_vkCreateSampler *pPacket = reinterpret_cast<packet_vkCreateSampler *>(pHeader->pBody);
            assert(nullptr != pPacket);
            if (nullptr != pPacket) {
                VkSamplerCreateInfo *pCreateInfo = const_cast<VkSamplerCreateInfo *>(pPacket->pCreateInfo);
                if (nullptr != pCreateInfo && nullptr != g_pReplaySettings) {
                    if (g_pReplaySettings->forceDisableAF && pCreateInfo->anisotropyEnable) {
                        pCreateInfo->anisotropyEnable = VK_FALSE;
                        pCreateInfo->maxAnisotropy = 1.0f;
                    }
                }
            }
            break;
        }
        default: {
            break;
        }
    }

}

void vkReplay::changeDeviceFeature(VkBool32 *traceFeatures,VkBool32 *deviceFeatures,uint32_t numOfFeatures)
{
    if (g_pReplaySettings->forceDisableAF) {
        for (uint32_t i = 0; i < numOfFeatures; i++) {
            const char* feature = GetPhysDevFeatureString(i);
            if (strcmp(feature,"samplerAnisotropy")) {
                continue;
            }
            *(traceFeatures + i) = VK_FALSE;
            break;
        }
    }
}

void vkReplay::deviceWaitIdle()
{
    if (g_pReplaySettings->premapping) {
        for (auto obj = m_objMapper.m_indirect_devices.begin(); obj != m_objMapper.m_indirect_devices.end(); obj++) {
            if (*obj->second != VK_NULL_HANDLE)
                m_vkDeviceFuncs.DeviceWaitIdle(*obj->second);
        }
    }
    else {
        for (auto obj = m_objMapper.m_devices.begin(); obj != m_objMapper.m_devices.end(); obj++) {
            m_vkDeviceFuncs.DeviceWaitIdle(obj->second);
        }
    }
}

void vkReplay::on_terminate() {
    if (g_pReplaySettings->enablePipelineCache) {
        assert(nullptr != m_pipelinecache_accessor);
        auto cache_list = m_pipelinecache_accessor->GetCollectedPacketInfo();
        size_t datasize = 0;
        for (const auto &info: cache_list) {
            std::string &&full_path = m_pipelinecache_accessor->FindFile(info.second, m_replay_gpu, m_replay_pipelinecache_uuid);
            if (false == full_path.empty()) {
                // Cache data for the pipeline can be found from the disk,
                // no need to write again.
                continue;
            }
            auto remapped_device = m_objMapper.remap_devices(info.first);
            auto remapped_pipeline_cache = m_objMapper.remap_pipelinecaches(info.second);
            if (VK_NULL_HANDLE == remapped_pipeline_cache) {
                continue;
            }
            auto result = m_vkDeviceFuncs.GetPipelineCacheData(remapped_device, remapped_pipeline_cache, &datasize, nullptr);
            if (VK_SUCCESS == result) {
                uint8_t *data = VKTRACE_NEW_ARRAY(uint8_t, datasize);
                assert(nullptr != data);
                result = m_vkDeviceFuncs.GetPipelineCacheData(remapped_device, remapped_pipeline_cache, &datasize, data);
                if (VK_SUCCESS == result) {
                    m_pipelinecache_accessor->WritePipelineCache(info.second, data, datasize, m_replay_gpu, m_replay_pipelinecache_uuid);
                }
                VKTRACE_DELETE(data);
            }
        }
    }
    // Output the rate of the injected calls
    float rate = 0;
    if (vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_10)) {
        if (m_CallStats[VKTRACE_TPI_VK_vkFlushMappedMemoryRanges].total) {
            rate = m_CallStats[VKTRACE_TPI_VK_vkFlushMappedMemoryRanges].injectedCallCount;
            rate /= m_CallStats[VKTRACE_TPI_VK_vkFlushMappedMemoryRanges].total;
            vktrace_LogAlways("The rate of the injected vkFlushMappedMemoryRanges is %f", rate);
        }
    }
    if (m_CallStats[VKTRACE_TPI_VK_vkGetFenceStatus].total) {
        rate = m_CallStats[VKTRACE_TPI_VK_vkGetFenceStatus].injectedCallCount;
        rate /= m_CallStats[VKTRACE_TPI_VK_vkGetFenceStatus].total;
        vktrace_LogAlways("The rate of the injected vkGetFenceStatus is %f", rate);
    }
    if (m_CallStats[VKTRACE_TPI_VK_vkGetEventStatus].total) {
        rate = m_CallStats[VKTRACE_TPI_VK_vkGetEventStatus].injectedCallCount;
        rate /= m_CallStats[VKTRACE_TPI_VK_vkGetEventStatus].total;
        vktrace_LogAlways("The rate of the injected vkGetEventStatus is %f", rate);
    }
    if (m_CallStats[VKTRACE_TPI_VK_vkGetQueryPoolResults].total) {
        rate = m_CallStats[VKTRACE_TPI_VK_vkGetQueryPoolResults].injectedCallCount;
        rate /= m_CallStats[VKTRACE_TPI_VK_vkGetQueryPoolResults].total;
        vktrace_LogAlways("The rate of the injected vkGetQueryPoolResults is %f", rate);
    }
}

vktrace_replay::PipelineCacheAccessor::Ptr vkReplay::get_pipelinecache_accessor() const {
    return m_pipelinecache_accessor;
}

// vkReplay::interpret_pnext_handles translate handles in all Vulkan structures that have a
// pNext and at least one handle.
//
// Ideally, this function would be automatically generated. It's not, so if a new structure
// is added to Vulkan that contains a pNext and a handle, this function should add handling
// of that structure.

void vkReplay::interpret_pnext_handles(void *struct_ptr) {
    VkApplicationInfo *pnext = (VkApplicationInfo *)struct_ptr;

    // We skip the first struct - it is the arg to the api call, and handles are translated
    // by the api call handling function above.
    if (pnext) pnext = (VkApplicationInfo *)pnext->pNext;

    // Loop through all the pnext structures attached to struct_ptr and
    // remap handles in those structures.
    while (pnext) {
        switch (pnext->sType) {
            case VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO: {
                VkDeviceGroupDeviceCreateInfo *p = (VkDeviceGroupDeviceCreateInfo *)pnext;
                for (uint32_t i = 0; i < p->physicalDeviceCount; i++) {
                    *((VkPhysicalDevice *)(&p->pPhysicalDevices[i])) = m_objMapper.remap_physicaldevices(p->pPhysicalDevices[i]);
                }
            } break;
            case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO: {
                VkSamplerYcbcrConversionInfo *p = (VkSamplerYcbcrConversionInfo *)pnext;
                p->conversion = m_objMapper.remap_samplerycbcrconversions(p->conversion);
            } break;
            case VK_STRUCTURE_TYPE_DEDICATED_ALLOCATION_MEMORY_ALLOCATE_INFO_NV:
            case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO: {
                VkDedicatedAllocationMemoryAllocateInfoNV *p = (VkDedicatedAllocationMemoryAllocateInfoNV *)pnext;
                p->image = m_objMapper.remap_images(p->image);
                p->buffer = m_objMapper.remap_buffers(p->buffer);
            } break;
            case VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV: {
                VkAccelerationStructureMemoryRequirementsInfoNV *p = (VkAccelerationStructureMemoryRequirementsInfoNV *)pnext;
                p->accelerationStructure = m_objMapper.remap_accelerationstructurenvs(p->accelerationStructure);
            } break;
            case VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR: {
                VkAcquireNextImageInfoKHR *p = (VkAcquireNextImageInfoKHR *)pnext;
                p->swapchain = m_objMapper.remap_swapchainkhrs(p->swapchain);
                p->semaphore = m_objMapper.remap_semaphores(p->semaphore);
                p->fence = m_objMapper.remap_fences(p->fence);
            } break;
            case VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV: {
                VkBindAccelerationStructureMemoryInfoNV *p = (VkBindAccelerationStructureMemoryInfoNV *)pnext;
                p->accelerationStructure = m_objMapper.remap_accelerationstructurenvs(p->accelerationStructure);
                p->memory = m_objMapper.remap_devicememorys(p->memory);
            } break;

#if 0
            List of structures with pNext pointers that include handles and are not yet
            implemented (as of Vulkan Header 1.1.105).  Note that many of these structs
            are directly passed in as args to Vulkan API calls. Handles in direct args
            structs are translated by the handler functions, so those API replay
            functions work OK.  We just do not yet handle them correctly in pNext lists.

            VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO
            VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO
            VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR
            VK_STRUCTURE_TYPE_BIND_SPARSE_INFO  // Has subs-structs with handles
            VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_EXT
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2
            VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO
            VK_STRUCTURE_TYPE_CMD_PROCESS_COMMANDS_INFO_NVX  // Has sub-structs with handles
            VK_STRUCTURE_TYPE_CMD_RESERVE_SPACE_FOR_COMMANDS_INFO_NVX
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO
            VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT
            VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
            VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO
            VK_STRUCTURE_TYPE_DISPLAY_PLANE_INFO_2_KHR
            VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
            VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV
            VK_STRUCTURE_TYPE_GEOMETRY_NV
            VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
            VK_STRUCTURE_TYPE_HDR_METADATA_EXT
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2
            VK_STRUCTURE_TYPE_IMAGE_SPARSE_MEMORY_REQUIREMENTS_INFO_2
            VK_STRUCTURE_TYPE_IMAGE_SWAPCHAIN_CREATE_INFO_KHR
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
            VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR
            VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR
            VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE
            VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
            VK_STRUCTURE_TYPE_PRESENT_INFO_KHR
            VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
            VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR
            VK_STRUCTURE_TYPE_SHADER_MODULE_VALIDATION_CACHE_CREATE_INFO_EXT
            VK_STRUCTURE_TYPE_SUBMIT_INFO
            VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET  // Has sub-structs with handles
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV
#endif
            default:
                break;
        }
        pnext = (VkApplicationInfo *)pnext->pNext;
    }
    return;
}

#if !defined(ANDROID) && defined(PLATFORM_LINUX)
VkResult vkReplay::create_headless_surface_ext(VkInstance instance,
                                         VkSurfaceKHR* pSurface) {
    VkResult result = VK_ERROR_VALIDATION_FAILED_EXT;
    switch (m_headlessExtensionChoice) {
        case HeadlessExtensionChoice::VK_EXT: {
            VkHeadlessSurfaceCreateInfoEXT createInfo = {};
            createInfo.pNext = NULL;
            createInfo.flags = 0;
            assert(NULL != m_vkFuncs.CreateHeadlessSurfaceEXT);
            result = m_vkFuncs.CreateHeadlessSurfaceEXT(instance, &createInfo, NULL, pSurface);
            break;
        }
        case HeadlessExtensionChoice::VK_ARMX: {
            VkHeadlessSurfaceCreateInfoARM createInfo = {};
            createInfo.pNext = NULL;
            createInfo.flags = 0;
            assert(NULL != m_PFN_vkCreateHeadlessSurfaceARM);
            result = m_PFN_vkCreateHeadlessSurfaceARM(instance, &createInfo, NULL, pSurface);
            break;
        }
        case HeadlessExtensionChoice::VK_NONE: {
            break;
        }
        default: {
            break;
        }
    }

    return result;
}
#endif // !defined(ANDROID) && defined(PLATFORM_LINUX)

#if defined(PLATFORM_LINUX) || defined(PLATFORM_OSX)
#define STRNCASECMP strncasecmp
#elif defined(PLATFORM_WINDOWS)
#define STRNCASECMP _strnicmp
#endif

bool vkReplay::checkFVRSParam() {
    char *copiedArgs = vktrace_allocate_and_copy(g_pReplaySettings->forceVariableRateShading);
    char *token = std::strtok(copiedArgs, "-x*,");
    m_fragmentSize.width = atoi(token);
    token = std::strtok(NULL, "-");
    m_fragmentSize.height = atoi(token);
    token = std::strtok(NULL, "-");
    // If overrideOnly is false or NULL, default value (false) would be used.
    // Otherwise, a bad boolean paramter error should be returned.
    if (token != NULL) {
        if (STRNCASECMP(token, "true", 4) == 0) {
            m_fvrsOverrideOnly = true;
        } else if (STRNCASECMP(token, "false", 5) != 0) {
            vktrace_LogError("Bad boolean parameter for overrideOnly. FVRS format: <width>-<height>-<overrideOnly>");
            return false;
        }
    }
    vktrace_LogVerbose("FVRS settings: fragment width = %u, height = %u, overriden only = %s", m_fragmentSize.width, m_fragmentSize.height, token);
    vktrace_free(copiedArgs);
    return true;
}
