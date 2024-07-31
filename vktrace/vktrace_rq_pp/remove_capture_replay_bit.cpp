#include "vktrace_rq_pp.h"
#include "vktrace_vk_packet_id.h"
#include "vk_struct_size_helper.h"

static void vktrace_add_pnext_structs_to_pp_trace_packet(vktrace_trace_packet_header* pHeader, void* pOut, const void* pIn) {
    void** ppOutNext;
    const void* pInNext;
    if (!pIn) return;
    // Add the pNext chain to trace packet.
    while (((VkApplicationInfo*)pIn)->pNext) {
        ppOutNext = (void**)&(((VkApplicationInfo*)pOut)->pNext);
        pInNext = (void*)((VkApplicationInfo*)pIn)->pNext;
        size_t size = 0;
        get_struct_size(pInNext, &size);
        if (size > 0) {
            pOut = *ppOutNext;
            pIn = pInNext;
            vktrace_finalize_buffer_address(pHeader, ppOutNext);
        } else {
            // Skip and remove from chain, must be an unknown type
            ((VkApplicationInfo*)pOut)->pNext = *ppOutNext ? ((VkApplicationInfo*)*ppOutNext)->pNext : NULL;
            pIn = pInNext;  // Should not remove from original struct, just skip
        }
    }
}

static int post_rm_capture_replay_create_buffer(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader)
{
    packet_vkCreateBuffer* pPacket = interpret_body_as_vkCreateBuffer(pHeader);

    // remove VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_EXT in vkCreateBuffer if it is for AS
    if (pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT) {
        const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->flags &= ~VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_EXT;
        VkBufferOpaqueCaptureAddressCreateInfo *pCaptureAddressCreateInfo = (VkBufferOpaqueCaptureAddressCreateInfo*)find_ext_struct((const vulkan_struct_header*)pPacket->pCreateInfo->pNext, VK_STRUCTURE_TYPE_BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO);
        if (pCaptureAddressCreateInfo != nullptr) {
            pCaptureAddressCreateInfo->opaqueCaptureAddress = 0;
        }
    }

    VkBufferCreateInfo* pCreateInfo = const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo);
    if (pCreateInfo) vktrace_add_pnext_structs_to_pp_trace_packet(pHeader, (void*)pPacket->pCreateInfo, pCreateInfo);
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pCreateInfo->pQueueFamilyIndices));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pCreateInfo));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAllocator));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pBuffer));

    return 0;
}

static int post_rm_capture_replay_allocate_memory(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader)
{
    packet_vkAllocateMemory* pPacket = interpret_body_as_vkAllocateMemory(pHeader);
    VkMemoryAllocateInfo* pAllocateInfo = const_cast<VkMemoryAllocateInfo*>(pPacket->pAllocateInfo);

    // remove VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT in vkAllocateMemory if it is for AS
    VkMemoryAllocateFlagsInfo *pAllocateFlagInfo = (VkMemoryAllocateFlagsInfo*)find_ext_struct((const vulkan_struct_header*)pAllocateInfo->pNext, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO);
    if (pAllocateFlagInfo != nullptr && (pAllocateFlagInfo->flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR)) {
        const_cast<VkMemoryAllocateFlagsInfo*>(pAllocateFlagInfo)->flags &= ~VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
        VkMemoryOpaqueCaptureAddressAllocateInfo *pCaptureAddressAllocateInfo = (VkMemoryOpaqueCaptureAddressAllocateInfo*)find_ext_struct((const vulkan_struct_header*)pAllocateInfo->pNext, VK_STRUCTURE_TYPE_MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO);
        if (pCaptureAddressAllocateInfo != nullptr) {
            pCaptureAddressAllocateInfo->opaqueCaptureAddress = 0;
        }
    }

    if (pPacket->pAllocateInfo) vktrace_add_pnext_structs_to_pp_trace_packet(pHeader, (void*)pPacket->pAllocateInfo, pAllocateInfo);
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAllocateInfo));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAllocator));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pMemory));

    return 0;
}

static int post_rm_capture_replay_create_as(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader)
{
    packet_vkCreateAccelerationStructureKHR* pPacket = (packet_vkCreateAccelerationStructureKHR*)pHeader->pBody;
    pPacket->header = pHeader;
    pPacket->pCreateInfo = (const VkAccelerationStructureCreateInfoKHR*)vktrace_trace_packet_interpret_buffer_pointer(pHeader, (intptr_t)pPacket->pCreateInfo);

    if (pPacket->pCreateInfo->createFlags & VK_ACCELERATION_STRUCTURE_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_KHR) {
        const_cast<VkAccelerationStructureCreateInfoKHR*>(pPacket->pCreateInfo)->createFlags &= ~VK_ACCELERATION_STRUCTURE_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_KHR;
        const_cast<VkAccelerationStructureCreateInfoKHR*>(pPacket->pCreateInfo)->deviceAddress = 0;
    }

    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pCreateInfo));
    return 0;
}

static int post_rm_capture_replay_create_RTPioelines(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader)
{
    packet_vkCreateRayTracingPipelinesKHR* pPacket = (packet_vkCreateRayTracingPipelinesKHR*)pHeader->pBody;
    pPacket->header = pHeader;
    pPacket->pCreateInfos = (const VkRayTracingPipelineCreateInfoKHR*)vktrace_trace_packet_interpret_buffer_pointer(pHeader, (intptr_t)pPacket->pCreateInfos);

    for (uint32_t i = 0; i < pPacket->createInfoCount; ++i) {
        if (pPacket->pCreateInfos[i].flags & VK_PIPELINE_CREATE_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR) {
            const_cast<VkRayTracingPipelineCreateInfoKHR*>(pPacket->pCreateInfos)[i].flags &= ~VK_PIPELINE_CREATE_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR;
        }
    }

    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pCreateInfos));
    return 0;
}

int post_rm_capture_replay(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header*& pHeader) {
    if (pHeader->packet_id != VKTRACE_TPI_VK_vkCreateBuffer &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkAllocateMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCreateAccelerationStructureKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCreateRayTracingPipelinesKHR) {
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
        case VKTRACE_TPI_VK_vkCreateBuffer: {
            ret = post_rm_capture_replay_create_buffer(pFileHeader, pHeader);
            break;
        }
        case VKTRACE_TPI_VK_vkAllocateMemory: {
            ret = post_rm_capture_replay_allocate_memory(pFileHeader, pHeader);
            break;
        }
        case VKTRACE_TPI_VK_vkCreateAccelerationStructureKHR: {
            ret = post_rm_capture_replay_create_as(pFileHeader, pHeader);
            break;
        }
        case VKTRACE_TPI_VK_vkCreateRayTracingPipelinesKHR: {
            ret = post_rm_capture_replay_create_RTPioelines(pFileHeader, pHeader);
            break;
        }
        default:
            // Should not be here
            vktrace_LogError("post_fix_asbuffer_size(): unexpected API!");
            break;
    }

    if (ret == 0 && recompress) {
        ret = compress_packet(g_compressor, pHeader);
    }
    free(pCopy);

    return ret;
}
