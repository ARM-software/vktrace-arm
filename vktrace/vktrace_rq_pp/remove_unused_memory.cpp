#include "vktrace_rq_pp.h"
#include "vktrace_vk_packet_id.h"
#include "vk_struct_size_helper.h"

using namespace std;
static unordered_map<VkDeviceMemory, vector<uint64_t>> g_allocateMemory2PacketIndex;
static unordered_map<VkDeviceMemory, bool> g_allocateMemory2BindBuffer;
static unordered_map<VkDeviceMemory, bool> g_allocateMemory2BindImage;
static unordered_map<uint64_t, VkDeviceMemory> g_removePacketIndex2Memory;

enum handle_type {
    VKBUFFER = 0x1,
    VKBUFFERVIEW,
    VKIMAGE,
    VKIMAGEVIEW,
    VKMEMORY
};
static unordered_map<uint64_t, unordered_map<uint64_t, uint64_t>> g_createHandle2PacketIndex;
static unordered_map<uint64_t, uint64_t> g_deletePacketIndex2Handle;

static int pre_remove_unused_memory_handle_packet(vktrace_trace_packet_header* pHeader) {
    if (pHeader->packet_id != VKTRACE_TPI_VK_vkAllocateMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkMapMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkUnmapMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkFlushMappedMemoryRanges &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindImageMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindImageMemory2 &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindImageMemory2KHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory2 &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory2KHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkFreeMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkGetDeviceMemoryOpaqueCaptureAddress &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkGetDeviceMemoryOpaqueCaptureAddressKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkGetMemoryAndroidHardwareBufferANDROID &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkGetMemoryFdKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkQueueBindSparse &&
        // pHeader->packet_id != VKTRACE_TPI_VK_vkMapMemory2KHR &&
        // pHeader->packet_id != VKTRACE_TPI_VK_vkUnmapMemory2KHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyDevice) {
        return 0;
    }

    int ret = 0;
    switch (pHeader->packet_id)
    {
    case VKTRACE_TPI_VK_vkAllocateMemory: {
        packet_vkAllocateMemory* pPacket = interpret_body_as_vkAllocateMemory(pHeader);
        g_allocateMemory2PacketIndex[*pPacket->pMemory].push_back(pPacket->header->global_packet_index);
        break;
    }
    case VKTRACE_TPI_VK_vkMapMemory: {
        packet_vkMapMemory* pPacket = interpret_body_as_vkMapMemory(pHeader);
        g_allocateMemory2PacketIndex[pPacket->memory].push_back(pPacket->header->global_packet_index);
        break;
    }
    case VKTRACE_TPI_VK_vkUnmapMemory: {
        packet_vkUnmapMemory* pPacket = interpret_body_as_vkUnmapMemory(pHeader);
        g_allocateMemory2PacketIndex[pPacket->memory].push_back(pPacket->header->global_packet_index);
        break;
    }
    case VKTRACE_TPI_VK_vkFlushMappedMemoryRanges: {
        packet_vkFlushMappedMemoryRanges* pPacket = interpret_body_as_vkFlushMappedMemoryRanges(pHeader);
        if (pPacket->memoryRangeCount > 1) {
            break;
        }
        for (uint32_t i = 0; i < pPacket->memoryRangeCount; i++) {
            g_allocateMemory2PacketIndex[pPacket->pMemoryRanges[i].memory].push_back(pPacket->header->global_packet_index);
        }
        break;
    }
    case VKTRACE_TPI_VK_vkBindImageMemory: {
        packet_vkBindImageMemory* pPacket = interpret_body_as_vkBindImageMemory(pHeader);
        g_allocateMemory2BindImage[pPacket->memory] = true;
        break;
    }
    case VKTRACE_TPI_VK_vkBindImageMemory2: {
        packet_vkBindImageMemory2* pPacket = interpret_body_as_vkBindImageMemory2(pHeader);
        for (uint32_t i = 0; i < pPacket->bindInfoCount; i++) {
            g_allocateMemory2BindImage[pPacket->pBindInfos[i].memory] = true;
        }
        break;
    }
    case VKTRACE_TPI_VK_vkBindImageMemory2KHR: {
        packet_vkBindImageMemory2KHR* pPacket = interpret_body_as_vkBindImageMemory2KHR(pHeader);
        for (uint32_t i = 0; i < pPacket->bindInfoCount; i++) {
            g_allocateMemory2BindImage[pPacket->pBindInfos[i].memory] = true;
        }
        break;
    }
    case VKTRACE_TPI_VK_vkBindBufferMemory: {
        packet_vkBindBufferMemory* pPacket = interpret_body_as_vkBindBufferMemory(pHeader);
        g_allocateMemory2BindBuffer[pPacket->memory] = true;
        break;
    }
    case VKTRACE_TPI_VK_vkBindBufferMemory2: {
        packet_vkBindBufferMemory2* pPacket = interpret_body_as_vkBindBufferMemory2(pHeader);
        for (uint32_t i = 0; i < pPacket->bindInfoCount; i++) {
            g_allocateMemory2BindBuffer[pPacket->pBindInfos[i].memory] = true;
        }
        break;
    }
    case VKTRACE_TPI_VK_vkBindBufferMemory2KHR: {
        packet_vkBindBufferMemory2KHR* pPacket = interpret_body_as_vkBindBufferMemory2KHR(pHeader);
        for (uint32_t i = 0; i < pPacket->bindInfoCount; i++) {
            g_allocateMemory2BindBuffer[pPacket->pBindInfos[i].memory] = true;
        }
        break;
    }
    case VKTRACE_TPI_VK_vkGetDeviceMemoryOpaqueCaptureAddress:
    case VKTRACE_TPI_VK_vkGetDeviceMemoryOpaqueCaptureAddressKHR: {
        packet_vkGetDeviceMemoryOpaqueCaptureAddress* pPacket = interpret_body_as_vkGetDeviceMemoryOpaqueCaptureAddress(pHeader);
        g_allocateMemory2PacketIndex[pPacket->pInfo->memory].push_back(pPacket->header->global_packet_index);
        break;
    }
    case VKTRACE_TPI_VK_vkGetMemoryAndroidHardwareBufferANDROID: {
        packet_vkGetMemoryAndroidHardwareBufferANDROID* pPacket = interpret_body_as_vkGetMemoryAndroidHardwareBufferANDROID(pHeader);
        g_allocateMemory2BindBuffer[pPacket->pInfo->memory] = true;
        break;
    }
    case VKTRACE_TPI_VK_vkGetMemoryFdKHR: {
        packet_vkGetMemoryFdKHR* pPacket = interpret_body_as_vkGetMemoryFdKHR(pHeader);
        g_allocateMemory2BindBuffer[pPacket->pGetFdInfo->memory] = true;
        break;
    }
    case VKTRACE_TPI_VK_vkQueueBindSparse: {
        packet_vkQueueBindSparse* pPacket = interpret_body_as_vkQueueBindSparse(pHeader);
        for (uint32_t i = 0; i < pPacket->bindInfoCount; i++) {
            for (uint32_t j = 0; j < pPacket->pBindInfo[i].bufferBindCount; j++) {
                for (uint32_t k = 0; k < pPacket->pBindInfo[i].pBufferBinds[j].bindCount; k++) {
                    g_allocateMemory2BindBuffer[pPacket->pBindInfo[i].pBufferBinds[j].pBinds[k].memory] = true;
                }
            }
            for (uint32_t j = 0; j < pPacket->pBindInfo[i].imageBindCount; j++) {
                for (uint32_t k = 0; k < pPacket->pBindInfo[i].pImageBinds[j].bindCount; k++) {
                    g_allocateMemory2BindImage[pPacket->pBindInfo[i].pImageBinds[j].pBinds[k].memory] = true;
                }
            }
            for (uint32_t j = 0; j < pPacket->pBindInfo[i].imageOpaqueBindCount; j++) {
                for (uint32_t k = 0; k < pPacket->pBindInfo[i].pImageOpaqueBinds[j].bindCount; k++) {
                    g_allocateMemory2BindImage[pPacket->pBindInfo[i].pImageOpaqueBinds[j].pBinds[k].memory] = true;
                }
            }
        }
        break;
    }
    case VKTRACE_TPI_VK_vkFreeMemory: {
        packet_vkFreeMemory* pPacket = interpret_body_as_vkFreeMemory(pHeader);

        if (g_allocateMemory2BindBuffer.find(pPacket->memory) == g_allocateMemory2BindBuffer.end()
            && g_allocateMemory2BindImage.find(pPacket->memory) == g_allocateMemory2BindImage.end()
            && g_allocateMemory2PacketIndex.find(pPacket->memory) != g_allocateMemory2PacketIndex.end()) {
            for(uint32_t i = 0; i < g_allocateMemory2PacketIndex[pPacket->memory].size(); i++) {
                g_removePacketIndex2Memory[g_allocateMemory2PacketIndex[pPacket->memory][i]] = pPacket->memory;
                vktrace_LogAlways("remove unused memory packet index= %u", g_allocateMemory2PacketIndex[pPacket->memory][i]);
            }
            g_removePacketIndex2Memory[pPacket->header->global_packet_index] = pPacket->memory;
            vktrace_LogAlways("remove unused memory packet index= %u", pPacket->header->global_packet_index);
        }

        g_allocateMemory2PacketIndex[pPacket->memory].clear();
        g_allocateMemory2PacketIndex.erase(pPacket->memory);
        g_allocateMemory2BindImage.erase(pPacket->memory);
        g_allocateMemory2BindBuffer.erase(pPacket->memory);
        break;
    }
    case VKTRACE_TPI_VK_vkDestroyDevice: {
        break;
    }
    default:
        // Should not be here
        vktrace_LogError("pre_remove_unused_memory_handle_packet(): unexpected API!");
        break;
    }

    return ret;
}

