#include <set>
#include <vector>
#include <unordered_map>

#include "vktrace_trace_packet_utils.h"
#include "vktrace_rq_pp.h"
#include "vktrace_vk_packet_id.h"
#include "vktrace_pageguard_memorycopy.h"

using namespace std;

struct SBT_info {
    VkDeviceMemory mem;
    VkDeviceAddress addr;
    bool mapped;
    vector<VkBuffer> srcBuffers;
};

struct memInfo {
    VkDeviceMemory mem;
    vector<uint64_t> flushIndices;
    vector<uint64_t> unmapIndices;
};
unordered_map<VkBuffer, memInfo> allBuffers;

VkPhysicalDeviceRayTracingPipelinePropertiesKHR physicalRtProperties;

unordered_map<VkBuffer, SBT_info> sbtBufferMemory;
unordered_map<VkDeviceAddress, VkBuffer> sbtAddressToBuffer;

unordered_map<VkPipeline, vector<string>> sbtHandles;
unordered_map<VkPipeline, vector<string>> sbtHandlesPost;

set<uint64_t> sbtFlushIndices;
set<uint64_t> sbtUnmapIndices;

void initString(string &s) {
    const char *deadbeef = "DEADBEEF";
    for (int i = 0; i < s.length(); ++i) {
        s[i] = deadbeef[i % 8];
    }
}

