#include <vector>
#include <unordered_map>

#include "vktrace_common.h"
#include "vktrace_trace_packet_utils.h"
#include "vktrace_vk_packet_id.h"
#include "vktrace_rq_pp.h"

using namespace std;

static unordered_map<VkDevice, unordered_map<VkDeviceMemory, mem_info>> g_vkDevice2Mem2MemInfo_unmap;
static unordered_map<VkDeviceAddress, VkAccelerationStructureKHR> asAddressToAccelerationStructure;
static unordered_map<VkDeviceAddress, VkBuffer> bufferAddressToBuffer;
static VkDeviceAddress minTraceBufferDeviceAddress = UINT64_MAX;
static VkDeviceAddress maxTraceBufferDeviceAddress = 0;
static unordered_map<VkBuffer, VkDeviceSize> traceBufferToSize;

static int pre_find_as_unmap_alloc_mem(vktrace_trace_packet_header*& pHeader) {
    packet_vkAllocateMemory* pPacket = interpret_body_as_vkAllocateMemory(pHeader);
    mem_info memInfo = {.device = pPacket->device,
                        .memory = *pPacket->pMemory,
                        .size = pPacket->pAllocateInfo->allocationSize,
                        .memoryTypeIndex = pPacket->pAllocateInfo->memoryTypeIndex,
                        .didFlush = NoFlush,
                        .rangeSize = 0,
                        .rangeOffset = 0};

    g_vkDevice2Mem2MemInfo_unmap[pPacket->device][*pPacket->pMemory] = memInfo;

    return 0;
}

int pre_find_as_unmap(vktrace_trace_file_header* pFileHeader, FileLike* traceFile) {
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

        if (pHeader->packet_id != VKTRACE_TPI_VK_vkAllocateMemory) {
            continue;
        }

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

        switch (pHeader->packet_id) {
            case VKTRACE_TPI_VK_vkAllocateMemory: {
                ret = pre_find_as_unmap_alloc_mem(pHeader);
                break;
            }
            default:
                // Should not be here
                vktrace_LogError("pre_fix_asbuffer_size_handle_packet(): unexpected API!");
                break;
        }
        vktrace_free(pHeader);

        if (ret != 0) {
            vktrace_LogError("Failed to handle packet %llu in pre_rq!", pHeader->global_packet_index);
            break;
        }
    }

    return ret;
}

static int finalize_vkUnmapMemory(vktrace_trace_packet_header* &pHeader, packet_vkUnmapMemory* pPacket, size_t siz)
{
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pData));
    return 0;
}

static int finalize_vkUnmapMemoryRemapAsInstanceSGHandle(vktrace_trace_packet_header* &pHeader, packet_vkUnmapMemoryRemapAsInstanceSGHandle* pPacket, size_t siz)
{
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pData));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pDataAsOffset));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pDataSGHandleOffset));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pDataBufferOffset));
    return 0;
}

extern int finalize_vkFlushMappedMemoryRanges(vktrace_trace_packet_header* &pHeader, packet_vkFlushMappedMemoryRanges* pPacket);

static int post_find_as_flush_memory(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader)
{
    packet_vkFlushMappedMemoryRanges* pPacket = interpret_body_as_vkFlushMappedMemoryRanges(pHeader);

    for (uint32_t i = 0; i < pPacket->memoryRangeCount; i++) {
        // update the memory flush info
        if (g_vkDevice2Mem2MemInfo_unmap.find(pPacket->device) != g_vkDevice2Mem2MemInfo_unmap.end() &&
            g_vkDevice2Mem2MemInfo_unmap[pPacket->device].find((pPacket->pMemoryRanges[i]).memory) != g_vkDevice2Mem2MemInfo_unmap[pPacket->device].end()) {
            mem_info& memInfo = g_vkDevice2Mem2MemInfo_unmap[pPacket->device][(pPacket->pMemoryRanges[i]).memory];
            memInfo.didFlush = ApiFlush;
        }
    }
    return finalize_vkFlushMappedMemoryRanges(pHeader, pPacket);
}

