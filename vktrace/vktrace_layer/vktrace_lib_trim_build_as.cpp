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
#include "vktrace_lib_trim_build_as.h"
#include "vktrace_vk_vk.h"
#include "vktrace_platform.h"
#include "vk_dispatch_table_helper.h"
#include "vktrace_common.h"
#include "vktrace_lib_helpers.h"
#include "vktrace_lib_trim.h"

using namespace std;
namespace trim {

VkResult create_staging_buffer_on_memory(VkDevice device, VkBuffer* buffer, VkDeviceMemory memory,
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

    result = mdd(device)->devTable.CreateBuffer(device, &bufCreateInfo, NULL, buffer);
    if (result != VK_SUCCESS) {
        vktrace_LogError(
            "vkBuildAccelerationStructuresKHR: Failed to create staging buffer to dump build AS input buffers when trim!");
        return result;
    }

    VkMemoryRequirements mem_reqs;
    mdd(device)->devTable.GetBufferMemoryRequirements(device, *buffer, &mem_reqs);

    if (mem_reqs.memoryTypeBits == 0) {
        mdd(device)->devTable.DestroyBuffer(device, *buffer, NULL);
        vktrace_LogError("vkBuildAccelerationStructuresKHR: Failed to get staging buffer memory type!");
        return result;
    }

    result = mdd(device)->devTable.BindBufferMemory(device, *buffer, memory, offset);
    if (result != VK_SUCCESS) {
        vktrace_LogError("vkBuildAccelerationStructuresKHR: Failed to bind new staging buffer to exiting memory");
        return result;
    }
    return result;
}

VkResult dump_build_AS_buffer_data(VkDevice device, BuildAsBufferInfo& info, const void* pData) {
    VkBufferCreateInfo bufCreateInfo;
    bufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCreateInfo.pNext = NULL;
    bufCreateInfo.flags = 0;
    bufCreateInfo.size = info.dataSize;
    bufCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    bufCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufCreateInfo.queueFamilyIndexCount = 0;
    bufCreateInfo.pQueueFamilyIndices = NULL;

    VkBuffer stagingBuf;
    VkResult result = VK_SUCCESS;

    result = mdd(device)->devTable.CreateBuffer(device, &bufCreateInfo, NULL, &stagingBuf);

    if (result != VK_SUCCESS) {
        vktrace_LogError("Failed to create staging buffer to dump build AS input buffers when trim!");
        return result;
    }

    VkMemoryRequirements mem_reqs;
    mdd(device)->devTable.GetBufferMemoryRequirements(device, stagingBuf, &mem_reqs);

    if (mem_reqs.memoryTypeBits == 0) {
        mdd(device)->devTable.DestroyBuffer(device, stagingBuf, NULL);
        vktrace_LogError("Failed to find a memory heap to dump build AS input buffers when trim!");
        return result;
    }

    // Use coherent memory for staging buffer to avoid manual synchronization in trim/replay.
    int memIndex = LookUpMemoryProperties(device, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkMemoryAllocateInfo allocInfo;
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = NULL;
    allocInfo.allocationSize = mem_reqs.size;
    allocInfo.memoryTypeIndex = memIndex;

    VkDeviceMemory stagingMem;
    result = mdd(device)->devTable.AllocateMemory(device, &allocInfo, NULL, &stagingMem);
    if (result != VK_SUCCESS) {
        mdd(device)->devTable.DestroyBuffer(device, stagingBuf, NULL);
        vktrace_LogError("Failed to allocate staging buffer memory to dump build AS input buffers when trim!");
        return result;
    }

    result = mdd(device)->devTable.BindBufferMemory(device, stagingBuf, stagingMem, 0);
    if (result != VK_SUCCESS) {
        mdd(device)->devTable.DestroyBuffer(device, stagingBuf, NULL);
        mdd(device)->devTable.FreeMemory(device, stagingMem, NULL);
        vktrace_LogError("Failed to bind staging buffer memory to dump build AS input buffers when trim!");
        return result;
    }

    info.stagingBuf = stagingBuf;
    info.stagingMem = stagingMem;

    result = mdd(device)->devTable.MapMemory(device, stagingMem, 0, info.dataSize, 0, &(info.hostAddress));
    if (result != VK_SUCCESS) {
        mdd(device)->devTable.DestroyBuffer(device, stagingBuf, NULL);
        mdd(device)->devTable.FreeMemory(device, stagingMem, NULL);
        vktrace_LogError("Failed to map staging buffer memory to dump build AS input buffers when trim!");
        return result;
    }

    uint8_t* scrData = (uint8_t*)pData;
    memcpy(info.hostAddress, scrData + info.bufOffset, info.dataSize);
    return VK_SUCCESS;
}

VkResult remove_build_by_as(VkDevice device, VkAccelerationStructureKHR as) {
    VkResult result = VK_SUCCESS;
    std::vector<BuildAsInfo>& buildAsInfo = get_BuildAccelerationStrucutres_object();
    bool foundAsInDst = false, foundAsInSrc = false, isVkreplayClearing = false;
    auto it_info = buildAsInfo.begin();
    while (it_info != buildAsInfo.end()) {
        foundAsInDst = false;
        foundAsInSrc = false;
        vktrace_trace_packet_header* pHeader = copy_packet(it_info->pBuildAccelerationtStructure);
        packet_vkBuildAccelerationStructuresKHR* pPacket = interpret_body_as_vkBuildAccelerationStructuresKHR(pHeader);
        for (uint32_t i = 0; i < pPacket->infoCount; ++i) {
            if (pPacket->pInfos[i].dstAccelerationStructure == as) {
                foundAsInDst = true;
                break;
            } else if (pPacket->pInfos[i].srcAccelerationStructure == as) {
                foundAsInSrc = true;
            }
        }
        if (foundAsInDst) {
            only_buildas_destroy_inputs(device, *it_info);
            it_info = buildAsInfo.erase(it_info);
        } else {
            it_info++;
        }
        vktrace_delete_trace_packet_no_lock(&pHeader);
    }
    if (!foundAsInDst && foundAsInSrc && !isVkreplayClearing) {
        vktrace_LogError(
            "Skip removing build because target AS is only found as src of vkBuildAccelerationStructuresKHR"
            " and dst AS hasn't been handled yet!");
        result = VK_ERROR_UNKNOWN;
    }

    return result;
}

VkResult remove_CopyAs_by_as(VkAccelerationStructureKHR as) {
    VkResult result = VK_SUCCESS;
    std::vector<CopyAsInfo>& CopyAsInfo = get_CopyAccelerationStructure_object();
    auto it_info = CopyAsInfo.begin();
    bool foundAsInDst = false, foundAsInSrc = false, isVkreplayClearing = false;
    while (it_info != CopyAsInfo.end()) {
        vktrace_trace_packet_header* pHeader = copy_packet(it_info->pCopyAccelerationStructureKHR);
        packet_vkCopyAccelerationStructureKHR* pPacket = interpret_body_as_vkCopyAccelerationStructureKHR(pHeader);
        if (pPacket->pInfo->dst == as) {
            vktrace_trace_packet_header* pCopyAs= it_info->pCopyAccelerationStructureKHR;
            vktrace_delete_trace_packet_no_lock(&pCopyAs);
            it_info = CopyAsInfo.erase(it_info);
            foundAsInDst = true;
        } else if (pPacket->pInfo->src == as) {
            foundAsInSrc = true;
            it_info++;
        } else {
            it_info++;
        }
        vktrace_delete_trace_packet_no_lock(&pHeader);
    }
    if (!foundAsInDst && foundAsInSrc && !isVkreplayClearing) {
        vktrace_LogError(
            "Skip removing CopyAsInfo because target AS is only found as src of vkCopyAccelerationStructureKHR"
            " and dst AS hasn't been handled yet!");
        result = VK_ERROR_UNKNOWN;
    }
    return result;
}

VkResult generate_shader_device_buffer(bool makeCall, VkDevice device, VkDeviceSize size, uint32_t queueFamilyIndex,
                                       VkBufferUsageFlags usage, _shader_host_buffer& devBuffer) {
    // create build scratch buffer
    VkBufferCreateInfo bufCreateInfo;
    bufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCreateInfo.pNext = NULL;
    bufCreateInfo.flags = 0;
    bufCreateInfo.size = size;
    bufCreateInfo.usage = usage;
    bufCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufCreateInfo.queueFamilyIndexCount = 1;
    bufCreateInfo.pQueueFamilyIndices = &queueFamilyIndex;

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
    VkMemoryAllocateInfo allocInfo;
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = NULL;
    allocInfo.allocationSize = mem_reqs.size;
    allocInfo.memoryTypeIndex = memIndex;
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

    // get host address
    vktrace_trace_packet_header* pheadHostAddr =
        generate::vkMapMemory(true, device, devBuffer.memory, 0, mem_reqs.size, 0, &devBuffer.hostAddress);

    // write packets to trace file
    vktrace_write_trace_packet(pheadCreateBuf, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pheadCreateBuf);

    vktrace_write_trace_packet(pheadGetBufMemReq, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pheadGetBufMemReq);

    vktrace_write_trace_packet(pheadAlloMem, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pheadAlloMem);

    vktrace_write_trace_packet(pheadBindBufMem, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pheadBindBufMem);

    vktrace_write_trace_packet(pheadHostAddr, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pheadHostAddr);

    return VK_SUCCESS;
}

void generate_destroy_shader_device_buffer(VkDevice device, _shader_host_buffer& devBuffer) {
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
    _shader_host_buffer devBuf = {
        .buffer = buildAsBufInfo.stagingBuf, .memory = buildAsBufInfo.stagingMem, .hostAddress = buildAsBufInfo.hostAddress};
    VkResult result = generate_shader_device_buffer(false, device, buildAsBufInfo.dataSize, queueFamilyIndex,
                                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, devBuf);
    if (result != VK_SUCCESS) {
        vktrace_LogError("geneate_create_as_input: create shader device buffer fail!");
    }

    void* address = devBuf.hostAddress;
    vktrace_trace_packet_header* pHeader;

    if (address == nullptr) {
        pHeader = generate::vkMapMemory(true, device, buildAsBufInfo.stagingMem, 0, buildAsBufInfo.dataSize, 0, &address);
        vktrace_write_trace_packet(pHeader, vktrace_trace_get_trace_file());
        vktrace_delete_trace_packet(&pHeader);
    }

    pHeader = generate::vkUnmapMemory(true, buildAsBufInfo.dataSize, address, device, buildAsBufInfo.stagingMem);
    vktrace_write_trace_packet(pHeader, vktrace_trace_get_trace_file());
    vktrace_delete_trace_packet(&pHeader);

    {
        pHeader = generate::vkMapMemory(false, device, buildAsBufInfo.stagingMem, 0, buildAsBufInfo.dataSize, 0, &address);
        vktrace_write_trace_packet(pHeader, vktrace_trace_get_trace_file());
        vktrace_delete_trace_packet(&pHeader);
    }

    return VK_SUCCESS;
}

VkResult generate_buildas_create_inputs(VkDevice device, uint32_t queueFamilyIndex, BuildAsInfo& buildAsInfo) {
    VkResult result = VK_SUCCESS;
    packet_vkBuildAccelerationStructuresKHR* pPacket =
        (packet_vkBuildAccelerationStructuresKHR*)buildAsInfo.pBuildAccelerationtStructure->pBody;
    vktrace_trace_packet_header* pBuildHeader = copy_packet(buildAsInfo.pBuildAccelerationtStructure);
    packet_vkBuildAccelerationStructuresKHR* pBuildPacket =
        interpret_body_as_vkBuildAccelerationStructuresKHR(pBuildHeader);
    uint64_t offset;
    uint64_t* pAddr;
    int geoInfoSize = buildAsInfo.geometryInfo.size();
    for (int j = 0; j < geoInfoSize; j++) {
        BuildAsGeometryInfo& geoInfo = buildAsInfo.geometryInfo[j];
        uint32_t infoIndex = buildAsInfo.geometryInfo[j].infoIndex;
        uint32_t geoIndex = buildAsInfo.geometryInfo[j].geoIndex;
        if (geoInfo.type == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
            result = geneate_create_as_input(device, queueFamilyIndex, geoInfo.triangle.vb);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_buildas_input_buffers: create vb buffer fail!");
                assert(0);
            } else if (geoInfo.triangle.vb.dataSize > 0) {
                offset = (uint64_t)(&pBuildPacket->pInfos[infoIndex]
                                         .pGeometries[geoIndex]
                                         .geometry.triangles.vertexData.deviceAddress) -
                         (uint64_t)pBuildPacket;
                pAddr = (uint64_t*)((uint64_t)pPacket + offset);
                *pAddr = (uint64_t)geoInfo.triangle.vb.hostAddress;
            }

            result = geneate_create_as_input(device, queueFamilyIndex, geoInfo.triangle.ib);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_buildas_input_buffers: create ib buffer fail!");
                assert(0);
            } else if (geoInfo.triangle.ib.dataSize > 0) {
                offset = (uint64_t)(&pBuildPacket->pInfos[infoIndex]
                                         .pGeometries[geoIndex]
                                         .geometry.triangles.indexData.deviceAddress) -
                         (uint64_t)pBuildPacket;
                pAddr = (uint64_t*)((uint64_t)pPacket + offset);
                *pAddr = (uint64_t)geoInfo.triangle.ib.hostAddress;
            }

            result = geneate_create_as_input(device, queueFamilyIndex, geoInfo.triangle.tb);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_buildas_input_buffers: create tb buffer fail!");
                assert(0);
            } else if (geoInfo.triangle.tb.dataSize > 0) {
                offset = (uint64_t)(&pBuildPacket->pInfos[infoIndex].pGeometries[geoIndex].geometry.triangles.transformData.deviceAddress) - (uint64_t)pBuildPacket;
                pAddr = (uint64_t*)((uint64_t)pPacket + offset);
                *pAddr = (uint64_t)geoInfo.triangle.tb.hostAddress;
            }
        } else if (geoInfo.type == VK_GEOMETRY_TYPE_AABBS_KHR) {
            result = geneate_create_as_input(device, queueFamilyIndex, geoInfo.aabb.info);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_buildas_input_buffers: create aabb buffer fail!");
                assert(0);
            } else if (geoInfo.aabb.info.dataSize > 0) {
                offset = (uint64_t)(&pBuildPacket->pInfos[infoIndex].pGeometries[geoIndex].geometry.aabbs.data.deviceAddress) -
                         (uint64_t)pBuildPacket;
                pAddr = (uint64_t*)((uint64_t)pPacket + offset);
                *pAddr = (uint64_t)geoInfo.aabb.info.hostAddress;
            }
        } else if (geoInfo.type == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
            // record instance data of vkBuildAccelerationStructuresKHR package, so there is not need process.
        }

        if (pBuildPacket->ppBuildRangeInfos != NULL) {
            uint32_t* pOffsetAddr = NULL;
            offset =
                    (uint64_t)(&pBuildPacket->ppBuildRangeInfos[infoIndex][geoIndex].primitiveOffset) -
                    (uint64_t)pBuildPacket;
            pOffsetAddr = (uint32_t*)((uint64_t)pPacket + offset);
            *pOffsetAddr = 0;

            offset =
                    (uint64_t)(&pBuildPacket->ppBuildRangeInfos[infoIndex][geoIndex].firstVertex) -
                    (uint64_t)pBuildPacket;
            pOffsetAddr = (uint32_t*)((uint64_t)pPacket + offset);
            *pOffsetAddr = 0;

            offset =
                    (uint64_t)(&pBuildPacket->ppBuildRangeInfos[infoIndex][geoIndex].transformOffset) -
                    (uint64_t)pBuildPacket;
            pOffsetAddr = (uint32_t*)((uint64_t)pPacket + offset);
            *pOffsetAddr = 0;
        }
    }
    vktrace_delete_trace_packet(&pBuildHeader);
    return result;
}