int pre_remove_unused_memory_handle(vktrace_trace_file_header* pFileHeader, FileLike* traceFile) {
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

        ret = pre_remove_unused_memory_handle_packet(pHeader);

        vktrace_free(pHeader);

        if (ret != 0) {
            vktrace_LogError("Failed to handle packet %llu in pre_remove_unused_memory_handle!", pHeader->global_packet_index);
            break;
        }
    }

    return ret;
}

static int pre_remove_unused_handle_packet(vktrace_trace_packet_header* pHeader) {
    if (pHeader->packet_id != VKTRACE_TPI_VK_vkCreateBuffer &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCreateBufferView &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyBuffer &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyBufferView &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCreateImage &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCreateImageView &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyImage &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyImageView &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkAllocateMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkFreeMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyDevice) {
        return 0;
    }

    int ret = 0;
    switch (pHeader->packet_id)
    {
    case VKTRACE_TPI_VK_vkCreateBuffer: {
        packet_vkCreateBuffer* pPacket = interpret_body_as_vkCreateBuffer(pHeader);
        g_createHandle2PacketIndex[(uint64_t)*pPacket->pBuffer][VKBUFFER] = pPacket->header->global_packet_index;
        break;
    }
    case VKTRACE_TPI_VK_vkCreateBufferView: {
        packet_vkCreateBufferView* pPacket = interpret_body_as_vkCreateBufferView(pHeader);
        g_createHandle2PacketIndex[(uint64_t)*pPacket->pView][VKBUFFERVIEW] = pPacket->header->global_packet_index;
        break;
    }
    case VKTRACE_TPI_VK_vkDestroyBuffer: {
        packet_vkDestroyBuffer* pPacket = interpret_body_as_vkDestroyBuffer(pHeader);
        if (g_createHandle2PacketIndex.find((uint64_t)pPacket->buffer) != g_createHandle2PacketIndex.end() &&
            g_createHandle2PacketIndex[(uint64_t)pPacket->buffer].find(VKBUFFER) != g_createHandle2PacketIndex[(uint64_t)pPacket->buffer].end()) {
            g_createHandle2PacketIndex[(uint64_t)pPacket->buffer].erase(VKBUFFER);
        } else {
            g_deletePacketIndex2Handle[pPacket->header->global_packet_index] = (uint64_t)pPacket->buffer;
        }
        break;
    }
    case VKTRACE_TPI_VK_vkDestroyBufferView: {
        packet_vkDestroyBufferView* pPacket = interpret_body_as_vkDestroyBufferView(pHeader);
        if (g_createHandle2PacketIndex.find((uint64_t)pPacket->bufferView) != g_createHandle2PacketIndex.end() &&
            g_createHandle2PacketIndex[(uint64_t)pPacket->bufferView].find(VKBUFFERVIEW) != g_createHandle2PacketIndex[(uint64_t)pPacket->bufferView].end()) {
            g_createHandle2PacketIndex[(uint64_t)pPacket->bufferView].erase(VKBUFFERVIEW);
        } else {
            g_deletePacketIndex2Handle[pPacket->header->global_packet_index] = (uint64_t)pPacket->bufferView;
        }
        break;
    }
    case VKTRACE_TPI_VK_vkCreateImage: {
        packet_vkCreateImage* pPacket = interpret_body_as_vkCreateImage(pHeader);
        g_createHandle2PacketIndex[(uint64_t)*pPacket->pImage][VKIMAGE] = pPacket->header->global_packet_index;
        break;
    }
    case VKTRACE_TPI_VK_vkCreateImageView: {
        packet_vkCreateImageView* pPacket = interpret_body_as_vkCreateImageView(pHeader);
        g_createHandle2PacketIndex[(uint64_t)*pPacket->pView][VKIMAGEVIEW] = pPacket->header->global_packet_index;
        break;
    }
    case VKTRACE_TPI_VK_vkDestroyImage: {
        packet_vkDestroyImage* pPacket = interpret_body_as_vkDestroyImage(pHeader);
        if (g_createHandle2PacketIndex.find((uint64_t)pPacket->image) != g_createHandle2PacketIndex.end() &&
            g_createHandle2PacketIndex[(uint64_t)pPacket->image].find(VKIMAGE) != g_createHandle2PacketIndex[(uint64_t)pPacket->image].end()) {
            g_createHandle2PacketIndex[(uint64_t)pPacket->image].erase(VKIMAGE);
        } else {
            g_deletePacketIndex2Handle[pPacket->header->global_packet_index] = (uint64_t)pPacket->image;
        }
        break;
    }
    case VKTRACE_TPI_VK_vkDestroyImageView: {
        packet_vkDestroyImageView* pPacket = interpret_body_as_vkDestroyImageView(pHeader);
        if (g_createHandle2PacketIndex.find((uint64_t)pPacket->imageView) != g_createHandle2PacketIndex.end() &&
            g_createHandle2PacketIndex[(uint64_t)pPacket->imageView].find(VKIMAGEVIEW) != g_createHandle2PacketIndex[(uint64_t)pPacket->imageView].end()) {
            g_createHandle2PacketIndex[(uint64_t)pPacket->imageView].erase(VKIMAGEVIEW);
        } else {
            g_deletePacketIndex2Handle[pPacket->header->global_packet_index] = (uint64_t)pPacket->imageView;
        }
        break;
    }
    case VKTRACE_TPI_VK_vkAllocateMemory: {
        packet_vkAllocateMemory* pPacket = interpret_body_as_vkAllocateMemory(pHeader);
        g_createHandle2PacketIndex[(uint64_t)*pPacket->pMemory][VKMEMORY] = pPacket->header->global_packet_index;
        break;
    }
    case VKTRACE_TPI_VK_vkFreeMemory: {
        packet_vkFreeMemory* pPacket = interpret_body_as_vkFreeMemory(pHeader);
        if (g_createHandle2PacketIndex.find((uint64_t)pPacket->memory) != g_createHandle2PacketIndex.end() &&
            g_createHandle2PacketIndex[(uint64_t)pPacket->memory].find(VKMEMORY) != g_createHandle2PacketIndex[(uint64_t)pPacket->memory].end()) {
            g_createHandle2PacketIndex[(uint64_t)pPacket->memory].erase(VKMEMORY);
        } else {
            g_deletePacketIndex2Handle[pPacket->header->global_packet_index] = (uint64_t)pPacket->memory;
        }
        break;
    }
    case VKTRACE_TPI_VK_vkDestroyDevice: {
        break;
    }
    default:
        // Should not be here
        vktrace_LogError("pre_remove_unused_handle_packet(): unexpected API!");
        break;
    }

    return ret;
}

