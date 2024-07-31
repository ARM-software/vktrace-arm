#include "vktrace_rq_pp.h"
#include "vktrace_vk_packet_id.h"
#include "vk_struct_size_helper.h"

using namespace std;
static VkCommandBuffer g_buildASCmdBuf = VK_NULL_HANDLE;
static std::vector<uint64_t> g_perBuildASPackets;
static std::unordered_map<uint64_t, std::vector<uint64_t>> g_perDummyBuildASList;
static std::unordered_map<VkAccelerationStructureKHR, std::vector<uint64_t>> g_buildASPacketIndex;
static std::unordered_map<VkAccelerationStructureKHR, VkAccelerationStructureKHR> g_buildAccelerationStructureCopyMode;
static std::unordered_map<VkAccelerationStructureKHR, VkAccelerationStructureKHR> g_buildAccelerationStructureUpdateMode;
static std::unordered_map<uint64_t, bool> g_deleteDummyBuildASPackets;

static int pre_record_per_build_as_packet(vktrace_trace_packet_header* pHeader) {
    int ret = 0;
    static bool preFindBuildAS = false;
    static bool posFindBuildAS = false;
    static bool preFindCopyAS = false;
    static bool preHostBuildAS = false;
    static bool startRecord = false;
    static uint64_t buildASIndex = 0;

    if (posFindBuildAS) {
        return 0;
    }

    if (pHeader->packet_id == VKTRACE_TPI_VK_vkCreateBuffer) {
        if (preFindBuildAS && buildASIndex && g_perBuildASPackets.size()) {
            g_perDummyBuildASList[buildASIndex] = g_perBuildASPackets;
            buildASIndex = 0;
            preFindBuildAS = false;
            g_perBuildASPackets.clear();
        }

        if (preFindBuildAS && preFindCopyAS) {
            preFindCopyAS = false;
            preFindBuildAS = false;
            g_perBuildASPackets.clear();
        }
    }

    if (pHeader->packet_id == VKTRACE_TPI_VK_vkCmdCopyAccelerationStructureKHR ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkCopyAccelerationStructureKHR) {
        if (preFindBuildAS && buildASIndex && g_perBuildASPackets.size()) {
            if (pHeader->packet_id == VKTRACE_TPI_VK_vkCmdCopyAccelerationStructureKHR) {
                g_perBuildASPackets.pop_back();
            }
            g_perDummyBuildASList[buildASIndex] = g_perBuildASPackets;
            g_perBuildASPackets.clear();
            buildASIndex = 0;
            preFindCopyAS = true;
        }
    }

    if (pHeader->packet_id == VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresKHR ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkBuildAccelerationStructuresKHR) {
        preFindBuildAS = true;
        startRecord = true;
        buildASIndex = pHeader->global_packet_index;
    }

    if (pHeader->packet_id == VKTRACE_TPI_VK_vkBuildAccelerationStructuresKHR) {
        preHostBuildAS = true;
    }

    if (pHeader->packet_id == VKTRACE_TPI_VK_vkFreeCommandBuffers) {
        if (startRecord) {
            posFindBuildAS = true;
        }
    }

    if (pHeader->packet_id == VKTRACE_TPI_VK_vkBeginCommandBuffer) {
        if (startRecord && preHostBuildAS) {
            posFindBuildAS = true;
        }
    }

    if (startRecord) {
        g_perBuildASPackets.push_back(pHeader->global_packet_index);
    }
    return ret;
}

