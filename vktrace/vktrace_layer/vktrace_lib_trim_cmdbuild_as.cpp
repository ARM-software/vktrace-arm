/*
 *
 * Copyright (C) 2019 ARM Limited
 * All Rights Reserved.
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
 *
 */
#include <stdbool.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stdio.h>
#include <fstream>
#include "vktrace_lib_trim_cmdbuild_as.h"
#include "vktrace_vk_vk.h"
#include "vktrace_platform.h"
#include "vk_dispatch_table_helper.h"
#include "vktrace_common.h"
#include "vktrace_lib_helpers.h"
#include "vktrace_lib_trim.h"

using namespace std;
extern std::unordered_map<VkDeviceAddress, VkBuffer> g_deviceAddressToBuffer;
extern std::unordered_map<VkDeviceMemory, std::vector<VkBuffer>> g_MemoryToBuffers;

namespace trim {

VkDevice get_device_from_commandbuf(VkCommandBuffer commandBuffer) {
    ObjectInfo* objCmdBuf = get_CommandBuffer_objectInfo(commandBuffer);
    assert(objCmdBuf != NULL);

    ObjectInfo* objCmdPool = get_CommandPool_objectInfo(objCmdBuf->ObjectInfo.CommandBuffer.commandPool);
    assert(objCmdPool != NULL);

    return objCmdPool->belongsToDevice;
}

VkResult create_staging_buffer_on_memory(VkCommandBuffer commandBuffer, VkBuffer* buffer, VkDeviceMemory memory,
                                         VkDeviceSize offset, VkDeviceSize size) {
    VkResult result = VK_SUCCESS;

    VkBufferCreateInfo bufCreateInfo;
    bufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCreateInfo.pNext = NULL;
    bufCreateInfo.flags = 0;
    bufCreateInfo.size = size;
    bufCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufCreateInfo.queueFamilyIndexCount = 0;
    bufCreateInfo.pQueueFamilyIndices = NULL;

    VkDevice device = get_device_from_commandbuf(commandBuffer);

    result = mdd(commandBuffer)->devTable.CreateBuffer(device, &bufCreateInfo, NULL, buffer);
    if (result != VK_SUCCESS) {
        vktrace_LogError(
            "vkCmdBuildAccelerationStructuresKHR: Failed to create staging buffer to dump build AS input buffers when trim!");
        return result;
    }

    VkMemoryRequirements mem_reqs;
    mdd(commandBuffer)->devTable.GetBufferMemoryRequirements(device, *buffer, &mem_reqs);

    if (mem_reqs.memoryTypeBits == 0) {
        mdd(commandBuffer)->devTable.DestroyBuffer(device, *buffer, NULL);
        vktrace_LogError("vkCmdBuildAccelerationStructuresKHR: Failed to get staging buffer memory type!");
        return result;
    }

    result = mdd(commandBuffer)->devTable.BindBufferMemory(device, *buffer, memory, offset);
    if (result != VK_SUCCESS) {
        vktrace_LogError("vkCmdBuildAccelerationStructuresKHR: Failed to bind new staging buffer to exiting memory");
        return result;
    }
    return result;
}

VkResult dump_build_AS_buffer_data(VkCommandBuffer commandBuffer, BuildAsBufferInfo& info) {
    VkBufferCreateInfo bufCreateInfo;
    bufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCreateInfo.pNext = NULL;
    bufCreateInfo.flags = 0;
    bufCreateInfo.size = info.dataSize;
    bufCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    bufCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufCreateInfo.queueFamilyIndexCount = 0;
    bufCreateInfo.pQueueFamilyIndices = NULL;

    VkDevice device = get_device_from_commandbuf(commandBuffer);
    VkBuffer stagingBuf;
    VkResult result = VK_SUCCESS;

    auto it1 = g_deviceToFeatureSupport.find(device);
    if (it1 != g_deviceToFeatureSupport.end()) {
        if (it1->second.bufferDeviceAddressCaptureReplay) {
            bufCreateInfo.flags |= VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_EXT;
        }
    }

    result = mdd(commandBuffer)->devTable.CreateBuffer(device, &bufCreateInfo, NULL, &stagingBuf);

    if (result != VK_SUCCESS) {
        vktrace_LogError("Failed to create staging buffer to dump build AS input buffers when trim!");
        return result;
    }

    VkMemoryRequirements mem_reqs;
    mdd(commandBuffer)->devTable.GetBufferMemoryRequirements(device, stagingBuf, &mem_reqs);

    if (mem_reqs.memoryTypeBits == 0) {
        mdd(commandBuffer)->devTable.DestroyBuffer(device, stagingBuf, NULL);
        vktrace_LogError("Failed to find a memory heap to dump build AS input buffers when trim!");
        return result;
    }

    // Use coherent memory for staging buffer to avoid manual synchronization in trim/replay.
    int memIndex = LookUpMemoryProperties(device, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkMemoryAllocateFlagsInfo allocateFlagsInfo;
    allocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocateFlagsInfo.pNext = NULL;
    allocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    allocateFlagsInfo.deviceMask = 0;

    auto it2 = g_deviceToFeatureSupport.find(device);
    if (it2 != g_deviceToFeatureSupport.end()) {
        if (it2->second.bufferDeviceAddressCaptureReplay) {
            allocateFlagsInfo.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
        }
    }

    VkMemoryAllocateInfo allocInfo;
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &allocateFlagsInfo;
    allocInfo.allocationSize = mem_reqs.size;
    allocInfo.memoryTypeIndex = memIndex;

    VkDeviceMemory stagingMem;
    result = mdd(commandBuffer)->devTable.AllocateMemory(device, &allocInfo, NULL, &stagingMem);
    if (result != VK_SUCCESS) {
        allocateFlagsInfo.flags &= ~VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
        result = mdd(commandBuffer)->devTable.AllocateMemory(device, &allocInfo, NULL, &stagingMem);
    }
    if (result != VK_SUCCESS) {
        mdd(commandBuffer)->devTable.DestroyBuffer(device, stagingBuf, NULL);
        vktrace_LogError("Failed to allocate staging buffer memory to dump build AS input buffers when trim!");
        return result;
    }

    result = mdd(commandBuffer)->devTable.BindBufferMemory(device, stagingBuf, stagingMem, 0);
    if (result != VK_SUCCESS) {
        mdd(commandBuffer)->devTable.DestroyBuffer(device, stagingBuf, NULL);
        mdd(commandBuffer)->devTable.FreeMemory(device, stagingMem, NULL);
        vktrace_LogError("Failed to bind staging buffer memory to dump build AS input buffers when trim!");
        return result;
    }

    info.stagingBuf = stagingBuf;
    info.stagingMem = stagingMem;

    VkBufferDeviceAddressInfo bufInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = NULL, .buffer = stagingBuf};
    info.stagingAddr = mdd(commandBuffer)->devTable.GetBufferDeviceAddressKHR(device, &bufInfo);
    if (info.stagingAddr == 0) {
        info.stagingAddr = mdd(commandBuffer)->devTable.GetBufferDeviceAddress(device, &bufInfo);
    }

    VkBufferMemoryBarrier barrier[2];
    barrier[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier[0].pNext = NULL;
    barrier[0].srcAccessMask = VK_ACCESS_NONE;
    barrier[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[0].buffer = info.buffer;
    barrier[0].offset = 0;            // info.bufOffset;
    barrier[0].size = VK_WHOLE_SIZE;  // info.dataSize;

    barrier[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier[1].pNext = NULL;
    barrier[1].srcAccessMask = VK_ACCESS_NONE;
    barrier[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[1].buffer = info.stagingBuf;
    barrier[1].offset = 0;
    barrier[1].size = VK_WHOLE_SIZE;  // info.dataSize;

    // transfer wait all commands on source buffer is done
    mdd(commandBuffer)
        ->devTable.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                      NULL, 2, barrier, 0, NULL);

    VkBufferCopy region;
    region.srcOffset = info.bufOffset;
    region.dstOffset = 0;
    region.size = info.dataSize;
    mdd(commandBuffer)->devTable.CmdCopyBuffer(commandBuffer, info.buffer, stagingBuf, 1, &region);

    barrier[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier[0].pNext = NULL;
    barrier[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier[0].dstAccessMask = VK_ACCESS_NONE;
    barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[0].buffer = info.buffer;
    barrier[0].offset = 0;            // info.bufOffset;
    barrier[0].size = VK_WHOLE_SIZE;  // info.dataSize;

    barrier[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier[1].pNext = NULL;
    barrier[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[1].buffer = info.stagingBuf;
    barrier[1].offset = 0;            // info.bufOffset;
    barrier[1].size = VK_WHOLE_SIZE;  // info.dataSize;

    // all commands to source buffer wait the end of the transfer
    mdd(commandBuffer)
        ->devTable.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, NULL,
                                      2, barrier, 0, NULL);
    return VK_SUCCESS;
}

VkResult verify_buffer(ObjectInfo* objInfo, VkDeviceSize beginOffset, VkDeviceSize size) {
    if (beginOffset < objInfo->ObjectInfo.Buffer.memoryOffset) {
        // vktrace_LogError("verify_buffer fail! Buffer begin offset is smaller than buffer begin!");
        return VK_ERROR_UNKNOWN;
    }
    if (beginOffset + size > objInfo->ObjectInfo.Buffer.memoryOffset + objInfo->ObjectInfo.Buffer.size) {
        // vktrace_LogError("verify_buffer fail! Buffer offset + size is larger than buffer size!");
        return VK_ERROR_UNKNOWN;
    }
    return VK_SUCCESS;
}

// Inputs
// address: The device address from a vertex/index/transform/aabb/instance buffer
// bufOffset: data offset from address
// size: data size
// outputs:
// memory: the VkDeviceMemory the address located in
// buffer: the VkBuffer the address located in
// bufOffset: new offset from buffer beginning
// device: device used to create the memory and buffer
// memOffset: offset from the memory beginning
VkResult get_cmdbuildas_memory_buffer(VkDeviceAddress address, VkDeviceSize size, VkDeviceSize* bufOffset,
                                      VkDeviceMemory* memory, VkBuffer* buffer, VkDeviceSize* memOffset, uint64_t* buf_gid,
                                      uint64_t* bufMem_gid) {
    *memory = VK_NULL_HANDLE;
    *buffer = VK_NULL_HANDLE;
    *memOffset = 0;
    *buf_gid = 0;
    *bufMem_gid = 0;

    VkResult result = VK_SUCCESS;
    VkDeviceAddress memAddr = 0;

    // 1st, search the memory and buffer which matches the address, offset and size
    for (auto it_addr2buf = g_deviceAddressToBuffer.begin(); it_addr2buf != g_deviceAddressToBuffer.end(); it_addr2buf++) {
        // get buffer info
        VkDeviceAddress bufAddr = it_addr2buf->first;
        VkBuffer curBuffer = it_addr2buf->second;
        ObjectInfo* objBuf = get_Buffer_objectInfo(curBuffer);
        VkDeviceMemory bufMem = objBuf->ObjectInfo.Buffer.memory;
        VkDeviceAddress curBufBindOffset = objBuf->ObjectInfo.Buffer.memoryOffset;

        // If we haven't found the memory, then check which memory it belongs to first
        if (*memory == VK_NULL_HANDLE) {
            // get memory info which buffer bound to
            ObjectInfo* objMem = get_DeviceMemory_objectInfo(bufMem);
            memAddr = bufAddr - curBufBindOffset;
            if (address >= memAddr && address + *bufOffset + size <= memAddr + objMem->ObjectInfo.DeviceMemory.size) {
                // Found the memory!
                *memory = bufMem;
                *memOffset = address + *bufOffset - memAddr;
                *bufMem_gid = objMem->gid;
            } else {
                // It's not the memory we are looking for, try next one
                continue;
            }
        } else if (objBuf->ObjectInfo.Buffer.memory != *memory) {
            continue;
        }

        // We got the memory, now try to find the buffer which contains the address
        if (*buffer == VK_NULL_HANDLE) {
            // result = verify_buffer(objBuf, *memOffset + *bufOffset, size);
            result = verify_buffer(objBuf, *memOffset, size);
            if (result == VK_SUCCESS) {
                *buffer = curBuffer;
                // *bufOffset += *memOffset - objBuf->ObjectInfo.Buffer.memoryOffset;
                *bufOffset = *memOffset - objBuf->ObjectInfo.Buffer.memoryOffset;
                *buf_gid = objBuf->gid;
                return VK_SUCCESS;
            }
        }
    }

    if (*memory == VK_NULL_HANDLE) {
        vktrace_LogError("get_cmdbuildas_memory_buffer fail! Cannot find a memory for the address %llu!", address);
        assert(0);
        return VK_ERROR_UNKNOWN;
    }

    // 2nd, we found the memory but not the buffer
    // search buffer inside memory, which contains the address and range
    auto it_buffs = g_MemoryToBuffers.find(*memory);
    if (it_buffs == g_MemoryToBuffers.end()) {
        vktrace_LogError("get_cmdbuildas_memory_buffer fail! Cannot find the memory in g_MemoryToBuffers %llu!", *memory);
        assert(0);
        return VK_ERROR_UNKNOWN;
    }

    auto& buffers = it_buffs->second;
    uint32_t buf_num = buffers.size();
    for (uint32_t i = 0; i < buf_num; i++) {
        ObjectInfo* objBuf = get_Buffer_objectInfo(buffers[i]);
        if (*memOffset >= objBuf->ObjectInfo.Buffer.memoryOffset &&
            // *memOffset + *bufOffset + size - objBuf->ObjectInfo.Buffer.memoryOffset < objBuf->ObjectInfo.Buffer.size) {
            *memOffset + size - objBuf->ObjectInfo.Buffer.memoryOffset < objBuf->ObjectInfo.Buffer.size) {
            *buffer = buffers[i];
            // *bufOffset += *memOffset - objBuf->ObjectInfo.Buffer.memoryOffset;
            *bufOffset = *memOffset - objBuf->ObjectInfo.Buffer.memoryOffset;
            return VK_SUCCESS;
            ;
        }
    }

    vktrace_LogError("get_cmdbuildas_memory_buffer fail! Cannot find a buffer for the address %ll!", address);
    assert(0);
    return VK_ERROR_UNKNOWN;
}

VkResult remove_cmdbuild_by_as(VkDevice device, VkAccelerationStructureKHR as) {
    VkResult result = VK_SUCCESS;
    std::vector<BuildAsInfo>& buildAsInfo = get_cmdBuildAccelerationStrucutres_object();
    bool foundAsInDst = false, foundAsInSrc = false, isVkreplayClearing = false;
    auto it_info = buildAsInfo.begin();
    while (it_info != buildAsInfo.end()) {
        foundAsInDst = false;
        foundAsInSrc = false;
        vktrace_trace_packet_header* pHeader = copy_packet(it_info->pCmdBuildAccelerationtStructure);
        packet_vkCmdBuildAccelerationStructuresKHR* pPacket = interpret_body_as_vkCmdBuildAccelerationStructuresKHR(pHeader);
        for (uint32_t i = 0; i < pPacket->infoCount; ++i) {
            if (pPacket->pInfos[i].dstAccelerationStructure == as) {
                foundAsInDst = true;
                break;
            } else if (pPacket->pInfos[i].srcAccelerationStructure == as) {
                foundAsInSrc = true;
                // Vkreplay destorys AS out of order in the end of trace, but it always frees
                // command buffers before destorying AS, so we could avoid too many error logs
                // by checking the command buffer state.
                if (NULL == get_CommandBuffer_objectInfo(pPacket->commandBuffer)) isVkreplayClearing = true;
            }
        }
        if (foundAsInDst) {
            only_cmdbuildas_destroy_inputs(device, *it_info);
            it_info = buildAsInfo.erase(it_info);
        } else {
            it_info++;
        }
        vktrace_delete_trace_packet_no_lock(&pHeader);
    }
    if (!foundAsInDst && foundAsInSrc && !isVkreplayClearing) {
        vktrace_LogError(
            "Skip removing cmdbuild because target AS is only found as src of vkCmdBuildAccelerationStructuresKHR"
            " and dst AS hasn't been handled yet!");
        result = VK_ERROR_UNKNOWN;
    }

    return result;
}

VkResult remove_cmdCopyAs_by_as(VkAccelerationStructureKHR as) {
    VkResult result = VK_SUCCESS;
    std::vector<CopyAsInfo>& CopyAsInfo = get_cmdCopyAccelerationStructure_object();
    auto it_info = CopyAsInfo.begin();
    bool foundAsInDst = false, foundAsInSrc = false, isVkreplayClearing = false;
    while (it_info != CopyAsInfo.end()) {
        vktrace_trace_packet_header* pHeader = copy_packet(it_info->pCmdCopyAccelerationStructureKHR);
        packet_vkCmdCopyAccelerationStructureKHR* pPacket = interpret_body_as_vkCmdCopyAccelerationStructureKHR(pHeader);
        if (pPacket->pInfo->dst == as) {
            vktrace_trace_packet_header* pCmdCopyAs= it_info->pCmdCopyAccelerationStructureKHR;
            vktrace_delete_trace_packet_no_lock(&pCmdCopyAs);
            it_info = CopyAsInfo.erase(it_info);
            foundAsInDst = true;
        } else if (pPacket->pInfo->src == as) {
            foundAsInSrc = true;
            it_info++;
            // Vkreplay destorys AS out of order in the end of trace, but it always frees
            // command buffers before destorying AS, so we could avoid too many error logs
            // by checking the command buffer state.
            if (NULL == get_CommandBuffer_objectInfo(pPacket->commandBuffer)) isVkreplayClearing = true;
        } else {
            it_info++;
        }
        vktrace_delete_trace_packet_no_lock(&pHeader);
    }
    if (!foundAsInDst && foundAsInSrc && !isVkreplayClearing) {
        vktrace_LogError(
            "Skip removing CopyAsInfo because target AS is only found as src of vkCmdCopyAccelerationStructureKHR"
            " and dst AS hasn't been handled yet!");
        result = VK_ERROR_UNKNOWN;
    }
    return result;
}

static std::unordered_map<VkDevice, _scratch_size> max_scratch_size;

void set_scratch_size(VkDevice device, VkDeviceSize buildSize, VkDeviceSize updateSize) {
    auto iter = max_scratch_size.find(device);
    _scratch_size scratch_size = {0, 0};
    if (iter == max_scratch_size.end()) {
        scratch_size.buildSize = buildSize;
        scratch_size.updateSize = updateSize;
        max_scratch_size[device] = scratch_size;
    } else {
        if (iter->second.buildSize < buildSize) {
            iter->second.buildSize = buildSize;
        }
        if (iter->second.updateSize < updateSize) {
            iter->second.updateSize = updateSize;
        }
    }
}

_scratch_size g_scratch = {MAX_BUFFER_DEVICEADDRESS_SIZE, MAX_BUFFER_DEVICEADDRESS_SIZE};
_scratch_size* get_max_scratch_size(VkDevice device) {
    auto iter = max_scratch_size.find(device);
    if (iter != max_scratch_size.end()) {
        return &(iter->second);
    } else {
        return &g_scratch;
    }
}

VkResult generate_shader_device_buffer(bool makeCall, VkDevice device, VkDeviceSize size, uint32_t queueFamilyIndex,
                                       VkBufferUsageFlags usage, _shader_device_buffer& devBuffer) {
    // create build scratch buffer
    VkBufferCreateInfo bufCreateInfo;
    bufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCreateInfo.pNext = NULL;
    bufCreateInfo.flags = 0;
    bufCreateInfo.size = size;
    bufCreateInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | usage;
    bufCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufCreateInfo.queueFamilyIndexCount = 1;
    bufCreateInfo.pQueueFamilyIndices = &queueFamilyIndex;

    auto it1 = g_deviceToFeatureSupport.find(device);
    if (it1 != g_deviceToFeatureSupport.end()) {
        if (it1->second.bufferDeviceAddressCaptureReplay) {
            bufCreateInfo.flags |= VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_EXT;
        }
    }

    vktrace_trace_packet_header* pheadCreateBuf =
        generate::vkCreateBuffer(makeCall, device, &bufCreateInfo, NULL, &devBuffer.buffer);
    packet_vkCreateBuffer* pCreateBuf = (packet_vkCreateBuffer*)pheadCreateBuf->pBody;
    assert(pCreateBuf->result == VK_SUCCESS);
    if (pCreateBuf->result != VK_SUCCESS) {
        vktrace_delete_trace_packet(&pheadCreateBuf);
        vktrace_LogError("generate_shader_device_buffer: create shader device buffer fail!");
        assert(0);
        return pCreateBuf->result;
    }

    // get build scratch buffer memory requirements
    VkMemoryRequirements mem_reqs;
    vktrace_trace_packet_header* pheadGetBufMemReq =
        generate::vkGetBufferMemoryRequirements(true, device, devBuffer.buffer, &mem_reqs);
    assert(mem_reqs.memoryTypeBits != 0);
    int memIndex = 0;
    for (int i = 0; i < 32; i++) {
        if (mem_reqs.memoryTypeBits & (1 << i)) {
            memIndex = i;
            break;
        }
    }

    // allocate build scratch buffer memory
    VkMemoryAllocateFlagsInfo allocateFlagsInfo;
    allocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocateFlagsInfo.pNext = NULL;
    allocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    allocateFlagsInfo.deviceMask = 0;

    auto it2 = g_deviceToFeatureSupport.find(device);
    if (it2 != g_deviceToFeatureSupport.end()) {
        if (it2->second.bufferDeviceAddressCaptureReplay) {
            allocateFlagsInfo.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
        }
    }

    VkMemoryAllocateInfo allocInfo;
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &allocateFlagsInfo;
    allocInfo.allocationSize = mem_reqs.size;
    allocInfo.memoryTypeIndex = memIndex;

    VkDeviceMemory verifiedMem;
    VkResult result = mdd(device)->devTable.AllocateMemory(device, &allocInfo, NULL, &verifiedMem);
    if (result != VK_SUCCESS) {
        allocateFlagsInfo.flags &= ~VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
        result = mdd(device)->devTable.AllocateMemory(device, &allocInfo, NULL, &verifiedMem);
    }
    if (result == VK_SUCCESS) {
        mdd(device)->devTable.FreeMemory(device, verifiedMem, NULL);
    }

    vktrace_trace_packet_header* pheadAlloMem = generate::vkAllocateMemory(makeCall, device, &allocInfo, NULL, &devBuffer.memory);
    packet_vkAllocateMemory* pAllocMem = (packet_vkAllocateMemory*)pheadAlloMem->pBody;
    assert(pAllocMem->result == VK_SUCCESS);
    if (pAllocMem->result != VK_SUCCESS) {
        vktrace_delete_trace_packet(&pheadCreateBuf);
        vktrace_delete_trace_packet(&pheadGetBufMemReq);
        vktrace_delete_trace_packet(&pheadAlloMem);
        vktrace_LogError("generate_shader_device_buffer: allocate memory fail!");
        assert(0);
        return pAllocMem->result;
    }

    // bind buffer to memory
    vktrace_trace_packet_header* pheadBindBufMem =
        generate::vkBindBufferMemory(makeCall, device, devBuffer.buffer, devBuffer.memory, 0);
    packet_vkBindBufferMemory* pBindBufMem = (packet_vkBindBufferMemory*)pheadBindBufMem->pBody;
    if (pBindBufMem->result != VK_SUCCESS) {
        vktrace_delete_trace_packet(&pheadCreateBuf);
        vktrace_delete_trace_packet(&pheadGetBufMemReq);
        vktrace_delete_trace_packet(&pheadAlloMem);
        vktrace_delete_trace_packet(&pheadBindBufMem);
        vktrace_LogError("generate_shader_device_buffer: BindBufferMemory fail!");
        return pBindBufMem->result;
    }

    // get buffer device address
    VkBufferDeviceAddressInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = NULL, .buffer = devBuffer.buffer};
    vktrace_trace_packet_header* pheadBufDevAddr =
        generate::vkGetBufferDeviceAddressKHR(makeCall, device, &info, &devBuffer.address);

    // write packets to trace file
    vktrace_write_trace_packet(pheadCreateBuf, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pheadCreateBuf);

    vktrace_write_trace_packet(pheadGetBufMemReq, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pheadGetBufMemReq);

    vktrace_write_trace_packet(pheadAlloMem, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pheadAlloMem);

    vktrace_write_trace_packet(pheadBindBufMem, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pheadBindBufMem);

    vktrace_write_trace_packet(pheadBufDevAddr, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pheadBufDevAddr);

    return VK_SUCCESS;
}

void generate_destroy_shader_device_buffer(VkDevice device, _shader_device_buffer& devBuffer) {
    vktrace_trace_packet_header* pHeader;
    pHeader = generate::vkDestroyBuffer(true, device, devBuffer.buffer, NULL);
    vktrace_write_trace_packet(pHeader, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pHeader);

    pHeader = generate::vkFreeMemory(true, device, devBuffer.memory, NULL);
    vktrace_write_trace_packet(pHeader, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pHeader);
}

static VkResult geneate_create_as_input(VkDevice device, uint32_t queueFamilyIndex, BuildAsBufferInfo& buildAsBufInfo) {
    if (buildAsBufInfo.dataSize == 0) {
        return VK_SUCCESS;
    }
    _shader_device_buffer devBuf = {
        .buffer = buildAsBufInfo.stagingBuf, .memory = buildAsBufInfo.stagingMem, .address = buildAsBufInfo.stagingAddr};
    VkResult result = generate_shader_device_buffer(false, device, buildAsBufInfo.dataSize, queueFamilyIndex,
                                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, devBuf);
    if (result != VK_SUCCESS) {
        vktrace_LogError("geneate_create_as_input: create shader device buffer fail!");
    }

    void* address;
    vktrace_trace_packet_header* pHeader;

    pHeader = generate::vkMapMemory(true, device, buildAsBufInfo.stagingMem, 0, buildAsBufInfo.dataSize, 0, &address);
    vktrace_write_trace_packet(pHeader, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pHeader);

    pHeader = generate::vkUnmapMemory(true, buildAsBufInfo.dataSize, address, device, buildAsBufInfo.stagingMem);
    vktrace_write_trace_packet(pHeader, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pHeader);

    return VK_SUCCESS;
}

VkResult generate_cmdbuildas_create_inputs(VkDevice device, uint32_t queueFamilyIndex, BuildAsInfo& buildAsInfo) {
    VkResult result = VK_SUCCESS;
    packet_vkCmdBuildAccelerationStructuresKHR* pPacket =
        (packet_vkCmdBuildAccelerationStructuresKHR*)buildAsInfo.pCmdBuildAccelerationtStructure->pBody;
    vktrace_trace_packet_header* pcmdBuildHeader = copy_packet(buildAsInfo.pCmdBuildAccelerationtStructure);
    packet_vkCmdBuildAccelerationStructuresKHR* pcmdBuildPacket =
        interpret_body_as_vkCmdBuildAccelerationStructuresKHR(pcmdBuildHeader);
    uint64_t offset;
    VkDeviceAddress* pAddr;
    int geoInfoSize = buildAsInfo.geometryInfo.size();
    for (int j = 0; j < geoInfoSize; j++) {
        BuildAsGeometryInfo& geoInfo = buildAsInfo.geometryInfo[j];
        uint32_t infoIndex = buildAsInfo.geometryInfo[j].infoIndex;
        uint32_t geoIndex = buildAsInfo.geometryInfo[j].geoIndex;
        if (geoInfo.type == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
            result = geneate_create_as_input(device, queueFamilyIndex, geoInfo.triangle.vb);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_cmdbuildas_create_inputs: create vb buffer fail!");
                assert(0);
            } else if (geoInfo.triangle.vb.dataSize > 0) {
                offset = (uint64_t)(&pcmdBuildPacket->pInfos[infoIndex]
                                         .pGeometries[geoIndex]
                                         .geometry.triangles.vertexData.deviceAddress) -
                         (uint64_t)pcmdBuildPacket;
                pAddr = (VkDeviceAddress*)((uint64_t)pPacket + offset);
                *pAddr = geoInfo.triangle.vb.stagingAddr;
            }

            result = geneate_create_as_input(device, queueFamilyIndex, geoInfo.triangle.ib);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_cmdbuildas_create_inputs: create ib buffer fail!");
                assert(0);
            } else if (geoInfo.triangle.ib.dataSize > 0) {
                offset = (uint64_t)(&pcmdBuildPacket->pInfos[infoIndex]
                                         .pGeometries[geoIndex]
                                         .geometry.triangles.indexData.deviceAddress) -
                         (uint64_t)pcmdBuildPacket;
                pAddr = (VkDeviceAddress*)((uint64_t)pPacket + offset);
                *pAddr = geoInfo.triangle.ib.stagingAddr;
            }

            result = geneate_create_as_input(device, queueFamilyIndex, geoInfo.triangle.tb);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_cmdbuildas_create_inputs: create tb buffer fail!");
                assert(0);
            } else if (geoInfo.triangle.tb.dataSize > 0) {
                offset = (uint64_t)(&pcmdBuildPacket->pInfos[infoIndex].pGeometries[geoIndex].geometry.triangles.transformData.deviceAddress) - (uint64_t)pcmdBuildPacket;
                pAddr = (VkDeviceAddress*)((uint64_t)pPacket + offset);
                *pAddr = geoInfo.triangle.tb.stagingAddr;
            }
        } else if (geoInfo.type == VK_GEOMETRY_TYPE_AABBS_KHR) {
            result = geneate_create_as_input(device, queueFamilyIndex, geoInfo.aabb.info);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_cmdbuildas_create_inputs: create aabb buffer fail!");
                assert(0);
            } else if (geoInfo.aabb.info.dataSize > 0) {
                offset = (uint64_t)(&pcmdBuildPacket->pInfos[infoIndex].pGeometries[geoIndex].geometry.aabbs.data.deviceAddress) -
                         (uint64_t)pcmdBuildPacket;
                pAddr = (VkDeviceAddress*)((uint64_t)pPacket + offset);
                *pAddr = geoInfo.aabb.info.stagingAddr;
            }
        } else if (geoInfo.type == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
            result = geneate_create_as_input(device, queueFamilyIndex, geoInfo.instance.info);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_cmdbuildas_create_inputs: create instance buffer fail!");
                assert(0);
            } else if (geoInfo.instance.info.dataSize > 0) {
                offset =
                    (uint64_t)(&pcmdBuildPacket->pInfos[infoIndex].pGeometries[geoIndex].geometry.instances.data.deviceAddress) -
                    (uint64_t)pcmdBuildPacket;
                pAddr = (VkDeviceAddress*)((uint64_t)pPacket + offset);
                *pAddr = geoInfo.instance.info.stagingAddr;
            }
        }

        if (pcmdBuildPacket->ppBuildRangeInfos != NULL) {
            uint32_t* pOffsetAddr = NULL;
            offset =
                    (uint64_t)(&pcmdBuildPacket->ppBuildRangeInfos[infoIndex][geoIndex].primitiveOffset) -
                    (uint64_t)pcmdBuildPacket;
            pOffsetAddr = (uint32_t*)((uint64_t)pPacket + offset);
            *pOffsetAddr = 0;

            offset =
                    (uint64_t)(&pcmdBuildPacket->ppBuildRangeInfos[infoIndex][geoIndex].firstVertex) -
                    (uint64_t)pcmdBuildPacket;
            pOffsetAddr = (uint32_t*)((uint64_t)pPacket + offset);
            *pOffsetAddr = 0;

            offset =
                    (uint64_t)(&pcmdBuildPacket->ppBuildRangeInfos[infoIndex][geoIndex].transformOffset) -
                    (uint64_t)pcmdBuildPacket;
            pOffsetAddr = (uint32_t*)((uint64_t)pPacket + offset);
            *pOffsetAddr = 0;
        }
    }
    vktrace_delete_trace_packet(&pcmdBuildHeader);
    return result;
}

static VkResult geneate_destroy_as_input(VkDevice device, BuildAsBufferInfo& buildAsBufInfo) {
    if (buildAsBufInfo.dataSize == 0) {
        return VK_SUCCESS;
    }
    _shader_device_buffer devBuf = {
        .buffer = buildAsBufInfo.stagingBuf, .memory = buildAsBufInfo.stagingMem, .address = buildAsBufInfo.stagingAddr};
    generate_destroy_shader_device_buffer(device, devBuf);
    return VK_SUCCESS;
}

VkResult generate_cmdbuildas_destroy_inputs(VkDevice device, BuildAsInfo& buildAsInfo) {
    VkResult result = VK_SUCCESS;
    int geoInfoSize = buildAsInfo.geometryInfo.size();
    for (int j = 0; j < geoInfoSize; j++) {
        BuildAsGeometryInfo& geoInfo = buildAsInfo.geometryInfo[j];
        if (geoInfo.type == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
            result = geneate_destroy_as_input(device, geoInfo.triangle.vb);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_cmdbuildas_destroy_inputs: destroy vb buffer fail!");
                assert(0);
            }
            result = geneate_destroy_as_input(device, geoInfo.triangle.ib);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_cmdbuildas_destroy_inputs: destroy ib buffer fail!");
                assert(0);
            }
            result = geneate_destroy_as_input(device, geoInfo.triangle.tb);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_cmdbuildas_destroy_inputs: destroy tb buffer fail!");
                assert(0);
            }
        } else if (geoInfo.type == VK_GEOMETRY_TYPE_AABBS_KHR) {
            result = geneate_destroy_as_input(device, geoInfo.aabb.info);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_cmdbuildas_destroy_inputs: destroy aabb buffer fail!");
                assert(0);
            }
        } else if (geoInfo.type == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
            result = geneate_destroy_as_input(device, geoInfo.instance.info);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_cmdbuildas_destroy_inputs: destroy instance buffer fail!");
                assert(0);
            }
        } else {
            // a haked type to dump dst as to file
            assert(geoInfo.type == VK_GEOMETRY_TYPE_MAX_ENUM_KHR);
        }
    }
    return result;
}