int pre_remove_unused_handle(vktrace_trace_file_header* pFileHeader, FileLike* traceFile) {
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

        ret = pre_remove_unused_handle_packet(pHeader);

        vktrace_free(pHeader);

        if (ret != 0) {
            vktrace_LogError("Failed to handle packet %llu in pre_remove_unused_handle!", pHeader->global_packet_index);
            break;
        }
    }

    return ret;
}

int post_remove_unused_handle(vktrace_trace_file_header* pFileHeader,  vktrace_trace_packet_header* &pHeader, FILE* newTraceFile,
                                         uint64_t* fileOffset, uint64_t* fileSize, bool &rmdp) {
    int ret = 0;
    if (pHeader->packet_id == VKTRACE_TPI_VK_vkAllocateMemory ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkFreeMemory ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkMapMemory ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkUnmapMemory ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkFlushMappedMemoryRanges) {
        if (g_removePacketIndex2Memory.find(pHeader->global_packet_index) != g_removePacketIndex2Memory.end()) {
            rmdp = true;
        }
    }

    if (pHeader->packet_id == VKTRACE_TPI_VK_vkDestroyBuffer ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkDestroyBufferView ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkDestroyImage ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkDestroyImageView ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkAllocateMemory ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkFreeMemory) {
        if (g_deletePacketIndex2Handle.find(pHeader->global_packet_index) != g_deletePacketIndex2Handle.end()) {
            rmdp = true;
        }
    }
    return ret;
}