static int post_find_as_map_memory(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader)
{
    packet_vkMapMemory* pPacket = interpret_body_as_vkMapMemory(pHeader);

    // update memory size info
    VkDeviceSize rangeSize  = pPacket->size;
    VkDeviceSize offset     = pPacket->offset;
    if (g_vkDevice2Mem2MemInfo_unmap.find(pPacket->device) != g_vkDevice2Mem2MemInfo_unmap.end() &&
        g_vkDevice2Mem2MemInfo_unmap[pPacket->device].find(pPacket->memory) != g_vkDevice2Mem2MemInfo_unmap[pPacket->device].end()) {
        mem_info& memInfo = g_vkDevice2Mem2MemInfo_unmap[pPacket->device][pPacket->memory];
        VkDeviceSize totalSize = memInfo.size;
        if (rangeSize == VK_WHOLE_SIZE) {
            rangeSize = totalSize - offset;
        }
        memInfo.rangeSize = rangeSize;
    }
    else {
        vktrace_LogError("gid %llu, can't find the memory info when post processing vkMapMemory!", pHeader->global_packet_index);
    }

    vktrace_finalize_buffer_address(pHeader, (void **)&(pPacket->ppData));
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

static int post_find_as_unmap_memory(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader)
{
    packet_vkUnmapMemory* pPacket = interpret_body_as_vkUnmapMemory(pHeader);

    bool foundAs = false;
    bool foundBufDeviceAddr = false;
    // look for AS
    VkDeviceMemory memory   = pPacket->memory;
    uint64_t siz            = pHeader->size;
    uint64_t pData_size     = 0;
    if (g_vkDevice2Mem2MemInfo_unmap.find(pPacket->device) != g_vkDevice2Mem2MemInfo_unmap.end() &&
        g_vkDevice2Mem2MemInfo_unmap[pPacket->device].find(pPacket->memory) != g_vkDevice2Mem2MemInfo_unmap[pPacket->device].end()) {
        mem_info& memInfo = g_vkDevice2Mem2MemInfo_unmap[pPacket->device][pPacket->memory];
        if (memInfo.didFlush == NoFlush) {
            pData_size = memInfo.rangeSize;
        }
    } else {
        vktrace_LogError("gid %llu, can't find the memory info when post processing vkUnmapMemory!", pHeader->global_packet_index);
        finalize_vkUnmapMemory(pHeader, pPacket, pData_size);
        return -1;
    }

    uint64_t pDataCalcSize = siz - (ROUNDUP_TO_8(sizeof(vktrace_trace_packet_header) + ROUNDUP_TO_8(sizeof(packet_vkUnmapMemory)) + sizeof(uint32_t)));
    pData_size = pData_size < pDataCalcSize ? pData_size : pDataCalcSize;

    uint32_t *AsOffsetArrays = nullptr;
    uint32_t *BufOffsetArrays = nullptr;
    uint64_t *SGHandleOffsetArrays = nullptr;
    int offsetSize = 0;

    if(pPacket->pData != nullptr) {
        vector<uint32_t> offsetAsDeviceAddr;
        vector<uint32_t> offsetBufDeviceAddr;
        // traverse pData
        uint64_t *temp = (uint64_t *)(pPacket->pData);
        for(size_t j = 0; j < pData_size / sizeof(uint64_t); j++) {
            VkDeviceAddress addr = *(uint64_t*)(temp + j);
            if (addr == 0) {
                continue;
            }

            if (asAddressToAccelerationStructure.find(addr) != asAddressToAccelerationStructure.end()) {
                foundAs = true;
                offsetAsDeviceAddr.push_back(j * sizeof(uint64_t));
                continue;
            }

            if ((addr < minTraceBufferDeviceAddress) || (addr > (maxTraceBufferDeviceAddress + MAX_BUFFER_DEVICEADDRESS_SIZE))) {
                continue;
            }

            if (processBufDeviceAddr) {
                if (bufferAddressToBuffer.find(addr) != bufferAddressToBuffer.end()) {
                    foundBufDeviceAddr = true;
                    offsetBufDeviceAddr.push_back(j * sizeof(uint64_t));
                } else {
                    if (findClosestAddress(addr) != bufferAddressToBuffer.end()) {
                        foundBufDeviceAddr = true;
                        offsetBufDeviceAddr.push_back(j * sizeof(uint64_t));
                    }
                }
            }
        }

        AsOffsetArrays = new uint32_t[offsetAsDeviceAddr.size() + 1];
        AsOffsetArrays[0] = offsetAsDeviceAddr.size();
        memcpy(AsOffsetArrays + 1, offsetAsDeviceAddr.data(), offsetAsDeviceAddr.size() * sizeof(uint32_t));
        offsetSize += (offsetAsDeviceAddr.size() + 1) * sizeof(uint32_t);

        BufOffsetArrays = new uint32_t[offsetBufDeviceAddr.size() + 1];
        BufOffsetArrays[0] = offsetBufDeviceAddr.size();
        memcpy(BufOffsetArrays + 1, offsetBufDeviceAddr.data(), offsetBufDeviceAddr.size() * sizeof(uint32_t));
        offsetSize += (offsetBufDeviceAddr.size() + 1) * sizeof(uint32_t);

        SGHandleOffsetArrays = new uint64_t[1];
        SGHandleOffsetArrays[0] = 0;
        offsetSize += 1 * sizeof(uint64_t);
    }

    packet_vkUnmapMemoryRemapAsInstanceSGHandle* pPacketRemap = nullptr;
    if (foundAs || foundBufDeviceAddr) {
        // duplicate a new packet vkUnmapMemoryRemapAsInstanceSGHandle
        vktrace_trace_packet_header* pHeaderRemap = static_cast<vktrace_trace_packet_header*>(malloc(siz + offsetSize + sizeof(void *) * 3));
        memset(pHeaderRemap, 0, siz + offsetSize + sizeof(void *) * 3);
        memcpy(pHeaderRemap, pHeader, sizeof(vktrace_trace_packet_header));
        pHeaderRemap->packet_id = VKTRACE_TPI_VK_vkUnmapMemoryRemapAsInstanceSGHandle;
        pHeaderRemap->size = siz + offsetSize + sizeof(void *) * 3;
        pHeaderRemap->pBody = (uintptr_t)(((char*)pHeaderRemap) + sizeof(vktrace_trace_packet_header));
        pHeaderRemap->next_buffers_offset = sizeof(vktrace_trace_packet_header) +
                                   ROUNDUP_TO_8(sizeof(packet_vkUnmapMemoryRemapAsInstanceSGHandle));  // Initial offset is from start of header to after the packet body.
        pPacketRemap = interpret_body_as_vkUnmapMemoryRemapAsInstanceSGHandle(pHeaderRemap);
        if (pData_size) {
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->pData), pData_size, pPacket->pData);
            uint64_t length = ((AsOffsetArrays[0] + 1) * sizeof(uint32_t));
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->pDataAsOffset), length, AsOffsetArrays);
            length = 1 * sizeof(uint64_t);
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->pDataSGHandleOffset), length, SGHandleOffsetArrays);
            length = (BufOffsetArrays[0] + 1) * sizeof(uint32_t);
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->pDataBufferOffset), length, BufOffsetArrays);
        }
        pPacketRemap->device = pPacket->device;
        pPacketRemap->memory = pPacket->memory;
        pHeader = pHeaderRemap;

        delete []AsOffsetArrays;
        vktrace_LogDebug("Found as instance or buffer device address in packet vkUnmapMemory, gid = %d, change it to a special one.", pHeader->global_packet_index);
    }

    if (foundAs || foundBufDeviceAddr)
        return finalize_vkUnmapMemoryRemapAsInstanceSGHandle(pHeader, pPacketRemap, pData_size);
    return finalize_vkUnmapMemory(pHeader, pPacket, pData_size);
}

int post_find_as_unmap(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header*& pHeader) {
    if (pHeader->packet_id != VKTRACE_TPI_VK_vkMapMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkUnmapMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkFlushMappedMemoryRanges &&
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
    switch (pHeader->packet_id) {
        case VKTRACE_TPI_VK_vkMapMemory: {
            ret = post_find_as_map_memory(pFileHeader, pCopy);
            break;
        }
        case VKTRACE_TPI_VK_vkUnmapMemory: {
            ret = post_find_as_unmap_memory(pFileHeader, pHeader);
            break;
        }
        case VKTRACE_TPI_VK_vkFlushMappedMemoryRanges: {
            ret = post_find_as_flush_memory(pFileHeader, pCopy);
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
            vktrace_LogError("post_find_as_unmap(): unexpected API!");
            break;
    }

    if (ret == 0 && recompress) {
        ret = compress_packet(g_compressor, pHeader);
    }
    free(pCopy);

    return ret;
}
