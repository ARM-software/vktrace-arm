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
#pragma once

#include <stdbool.h>
#include <unordered_map>
#include "vulkan/vulkan.h"
#include "vktrace_platform.h"
#include "vktrace_common.h"

#include "vktrace_interconnect.h"
#include "vktrace_filelike.h"
#include "vktrace_trace_packet_utils.h"
#include "vktrace_vk_exts.h"
#include <stdio.h>

#include "vktrace_pageguard_memorycopy.h"
#include "vktrace_lib_pagestatusarray.h"

typedef class PageGuardMappedMemory {
    friend class PageGuardCapture;

   private:
    VkDevice MappedDevice;
    VkDeviceMemory MappedMemory;
    VkDeviceSize MappedOffset;
    PBYTE pMappedData;  /// point to mapped memory in app process, if pRealMappedData==nullptr, pMappedData point to real mapped
                        /// memory,
    /// if pRealMappedData!=nullptr, pMappedData point to real mapped memory copy, the copy can be added page guard
    PBYTE pRealMappedData;      /// point to real mapped memory in app process
    PBYTE pChangedDataPackage;  /// if not nullptr, it point to a package which include changed info array and changed data block,
                                /// allocated by this class
    VkDeviceSize MappedSize;    /// the size of range

    VkDeviceSize PageGuardSize;  /// size for one block

   protected:
    PageStatusArray *pPageStatus;
    bool BlockConflictError;  /// record if any block has been read by host and also write by host
    VkDeviceSize PageSizeLeft;
    VkDeviceSize StartingAddressOffset;  /// the offset relative to the beginning of a system page where the starting address of the
                                         /// mapped memory (returned to target title) located.
    uint64_t PageGuardAmount;
    bool NoGuard;

   public:
    PageGuardMappedMemory();
    ~PageGuardMappedMemory();

    VkDevice &getMappedDevice();

    VkDeviceMemory &getMappedMemory();

    VkDeviceSize &getMappedOffset();

    PBYTE &getRealMappedDataPointer();  /// get pointer to real mapped memory in app process

    PBYTE &getMappedDataPointer();  /// get pointer to mapped memory in app process

    VkDeviceSize &getMappedSize();  /// get the size of range

    bool isUseCopyForRealMappedMemory();

    bool noGuard() { return NoGuard; }

    /// get head addr and size for a block which is located by a given index
    bool getChangedRangeByIndex(uint64_t index, PBYTE *paddr, VkDeviceSize *pBlockSize);

    /// if return value <0, mean addr is out of page guard.
    int64_t getIndexOfChangedBlockByAddr(PBYTE addr);

    void setMappedBlockChanged(uint64_t index, bool bChanged, int useWhich);

    bool isMappedBlockChanged(uint64_t index, int useWhich);

    bool isMappedBlockLoaded(uint64_t index);

    void setMappedBlockLoaded(uint64_t index, bool bLoaded);

    uint64_t getMappedBlockSize(uint64_t index);

    uint64_t getMappedBlockOffset(uint64_t index);

    bool isNoMappedBlockChanged();
#if defined(WIN32)
    uint64_t getWriteWatchForPage(DWORD dwFlags, void *pgAddr);
#endif

    void resetMemoryObjectAllChangedFlagAndPageGuard();

    void resetMemoryObjectAllReadFlagAndPageGuard();

    bool setAllPageGuardAndFlag(bool bSetPageGuard, bool bSetBlockChanged);

    /// Get VkMemoryPropertyFlags for a logical device and specific memoryTypeIndex
    VkMemoryPropertyFlags getMemoryPropertyFlags(std::unordered_map<VkDevice, VkPhysicalDevice> &mapDevice, VkDevice device,
                                                 uint32_t memoryTypeIndex);

    /// Get VkMemoryPropertyFlags for this memory object.
    bool getMemoryProperty(std::unordered_map<VkDeviceMemory, VkMemoryAllocateInfo> &mapMemoryCreateInfo,
                           std::unordered_map<VkDevice, VkPhysicalDevice> &mapDevice, VkDeviceSize *pMemoryWholeSize,
                           VkMemoryPropertyFlags *pMemoryPropertyFlags);

    bool vkMapMemoryPageGuardHandle(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkFlags flags,
                                    void **ppData, void *pExternalHostMemory = nullptr);

    void vkUnmapMemoryPageGuardHandle(VkDevice device, VkDeviceMemory memory, void **MappedData);

    void SyncRealMappedMemoryToMemoryCopyHandle(VkDevice device, VkDeviceMemory memory);

    void backupBlockChangedArraySnapshot();

    void backupBlockReadArraySnapshot();

    size_t getChangedBlockAmount(int useWhich);

    /// is RangeLimit cover or partly cover Range
    bool isRangeIncluded(VkDeviceSize RangeOffsetLimit, VkDeviceSize RangeSizeLimit, VkDeviceSize RangeOffset,
                         VkDeviceSize RangeSize);

    /// for output,
    /// if pData!=nullptr,the pData + Offset is head addr of an array of PageGuardChangedBlockInfo, the [0] is block amount, size
    /// (size for all changed blocks which amount is block amount),then block1 offset,block1 size....,
    ///               the block? offset is  this changed block offset to mapped memory head addr,the array followed by changed
    ///               blocks data
    ///
    /// if pData==nullptr, only get size
    /// size_t *pdwSaveSize, the size of all changed blocks
    /// size_t *pInfoSize, the size of array of PageGuardChangedBlockInfo
    /// VkDeviceSize RangeOffset, RangeSize, only consider the block which is in the range which start from RangeOffset and size is
    /// RangeSize, if RangeOffset<0, consider whole mapped memory
    /// return the amount of changed blocks.
    uint64_t getChangedBlockInfo(VkDeviceSize RangeOffset, VkDeviceSize RangeSize, uint64_t *pdwSaveSize, uint64_t *pInfoSize,
                                 PBYTE pData, uint64_t DataOffset, int useWhich = BLOCK_FLAG_ARRAY_CHANGED);

    /// return: if memory already changed;
    ///        evenif no change to mmeory, it will still allocate memory for info array which only include one
    ///        PageGuardChangedBlockInfo,its  offset and length are all 0;
    /// VkDeviceSize       *pChangedSize, the size of all changed data block
    /// VkDeviceSize       *pDataPackageSize, size for ChangedDataPackage
    /// PBYTE              *ppChangedDataPackage, is an array of PageGuardChangedBlockInfo + all changed data, the [0] offset is
    /// blocks amount, length is size amount of these blocks(not include this info array),then block1 offset,block1 size....
    ///                     allocate needed memory in the method, ppChangedDataPackage point to returned pointer, if
    ///                     ppChangedDataPackage==null, only calculate size
    bool vkFlushMappedMemoryRangePageGuardHandle(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
                                                 VkDeviceSize *pChangedSize, VkDeviceSize *pDataPackageSize,
                                                 PBYTE *ppChangedDataPackage);

    void clearChangedDataPackage();

    /// get ptr and size of OPTChangedDataPackage;
    PBYTE getChangedDataPackage(VkDeviceSize *pSize);

} PageGuardMappedMemory, *LPPageGuardMappedMemory;