static int pre_remove_dummy_build_as_packet(vktrace_trace_packet_header* pHeader) {
    if (pHeader->packet_id != VKTRACE_TPI_VK_vkBeginCommandBuffer &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBuildAccelerationStructuresKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCmdCopyAccelerationStructureKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCopyAccelerationStructureKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkFreeCommandBuffers) {
        return 0;
    }

    int ret = 0;
    static bool preFindBuildAS = false;
    static bool posFindBuildAS = false;
    static bool preHostBuildAS = false;
    if (posFindBuildAS) {
        return 0;
    }

    switch (pHeader->packet_id)
    {
    case VKTRACE_TPI_VK_vkBeginCommandBuffer: {
        packet_vkBeginCommandBuffer* pPacket = interpret_body_as_vkBeginCommandBuffer(pHeader);
        if (g_buildASCmdBuf != pPacket->commandBuffer) {
            g_buildASCmdBuf = pPacket->commandBuffer;
        }
        if (preHostBuildAS) {
            posFindBuildAS = true;
        }
        break;
    }
    case VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresKHR: {
        preFindBuildAS = true;
        packet_vkCmdBuildAccelerationStructuresKHR* pPacket = interpret_body_as_vkCmdBuildAccelerationStructuresKHR(pHeader);
        if (g_buildASCmdBuf == pPacket->commandBuffer) {
            for (uint32_t i = 0; i < pPacket->infoCount; ++i) {
                if (pPacket->pInfos[i].mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR) {
                    if ((g_buildAccelerationStructureCopyMode.find(pPacket->pInfos[i].dstAccelerationStructure) != g_buildAccelerationStructureCopyMode.end()) &&
                        (g_buildAccelerationStructureCopyMode[pPacket->pInfos[i].dstAccelerationStructure] == 0)) {
                        // This only remove the last build mode for BuildAS
                        if (g_buildASPacketIndex.find(pPacket->pInfos[i].dstAccelerationStructure) != g_buildASPacketIndex.end()) {
                            auto& itASList = g_buildASPacketIndex[pPacket->pInfos[i].dstAccelerationStructure];
                            for (uint32_t i = 0; i < itASList.size(); i++) {
                                g_deleteDummyBuildASPackets[itASList[i]] = true;
                            }
                        }
                    }
                    g_buildAccelerationStructureCopyMode[pPacket->pInfos[i].dstAccelerationStructure] = 0;
                } else if (pPacket->pInfos[i].mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR){
                    if ((g_buildAccelerationStructureUpdateMode.find(pPacket->pInfos[i].dstAccelerationStructure) != g_buildAccelerationStructureUpdateMode.end()) &&
                        (g_buildAccelerationStructureUpdateMode[pPacket->pInfos[i].dstAccelerationStructure] == pPacket->pInfos[i].srcAccelerationStructure)) {
                        // This only skips the update mode for BuildAS
                        g_deleteDummyBuildASPackets[pPacket->header->global_packet_index] = true;
                    }
                    g_buildAccelerationStructureUpdateMode[pPacket->pInfos[i].dstAccelerationStructure] = pPacket->pInfos[i].srcAccelerationStructure;
                }
                g_buildASPacketIndex[pPacket->pInfos[i].dstAccelerationStructure].push_back(pPacket->header->global_packet_index);
            }
        }
        break;
    }
    case VKTRACE_TPI_VK_vkBuildAccelerationStructuresKHR: {
        preFindBuildAS = true;
        preHostBuildAS = true;
        packet_vkBuildAccelerationStructuresKHR* pPacket = interpret_body_as_vkBuildAccelerationStructuresKHR(pHeader);
        for (uint32_t i = 0; i < pPacket->infoCount; ++i) {
            if (pPacket->pInfos[i].mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR) {
                if ((g_buildAccelerationStructureCopyMode.find(pPacket->pInfos[i].dstAccelerationStructure) != g_buildAccelerationStructureCopyMode.end()) &&
                    (g_buildAccelerationStructureCopyMode[pPacket->pInfos[i].dstAccelerationStructure] == 0)) {
                    // This only remove the last build mode for BuildAS
                    if (g_buildASPacketIndex.find(pPacket->pInfos[i].dstAccelerationStructure) != g_buildASPacketIndex.end()) {
                        auto& itASList = g_buildASPacketIndex[pPacket->pInfos[i].dstAccelerationStructure];
                        for (uint32_t i = 0; i < itASList.size(); i++) {
                            g_deleteDummyBuildASPackets[itASList[i]] = true;
                        }
                    }
                }
                g_buildAccelerationStructureCopyMode[pPacket->pInfos[i].dstAccelerationStructure] = 0;
            } else if (pPacket->pInfos[i].mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR){
                if ((g_buildAccelerationStructureUpdateMode.find(pPacket->pInfos[i].dstAccelerationStructure) != g_buildAccelerationStructureUpdateMode.end()) &&
                    (g_buildAccelerationStructureUpdateMode[pPacket->pInfos[i].dstAccelerationStructure] == pPacket->pInfos[i].srcAccelerationStructure)) {
                    // This only skips the update mode for BuildAS
                    g_deleteDummyBuildASPackets[pPacket->header->global_packet_index] = true;
                }
                g_buildAccelerationStructureUpdateMode[pPacket->pInfos[i].dstAccelerationStructure] = pPacket->pInfos[i].srcAccelerationStructure;
            }
            g_buildASPacketIndex[pPacket->pInfos[i].dstAccelerationStructure].push_back(pPacket->header->global_packet_index);
        }
        break;
    }
    case VKTRACE_TPI_VK_vkCmdCopyAccelerationStructureKHR: {
        packet_vkCmdCopyAccelerationStructureKHR* pPacket = interpret_body_as_vkCmdCopyAccelerationStructureKHR(pHeader);
        g_buildAccelerationStructureCopyMode[pPacket->pInfo->src] = pPacket->pInfo->dst;
        break;
    }
    case VKTRACE_TPI_VK_vkCopyAccelerationStructureKHR: {
        packet_vkCopyAccelerationStructureKHR* pPacket = interpret_body_as_vkCopyAccelerationStructureKHR(pHeader);
        g_buildAccelerationStructureCopyMode[pPacket->pInfo->src] = pPacket->pInfo->dst;
        break;
    }
    case VKTRACE_TPI_VK_vkFreeCommandBuffers: {
        packet_vkFreeCommandBuffers* pPacket = interpret_body_as_vkFreeCommandBuffers(pHeader);
        if (g_buildASCmdBuf == pPacket->pCommandBuffers[0]) {
            g_buildASCmdBuf = VK_NULL_HANDLE;
        }
        if (preFindBuildAS) {
            posFindBuildAS = true;
        }
        break;
    }
    default:
        // Should not be here
        vktrace_LogError("pre_remove_dummy_build_as_packet(): unexpected API!");
        break;
    }

    return ret;
}

