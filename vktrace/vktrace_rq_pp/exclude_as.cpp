#include <unordered_map>

#include "vktrace_pageguard_memorycopy.h"
#include "vktrace_vk_packet_id.h"
#include "vktrace_rq_pp.h"

using namespace std;

typedef struct as_info {
    VkAccelerationStructureKHR as;
    VkBuffer buffer;
    VkDevice device;
    VkDeviceMemory memory;
    VkDeviceSize offsetInBuf;
    VkDeviceSize offsetInMem;
    VkDeviceSize size;
} as_info;

static unordered_map<VkDevice, unordered_map<VkAccelerationStructureKHR, as_info>> g_vkDevice2As2AsInfo;
static unordered_map<VkDevice, unordered_map<VkBuffer, buffer_info>> g_vkDevice2Buf2BufInfo_rmid;

static int post_rmid_create_buffer(vktrace_trace_packet_header* &pHeader)
{
    packet_vkCreateBuffer* pPacket = interpret_body_as_vkCreateBuffer(pHeader);
    buffer_info     bufInfo = { .device = pPacket->device,
                                .buffer = *pPacket->pBuffer,
                                .size = pPacket->pCreateInfo->size,
                                .memory = VK_NULL_HANDLE,
                                .memoryOffset = 0 };
    g_vkDevice2Buf2BufInfo_rmid[pPacket->device][bufInfo.buffer]=bufInfo;
    return 0;
}

static int post_rmid_bind_buffer_memory(vktrace_trace_packet_header* &pHeader)
{
    packet_vkBindBufferMemory* pPacket = interpret_body_as_vkBindBufferMemory(pHeader);
    buffer_info &bufInfo = g_vkDevice2Buf2BufInfo_rmid[pPacket->device][pPacket->buffer];

    bufInfo.memory = pPacket->memory;
    bufInfo.memoryOffset = pPacket->memoryOffset;
    return 0;
}

static int post_rmid_bind_buffer_memory2(vktrace_trace_packet_header* &pHeader)
{
    packet_vkBindBufferMemory2* pPacket = interpret_body_as_vkBindBufferMemory2(pHeader);
    for (uint32_t index = 0; index < pPacket->bindInfoCount; index++) {
        buffer_info &bufInfo = g_vkDevice2Buf2BufInfo_rmid[pPacket->device][pPacket->pBindInfos[index].buffer];
        bufInfo.memory = pPacket->pBindInfos[index].memory;
        bufInfo.memoryOffset = pPacket->pBindInfos[index].memoryOffset;
    }
    return 0;
}

static int post_rmid_destroy_buffer(vktrace_trace_packet_header* &pHeader)
{
    packet_vkDestroyBuffer* pPacket = interpret_body_as_vkDestroyBuffer(pHeader);
    g_vkDevice2Buf2BufInfo_rmid[pPacket->device].erase(pPacket->buffer);
    return 0;
}

static int post_rmid_create_as(vktrace_trace_packet_header* &pHeader)
{
    packet_vkCreateAccelerationStructureKHR* pPacket = interpret_body_as_vkCreateAccelerationStructureKHR(pHeader);
    as_info asInfo = {  .as = *pPacket->pAccelerationStructure,
                        .buffer = pPacket->pCreateInfo->buffer,
                        .device = pPacket->device,
                        .memory = g_vkDevice2Buf2BufInfo_rmid[pPacket->device][pPacket->pCreateInfo->buffer].memory,
                        .offsetInBuf = pPacket->pCreateInfo->offset,
                        .offsetInMem = g_vkDevice2Buf2BufInfo_rmid[pPacket->device][pPacket->pCreateInfo->buffer].memoryOffset + pPacket->pCreateInfo->offset,
                        .size   = pPacket->pCreateInfo->size };
    g_vkDevice2As2AsInfo[pPacket->device][*pPacket->pAccelerationStructure] = asInfo;
    return 0;
}

static int post_rmid_destroy_as(vktrace_trace_packet_header* &pHeader)
{
    packet_vkDestroyAccelerationStructureKHR* pPacket = interpret_body_as_vkDestroyAccelerationStructureKHR(pHeader);
    g_vkDevice2As2AsInfo[pPacket->device].erase(pPacket->accelerationStructure);
    return 0;
}

