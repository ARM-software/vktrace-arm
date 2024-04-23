/*
 * (C) COPYRIGHT 2019 ARM Limited
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

#if defined(__i386__)
typedef struct VkPhysicalDeviceLimits_padding {
    uint32_t              maxImageDimension1D;
    uint32_t              maxImageDimension2D;
    uint32_t              maxImageDimension3D;
    uint32_t              maxImageDimensionCube;
    uint32_t              maxImageArrayLayers;
    uint32_t              maxTexelBufferElements;
    uint32_t              maxUniformBufferRange;
    uint32_t              maxStorageBufferRange;
    uint32_t              maxPushConstantsSize;
    uint32_t              maxMemoryAllocationCount;
    uint32_t              maxSamplerAllocationCount;
    uint32_t padding_0; // Padding
    VkDeviceSize          bufferImageGranularity;
    VkDeviceSize          sparseAddressSpaceSize;
    uint32_t              maxBoundDescriptorSets;
    uint32_t              maxPerStageDescriptorSamplers;
    uint32_t              maxPerStageDescriptorUniformBuffers;
    uint32_t              maxPerStageDescriptorStorageBuffers;
    uint32_t              maxPerStageDescriptorSampledImages;
    uint32_t              maxPerStageDescriptorStorageImages;
    uint32_t              maxPerStageDescriptorInputAttachments;
    uint32_t              maxPerStageResources;
    uint32_t              maxDescriptorSetSamplers;
    uint32_t              maxDescriptorSetUniformBuffers;
    uint32_t              maxDescriptorSetUniformBuffersDynamic;
    uint32_t              maxDescriptorSetStorageBuffers;
    uint32_t              maxDescriptorSetStorageBuffersDynamic;
    uint32_t              maxDescriptorSetSampledImages;
    uint32_t              maxDescriptorSetStorageImages;
    uint32_t              maxDescriptorSetInputAttachments;
    uint32_t              maxVertexInputAttributes;
    uint32_t              maxVertexInputBindings;
    uint32_t              maxVertexInputAttributeOffset;
    uint32_t              maxVertexInputBindingStride;
    uint32_t              maxVertexOutputComponents;
    uint32_t              maxTessellationGenerationLevel;
    uint32_t              maxTessellationPatchSize;
    uint32_t              maxTessellationControlPerVertexInputComponents;
    uint32_t              maxTessellationControlPerVertexOutputComponents;
    uint32_t              maxTessellationControlPerPatchOutputComponents;
    uint32_t              maxTessellationControlTotalOutputComponents;
    uint32_t              maxTessellationEvaluationInputComponents;
    uint32_t              maxTessellationEvaluationOutputComponents;
    uint32_t              maxGeometryShaderInvocations;
    uint32_t              maxGeometryInputComponents;
    uint32_t              maxGeometryOutputComponents;
    uint32_t              maxGeometryOutputVertices;
    uint32_t              maxGeometryTotalOutputComponents;
    uint32_t              maxFragmentInputComponents;
    uint32_t              maxFragmentOutputAttachments;
    uint32_t              maxFragmentDualSrcAttachments;
    uint32_t              maxFragmentCombinedOutputResources;
    uint32_t              maxComputeSharedMemorySize;
    uint32_t              maxComputeWorkGroupCount[3];
    uint32_t              maxComputeWorkGroupInvocations;
    uint32_t              maxComputeWorkGroupSize[3];
    uint32_t              subPixelPrecisionBits;
    uint32_t              subTexelPrecisionBits;
    uint32_t              mipmapPrecisionBits;
    uint32_t              maxDrawIndexedIndexValue;
    uint32_t              maxDrawIndirectCount;
    float                 maxSamplerLodBias;
    float                 maxSamplerAnisotropy;
    uint32_t              maxViewports;
    uint32_t              maxViewportDimensions[2];
    float                 viewportBoundsRange[2];
    uint32_t              viewportSubPixelBits;
    size_t                minMemoryMapAlignment;
    VkDeviceSize          minTexelBufferOffsetAlignment;
    VkDeviceSize          minUniformBufferOffsetAlignment;
    VkDeviceSize          minStorageBufferOffsetAlignment;
    int32_t               minTexelOffset;
    uint32_t              maxTexelOffset;
    int32_t               minTexelGatherOffset;
    uint32_t              maxTexelGatherOffset;
    float                 minInterpolationOffset;
    float                 maxInterpolationOffset;
    uint32_t              subPixelInterpolationOffsetBits;
    uint32_t              maxFramebufferWidth;
    uint32_t              maxFramebufferHeight;
    uint32_t              maxFramebufferLayers;
    VkSampleCountFlags    framebufferColorSampleCounts;
    VkSampleCountFlags    framebufferDepthSampleCounts;
    VkSampleCountFlags    framebufferStencilSampleCounts;
    VkSampleCountFlags    framebufferNoAttachmentsSampleCounts;
    uint32_t              maxColorAttachments;
    VkSampleCountFlags    sampledImageColorSampleCounts;
    VkSampleCountFlags    sampledImageIntegerSampleCounts;
    VkSampleCountFlags    sampledImageDepthSampleCounts;
    VkSampleCountFlags    sampledImageStencilSampleCounts;
    VkSampleCountFlags    storageImageSampleCounts;
    uint32_t              maxSampleMaskWords;
    VkBool32              timestampComputeAndGraphics;
    float                 timestampPeriod;
    uint32_t              maxClipDistances;
    uint32_t              maxCullDistances;
    uint32_t              maxCombinedClipAndCullDistances;
    uint32_t              discreteQueuePriorities;
    float                 pointSizeRange[2];
    float                 lineWidthRange[2];
    float                 pointSizeGranularity;
    float                 lineWidthGranularity;
    VkBool32              strictLines;
    VkBool32              standardSampleLocations;
    uint32_t padding_1; // Padding
    VkDeviceSize          optimalBufferCopyOffsetAlignment;
    VkDeviceSize          optimalBufferCopyRowPitchAlignment;
    VkDeviceSize          nonCoherentAtomSize;
} VkPhysicalDeviceLimits_padding;

typedef struct VkPhysicalDeviceProperties_padding {
    uint32_t                            apiVersion;
    uint32_t                            driverVersion;
    uint32_t                            vendorID;
    uint32_t                            deviceID;
    VkPhysicalDeviceType                deviceType;
    char                                deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t                             pipelineCacheUUID[VK_UUID_SIZE];
    uint32_t padding_0; // Padding
    VkPhysicalDeviceLimits_padding              limits;
    VkPhysicalDeviceSparseProperties    sparseProperties;
    uint32_t padding_1; // Padding
} VkPhysicalDeviceProperties_padding;

typedef struct VkPhysicalDeviceProperties2_padding {
    VkStructureType               sType;
    void*                         pNext;
    VkPhysicalDeviceProperties_padding    properties;
} VkPhysicalDeviceProperties2_padding;

//typedef VkPhysicalDeviceProperties2_padding VkPhysicalDeviceProperties2KHR_padding;

typedef struct VkMemoryHeap_padding {
    VkDeviceSize         size;
    VkMemoryHeapFlags    flags;
    uint32_t padding_0; // Padding
} VkMemoryHeap_padding;

typedef struct VkPhysicalDeviceMemoryProperties_padding {
    uint32_t        memoryTypeCount;
    VkMemoryType    memoryTypes[VK_MAX_MEMORY_TYPES];
    uint32_t        memoryHeapCount;
    VkMemoryHeap_padding    memoryHeaps[VK_MAX_MEMORY_HEAPS];
} VkPhysicalDeviceMemoryProperties_padding;

typedef struct VkPhysicalDeviceMemoryProperties2_padding {
    VkStructureType                     sType;
    void*                               pNext;
    VkPhysicalDeviceMemoryProperties_padding    memoryProperties;
} VkPhysicalDeviceMemoryProperties2_padding;

//typedef VkPhysicalDeviceMemoryProperties2_padding VkPhysicalDeviceMemoryProperties2KHR_padding;

//typedef struct VkMemoryAllocateInfo_padding {
//    VkStructureType    sType;
//    const void*        pNext;
//    VkDeviceSize       allocationSize;
//    uint32_t           memoryTypeIndex;
//    uint32_t padding_0; // Padding
//} VkMemoryAllocateInfo_padding;

//typedef struct VkMemoryRequirements_padding {
//    VkDeviceSize    size;
//    VkDeviceSize    alignment;
//    uint32_t        memoryTypeBits;
//    uint32_t padding_0; // Padding
//} VkMemoryRequirements_padding;
//
//typedef struct VkMemoryRequirements2_padding {
//    VkStructureType         sType;
//    void*                   pNext;
//    VkMemoryRequirements_padding    memoryRequirements;
//} VkMemoryRequirements2_padding;
//
//typedef VkMemoryRequirements2_padding VkMemoryRequirements2KHR_padding;

typedef struct VkSparseMemoryBind_padding {
    VkDeviceSize               resourceOffset;
    VkDeviceSize               size;
    VkDeviceMemory             memory;
    VkDeviceSize               memoryOffset;
    VkSparseMemoryBindFlags    flags;
    uint32_t padding_0; // Padding
} VkSparseMemoryBind_padding;

typedef struct VkSparseBufferMemoryBindInfo_padding {
    VkBuffer                     buffer;
    uint32_t                     bindCount;
    const VkSparseMemoryBind_padding*    pBinds;
} VkSparseBufferMemoryBindInfo_padding;

typedef struct VkSparseImageOpaqueMemoryBindInfo_padding {
    VkImage                      image;
    uint32_t                     bindCount;
    const VkSparseMemoryBind_padding*    pBinds;
} VkSparseImageOpaqueMemoryBindInfo_padding;

typedef struct VkSparseImageMemoryBind_padding {
    VkImageSubresource         subresource;
    VkOffset3D                 offset;
    VkExtent3D                 extent;
    uint32_t padding_0; // Padding
    VkDeviceMemory             memory;
    VkDeviceSize               memoryOffset;
    VkSparseMemoryBindFlags    flags;
    uint32_t padding_1; // Padding
} VkSparseImageMemoryBind_padding;

typedef struct VkSparseImageMemoryBindInfo_padding {
    VkImage                           image;
    uint32_t                          bindCount;
    const VkSparseImageMemoryBind_padding*    pBinds;
} VkSparseImageMemoryBindInfo_padding;

//typedef struct VkBindSparseInfo_padding {
//    VkStructureType                             sType;
//    const void*                                 pNext;
//    uint32_t                                    waitSemaphoreCount;
//    const VkSemaphore*                          pWaitSemaphores;
//    uint32_t                                    bufferBindCount;
//    const VkSparseBufferMemoryBindInfo_padding*         pBufferBinds;
//    uint32_t                                    imageOpaqueBindCount;
//    const VkSparseImageOpaqueMemoryBindInfo_padding*    pImageOpaqueBinds;
//    uint32_t                                    imageBindCount;
//    const VkSparseImageMemoryBindInfo_padding*          pImageBinds;
//    uint32_t                                    signalSemaphoreCount;
//    const VkSemaphore*                          pSignalSemaphores;
//} VkBindSparseInfo_padding;

typedef struct VkBufferCreateInfo_padding {
    VkStructureType        sType;
    const void*            pNext;
    VkBufferCreateFlags    flags;
    uint32_t padding_0; // Padding
    VkDeviceSize           size;
    VkBufferUsageFlags     usage;
    VkSharingMode          sharingMode;
    uint32_t               queueFamilyIndexCount;
    const uint32_t*        pQueueFamilyIndices;
} VkBufferCreateInfo_padding;

typedef struct VkBufferViewCreateInfo_padding {
    VkStructureType            sType;
    const void*                pNext;
    VkBufferViewCreateFlags    flags;
    uint32_t padding_0; // Padding
    VkBuffer                   buffer;
    VkFormat                   format;
    uint32_t padding_1; // Padding
    VkDeviceSize               offset;
    VkDeviceSize               range;
} VkBufferViewCreateInfo_padding;

typedef struct VkImageViewCreateInfo_padding {
    VkStructureType            sType;
    const void*                pNext;
    VkImageViewCreateFlags     flags;
    uint32_t padding_0; // Padding
    VkImage                    image;
    VkImageViewType            viewType;
    VkFormat                   format;
    VkComponentMapping         components;
    VkImageSubresourceRange    subresourceRange;
    uint32_t padding_1; // Padding
} VkImageViewCreateInfo_padding;

typedef struct VkGraphicsPipelineCreateInfo_padding {
    VkStructureType                                  sType;
    const void*                                      pNext;
    VkPipelineCreateFlags                            flags;
    uint32_t                                         stageCount;
    const VkPipelineShaderStageCreateInfo*           pStages;
    const VkPipelineVertexInputStateCreateInfo*      pVertexInputState;
    const VkPipelineInputAssemblyStateCreateInfo*    pInputAssemblyState;
    const VkPipelineTessellationStateCreateInfo*     pTessellationState;
    const VkPipelineViewportStateCreateInfo*         pViewportState;
    const VkPipelineRasterizationStateCreateInfo*    pRasterizationState;
    const VkPipelineMultisampleStateCreateInfo*      pMultisampleState;
    const VkPipelineDepthStencilStateCreateInfo*     pDepthStencilState;
    const VkPipelineColorBlendStateCreateInfo*       pColorBlendState;
    const VkPipelineDynamicStateCreateInfo*          pDynamicState;
    VkPipelineLayout                                 layout;
    VkRenderPass                                     renderPass;
    uint32_t                                         subpass;
    uint32_t padding_0; // Padding
    VkPipeline                                       basePipelineHandle;
    int32_t                                          basePipelineIndex;
    uint32_t padding_1; // Padding
} VkGraphicsPipelineCreateInfo_padding;

typedef struct VkComputePipelineCreateInfo_padding {
    VkStructureType                    sType;
    const void*                        pNext;
    VkPipelineCreateFlags              flags;
    uint32_t padding_0; // Padding
    VkPipelineShaderStageCreateInfo    stage;
    VkPipelineLayout                   layout;
    VkPipeline                         basePipelineHandle;
    int32_t                            basePipelineIndex;
    uint32_t padding_1; // Padding
} VkComputePipelineCreateInfo_padding;

//typedef struct VkDescriptorImageInfo_padding {
//    VkSampler        sampler;
//    VkImageView      imageView;
//    VkImageLayout    imageLayout;
//    uint32_t padding_0; // Padding
//} VkDescriptorImageInfo_padding;

typedef struct VkWriteDescriptorSet_padding {
    VkStructureType                  sType;
    const void*                      pNext;
    VkDescriptorSet                  dstSet;
    uint32_t                         dstBinding;
    uint32_t                         dstArrayElement;
    uint32_t                         descriptorCount;
    VkDescriptorType                 descriptorType;
//    const VkDescriptorImageInfo_padding*     pImageInfo;
    const VkDescriptorImageInfo*     pImageInfo;
    const VkDescriptorBufferInfo*    pBufferInfo;
    const VkBufferView*              pTexelBufferView;
    uint32_t padding_0; // Padding
} VkWriteDescriptorSet_padding;

typedef struct VkCopyDescriptorSet_padding {
    VkStructureType    sType;
    const void*        pNext;
    VkDescriptorSet    srcSet;
    uint32_t           srcBinding;
    uint32_t           srcArrayElement;
    VkDescriptorSet    dstSet;
    uint32_t           dstBinding;
    uint32_t           dstArrayElement;
    uint32_t           descriptorCount;
    uint32_t padding_0; // Padding
} VkCopyDescriptorSet_padding;

typedef struct VkFramebufferCreateInfo_padding {
    VkStructureType             sType;
    const void*                 pNext;
    VkFramebufferCreateFlags    flags;
    uint32_t padding_0; // Padding
    VkRenderPass                renderPass;
    uint32_t                    attachmentCount;
    const VkImageView*          pAttachments;
    uint32_t                    width;
    uint32_t                    height;
    uint32_t                    layers;
    uint32_t padding_1; // Padding
} VkFramebufferCreateInfo_padding;

typedef struct VkCommandBufferInheritanceInfo_padding {
    VkStructureType                  sType;
    const void*                      pNext;
    VkRenderPass                     renderPass;
    uint32_t                         subpass;
    uint32_t padding_0; // Padding
    VkFramebuffer                    framebuffer;
    VkBool32                         occlusionQueryEnable;
    VkQueryControlFlags              queryFlags;
    VkQueryPipelineStatisticFlags    pipelineStatistics;
    uint32_t padding_1; // Padding
} VkCommandBufferInheritanceInfo_padding;

//typedef struct VkCommandBufferBeginInfo_padding {
//    VkStructureType                          sType;
//    const void*                              pNext;
//    VkCommandBufferUsageFlags                flags;
//    const VkCommandBufferInheritanceInfo_padding*    pInheritanceInfo;
//} VkCommandBufferBeginInfo_padding;

typedef struct VkImageMemoryBarrier_padding {
    VkStructureType            sType;
    const void*                pNext;
    VkAccessFlags              srcAccessMask;
    VkAccessFlags              dstAccessMask;
    VkImageLayout              oldLayout;
    VkImageLayout              newLayout;
    uint32_t                   srcQueueFamilyIndex;
    uint32_t                   dstQueueFamilyIndex;
    VkImage                    image;
    VkImageSubresourceRange    subresourceRange;
    uint32_t padding_0; // Padding
} VkImageMemoryBarrier_padding;

typedef struct VkDescriptorUpdateTemplateCreateInfo_padding {
    VkStructureType                           sType;
    void*                                     pNext;
    VkDescriptorUpdateTemplateCreateFlags     flags;
    uint32_t                                  descriptorUpdateEntryCount;
    const VkDescriptorUpdateTemplateEntry*    pDescriptorUpdateEntries;
    VkDescriptorUpdateTemplateType            templateType;
    VkDescriptorSetLayout                     descriptorSetLayout;
    VkPipelineBindPoint                       pipelineBindPoint;
    uint32_t padding_0; // Padding
    VkPipelineLayout                          pipelineLayout;
    uint32_t                                  set;
    uint32_t padding_1; // Padding
} VkDescriptorUpdateTemplateCreateInfo_padding;

//typedef VkDescriptorUpdateTemplateCreateInfo_padding VkDescriptorUpdateTemplateCreateInfoKHR_padding;

//typedef struct VkPhysicalDeviceMaintenance3Properties_padding {
//    VkStructureType    sType;
//    void*              pNext;
//    uint32_t           maxPerSetDescriptors;
//    uint32_t padding_0; // Padding
//    VkDeviceSize       maxMemoryAllocationSize;
//} VkPhysicalDeviceMaintenance3Properties_padding;
//
//typedef VkPhysicalDeviceMaintenance3Properties_padding VkPhysicalDeviceMaintenance3PropertiesKHR_padding;

typedef struct VkSwapchainCreateInfoKHR_padding {
    VkStructureType                  sType;
    const void*                      pNext;
    VkSwapchainCreateFlagsKHR        flags;
    uint32_t padding_0; // Padding
    VkSurfaceKHR                     surface;
    uint32_t                         minImageCount;
    VkFormat                         imageFormat;
    VkColorSpaceKHR                  imageColorSpace;
    VkExtent2D                       imageExtent;
    uint32_t                         imageArrayLayers;
    VkImageUsageFlags                imageUsage;
    VkSharingMode                    imageSharingMode;
    uint32_t                         queueFamilyIndexCount;
    const uint32_t*                  pQueueFamilyIndices;
    VkSurfaceTransformFlagBitsKHR    preTransform;
    VkCompositeAlphaFlagBitsKHR      compositeAlpha;
    VkPresentModeKHR                 presentMode;
    VkBool32                         clipped;
    VkSwapchainKHR                   oldSwapchain;
} VkSwapchainCreateInfoKHR_padding;

//typedef struct VkBindImageMemorySwapchainInfoKHR_padding {
//    VkStructureType    sType;
//    const void*        pNext;
//    VkSwapchainKHR     swapchain;
//    uint32_t           imageIndex;
//    uint32_t padding_0; // Padding
//} VkBindImageMemorySwapchainInfoKHR_padding;

//typedef struct VkAcquireNextImageInfoKHR_padding {
//    VkStructureType    sType;
//    const void*        pNext;
//    VkSwapchainKHR     swapchain;
//    uint64_t           timeout;
//    VkSemaphore        semaphore;
//    VkFence            fence;
//    uint32_t           deviceMask;
//    uint32_t padding_0; // Padding
//} VkAcquireNextImageInfoKHR_padding;

typedef struct VkDisplayModePropertiesKHR_padding {
    VkDisplayModeKHR              displayMode;
    VkDisplayModeParametersKHR    parameters;
    uint32_t padding_0; // Padding
} VkDisplayModePropertiesKHR_padding;

typedef struct VkDisplayModeProperties2KHR_padding {
    VkStructureType               sType;
    void*                         pNext;
    VkDisplayModePropertiesKHR_padding    displayModeProperties;
} VkDisplayModeProperties2KHR_padding;

typedef struct VkDisplayPlanePropertiesKHR_padding {
    VkDisplayKHR    currentDisplay;
    uint32_t        currentStackIndex;
    uint32_t padding_0; // Padding
} VkDisplayPlanePropertiesKHR_padding;

typedef struct VkDisplayPlaneProperties2KHR_padding {
    VkStructureType                sType;
    void*                          pNext;
    VkDisplayPlanePropertiesKHR_padding    displayPlaneProperties;
} VkDisplayPlaneProperties2KHR_padding;

typedef struct VkDisplaySurfaceCreateInfoKHR_padding {
    VkStructureType                   sType;
    const void*                       pNext;
    VkDisplaySurfaceCreateFlagsKHR    flags;
    uint32_t padding_0; // Padding
    VkDisplayModeKHR                  displayMode;
    uint32_t                          planeIndex;
    uint32_t                          planeStackIndex;
    VkSurfaceTransformFlagBitsKHR     transform;
    float                             globalAlpha;
    VkDisplayPlaneAlphaFlagBitsKHR    alphaMode;
    VkExtent2D                        imageExtent;
    uint32_t padding_1; // Padding
} VkDisplaySurfaceCreateInfoKHR_padding;

//typedef struct VkMemoryGetFdInfoKHR_padding {
//    VkStructureType                       sType;
//    const void*                           pNext;
//    VkDeviceMemory                        memory;
//    VkExternalMemoryHandleTypeFlagBits    handleType;
//    uint32_t padding_0; // Padding
//} VkMemoryGetFdInfoKHR_padding;

//typedef struct VkImportSemaphoreFdInfoKHR_padding {
//    VkStructureType                          sType;
//    const void*                              pNext;
//    VkSemaphore                              semaphore;
//    VkSemaphoreImportFlags                   flags;
//    VkExternalSemaphoreHandleTypeFlagBits    handleType;
//    int                                      fd;
//    uint32_t padding_0; // Padding
//} VkImportSemaphoreFdInfoKHR_padding;

//typedef struct VkSemaphoreGetFdInfoKHR_padding {
//    VkStructureType                          sType;
//    const void*                              pNext;
//    VkSemaphore                              semaphore;
//    VkExternalSemaphoreHandleTypeFlagBits    handleType;
//    uint32_t padding_0; // Padding
//} VkSemaphoreGetFdInfoKHR_padding;

//typedef struct VkImportFenceFdInfoKHR_padding {
//    VkStructureType                      sType;
//    const void*                          pNext;
//    VkFence                              fence;
//    VkFenceImportFlags                   flags;
//    VkExternalFenceHandleTypeFlagBits    handleType;
//    int                                  fd;
//    uint32_t padding_0; // Padding
//} VkImportFenceFdInfoKHR_padding;

//typedef struct VkFenceGetFdInfoKHR_padding {
//    VkStructureType                      sType;
//    const void*                          pNext;
//    VkFence                              fence;
//    VkExternalFenceHandleTypeFlagBits    handleType;
//    uint32_t padding_0; // Padding
//} VkFenceGetFdInfoKHR_padding;

//typedef struct VkDisplayPlaneInfo2KHR_padding {
//    VkStructureType     sType;
//    const void*         pNext;
//    VkDisplayModeKHR    mode;
//    uint32_t            planeIndex;
//    uint32_t padding_0; // Padding
//} VkDisplayPlaneInfo2KHR_padding;

typedef struct VkDebugMarkerObjectNameInfoEXT_padding {
    VkStructureType               sType;
    const void*                   pNext;
    VkDebugReportObjectTypeEXT    objectType;
    uint32_t padding_0; // Padding
    uint64_t                      object;
    const char*                   pObjectName;
    uint32_t padding_1; // Padding
} VkDebugMarkerObjectNameInfoEXT_padding;

typedef struct VkDebugMarkerObjectTagInfoEXT_padding {
    VkStructureType               sType;
    const void*                   pNext;
    VkDebugReportObjectTypeEXT    objectType;
    uint32_t padding_0; // Padding
    uint64_t                      object;
    uint64_t                      tagName;
    size_t                        tagSize;
    const void*                   pTag;
} VkDebugMarkerObjectTagInfoEXT_padding;

//typedef struct VkConditionalRenderingBeginInfoEXT_padding {
//    VkStructureType                   sType;
//    const void*                       pNext;
//    VkBuffer                          buffer;
//    VkDeviceSize                      offset;
//    VkConditionalRenderingFlagsEXT    flags;
//    uint32_t padding_0; // Padding
//} VkConditionalRenderingBeginInfoEXT_padding;

//typedef struct VkCmdProcessCommandsInfoNVX_padding {
//    VkStructureType                      sType;
//    const void*                          pNext;
//    VkObjectTableNVX                     objectTable;
//    VkIndirectCommandsLayoutNVX          indirectCommandsLayout;
//    uint32_t                             indirectCommandsTokenCount;
//    const VkIndirectCommandsTokenNVX_padding*    pIndirectCommandsTokens;
//    uint32_t                             maxSequencesCount;
//    VkCommandBuffer                      targetCommandBuffer;
//    VkBuffer                             sequencesCountBuffer;
//    VkDeviceSize                         sequencesCountOffset;
//    VkBuffer                             sequencesIndexBuffer;
//    VkDeviceSize                         sequencesIndexOffset;
//} VkCmdProcessCommandsInfoNVX_padding;

//typedef struct VkCmdReserveSpaceForCommandsInfoNVX_padding {
//    VkStructureType                sType;
//    const void*                    pNext;
//    VkObjectTableNVX               objectTable;
//    VkIndirectCommandsLayoutNVX    indirectCommandsLayout;
//    uint32_t                       maxSequencesCount;
//    uint32_t padding_0; // Padding
//} VkCmdReserveSpaceForCommandsInfoNVX_padding;

//typedef struct VkObjectTableIndexBufferEntryNVX_padding {
//    VkObjectEntryTypeNVX          type;
//    VkObjectEntryUsageFlagsNVX    flags;
//    VkBuffer                      buffer;
//    VkIndexType                   indexType;
//    uint32_t padding_0; // Padding
//} VkObjectTableIndexBufferEntryNVX_padding;

//typedef struct VkObjectTablePushConstantEntryNVX_padding {
//    VkObjectEntryTypeNVX          type;
//    VkObjectEntryUsageFlagsNVX    flags;
//    VkPipelineLayout              pipelineLayout;
//    VkShaderStageFlags            stageFlags;
//    uint32_t padding_0; // Padding
//} VkObjectTablePushConstantEntryNVX_padding;

typedef struct VkPastPresentationTimingGOOGLE_padding {
    uint32_t    presentID;
    uint32_t padding_0; // Padding
    uint64_t    desiredPresentTime;
    uint64_t    actualPresentTime;
    uint64_t    earliestPresentTime;
    uint64_t    presentMargin;
} VkPastPresentationTimingGOOGLE_padding;

//typedef struct VkPresentTimeGOOGLE_padding {
//    uint32_t    presentID;
//    uint32_t padding_0; // Padding
//    uint64_t    desiredPresentTime;
//} VkPresentTimeGOOGLE_padding;
//
//typedef struct VkPresentTimesInfoGOOGLE_padding {
//    VkStructureType               sType;
//    const void*                   pNext;
//    uint32_t                      swapchainCount;
//    const VkPresentTimeGOOGLE_padding*    pTimes;
//} VkPresentTimesInfoGOOGLE_padding;

//typedef struct VkDebugUtilsObjectNameInfoEXT_padding {
//    VkStructureType    sType;
//    const void*        pNext;
//    VkObjectType       objectType;
//    uint32_t padding_0; // Padding
//    uint64_t           objectHandle;
//    const char*        pObjectName;
//    uint32_t padding_1; // Padding
//} VkDebugUtilsObjectNameInfoEXT_padding;

//typedef struct VkDebugUtilsMessengerCallbackDataEXT_padding {
//    VkStructureType                              sType;
//    const void*                                  pNext;
//    VkDebugUtilsMessengerCallbackDataFlagsEXT    flags;
//    const char*                                  pMessageIdName;
//    int32_t                                      messageIdNumber;
//    const char*                                  pMessage;
//    uint32_t                                     queueLabelCount;
//    VkDebugUtilsLabelEXT*                        pQueueLabels;
//    uint32_t                                     cmdBufLabelCount;
//    VkDebugUtilsLabelEXT*                        pCmdBufLabels;
//    uint32_t                                     objectCount;
//#if defined(__i386__)
//    VkDebugUtilsObjectNameInfoEXT_padding*               pObjects;
//#else
//    VkDebugUtilsObjectNameInfoEXT*               pObjects;
//#endif
//} VkDebugUtilsMessengerCallbackDataEXT_padding;

//typedef struct VkDebugUtilsObjectTagInfoEXT_padding {
//    VkStructureType    sType;
//    const void*        pNext;
//    VkObjectType       objectType;
//    uint32_t padding_0; // Padding
//    uint64_t           objectHandle;
//    uint64_t           tagName;
//    size_t             tagSize;
//    const void*        pTag;
//} VkDebugUtilsObjectTagInfoEXT_padding;

#endif