int pre_find_sbt(vktrace_trace_file_header* pFileHeader, FileLike *traceFile) {
    int ret = 0;
    for (auto it_gid = g_globalPacketIndexList.begin(); it_gid != g_globalPacketIndexList.end(); ++it_gid) {
        uint64_t gpIndex = (uint64_t)*it_gid;
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

        switch (pHeader->packet_id) {
            case VKTRACE_TPI_VK_vkCreateBuffer: {
                packet_vkCreateBuffer* pPacket = interpret_body_as_vkCreateBuffer(pHeader);
                allBuffers[*pPacket->pBuffer] = {0};
                if (pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) {
                    sbtBufferMemory[*pPacket->pBuffer] = {0, 0, 0};
                }
                break;
            }
            case VKTRACE_TPI_VK_vkBindBufferMemory: {
                packet_vkBindBufferMemory* pPacket = interpret_body_as_vkBindBufferMemory(pHeader);
                auto it2 = allBuffers.find(pPacket->buffer);
                if (it2 == allBuffers.end()) {
                    vktrace_LogError("Failed to find buffer %p when handling vkBindBufferMemory!", pPacket->buffer);
                    return -1;
                }
                it2->second.mem = pPacket->memory;
                auto it = sbtBufferMemory.find(pPacket->buffer);
                if (it != sbtBufferMemory.end()) {
                    it->second.mem = pPacket->memory;
                }
                VkDeviceAddress addr = pPacket->result;
                break;
            }
            case VKTRACE_TPI_VK_vkBindBufferMemory2: {
                packet_vkBindBufferMemory2* pPacket = interpret_body_as_vkBindBufferMemory2(pHeader);
                for (int i = 0; i < pPacket->bindInfoCount; ++i) {
                    auto it2 = allBuffers.find(pPacket->pBindInfos->buffer);
                    if (it2 == allBuffers.end()) {
                        vktrace_LogError("Failed to find buffer %p when handling vkBindBufferMemory!", pPacket->pBindInfos->buffer);
                        return -1;
                    }
                    it2->second.mem = pPacket->pBindInfos->memory;
                    auto it = sbtBufferMemory.find(pPacket->pBindInfos->buffer);
                    if (it != sbtBufferMemory.end()) {
                        it->second.mem = pPacket->pBindInfos->memory;
                    }
                    VkDeviceAddress addr = pPacket->result;
                }
                break;
            }
            case VKTRACE_TPI_VK_vkDestroyBuffer: {
                packet_vkDestroyBuffer* pPacket = interpret_body_as_vkDestroyBuffer(pHeader);
                auto it = sbtBufferMemory.find(pPacket->buffer);
                if (it != sbtBufferMemory.end()) {
                    sbtBufferMemory.erase(it);
                    auto it2 = sbtAddressToBuffer.begin();
                    while (it2 != sbtAddressToBuffer.end()) {
                        if (it2->second == pPacket->buffer) {
                            it2 = sbtAddressToBuffer.erase(it2);
                        }
                        else {
                            ++it2;
                        }
                    }
                }
                auto it3 = allBuffers.find(pPacket->buffer);
                if (it3 != allBuffers.end()) {
                    allBuffers.erase(it3);
                }
                break;
            }
            case VKTRACE_TPI_VK_vkGetBufferDeviceAddress:
            case VKTRACE_TPI_VK_vkGetBufferDeviceAddressKHR: {
                packet_vkGetBufferDeviceAddress* pPacket = interpret_body_as_vkGetBufferDeviceAddress(pHeader);
                auto it = sbtBufferMemory.find(pPacket->pInfo->buffer);
                if (it != sbtBufferMemory.end()) {
                    it->second.addr = pPacket->result;
                    sbtAddressToBuffer[pPacket->result] = pPacket->pInfo->buffer;
                }
                break;
            }
            case VKTRACE_TPI_VK_vkGetPhysicalDeviceProperties2:
            case VKTRACE_TPI_VK_vkGetPhysicalDeviceProperties2KHR: {
                packet_vkGetPhysicalDeviceProperties2KHR* pPacket = interpret_body_as_vkGetPhysicalDeviceProperties2KHR(pHeader);
                for (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *p = (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *)pPacket->pProperties->pNext;
                        p != NULL; p = (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *)p->pNext) {
                    if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR) {
                        physicalRtProperties = *p;
                    }
                }
                break;
            }
            case VKTRACE_TPI_VK_vkCreateRayTracingPipelinesKHR: {
                packet_vkCreateRayTracingPipelinesKHR* pPacket = interpret_body_as_vkCreateRayTracingPipelinesKHR(pHeader);
                for (int i = 0; i < pPacket->createInfoCount; ++i) {
                    vector<string> handles(pPacket->pCreateInfos[i].groupCount);
                    for (int j = 0; j < handles.size(); ++j) {
                        handles[j].resize(physicalRtProperties.shaderGroupHandleSize);
                        initString(handles[j]);
                    }
                    sbtHandles[pPacket->pPipelines[i]] = handles;
                }
                break;
            }
            case VKTRACE_TPI_VK_vkGetRayTracingShaderGroupHandlesKHR: {
                packet_vkGetRayTracingShaderGroupHandlesKHR* pPacket = interpret_body_as_vkGetRayTracingShaderGroupHandlesKHR(pHeader);
                const int handleSize = physicalRtProperties.shaderGroupHandleSize;

                auto it = sbtHandles.find(pPacket->pipeline);
                if (it == sbtHandles.end()) {
                    vktrace_LogError("Failed to find pipeline %p when handling vkGetRayTracingShaderGroupHandlesKHR!", pPacket->pipeline);
                    ret = -1;
                    break;
                }
                vector<string> &handles = it->second;
                char *p = (char *)pPacket->pData;
                for (int i = pPacket->firstGroup; i < pPacket->firstGroup + pPacket->groupCount; ++i) {
                    for (int j = 0; j < handleSize; ++j) {
                        handles[i][j] = p[(i - pPacket->firstGroup) * handleSize + j];
                    }
                }
                break;
            }
            case VKTRACE_TPI_VK_vkMapMemory: {
                packet_vkMapMemory* pPacket = interpret_body_as_vkMapMemory(pHeader);
                for (auto it = sbtBufferMemory.begin(); it != sbtBufferMemory.end(); ++it) {
                    if (it->second.mem == pPacket->memory) {
                        it->second.mapped = true;
                    }
                }
                break;
            }
            case VKTRACE_TPI_VK_vkCmdCopyBuffer: {
                packet_vkCmdCopyBuffer* pPacket = interpret_body_as_vkCmdCopyBuffer(pHeader);
                auto it = sbtBufferMemory.find(pPacket->dstBuffer);
                // If dstBuffer is a shader binding table buffer, srcBuffer must be a staging buffer.
                // So we might be able to find shader group handles in the data written to the latter one
                // (either in vkFlushMappedMemoryRanges or in vkUnmapMemory).
                if (it != sbtBufferMemory.end()) {
                    it->second.srcBuffers.push_back(pPacket->srcBuffer);
                    auto it2 = allBuffers.find(pPacket->srcBuffer);
                    if (it2 != allBuffers.end()) {
                        for (int i = 0; i < it2->second.flushIndices.size(); ++i) {
                            sbtFlushIndices.insert(it2->second.flushIndices[i]);
                        }
                        for (int i = 0; i < it2->second.unmapIndices.size(); ++i) {
                            sbtUnmapIndices.insert(it2->second.unmapIndices[i]);
                        }
                    }
                }
                break;
            }
            case VKTRACE_TPI_VK_vkFlushMappedMemoryRanges: {
                packet_vkFlushMappedMemoryRanges* pPacket = interpret_body_as_vkFlushMappedMemoryRanges(pHeader);
                for (int i = 0; i < pPacket->memoryRangeCount; ++i) {
                    for (auto it = allBuffers.begin(); it != allBuffers.end(); ++it) {
                        if (it->second.mem == pPacket->pMemoryRanges[i].memory) {
                            it->second.flushIndices.push_back(pHeader->global_packet_index);
                        }
                    }
                    for (auto it = sbtBufferMemory.begin(); it != sbtBufferMemory.end(); ++it) {
                        // If this is a flushing to a shader binding table buffer,
                        // we might be able to find shader group handles in the data written to it.
                        if (it->second.mem == pPacket->pMemoryRanges[i].memory) {
                            sbtFlushIndices.insert(pHeader->global_packet_index);
                        }
                    }
                }
                break;
            }
            case VKTRACE_TPI_VK_vkUnmapMemory: {
                packet_vkUnmapMemory* pPacket = interpret_body_as_vkUnmapMemory(pHeader);
                for (auto it = allBuffers.begin(); it != allBuffers.end(); ++it) {
                    if (it->second.mem == pPacket->memory) {
                        it->second.unmapIndices.push_back(pHeader->global_packet_index);
                    }
                }
                for (auto it = sbtBufferMemory.begin(); it != sbtBufferMemory.end(); ++it) {
                    if (it->second.mem == pPacket->memory) {
                        sbtUnmapIndices.insert(pHeader->global_packet_index);
                    }
                }
                break;
            }
        }
        vktrace_free(pHeader);
    }

    return ret;
}

