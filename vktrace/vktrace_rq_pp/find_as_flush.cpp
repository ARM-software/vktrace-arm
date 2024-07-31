#include <vector>
#include <unordered_map>

#include "vktrace_common.h"
#include "vktrace_tracelog.h"
#include "vktrace_trace_packet_utils.h"
#include "vktrace_vk_packet_id.h"
#include "vktrace_rq_pp.h"
#include "vk_struct_size_helper.h"
#include "vktrace_pageguard_memorycopy.h"

using namespace std;

static unordered_map<VkDeviceAddress, VkAccelerationStructureKHR> asAddressToAccelerationStructure;
static unordered_map<VkDeviceAddress, VkBuffer> bufferAddressToBuffer;
static VkDeviceAddress minTraceBufferDeviceAddress = UINT64_MAX;
static VkDeviceAddress maxTraceBufferDeviceAddress = 0;
static unordered_map<VkBuffer, VkDeviceSize> traceBufferToSize;

int pre_find_as_flush(vktrace_trace_file_header* pFileHeader, FileLike* traceFile) {
    int ret = 0;
    for (auto it = g_globalPacketIndexList.begin(); it != g_globalPacketIndexList.end(); ++it) {
        uint64_t gpIndex = (uint64_t)*it;
        if (g_globalPacketIndexToPacketInfo.find(gpIndex) == g_globalPacketIndexToPacketInfo.end()) {
            vktrace_LogError("Incorrect global packet index: %llu.", gpIndex);
            ret = -1;
            break;
        }
        // Read packet from the existing trace file
        packet_info packetInfo = g_globalPacketIndexToPacketInfo[gpIndex];
        vktrace_trace_packet_header* pHeader = (vktrace_trace_packet_header*)vktrace_malloc((size_t)packetInfo.size);
        vktrace_FileLike_SetCurrentPosition(traceFile, packetInfo.position);
        if (!vktrace_FileLike_ReadRaw(traceFile, pHeader, (size_t)packetInfo.size)) {
            vktrace_LogError("Failed to read trace packet with size of %llu.", (size_t)packetInfo.size);
            vktrace_free(pHeader);
            ret = -1;
            break;
        }
        pHeader->pBody = (uintptr_t)(((char*)pHeader) + sizeof(vktrace_trace_packet_header));

        if (pHeader->tracer_id == VKTRACE_TID_VULKAN_COMPRESSED) {
            if (g_compressor == nullptr) {
                g_compressor = create_compressor((VKTRACE_COMPRESS_TYPE)pFileHeader->compress_type);
                if (g_compressor == nullptr) {
                    vktrace_LogError("Create compressor failed.");
                    return -1;
                }
            }
            if (g_decompressor == nullptr) {
                g_decompressor = create_decompressor((VKTRACE_COMPRESS_TYPE)pFileHeader->compress_type);
                if (g_decompressor == nullptr) {
                    vktrace_LogError("Create decompressor failed.");
                    return -1;
                }
            }
            if (decompress_packet(g_decompressor, pHeader) < 0) {
                vktrace_LogError("Decompress the packet failed !");
                return -1;
            }
        }

        vktrace_free(pHeader);

        if (ret != 0) {
            vktrace_LogError("Failed to handle packet %llu in pre_rq!", pHeader->global_packet_index);
            break;
        }
    }

    return ret;
}

int finalize_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle(vktrace_trace_packet_header* &pHeader, packet_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle* pPacket)
{
    for (unsigned j = 0; j < pPacket->memoryRangeCount; j++) {
        vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->ppData[j]));
        vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->ppDataAsOffset[j]));
        vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->ppDataSGHandleOffset[j]));
        vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->ppDataBufferOffset[j]));
    }
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pMemoryRanges));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->ppData));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->ppDataAsOffset));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->ppDataSGHandleOffset));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->ppDataBufferOffset));
    return 0;
}

int finalize_vkFlushMappedMemoryRanges(vktrace_trace_packet_header* &pHeader, packet_vkFlushMappedMemoryRanges* pPacket)
{
    for (unsigned j = 0; j < pPacket->memoryRangeCount; j++) {
        vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->ppData[j]));
    }
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pMemoryRanges));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->ppData));
    return 0;
}