static VkResult only_destroy_as_input(VkDevice device, BuildAsBufferInfo& buildAsBufInfo) {
    if (buildAsBufInfo.dataSize == 0) {
        return VK_SUCCESS;
    }

    if (buildAsBufInfo.stagingBuf != VK_NULL_HANDLE) {
        mdd(device)->devTable.DestroyBuffer(device, buildAsBufInfo.stagingBuf, NULL);
    }

    if (buildAsBufInfo.stagingMem != VK_NULL_HANDLE) {
        mdd(device)->devTable.FreeMemory(device, buildAsBufInfo.stagingMem, NULL);
    }

    return VK_SUCCESS;
}

VkResult only_cmdbuildas_destroy_inputs(VkDevice device, BuildAsInfo& buildAsInfo) {
    VkResult result = VK_SUCCESS;
    int geoInfoSize = buildAsInfo.geometryInfo.size();
    for (int j = 0; j < geoInfoSize; j++) {
        BuildAsGeometryInfo& geoInfo = buildAsInfo.geometryInfo[j];
        if (geoInfo.type == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
            result = only_destroy_as_input(device, geoInfo.triangle.vb);
            if (result != VK_SUCCESS) {
                vktrace_LogError("only_cmdbuildas_destroy_inputs: destroy vb buffer fail!");
                assert(0);
            }
            result = only_destroy_as_input(device, geoInfo.triangle.ib);
            if (result != VK_SUCCESS) {
                vktrace_LogError("only_cmdbuildas_destroy_inputs: destroy ib buffer fail!");
                assert(0);
            }
            result = only_destroy_as_input(device, geoInfo.triangle.tb);
            if (result != VK_SUCCESS) {
                vktrace_LogError("only_cmdbuildas_destroy_inputs: destroy tb buffer fail!");
                assert(0);
            }
        } else if (geoInfo.type == VK_GEOMETRY_TYPE_AABBS_KHR) {
            result = only_destroy_as_input(device, geoInfo.aabb.info);
            if (result != VK_SUCCESS) {
                vktrace_LogError("only_cmdbuildas_destroy_inputs: destroy aabb buffer fail!");
                assert(0);
            }
        } else if (geoInfo.type == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
            result = only_destroy_as_input(device, geoInfo.instance.info);
            if (result != VK_SUCCESS) {
                vktrace_LogError("only_cmdbuildas_destroy_inputs: destroy instance buffer fail!");
                assert(0);
            }
        } else {
            // a haked type to dump dst as to file
            assert(geoInfo.type == VK_GEOMETRY_TYPE_MAX_ENUM_KHR);
        }
    }

    vktrace_trace_packet_header* pHeader = buildAsInfo.pCmdBuildAccelerationtStructure;
    vktrace_delete_trace_packet_no_lock(&pHeader);

    return result;
}
}  // namespace trim