int pre_remove_dummy_build_as(vktrace_trace_file_header* pFileHeader, FileLike* traceFile) {
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

        ret = pre_record_per_build_as_packet(pHeader);
        ret = pre_remove_dummy_build_as_packet(pHeader);

        vktrace_free(pHeader);

        if (ret != 0) {
            vktrace_LogError("Failed to handle packet %llu in pre_remove_dummy_build_as!", pHeader->global_packet_index);
            break;
        }
    }

    return ret;
}

int pre_remove_all_dummy_as(vktrace_trace_file_header* pFileHeader, FileLike* traceFile) {
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

        if (pHeader->packet_id == VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresKHR ||
            pHeader->packet_id == VKTRACE_TPI_VK_vkBuildAccelerationStructuresKHR) {
            if (g_deleteDummyBuildASPackets.find(pHeader->global_packet_index) != g_deleteDummyBuildASPackets.end() &&
                g_perDummyBuildASList.find(pHeader->global_packet_index) != g_perDummyBuildASList.end()) {
                if (g_deleteDummyBuildASPackets[pHeader->global_packet_index]) {
                    vktrace_LogAlways("remove dummy build as packet index= %u", pHeader->global_packet_index);
                    auto& itASList = g_perDummyBuildASList[pHeader->global_packet_index];
                    for (uint32_t i = 0; i < itASList.size(); i++) {
                        g_deleteDummyBuildASPackets[itASList[i]] = true;
                    }
                }
            }
        }

        vktrace_free(pHeader);

        if (ret != 0) {
            vktrace_LogError("Failed to handle packet %llu in pre_remove_all_dummy_as!", pHeader->global_packet_index);
            break;
        }
    }

    return ret;
}

int post_remove_dummy_build_as(vktrace_trace_file_header* pFileHeader,  vktrace_trace_packet_header* &pHeader, FILE* newTraceFile,
                               uint64_t* fileOffset, uint64_t* fileSize, bool &rmdp) {
    int ret = 0;
    if (g_deleteDummyBuildASPackets.find(pHeader->global_packet_index) != g_deleteDummyBuildASPackets.end()) {
        if (g_deleteDummyBuildASPackets[pHeader->global_packet_index]) {
            rmdp = true;
        }
    }

    if (pHeader->packet_id == VKTRACE_TPI_VK_vkQueuePresentKHR) {
        g_deleteDummyBuildASPackets.clear();
    }
    return ret;
}