static int post_rmid_unmap(vktrace_trace_packet_header* &pHeader)
{
    packet_vkUnmapMemory* pPacket =interpret_body_as_vkUnmapMemory(pHeader);
    // TODO
    return 0;
}

extern int finalize_vkFlushMappedMemoryRanges(vktrace_trace_packet_header* &pHeader, packet_vkFlushMappedMemoryRanges* pPacket);

static int post_rmid_flush_memory(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader)
{
    packet_vkFlushMappedMemoryRanges* pPacket = interpret_body_as_vkFlushMappedMemoryRanges(pHeader);

    // check whether it's a private packet for page guard data update
    // only remove invalid data from a private packet
    // we use page guard to generate private packet, which is always page aligned
    // but in some cases we do know we shouldn't update all page data
    // e.g. if a page contains the acceleration structure, which should always be touched
    PageGuardChangedBlockInfo* pChangedInfoArray = (PageGuardChangedBlockInfo*)(pPacket->ppData[0]);
    bool bprivate = false;
    if ((pChangedInfoArray == nullptr) ||
        ((static_cast<uint64_t>(pChangedInfoArray[0].reserve0)) & PAGEGUARD_SPECIAL_FORMAT_PACKET_FOR_VKFLUSHMAPPEDMEMORYRANGES)) {
        bprivate = true;
    }

    // only scan private data
    if (!bprivate) {
        vktrace_LogDebug("It's not a private vkFlushMappedMemoryRanges() call, pid = %llu.", pHeader->global_packet_index);
        return finalize_vkFlushMappedMemoryRanges(pHeader, pPacket);
    }

    if (!vktrace_check_min_version(VKTRACE_TRACE_FILE_VERSION_5)) {
        vktrace_LogAlways("The trace file version is less than 5. Doesn't support.");
        return finalize_vkFlushMappedMemoryRanges(pHeader, pPacket);
    }

    unordered_map<VkAccelerationStructureKHR, as_info> &as2asInfo = g_vkDevice2As2AsInfo[pPacket->device];
    for (unsigned j = 0; j < pPacket->memoryRangeCount; j++) {
        if (pPacket->ppData[j] != nullptr) {
            PageGuardChangedBlockInfo* pChangedInfoArray = (PageGuardChangedBlockInfo*)(pPacket->ppData[j]);
            if (pChangedInfoArray[0].length) {
                PBYTE pChangedData = (PBYTE)(pChangedInfoArray) + sizeof(PageGuardChangedBlockInfo) * (pChangedInfoArray[0].offset + 1);
                size_t CurrentOffset = 0;
                for (size_t i = 0; i < pChangedInfoArray[0].offset; i++) {
                    if (pChangedInfoArray[i + 1].length) {
                        for (auto iter = as2asInfo.begin(); iter != as2asInfo.end(); iter++) {
                            const as_info &asInfo = iter->second;
                            const VkMappedMemoryRange &pMemRange = pPacket->pMemoryRanges[j];
                            if (pMemRange.memory == iter->second.memory) {
                                uint32_t data_begin = pChangedInfoArray[i + 1].offset;
                                uint32_t data_end   = pChangedInfoArray[i + 1].offset + pChangedInfoArray[i + 1].length;
                                uint32_t as_begin = asInfo.offsetInMem;
                                uint32_t as_end = as_begin + asInfo.size;
                                if (data_begin <= as_begin && as_begin < data_end && data_end <= as_end) {
                                    /********************************************************************************
                                     *                                                                              *
                                     * data_begin |-----------------------| data_end                                *
                                     *                                                                              *
                                     *               as_begin |-------------------------------------------| as_end  *
                                     *                                                                              *
                                     ********************************************************************************/
                                    // adjust data end to be out of as
                                    memmove(pChangedData + CurrentOffset + as_begin - data_begin,
                                            pChangedData + CurrentOffset + pChangedInfoArray[i + 1].length,
                                            pChangedInfoArray[0].length - CurrentOffset - pChangedInfoArray[i + 1].length);
                                    data_end = as_begin;
                                    pChangedInfoArray[0].length -= (pChangedInfoArray[i + 1].length - (data_end - data_begin));
                                    pChangedInfoArray[i + 1].length = data_end - data_begin;
                                    vktrace_LogDebug("gid = %d, adjust data end to be out of AS", pPacket->header->global_packet_index);
                                }
                                else if (as_begin <= data_begin && data_begin < as_end && as_end <= data_end) {
                                    /********************************************************************************
                                     *                                                                              *
                                     *               data_begin |---------------------------------------| data_end  *
                                     *                                                                              *
                                     * as_begin |-----------------------| as_end                                    *
                                     *                                                                              *
                                     ********************************************************************************/
                                    // adjust data begin to be out of as
                                    memmove(pChangedData + CurrentOffset,
                                            pChangedData + CurrentOffset + as_end - data_begin,
                                            pChangedInfoArray[0].length - CurrentOffset - (as_end - data_begin));
                                    data_begin = as_end;
                                    pChangedInfoArray[i + 1].offset = data_begin;
                                    pChangedInfoArray[0].length -= (pChangedInfoArray[i + 1].length - (data_end - data_begin));
                                    pChangedInfoArray[i + 1].length = data_end - data_begin;
                                    vktrace_LogDebug("gid = %d, adjust data begin to be out of AS", pPacket->header->global_packet_index);
                                }
                                else if (data_begin < as_begin && data_end > as_end) {
                                    /********************************************************************************
                                     *                                                                              *
                                     *    data_begin |---------------------------------------| data_end             *
                                     *                                                                              *
                                     *          as_begin |-----------------------| as_end                           *
                                     *                                                                              *
                                     ********************************************************************************/
                                    // we have to split the data into two parts
                                    // TODO
                                    vktrace_LogError("We have to split the data into two parts, not supported!");
                                    assert (0);
                                }
                            }
                        }
                    }
                    CurrentOffset += pChangedInfoArray[i + 1].length;
                }
            }
        }
    }
    return finalize_vkFlushMappedMemoryRanges(pHeader, pPacket);
}