static VkResult geneate_destroy_as_input(VkDevice device, BuildAsBufferInfo& buildAsBufInfo) {
    if (buildAsBufInfo.dataSize == 0) {
        return VK_SUCCESS;
    }
    _shader_host_buffer devBuf = {
        .buffer = buildAsBufInfo.stagingBuf, .memory = buildAsBufInfo.stagingMem, .hostAddress = buildAsBufInfo.hostAddress};
    generate_destroy_shader_device_buffer(device, devBuf);
    return VK_SUCCESS;
}

VkResult generate_buildas_destroy_inputs(VkDevice device, BuildAsInfo& buildAsInfo) {
    VkResult result = VK_SUCCESS;
    int geoInfoSize = buildAsInfo.geometryInfo.size();
    for (int j = 0; j < geoInfoSize; j++) {
        BuildAsGeometryInfo& geoInfo = buildAsInfo.geometryInfo[j];
        if (geoInfo.type == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
            result = geneate_destroy_as_input(device, geoInfo.triangle.vb);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_buildas_destroy_input: destroy vb buffer fail!");
                assert(0);
            }
            result = geneate_destroy_as_input(device, geoInfo.triangle.ib);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_buildas_destroy_input: destroy ib buffer fail!");
                assert(0);
            }
            result = geneate_destroy_as_input(device, geoInfo.triangle.tb);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_buildas_destroy_input: destroy tb buffer fail!");
                assert(0);
            }
        } else if (geoInfo.type == VK_GEOMETRY_TYPE_AABBS_KHR) {
            result = geneate_destroy_as_input(device, geoInfo.aabb.info);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_buildas_destroy_input: destroy aabb buffer fail!");
                assert(0);
            }
        } else if (geoInfo.type == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
            result = geneate_destroy_as_input(device, geoInfo.instance.info);
            if (result != VK_SUCCESS) {
                vktrace_LogError("generate_buildas_destroy_input: destroy instance buffer fail!");
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

VkResult only_buildas_destroy_inputs(VkDevice device, BuildAsInfo& buildAsInfo) {
    VkResult result = VK_SUCCESS;
    int geoInfoSize = buildAsInfo.geometryInfo.size();
    for (int j = 0; j < geoInfoSize; j++) {
        BuildAsGeometryInfo& geoInfo = buildAsInfo.geometryInfo[j];
        if (geoInfo.type == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
            result = only_destroy_as_input(device, geoInfo.triangle.vb);
            if (result != VK_SUCCESS) {
                vktrace_LogError("only_buildas_destroy_inputs: destroy vb buffer fail!");
                assert(0);
            }
            result = only_destroy_as_input(device, geoInfo.triangle.ib);
            if (result != VK_SUCCESS) {
                vktrace_LogError("only_buildas_destroy_inputs: destroy ib buffer fail!");
                assert(0);
            }
            result = only_destroy_as_input(device, geoInfo.triangle.tb);
            if (result != VK_SUCCESS) {
                vktrace_LogError("only_buildas_destroy_inputs: destroy tb buffer fail!");
                assert(0);
            }
        } else if (geoInfo.type == VK_GEOMETRY_TYPE_AABBS_KHR) {
            result = only_destroy_as_input(device, geoInfo.aabb.info);
            if (result != VK_SUCCESS) {
                vktrace_LogError("only_buildas_destroy_inputs: destroy aabb buffer fail!");
                assert(0);
            }
        } else if (geoInfo.type == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
            result = only_destroy_as_input(device, geoInfo.instance.info);
            if (result != VK_SUCCESS) {
                vktrace_LogError("only_buildas_destroy_inputs: destroy instance buffer fail!");
                assert(0);
            }
        } else {
            // a haked type to dump dst as to file
            assert(geoInfo.type == VK_GEOMETRY_TYPE_MAX_ENUM_KHR);
        }
    }

    vktrace_trace_packet_header* pHeader = buildAsInfo.pBuildAccelerationtStructure;
    vktrace_delete_trace_packet_no_lock(&pHeader);

    return result;
}
}  // namespace trim