static std::unordered_map<VkDeviceAddress, VkBuffer>::iterator findClosestAddress(VkDeviceAddress addr) {
    uint64_t delta_min = ULONG_LONG_MAX;
    auto it_result = bufferAddressToBuffer.end();
    for (auto it = bufferAddressToBuffer.begin(); it != bufferAddressToBuffer.end(); ++it) {
        uint64_t delta = addr - it->first;
        if (addr >= it->first && delta >= 0 && delta < delta_min) {
            delta_min = delta;
            it_result = it;
        }
    }

    if (it_result != bufferAddressToBuffer.end()) {
        auto iter = traceBufferToSize.find(it_result->second);
        if (iter != traceBufferToSize.end()) {
            if (addr > it_result->first + iter->second)  {
                it_result = bufferAddressToBuffer.end();
            }
        } else {
            it_result = bufferAddressToBuffer.end();;
        }
    }

    return it_result;
}

static int post_find_as_flush_memory(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader)
{
    packet_vkFlushMappedMemoryRanges* pPacket = interpret_body_as_vkFlushMappedMemoryRanges(pHeader);

    uint32_t **AsOffsetArrays = new uint32_t*[pPacket->memoryRangeCount];
    uint64_t **SGHandleOffsetArrays = new uint64_t*[pPacket->memoryRangeCount];
    uint32_t **BufOffsetArrays = new uint32_t*[pPacket->memoryRangeCount];
    int offsetSize = 0;
    bool foundAs = false;
    bool foundBufDeviceAddr = false;
    for (uint32_t i = 0; i < pPacket->memoryRangeCount; i++) {
        PageGuardChangedBlockInfo *p = (PageGuardChangedBlockInfo *)pPacket->ppData[i];
        int segment = p[0].offset;
        PBYTE pChangedData = (PBYTE)(p) + sizeof(PageGuardChangedBlockInfo) * (p[0].offset + 1);
        size_t CurrentOffset = 0;
        vector<uint32_t> offsetAsDeviceAddr;
        vector<uint32_t> offsetBufDeviceAddr;
        for (size_t j = 0; j < segment; j++) {
            uint64_t *temp = (uint64_t *)(pChangedData + CurrentOffset);
            for (size_t k = 0; k < p[j + 1].length / sizeof(uint64_t); ++k) {
                VkDeviceAddress addr = *(uint64_t*)(temp + k);
                if (addr == 0) {
                    continue;
                }

                if (asAddressToAccelerationStructure.find(addr) != asAddressToAccelerationStructure.end()) {
                    foundAs = true;
                    offsetAsDeviceAddr.push_back(CurrentOffset + k * sizeof(uint64_t));
                    continue;
                }

                if ((addr < minTraceBufferDeviceAddress) || (addr > (maxTraceBufferDeviceAddress + MAX_BUFFER_DEVICEADDRESS_SIZE))) {
                    continue;
                }

                if (processBufDeviceAddr) {
                    if (bufferAddressToBuffer.find(addr) != bufferAddressToBuffer.end()) {
                        foundBufDeviceAddr = true;
                        offsetBufDeviceAddr.push_back(CurrentOffset + k * sizeof(uint64_t));
                    } else {
                        if (findClosestAddress(addr) != bufferAddressToBuffer.end()) {
                            foundBufDeviceAddr = true;
                            offsetBufDeviceAddr.push_back(CurrentOffset + k * sizeof(uint64_t));
                        }
                    }
                }
            }
            CurrentOffset += p[j + 1].length;
        }

        AsOffsetArrays[i] = new uint32_t[offsetAsDeviceAddr.size() + 1];
        AsOffsetArrays[i][0] = offsetAsDeviceAddr.size();
        memcpy(AsOffsetArrays[i] + 1, offsetAsDeviceAddr.data(), offsetAsDeviceAddr.size() * sizeof(uint32_t));
        offsetSize += (offsetAsDeviceAddr.size() + 1) * sizeof(uint32_t);

        BufOffsetArrays[i] = new uint32_t[offsetBufDeviceAddr.size() + 1];
        BufOffsetArrays[i][0] = offsetBufDeviceAddr.size();
        memcpy(BufOffsetArrays[i] + 1, offsetBufDeviceAddr.data(), offsetBufDeviceAddr.size() * sizeof(uint32_t));
        offsetSize += (offsetBufDeviceAddr.size() + 1) * sizeof(uint32_t);

        SGHandleOffsetArrays[i] = new uint64_t[1];
        SGHandleOffsetArrays[i][0] = 0;
        offsetSize += 1 * sizeof(uint64_t);
    }

    packet_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle* pPacketRemap = NULL;
    if (foundAs || foundBufDeviceAddr) {
        // duplicate a new packet vkFlushMappedMemoryRangesRemapAsInstanceSGHandle
        vktrace_trace_packet_header* pHeaderRemap = static_cast<vktrace_trace_packet_header*>(malloc((size_t)pHeader->size + offsetSize + sizeof(void **) * 6 + sizeof(void*) * pPacket->memoryRangeCount));
        memset(pHeaderRemap, 0, (size_t)pHeader->size + offsetSize + sizeof(void **) * 6 + sizeof(void*) * pPacket->memoryRangeCount);
        memcpy(pHeaderRemap, pHeader, sizeof(vktrace_trace_packet_header));
        pHeaderRemap->packet_id = VKTRACE_TPI_VK_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle;
        pHeaderRemap->size = pHeader->size + offsetSize + sizeof(void **) * 6 + sizeof(void*) * pPacket->memoryRangeCount;
        pHeaderRemap->pBody = (uintptr_t)(((char*)pHeaderRemap) + sizeof(vktrace_trace_packet_header));
        pHeaderRemap->next_buffers_offset = sizeof(vktrace_trace_packet_header) +
                                   ROUNDUP_TO_8(sizeof(packet_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle));  // Initial offset is from start of header to after the packet body.
        pPacketRemap = interpret_body_as_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle(pHeaderRemap);
        vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->pMemoryRanges), sizeof(VkMappedMemoryRange) * pPacket->memoryRangeCount, pPacket->pMemoryRanges);
        vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppData), sizeof(void*) * pPacket->memoryRangeCount, pPacket->ppData);
        vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataAsOffset), sizeof(void*) * pPacket->memoryRangeCount, AsOffsetArrays);
        vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataSGHandleOffset), sizeof(void*) * pPacket->memoryRangeCount, SGHandleOffsetArrays);
        vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataBufferOffset), sizeof(void*) * pPacket->memoryRangeCount, BufOffsetArrays);
        for (int i = 0; i < pPacket->memoryRangeCount; i++) {
            PageGuardChangedBlockInfo *p = (PageGuardChangedBlockInfo *)pPacket->ppData[i];
            int length = p[0].length + sizeof(PageGuardChangedBlockInfo) * (p[0].offset + 1);
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppData[i]), length, pPacket->ppData[i]);

            length = (AsOffsetArrays[i][0] + 1) * sizeof(uint32_t);
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataAsOffset[i]), length, AsOffsetArrays[i]);

            length = 1 * sizeof(uint64_t);
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataSGHandleOffset[i]), length, SGHandleOffsetArrays[i]);

            length = (BufOffsetArrays[i][0] + 1) * sizeof(uint32_t);
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataBufferOffset[i]), length, BufOffsetArrays[i]);
        }
        pPacketRemap->device = pPacket->device;
        pPacketRemap->memoryRangeCount = pPacket->memoryRangeCount;
        pPacketRemap->result = pPacket->result;

        pHeader = pHeaderRemap;

        vktrace_LogDebug("Found as instance or buffer device address in packet vkFlushMappedMemoryRanges, gid = %d, change it to a special one.", pHeader->global_packet_index);
    }

    for (int i = 0; i < pPacket->memoryRangeCount; ++i) {
        delete []AsOffsetArrays[i];
        delete []SGHandleOffsetArrays[i];
        delete []BufOffsetArrays[i];
    }
    delete []AsOffsetArrays;
    delete []SGHandleOffsetArrays;
    delete []BufOffsetArrays;

    if (foundAs || foundBufDeviceAddr)
        return finalize_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle(pHeader, pPacketRemap);
    return finalize_vkFlushMappedMemoryRanges(pHeader, pPacket);
}