static bool findSbt(int memoryRangeCount, void **ppData, uint64_t **offsetArrays, int &offsetSize)
{
    bool foundSbt = false;
    for (uint32_t i = 0; i < memoryRangeCount; i++) {
        PageGuardChangedBlockInfo *p = (PageGuardChangedBlockInfo *)ppData[i];
        int segment = p[0].offset;
        PBYTE pChangedData = (PBYTE)(p) + sizeof(PageGuardChangedBlockInfo) * (p[0].offset + 1);
        size_t CurrentOffset = 0;
        vector<uint64_t> offset;
        for (size_t j = 0; j < segment; j++) {
            uint32_t *temp = (uint32_t *)(pChangedData + CurrentOffset);
            const int handleSize = physicalRtProperties.shaderGroupHandleSize;
            char *tmp = (char *)(pChangedData + CurrentOffset);
            for (size_t k = 0; k <= p[j + 1].length - handleSize; ++k) {
                string s(handleSize, ' ');
                for (size_t m = 0; m < handleSize; ++m) {
                    s[m] = tmp[k + m];
                }
                for (auto it = sbtHandlesPost.begin(); it != sbtHandlesPost.end(); ++it) {
                    vector<string> &handles = it->second;
                    for (size_t m = 0; m < handles.size(); ++m) {
                        if (handles[m] == s) {
                            foundSbt = true;
                            uint64_t combinedValue = CurrentOffset + k;
                            combinedValue <<= 32;
                            combinedValue |= m;
                            offset.push_back(combinedValue);
                            offset.push_back((uint64_t)(it->first));
                        }
                    }
                }
            }
            CurrentOffset += p[j + 1].length;
        }
        if (foundSbt) {
            offsetArrays[i] = new uint64_t[offset.size() + 1];
            offsetArrays[i][0] = offset.size() / 2;
            memcpy(offsetArrays[i] + 1, offset.data(), offset.size() * sizeof(uint64_t));
            offsetSize += (offset.size() + 1) * sizeof(uint64_t);
        }
    }
    return foundSbt;
}

extern int finalize_vkFlushMappedMemoryRanges(vktrace_trace_packet_header* &pHeader, packet_vkFlushMappedMemoryRanges* pPacket);
extern int finalize_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle(vktrace_trace_packet_header* &pHeader, packet_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle* pPacket);