int post_rm_invalid_data(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader) {
    if (pHeader->packet_id != VKTRACE_TPI_VK_vkCreateBuffer &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory2 &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory2KHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyBuffer &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCreateAccelerationStructureKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyAccelerationStructureKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkUnmapMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkFlushMappedMemoryRanges) {
        return 0;
    }

    if (pHeader->packet_id == VKTRACE_TPI_VK_vkUnmapMemory ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkFlushMappedMemoryRanges) {
        if (g_params.paramFlag && strcmp(g_params.paramFlag, "-range") == 0) {
            if (pHeader->global_packet_index < g_params.srcGlobalPacketIndexStart ||
                pHeader->global_packet_index > g_params.srcGlobalPacketIndexEnd) {
                return 0;
            }
        }
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
        vktrace_LogError("post_rm_invalid_data(): malloc memory for new packet(pid=%d) fail!", pHeader->packet_id);
        return -1;
    }

    int ret = 0;
    switch (pHeader->packet_id)
    {
    case VKTRACE_TPI_VK_vkCreateBuffer: {
        ret = post_rmid_create_buffer(pCopy);
        break;
    }
    case VKTRACE_TPI_VK_vkBindBufferMemory: {
        ret = post_rmid_bind_buffer_memory(pCopy);
        break;
    }
    case VKTRACE_TPI_VK_vkBindBufferMemory2:
    case VKTRACE_TPI_VK_vkBindBufferMemory2KHR: {
        ret = post_rmid_bind_buffer_memory2(pCopy);
        break;
    }
    case VKTRACE_TPI_VK_vkDestroyBuffer: {
        ret = post_rmid_destroy_buffer(pCopy);
        break;
    }
    case VKTRACE_TPI_VK_vkCreateAccelerationStructureKHR: {
        ret = post_rmid_create_as(pCopy);
        break;
    }
    case VKTRACE_TPI_VK_vkDestroyAccelerationStructureKHR: {
        ret = post_rmid_destroy_as(pCopy);
        break;
    }
    case VKTRACE_TPI_VK_vkUnmapMemory: {
        ret = post_rmid_unmap(pCopy);
        break;
    }
    case VKTRACE_TPI_VK_vkFlushMappedMemoryRanges: {
        ret = post_rmid_flush_memory(pFileHeader, pHeader);
        break;
    }
    default:
        // Should not be here
        vktrace_LogError("post_rm_invalid_data(): unexpected API!");
        break;
    }

    if (ret == 0 && recompress) {
        ret = compress_packet(g_compressor, pHeader);
    }
    free(pCopy);

    return ret;
}

