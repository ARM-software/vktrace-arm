/*
 * Copyright (c) 2016-2019 Advanced Micro Devices, Inc. All rights reserved.
 * Copyright (C) 2019-2023 ARM Limited
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

// OPT: Optimization by using page-guard for speed up capture
//     The speed is extremely slow when use vktrace to capture DOOM4. It took over half a day and 900G of trace for a capture from
//     beginning to the game menu.
//     The reason that caused such slow capture is DOOM updates a big mapped memory(over 67M) frequently, vktrace copies this memory
//     block to harddrive when DOOM calls vkFlushmappedMemory to update it every time.
//     Here we use page guard to record which page of big memory block has been changed and only save those changed pages, it make
//     the capture time reduce to round 15 minutes, the trace file size is round 40G,
//     The Playback time for these trace file is round 7 minutes(on Win10/AMDFury/32GRam/I5 system).

#include "vktrace_pageguard_memorycopy.h"
#include "vktrace_lib_pagestatusarray.h"
#include "vktrace_lib_pageguardmappedmemory.h"
#include "vktrace_lib_pageguardcapture.h"
#include "vktrace_lib_pageguard.h"

PageGuardCapture::PageGuardCapture() {
    EmptyChangedInfoArray.offset = 0;
    EmptyChangedInfoArray.length = 0;
}

std::unordered_map<VkDeviceMemory, PageGuardMappedMemory>& PageGuardCapture::getMapMemory() { return MapMemory; }
std::unordered_map<VkDeviceMemory, VkMemoryAllocateInfo>& PageGuardCapture::getMapMemoryAllocateInfo() {
    return MapMemoryAllocateInfo;
}

std::unordered_map<VkDeviceMemory, void*>& PageGuardCapture::getMapMemoryExtHostPointer() { return MapMemoryExtHostPointer; }

std::unordered_map<VkDevice, VkPhysicalDevice>& PageGuardCapture::getMapDevice() { return MapDevice; }

void PageGuardCapture::vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    MapDevice[*pDevice] = physicalDevice;
}

void PageGuardCapture::vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) { MapDevice.erase(device); }

void PageGuardCapture::vkAllocateMemoryPageGuardHandle(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo,
                                                       const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory,
                                                       void* pExternalHostPointer) {
    MapMemoryAllocateInfo[*pMemory] = *pAllocateInfo;
    if (pExternalHostPointer != nullptr) {
        MapMemoryExtHostPointer[*pMemory] = pExternalHostPointer;
    }
}

void PageGuardCapture::vkFreeMemoryPageGuardHandle(VkDevice device, VkDeviceMemory memory,
                                                   const VkAllocationCallbacks* pAllocator) {
    auto iteratorExtPointer = MapMemoryExtHostPointer.find(memory);
    if (iteratorExtPointer != MapMemoryExtHostPointer.end()) {
        if (iteratorExtPointer->second != nullptr) {
            pageguardFreeMemory(iteratorExtPointer->second);
        }
        MapMemoryExtHostPointer.erase(memory);
    }
    MapMemoryAllocateInfo.erase(memory);
}

void PageGuardCapture::vkMapMemoryPageGuardHandle(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
                                                  VkFlags flags, void** ppData) {
    PageGuardMappedMemory OPTmappedmem;
    if (getPageGuardEnableFlag()) {
        {
            void* pExternalHostMemory = nullptr;
            auto iteratorExtPointer = MapMemoryExtHostPointer.find(memory);
            if (iteratorExtPointer != MapMemoryExtHostPointer.end()) {
                pExternalHostMemory = iteratorExtPointer->second;
            }
            OPTmappedmem.vkMapMemoryPageGuardHandle(device, memory, offset, size, flags, ppData, pExternalHostMemory);
            MapMemory[memory] = OPTmappedmem;
        }
    }
    MapMemoryPtr[memory] = (PBYTE)(*ppData);
    MapMemoryOffset[memory] = offset;
    MapMemorySize[memory] = size;
}

void PageGuardCapture::vkUnmapMemoryPageGuardHandle(VkDevice device, VkDeviceMemory memory, void** MappedData,
                                                    vkFlushMappedMemoryRangesFunc pFunc) {
    LPPageGuardMappedMemory lpOPTMemoryTemp = findMappedMemoryObject(device, memory);
    if (lpOPTMemoryTemp) {
        VkMappedMemoryRange memoryRange;
        flushTargetChangedMappedMemory(lpOPTMemoryTemp, pFunc, &memoryRange, false);
        lpOPTMemoryTemp->vkUnmapMemoryPageGuardHandle(device, memory, MappedData);
        MapMemory.erase(memory);
    }
    MapMemoryPtr.erase(memory);
    MapMemoryOffset.erase(memory);
    MapMemorySize.erase(memory);
}

void PageGuardCapture::SyncRealMappedMemoryToMemoryCopyHandle(VkDevice device, VkDeviceMemory memory) {
    LPPageGuardMappedMemory lpOPTMemoryTemp = findMappedMemoryObject(device, memory);
    if (lpOPTMemoryTemp) {
        lpOPTMemoryTemp->SyncRealMappedMemoryToMemoryCopyHandle(device, memory);
    }
}

void* PageGuardCapture::getMappedMemoryPointer(VkDevice device, VkDeviceMemory memory) { return MapMemoryPtr[memory]; }

VkDeviceSize PageGuardCapture::getMappedMemoryOffset(VkDevice device, VkDeviceMemory memory) { return MapMemoryOffset[memory]; }

VkDeviceSize PageGuardCapture::getMappedMemorySize(VkDevice device, VkDeviceMemory memory) { return MapMemorySize[memory]; }

// return: if it's target mapped memory and no change at all;
// PBYTE *ppPackageDataforOutOfMap, must be an array include memoryRangeCount elements
bool PageGuardCapture::vkFlushMappedMemoryRangesPageGuardHandle(VkDevice device, uint32_t memoryRangeCount,
                                                                const VkMappedMemoryRange* pMemoryRanges,
                                                                PBYTE* ppPackageDataforOutOfMap) {
    bool handleSuccessfully = false, bChanged = false;
    std::unordered_map<VkDeviceMemory, PageGuardMappedMemory>::const_iterator mappedmem_it;
    for (uint32_t i = 0; i < memoryRangeCount; i++) {
        VkMappedMemoryRange* pRange = (VkMappedMemoryRange*)&pMemoryRanges[i];

        ppPackageDataforOutOfMap[i] = nullptr;
        LPPageGuardMappedMemory lpOPTMemoryTemp = findMappedMemoryObject(device, pRange->memory);

        if (lpOPTMemoryTemp && !lpOPTMemoryTemp->noGuard()) {
            if (pRange->size == VK_WHOLE_SIZE) {
                pRange->size = lpOPTMemoryTemp->getMappedSize() - (pRange->offset - lpOPTMemoryTemp->MappedOffset);
            }
            if (lpOPTMemoryTemp->vkFlushMappedMemoryRangePageGuardHandle(device, pRange->memory, pRange->offset, pRange->size,
                                                                         nullptr, nullptr, nullptr)) {
                bChanged = true;
            }
        } else {
            bChanged = true;
            VkDeviceSize RealRangeSize = pRange->size;
            if (RealRangeSize == VK_WHOLE_SIZE) {
                RealRangeSize = MapMemorySize[pRange->memory] - (pRange->offset - MapMemoryOffset[pRange->memory]);
            }
            ppPackageDataforOutOfMap[i] = (PBYTE)pageguardAllocateMemory(RealRangeSize + 2 * sizeof(PageGuardChangedBlockInfo));
            PageGuardChangedBlockInfo* pInfoTemp = (PageGuardChangedBlockInfo*)ppPackageDataforOutOfMap[i];
            pInfoTemp[0].offset = 1;
            pInfoTemp[0].length = (DWORD)RealRangeSize;
            pInfoTemp[0].reserve0 = 0;
            pInfoTemp[0].reserve1 = 0;
            pInfoTemp[1].offset = (uint32_t)pRange->offset - (uint32_t)getMappedMemoryOffset(device, pRange->memory);
            pInfoTemp[1].length = (DWORD)RealRangeSize;
            pInfoTemp[1].reserve0 = 0;
            pInfoTemp[1].reserve1 = 0;
            PBYTE pDataInPackage = (PBYTE)(pInfoTemp + 2);
            void* pDataMapped = getMappedMemoryPointer(device, pRange->memory);
            vktrace_pageguard_memcpy(pDataInPackage, reinterpret_cast<PBYTE>(pDataMapped) + pInfoTemp[1].offset, RealRangeSize);
        }
    }
    if (!bChanged) {
        handleSuccessfully = true;
    }
    return handleSuccessfully;
}

LPPageGuardMappedMemory PageGuardCapture::findMappedMemoryObject(VkDevice device, VkDeviceMemory memory) {
    LPPageGuardMappedMemory pMappedMemoryObject = nullptr;
    std::unordered_map<VkDeviceMemory, PageGuardMappedMemory>::const_iterator mappedmem_it;
    mappedmem_it = MapMemory.find(memory);
    if (mappedmem_it != MapMemory.end()) {
        pMappedMemoryObject = ((PageGuardMappedMemory*)&(mappedmem_it->second));
        if (pMappedMemoryObject->MappedDevice != device) {
            pMappedMemoryObject = nullptr;
        }
    }
    return pMappedMemoryObject;
}

LPPageGuardMappedMemory PageGuardCapture::findMappedMemoryObject(PBYTE addr, VkDeviceSize* pOffsetOfAddr, PBYTE* ppBlock,
                                                                 VkDeviceSize* pBlockSize) {
    LPPageGuardMappedMemory pMappedMemoryObject = nullptr;
    LPPageGuardMappedMemory pMappedMemoryTemp;
    PBYTE pBlock = nullptr;
    VkDeviceSize OffsetOfAddr = 0, BlockSize = 0;

    for (std::unordered_map<VkDeviceMemory, PageGuardMappedMemory>::iterator it = MapMemory.begin(); it != MapMemory.end(); it++) {
        pMappedMemoryTemp = &(it->second);
        if ((addr >= pMappedMemoryTemp->pMappedData) && (addr < (pMappedMemoryTemp->pMappedData + pMappedMemoryTemp->MappedSize))) {
            pMappedMemoryObject = pMappedMemoryTemp;

            OffsetOfAddr = (VkDeviceSize)(addr - pMappedMemoryTemp->pMappedData);
            BlockSize = pMappedMemoryTemp->PageGuardSize;
            pBlock = addr - OffsetOfAddr % BlockSize;
            if (ppBlock) {
                *ppBlock = pBlock;
            }
            if (pBlockSize) {
                *pBlockSize = BlockSize;
            }
            if (pOffsetOfAddr) {
                *pOffsetOfAddr = OffsetOfAddr;
            }

            return pMappedMemoryObject;
        }
    }
    return NULL;
}

LPPageGuardMappedMemory PageGuardCapture::findMappedMemoryObject(VkDevice device, const VkMappedMemoryRange* pMemoryRange) {
    LPPageGuardMappedMemory pMappedMemoryObject = findMappedMemoryObject(device, pMemoryRange->memory);
    return pMappedMemoryObject;
}

// get size of all changed package in array of pMemoryRanges
VkDeviceSize PageGuardCapture::getALLChangedPackageSizeInMappedMemory(VkDevice device, uint32_t memoryRangeCount,
                                                                      const VkMappedMemoryRange* pMemoryRanges,
                                                                      PBYTE* ppPackageDataforOutOfMap) {
    VkDeviceSize allChangedPackageSize = 0, PackageSize = 0;
    LPPageGuardMappedMemory pMappedMemoryTemp;
    for (uint32_t i = 0; i < memoryRangeCount; i++) {
        pMappedMemoryTemp = findMappedMemoryObject(device, pMemoryRanges + i);
        if (pMappedMemoryTemp && !pMappedMemoryTemp->noGuard()) {
            pMappedMemoryTemp->getChangedDataPackage(&PackageSize);
        } else {
            PageGuardChangedBlockInfo* pInfoTemp = (PageGuardChangedBlockInfo*)ppPackageDataforOutOfMap[i];
            PackageSize = pInfoTemp->length + 2 * sizeof(PageGuardChangedBlockInfo);
        }
        allChangedPackageSize += PackageSize;
    }
    return ROUNDUP_TO_4(allChangedPackageSize);
}

// get ptr and size of OPTChangedDataPackage;
PBYTE PageGuardCapture::getChangedDataPackageOutOfMap(PBYTE* ppPackageDataforOutOfMap, DWORD dwRangeIndex, VkDeviceSize* pSize) {
    PBYTE pDataPackage = (PBYTE)ppPackageDataforOutOfMap[dwRangeIndex];
    PageGuardChangedBlockInfo* pInfo = (PageGuardChangedBlockInfo*)pDataPackage;
    if (pSize) {
        *pSize = sizeof(PageGuardChangedBlockInfo) * 2 + pInfo->length;
    }
    return pDataPackage;
}

void PageGuardCapture::clearChangedDataPackageOutOfMap(PBYTE* ppPackageDataforOutOfMap, DWORD dwRangeIndex) {
    pageguardFreeMemory(ppPackageDataforOutOfMap[dwRangeIndex]);
    ppPackageDataforOutOfMap[dwRangeIndex] = nullptr;
}

bool PageGuardCapture::isHostWriteFlagSetInMemoryBarriers(uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers) {
    bool flagSet = false;
    if ((memoryBarrierCount != 0) && (pMemoryBarriers)) {
        for (uint32_t i = 0; i < memoryBarrierCount; i++) {
            if (pMemoryBarriers[i].srcAccessMask & VK_ACCESS_HOST_WRITE_BIT) {
                flagSet = true;
            }
        }
    }
    return flagSet;
}

bool PageGuardCapture::isHostWriteFlagSetInBufferMemoryBarrier(uint32_t memoryBarrierCount,
                                                               const VkBufferMemoryBarrier* pMemoryBarriers) {
    bool flagSet = false;
    if ((memoryBarrierCount != 0) && (pMemoryBarriers)) {
        for (uint32_t i = 0; i < memoryBarrierCount; i++) {
            if (pMemoryBarriers[i].srcAccessMask & VK_ACCESS_HOST_WRITE_BIT) {
                flagSet = true;
            }
        }
    }
    return flagSet;
}

bool PageGuardCapture::isHostWriteFlagSetInImageMemoryBarrier(uint32_t memoryBarrierCount,
                                                              const VkImageMemoryBarrier* pMemoryBarriers) {
    bool flagSet = false;
    if ((memoryBarrierCount != 0) && (pMemoryBarriers)) {
        for (uint32_t i = 0; i < memoryBarrierCount; i++) {
            if (pMemoryBarriers[i].srcAccessMask & VK_ACCESS_HOST_WRITE_BIT) {
                flagSet = true;
            }
        }
    }
    return flagSet;
}

bool PageGuardCapture::isHostWriteFlagSet(VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                                          VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount,
                                          const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount,
                                          const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
                                          const VkImageMemoryBarrier* pImageMemoryBarriers) {
    bool flagSet = false, bWrite = isHostWriteFlagSetInMemoryBarriers(memoryBarrierCount, pMemoryBarriers) ||
                                   isHostWriteFlagSetInBufferMemoryBarrier(bufferMemoryBarrierCount, pBufferMemoryBarriers) ||
                                   isHostWriteFlagSetInImageMemoryBarrier(imageMemoryBarrierCount, pImageMemoryBarriers);
    if (bWrite || (srcStageMask & VK_PIPELINE_STAGE_HOST_BIT)) {
        flagSet = true;
    }
    return flagSet;
}

bool PageGuardCapture::isReadyForHostReadInMemoryBarriers(uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers) {
    bool isReady = false;
    if ((memoryBarrierCount != 0) && (pMemoryBarriers)) {
        for (uint32_t i = 0; i < memoryBarrierCount; i++) {
            if (pMemoryBarriers[i].dstAccessMask & VK_ACCESS_HOST_READ_BIT) {
                isReady = true;
            }
        }
    }
    return isReady;
}

bool PageGuardCapture::isReadyForHostReadInBufferMemoryBarrier(uint32_t memoryBarrierCount,
                                                               const VkBufferMemoryBarrier* pMemoryBarriers) {
    bool isReady = false;
    if ((memoryBarrierCount != 0) && (pMemoryBarriers)) {
        for (uint32_t i = 0; i < memoryBarrierCount; i++) {
            if (pMemoryBarriers[i].dstAccessMask & VK_ACCESS_HOST_READ_BIT) {
                isReady = true;
            }
        }
    }
    return isReady;
}

bool PageGuardCapture::isReadyForHostReadInImageMemoryBarrier(uint32_t memoryBarrierCount,
                                                              const VkImageMemoryBarrier* pMemoryBarriers) {
    bool isReady = false;
    if ((memoryBarrierCount != 0) && (pMemoryBarriers)) {
        for (uint32_t i = 0; i < memoryBarrierCount; i++) {
            if ((pMemoryBarriers[i].dstAccessMask & VK_ACCESS_HOST_READ_BIT)) {
                isReady = true;
            }
        }
    }
    return isReady;
}

bool PageGuardCapture::isReadyForHostRead(VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                                          VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount,
                                          const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount,
                                          const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
                                          const VkImageMemoryBarrier* pImageMemoryBarriers) {
    bool isReady = false, bRead = isReadyForHostReadInMemoryBarriers(memoryBarrierCount, pMemoryBarriers) ||
                                  isReadyForHostReadInBufferMemoryBarrier(bufferMemoryBarrierCount, pBufferMemoryBarriers) ||
                                  isReadyForHostReadInImageMemoryBarrier(imageMemoryBarrierCount, pImageMemoryBarriers);
    if (bRead || (dstStageMask & VK_PIPELINE_STAGE_HOST_BIT)) {
        isReady = true;
    }
    return isReady;
}

VkMemoryPropertyFlags PageGuardCapture::getMemoryPropertyFlags(VkDevice device, uint32_t memoryTypeIndex) {
    VkPhysicalDevice physicalDevice = getMapDevice()[device];
    VkPhysicalDeviceMemoryProperties PhysicalDeviceMemoryProperties;
    mid(physicalDevice)->instTable.GetPhysicalDeviceMemoryProperties(physicalDevice, &PhysicalDeviceMemoryProperties);
    assert(memoryTypeIndex < PhysicalDeviceMemoryProperties.memoryTypeCount);
    return PhysicalDeviceMemoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
}