static int post_sbt_flush_memory(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader)
{
    auto it = sbtFlushIndices.find(pHeader->global_packet_index);
    if (it == sbtFlushIndices.end())
        return 0;

    packet_vkFlushMappedMemoryRanges* pPacket = interpret_body_as_vkFlushMappedMemoryRanges(pHeader);

    uint32_t **AsOffsetArrays = new uint32_t*[pPacket->memoryRangeCount];
    uint64_t **SGHandleOffsetArrays = new uint64_t*[pPacket->memoryRangeCount];
    uint32_t **BufOffsetArrays = new uint32_t*[pPacket->memoryRangeCount];
    int offsetSize = 0;
    bool foundSbt = findSbt(pPacket->memoryRangeCount, pPacket->ppData, SGHandleOffsetArrays, offsetSize);

    packet_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle* pPacketRemap = NULL;
    if (foundSbt) {
        for (int i = 0; i < pPacket->memoryRangeCount; ++i) {
            AsOffsetArrays[i] = new uint32_t[1];
            AsOffsetArrays[i][0] = 0;
            offsetSize += 1 * sizeof(uint32_t);

            BufOffsetArrays[i] = new uint32_t[1];
            BufOffsetArrays[i][0] = 0;
            offsetSize += 1 * sizeof(uint32_t);
        }
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

            length = 1 * sizeof(uint32_t);
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataAsOffset[i]), length, AsOffsetArrays[i]);

            length = ((SGHandleOffsetArrays[i][0] * 2 + 1) * sizeof(uint64_t));
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataSGHandleOffset[i]), length, SGHandleOffsetArrays[i]);

            length = 1 * sizeof(uint32_t);
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataBufferOffset[i]), length, BufOffsetArrays[i]);
        }
        pPacketRemap->device = pPacket->device;
        pPacketRemap->memoryRangeCount = pPacket->memoryRangeCount;
        pPacketRemap->result = pPacket->result;

        pHeader = pHeaderRemap;

        vktrace_LogDebug("Found shader group handles in packet vkFlushMappedMemoryRanges, gid = %d, change it to a special one.", pHeader->global_packet_index);

        for (int i = 0; i < pPacket->memoryRangeCount; ++i) {
            delete []AsOffsetArrays[i];
            delete []SGHandleOffsetArrays[i];
        }
    }
    delete []AsOffsetArrays;
    delete []SGHandleOffsetArrays;

    if (foundSbt)
        return finalize_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle(pHeader, pPacketRemap);
    return finalize_vkFlushMappedMemoryRanges(pHeader, pPacket);
}

static int post_sbt_flush_memory_as_sbt(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader)
{
    auto it = sbtFlushIndices.find(pHeader->global_packet_index);
    if (it == sbtFlushIndices.end())
        return 0;

    packet_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle* pPacket = interpret_body_as_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle(pHeader);

    uint64_t **SGHandleOffsetArrays = new uint64_t*[pPacket->memoryRangeCount];
    int offsetSize = 0;
    bool foundSbt = findSbt(pPacket->memoryRangeCount, pPacket->ppData, SGHandleOffsetArrays, offsetSize);

    packet_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle* pPacketRemap = NULL;
    if (foundSbt) {
        // duplicate a new packet vkFlushMappedMemoryRangesRemapAsInstanceSGHandle
        vktrace_trace_packet_header* pHeaderRemap = static_cast<vktrace_trace_packet_header*>(malloc((size_t)pHeader->size + offsetSize - pPacket->memoryRangeCount * sizeof(uint64_t)));
        memset(pHeaderRemap, 0, (size_t)pHeader->size + offsetSize - pPacket->memoryRangeCount * sizeof(uint64_t));
        memcpy(pHeaderRemap, pHeader, sizeof(vktrace_trace_packet_header));
        pHeaderRemap->packet_id = VKTRACE_TPI_VK_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle;
        pHeaderRemap->size = pHeader->size + offsetSize - pPacket->memoryRangeCount * sizeof(uint64_t);
        pHeaderRemap->pBody = (uintptr_t)(((char*)pHeaderRemap) + sizeof(vktrace_trace_packet_header));
        pHeaderRemap->next_buffers_offset = sizeof(vktrace_trace_packet_header) +
                                   ROUNDUP_TO_8(sizeof(packet_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle));  // Initial offset is from start of header to after the packet body.
        pPacketRemap = interpret_body_as_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle(pHeaderRemap);
        vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->pMemoryRanges), sizeof(VkMappedMemoryRange) * pPacket->memoryRangeCount, pPacket->pMemoryRanges);
        vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppData), sizeof(void*) * pPacket->memoryRangeCount, pPacket->ppData);
        vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataAsOffset), sizeof(void*) * pPacket->memoryRangeCount, pPacket->ppDataAsOffset);
        vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataSGHandleOffset), sizeof(void*) * pPacket->memoryRangeCount, SGHandleOffsetArrays);
        vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataBufferOffset), sizeof(void*) * pPacket->memoryRangeCount, pPacket->ppDataBufferOffset);
        for (int i = 0; i < pPacket->memoryRangeCount; i++) {
            PageGuardChangedBlockInfo *p = (PageGuardChangedBlockInfo *)pPacket->ppData[i];
            int length = p[0].length + sizeof(PageGuardChangedBlockInfo) * (p[0].offset + 1);
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppData[i]), length, pPacket->ppData[i]);

            uint32_t *temp = (uint32_t*)pPacket->ppDataAsOffset[i];
            length = ((temp[0] + 1) * sizeof(uint32_t));
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataAsOffset[i]), length, pPacket->ppDataAsOffset[i]);

            length = ((SGHandleOffsetArrays[i][0] * 2 + 1) * sizeof(uint64_t));
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataSGHandleOffset[i]), length, SGHandleOffsetArrays[i]);

            temp = (uint32_t*)pPacket->ppDataBufferOffset[i];
            length = ((temp[0] + 1) * sizeof(uint32_t));
            vktrace_add_buffer_to_trace_packet(pHeaderRemap, (void**)&(pPacketRemap->ppDataBufferOffset[i]), length, pPacket->ppDataBufferOffset[i]);
        }
        pPacketRemap->device = pPacket->device;
        pPacketRemap->memoryRangeCount = pPacket->memoryRangeCount;
        pPacketRemap->result = pPacket->result;

        pHeader = pHeaderRemap;

        vktrace_LogDebug("Found shader group handles in packet vkFlushMappedMemoryRangesRemapAsInstanceSGHandle, gid = %d, add sbt infos into it.", pHeader->global_packet_index);

        for (int i = 0; i < pPacket->memoryRangeCount; ++i) {
            delete []SGHandleOffsetArrays[i];
        }

    }
    delete []SGHandleOffsetArrays;

    if (foundSbt)
        return finalize_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle(pHeader, pPacketRemap);
    return finalize_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle(pHeader, pPacket);
}