int post_find_as_reference(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader) {
    if (pHeader->packet_id != VKTRACE_TPI_VK_vkFlushMappedMemoryRanges &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkGetAccelerationStructureDeviceAddressKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkGetBufferDeviceAddress &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkGetBufferDeviceAddressKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkGetBufferDeviceAddressEXT &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCreateBuffer &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyBuffer &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyAccelerationStructureKHR) {
        return 0;
    }

    bool recompress = false;
    if (pHeader->tracer_id == VKTRACE_TID_VULKAN_COMPRESSED) {
        if (g_compressor == nullptr) {
            g_compressor = create_compressor((VKTRACE_COMPRESS_TYPE)pFileHeader->compress_type);
            if (g_compressor == nullptr) {
                vktrace_LogError("Create compressor failed.");
                return -1;
            }
        }
        if (g_decompressor == nullptr) {
            g_decompressor = create_decompressor((VKTRACE_COMPRESS_TYPE)pFileHeader->compress_type);
            if (g_decompressor == nullptr) {
                vktrace_LogError("Create decompressor failed.");
                return -1;
            }
        }
        if (decompress_packet(g_decompressor, pHeader) < 0) {
            vktrace_LogError("Decompress the packet failed !");
            return -1;
        }
        recompress = true;
    }

    // duplicate a new packet
    uint64_t packetSize = pHeader->size;
    vktrace_trace_packet_header* pCopy = static_cast<vktrace_trace_packet_header*>(malloc((size_t)packetSize));
    if (pCopy != nullptr) {
        memcpy(pCopy, pHeader, (size_t)packetSize);
        pCopy->pBody = (uintptr_t)(((char*)pCopy) + sizeof(vktrace_trace_packet_header));
    } else {
        vktrace_LogError("post_rt(): malloc memory for new packet(pid=%d) fail!", pHeader->packet_id);
        return -1;
    }

    int ret = 0;
    switch (pHeader->packet_id)
    {
        case VKTRACE_TPI_VK_vkFlushMappedMemoryRanges: {
            ret = post_find_as_flush_memory(pFileHeader, pHeader);
            break;
        }
        case VKTRACE_TPI_VK_vkGetAccelerationStructureDeviceAddressKHR: {
            packet_vkGetAccelerationStructureDeviceAddressKHR* pPacket = interpret_body_as_vkGetAccelerationStructureDeviceAddressKHR(pCopy);
            VkDeviceAddress addr = pPacket->result;
            asAddressToAccelerationStructure[addr] = pPacket->pInfo->accelerationStructure;
            break;
        }
        case VKTRACE_TPI_VK_vkGetBufferDeviceAddress:
        case VKTRACE_TPI_VK_vkGetBufferDeviceAddressKHR:
        case VKTRACE_TPI_VK_vkGetBufferDeviceAddressEXT: {
            packet_vkGetBufferDeviceAddressKHR* pPacket = interpret_body_as_vkGetBufferDeviceAddressKHR(pCopy);
            VkDeviceAddress addr = pPacket->result;
            bufferAddressToBuffer[addr] = pPacket->pInfo->buffer;
            minTraceBufferDeviceAddress = minTraceBufferDeviceAddress < addr ? minTraceBufferDeviceAddress : addr;
            maxTraceBufferDeviceAddress = maxTraceBufferDeviceAddress > addr ? maxTraceBufferDeviceAddress : addr;
            break;
        }
        case VKTRACE_TPI_VK_vkCreateBuffer: {
            packet_vkCreateBuffer* pPacket = interpret_body_as_vkCreateBuffer(pCopy);
            traceBufferToSize[*pPacket->pBuffer] = pPacket->pCreateInfo->size;
            break;
        }
        case VKTRACE_TPI_VK_vkDestroyBuffer: {
            packet_vkDestroyBuffer* pPacket = interpret_body_as_vkDestroyBuffer(pCopy);
            if (traceBufferToSize.find(pPacket->buffer) != traceBufferToSize.end()) {
                traceBufferToSize.erase(pPacket->buffer);
            }
            for (auto iter = bufferAddressToBuffer.begin(); iter != bufferAddressToBuffer.end(); iter++) {
                if (iter->second == pPacket->buffer) {
                    bufferAddressToBuffer.erase(iter);
                    break;
                }
            }
            break;
        }
        case VKTRACE_TPI_VK_vkDestroyAccelerationStructureKHR: {
            packet_vkDestroyAccelerationStructureKHR* pPacket = interpret_body_as_vkDestroyAccelerationStructureKHR(pCopy);
            for (auto iter = asAddressToAccelerationStructure.begin(); iter != asAddressToAccelerationStructure.end(); iter++) {
                if (iter->second == pPacket->accelerationStructure) {
                    asAddressToAccelerationStructure.erase(iter);
                    break;
                }
            }
            break;
        }
        default:
            vktrace_LogError("post_find_as_reference(): unexpected API!");
            break;
    }
    if (ret == 0 && recompress) {
        ret = compress_packet(g_compressor, pHeader);
    }
    free(pCopy);

    return ret;
}