int post_find_sbt(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader) {
    if (pHeader->packet_id != VKTRACE_TPI_VK_vkFlushMappedMemoryRanges &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCreateRayTracingPipelinesKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkGetRayTracingShaderGroupHandlesKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle) {
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
            ret = post_sbt_flush_memory(pFileHeader, pHeader);
            break;
        }
        case VKTRACE_TPI_VK_vkFlushMappedMemoryRangesRemapAsInstanceSGHandle: {
            ret = post_sbt_flush_memory_as_sbt(pFileHeader, pHeader);
            break;
        }
        case VKTRACE_TPI_VK_vkCreateRayTracingPipelinesKHR: {
            packet_vkCreateRayTracingPipelinesKHR* pPacket = interpret_body_as_vkCreateRayTracingPipelinesKHR(pCopy);
            for (int i = 0; i < pPacket->createInfoCount; ++i) {
                vector<string> handles(pPacket->pCreateInfos[i].groupCount);
                for (int j = 0; j < handles.size(); ++j) {
                    handles[j].resize(physicalRtProperties.shaderGroupHandleSize);
                    initString(handles[j]);
                }
                sbtHandlesPost[pPacket->pPipelines[i]] = handles;
            }
            break;
        }
        case VKTRACE_TPI_VK_vkGetRayTracingShaderGroupHandlesKHR: {
            packet_vkGetRayTracingShaderGroupHandlesKHR* pPacket = interpret_body_as_vkGetRayTracingShaderGroupHandlesKHR(pCopy);
            const int handleSize = physicalRtProperties.shaderGroupHandleSize;

            auto it = sbtHandlesPost.find(pPacket->pipeline);
            if (it == sbtHandlesPost.end()) {
                vktrace_LogError("Failed to find pipeline %p when handling vkGetRayTracingShaderGroupHandlesKHR!", pPacket->pipeline);
                ret = -1;
                break;
            }
            vector<string> &handles = it->second;
            char *p = (char *)pPacket->pData;
            for (int i = pPacket->firstGroup; i < pPacket->firstGroup + pPacket->groupCount; ++i) {
                for (int j = 0; j < handleSize; ++j) {
                    handles[i][j] = p[(i - pPacket->firstGroup) * handleSize + j];
                }
            }
            break;
        }
    }
    if (ret == 0 && recompress) {
        ret = compress_packet(g_compressor, pHeader);
    }
    free(pCopy);

    return ret;
}
