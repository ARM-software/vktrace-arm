#include "vkreplay_raytracingpipeline.h"
#include "vkreplay_vkreplay.h"

extern vkReplay* g_replay;

VkResult vkReplay::manually_replay_vkGetRayTracingShaderGroupHandlesKHR(packet_vkGetRayTracingShaderGroupHandlesKHR *pPacket) {
    if (callFailedDuringTrace(pPacket->result, pPacket->header->packet_id)) {
        vktrace_LogWarning("%d %s failed during trace, exit", pPacket->header->global_packet_index, "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetRayTracingShaderGroupHandlesKHR() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkPipeline remappedpipeline = m_objMapper.remap_pipelines(pPacket->pipeline);
    if (pPacket->pipeline != VK_NULL_HANDLE && remappedpipeline == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in GetRayTracingShaderGroupHandlesKHR() due to invalid remapped VkPipeline.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    // No need to remap firstGroup
    // No need to remap groupCount
    // No need to remap dataSize
    return rtHandler->getRayTracingShaderGroupHandles(pPacket);
}

VkResult vkReplay::manually_replay_vkCreateRayTracingPipelinesKHR(packet_vkCreateRayTracingPipelinesKHR *pPacket) {
    VkDevice remappeddevice = m_objMapper.remap_devices(pPacket->device);
    if (pPacket->device != VK_NULL_HANDLE && remappeddevice == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CreateRayTracingPipelinesKHR() due to invalid remapped VkDevice.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkDeferredOperationKHR remappeddeferredOperation = m_objMapper.remap_deferredoperationkhrs(pPacket->deferredOperation);
    if (pPacket->deferredOperation != VK_NULL_HANDLE && remappeddeferredOperation == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CreateRayTracingPipelinesKHR() due to invalid remapped VkDeferredOperationKHR.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    VkPipelineCache remappedpipelineCache = m_objMapper.remap_pipelinecaches(pPacket->pipelineCache);
    if (pPacket->pipelineCache != VK_NULL_HANDLE && remappedpipelineCache == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CreateRayTracingPipelinesKHR() due to invalid remapped VkPipelineCache.");
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (replayPropertyFeatureInfo.find(remappeddevice) != replayPropertyFeatureInfo.end()) {
        VulkanDevicePropertyFeatureInfo propertyFeatureInfo = replayPropertyFeatureInfo[remappeddevice];
        if (propertyFeatureInfo.feature_rayTracingPipelineShaderGroupHandleCaptureReplay) {
            std::vector<std::vector<VkRayTracingShaderGroupCreateInfoKHR>> modifiedGroups;
            modifiedGroups.resize(pPacket->createInfoCount);
            uint32_t shaderGroupHandleSize = 0;
            std::vector<uint8_t> shaderGroupHandleData;
            uint32_t lastDataSize = 0;

            if (pPacket->pData) {
                for (uint32_t i = 0; i < pPacket->createInfoCount; i++) {
                    shaderGroupHandleSize += propertyFeatureInfo.property_shaderGroupHandleCaptureReplaySize * pPacket->pCreateInfos[i].groupCount;
                }
                shaderGroupHandleData.resize(shaderGroupHandleSize);
                shaderGroupHandleData.assign((uint8_t*)pPacket->pData, (uint8_t*)pPacket->pData + shaderGroupHandleSize);
            }

            VkRayTracingPipelineCreateInfoKHR* modifiedCreateInfos = (VkRayTracingPipelineCreateInfoKHR*)pPacket->pCreateInfos;
            for (uint32_t i = 0; i < pPacket->createInfoCount && shaderGroupHandleSize; ++i) {
                modifiedCreateInfos[i].flags |= VK_PIPELINE_CREATE_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR;

                // restore pShaderGroupCaptureReplayHandle in shader group create infos.
                uint32_t groupCount = modifiedCreateInfos[i].groupCount;
                std::vector<VkRayTracingShaderGroupCreateInfoKHR>& modifiedGroupInfos = modifiedGroups[i];
                modifiedGroupInfos.reserve(groupCount);

                for (uint32_t groupIdx = 0; groupIdx < groupCount; ++groupIdx) {
                    modifiedGroupInfos.push_back(modifiedCreateInfos[i].pGroups[groupIdx]);

                    uint32_t byteOffset = propertyFeatureInfo.property_shaderGroupHandleCaptureReplaySize * groupIdx;
                    modifiedGroupInfos[groupIdx].pShaderGroupCaptureReplayHandle = shaderGroupHandleData.data() + lastDataSize + byteOffset;
                    vktrace_LogAlways("pShaderGroupCaptureReplayHandle allSize:%llu, dataOffset:%llu, groupOffset:%llu.", shaderGroupHandleSize, lastDataSize, byteOffset);
                }

                lastDataSize += propertyFeatureInfo.property_shaderGroupHandleCaptureReplaySize * groupCount;
                // Use modified shader group infos.
                modifiedCreateInfos[i].pGroups = modifiedGroupInfos.data();
            }
        }
    }

    return rtHandler->createRayTracingPipelinesKHR(pPacket);
}

void vkReplay::manually_replay_vkCmdTraceRaysKHR(packet_vkCmdTraceRaysKHR *pPacket) {
    VkCommandBuffer remappedcommandBuffer = m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdTraceRaysKHR() due to invalid remapped VkCommandBuffer.");
        return;
    }
    return rtHandler->cmdTraceRaysKHR(pPacket);
}

//==========================================================================================

unsigned int findMappableMemoryIdx(VkDevice device) {
    VkPhysicalDevice physicalDevice = g_replay->get_ReplayPhysicalDevices().find(device)->second;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (unsigned int i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            return i;
    }
    return 0;
}

VkPhysicalDeviceRayTracingPipelinePropertiesKHR RayTracingPipelineShaderInfo::physicalRtPropertiesTrace = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
VkPhysicalDeviceRayTracingPipelinePropertiesKHR RayTracingPipelineShaderInfo::physicalRtProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};

void RayTracingPipelineShaderInfo::createHandleDataBuffer(VkDevice device) {
    VkBufferCreateInfo createInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                  NULL,
                                  0,
                                  handleData.size(),
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VK_SHARING_MODE_EXCLUSIVE,
                                  0,
                                  NULL
                                  };
    VkResult result = vkCreateBuffer(device, &createInfo, NULL, &handleDataBuffer);
    if (result != VK_SUCCESS) {
        vktrace_LogError("Error when vkCreateBuffer in line %d of %s, result = %d", __LINE__, __func__, result);
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, handleDataBuffer, &requirements);

    unsigned int mappableMemoryIdx = findMappableMemoryIdx(device);

    VkMemoryAllocateInfo allocateInfo {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        NULL,
        requirements.size,
        mappableMemoryIdx
    };
    result = vkAllocateMemory(device, &allocateInfo, NULL, &handleDataMemory);
    if (result != VK_SUCCESS) {
        vktrace_LogError("Error when vkAllocateMemory in line %d of %s, result = %d", __LINE__, __func__, result);
    }

    result = vkBindBufferMemory(device, handleDataBuffer, handleDataMemory, 0);
    if (result != VK_SUCCESS) {
        vktrace_LogError("Error when vkBindBufferMemory in line %d of %s, result = %d", __LINE__, __func__, result);
    }

    void *pData;
    result = vkMapMemory(device, handleDataMemory, 0, VK_WHOLE_SIZE, 0, &pData);
    if (result != VK_SUCCESS) {
        vktrace_LogError("Error when vkMapMemory in line %d of %s, result = %d", __LINE__, __func__, result);
    }
    memcpy(pData, handleData.data(), handleData.size());
    vkUnmapMemory(device, handleDataMemory);
}

void RayTracingPipelineShaderInfo::copySBT_CPU(const VkCommandBuffer &remappedcommandBuffer, const VkDeviceMemory &srcMemory, ShaderType shaderType, int offset) {
    VkDevice replayDevice = 0;
    auto it_device = g_replay->replayCommandBufferToReplayDevice.find(remappedcommandBuffer);
    if (it_device != g_replay->replayCommandBufferToReplayDevice.end())
        replayDevice = it_device->second;

    void *pDst;
    VkResult result = vkMapMemory(replayDevice, shaderBindingTableMemory[shaderType], 0, VK_WHOLE_SIZE, 0, &pDst);
    if (result != VK_SUCCESS) {
        vktrace_LogError("Error when vkMapMemory in line %d of %s, result = %d", __LINE__, __func__, result);
    }

    devicememoryObj local_mem = g_replay->m_objMapper.find_devicememory(srcMemory);
    void *pSrc;
    result = vkMapMemory(replayDevice, local_mem.replayDeviceMemory, 0, VK_WHOLE_SIZE, 0, &pSrc);
    if (result != VK_SUCCESS) {
        vktrace_LogError("Error when vkMapMemory in line %d of %s, result = %d", __LINE__, __func__, result);
    }

    uint8_t *pSrc2 = (uint8_t *)pSrc;
    if (shaderType == RayTracingPipelineShaderInfo::RAYGEN)
        memcpy(pDst, pSrc2 + offset, rayGenSize);
    else if (shaderType == RayTracingPipelineShaderInfo::MISS)
        memcpy(pDst, pSrc2 + offset, missSize);
    else if (shaderType == RayTracingPipelineShaderInfo::HIT)
        memcpy(pDst, pSrc2 + offset, hitSize);
    else if (shaderType == RayTracingPipelineShaderInfo::CALLABLE)
        memcpy(pDst, pSrc2 + offset, callableSize);

    writeSBT_CPU(pDst, shaderType);

    vkUnmapMemory(replayDevice, shaderBindingTableMemory[shaderType]);
    vkUnmapMemory(replayDevice, local_mem.replayDeviceMemory);
}

void RayTracingPipelineShaderInfo::copySBT_GPU(const VkCommandBuffer &remappedCommandBuffer, const VkBuffer &srcBuffer, ShaderType shaderType, int offset) {
    VkBuffer local_buffer = g_replay->m_objMapper.remap_buffers(srcBuffer);
    if (local_buffer == VK_NULL_HANDLE && srcBuffer != VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in copySBT_GPU() due to invalid remapped VkBuffer.");
        return;
    }

    VkBufferCopy pRegions;
    pRegions.srcOffset = offset;
    pRegions.dstOffset = 0;
    if (shaderType == RayTracingPipelineShaderInfo::RAYGEN)
        pRegions.size = rayGenSize;
    else if (shaderType == RayTracingPipelineShaderInfo::MISS)
        pRegions.size = missSize;
    else if (shaderType == RayTracingPipelineShaderInfo::HIT)
        pRegions.size = hitSize;
    else if (shaderType == RayTracingPipelineShaderInfo::CALLABLE)
        pRegions.size = callableSize;
    vkCmdCopyBuffer(remappedCommandBuffer, local_buffer, shaderBindingTableBuffer[shaderType], 1, &pRegions);

    writeSBT_GPU(remappedCommandBuffer, shaderType);
}

void RayTracingPipelineShaderInfo::writeSBT_CPU(void *pData, ShaderType shaderType) {
    char *pDst = (char *)pData;
    char *pSrc = (char *)handleData.data();
    for (size_t i = 0; i < shaderIndices[shaderType].size(); ++i) {
        if (shaderType == RAYGEN)
            memcpy(pDst + i * rayGenStride, pSrc + shaderIndices[shaderType][i] * rayGenStride, physicalRtProperties.shaderGroupHandleSize);
        else if (shaderType == MISS)
            memcpy(pDst + i * missStride, pSrc + shaderIndices[shaderType][i] * missStride, physicalRtProperties.shaderGroupHandleSize);
        else if (shaderType == HIT)
            memcpy(pDst + i * hitStride, pSrc + shaderIndices[shaderType][i] * hitStride, physicalRtProperties.shaderGroupHandleSize);
        else if (shaderType == CALLABLE)
            memcpy(pDst + i * callableStride, pSrc + shaderIndices[shaderType][i] * callableStride, physicalRtProperties.shaderGroupHandleSize);
    }
}

void RayTracingPipelineShaderInfo::writeSBT_GPU(VkCommandBuffer commandBuffer, ShaderType shaderType) {
    VkBufferCopy pRegions;
    pRegions.size = physicalRtProperties.shaderGroupHandleSize;
    for (size_t i = 0; i < shaderIndices[shaderType].size(); ++i) {
        if (shaderType == RAYGEN) {
            pRegions.srcOffset = shaderIndices[shaderType][i] * physicalRtProperties.shaderGroupHandleSize;
            pRegions.dstOffset = i * rayGenStride;
        }
        else if (shaderType == MISS) {
            pRegions.srcOffset = shaderIndices[shaderType][i] * physicalRtProperties.shaderGroupHandleSize;
            pRegions.dstOffset = i * missStride;
        }
        else if (shaderType == HIT) {
            pRegions.srcOffset = shaderIndices[shaderType][i] * physicalRtProperties.shaderGroupHandleSize;
            pRegions.dstOffset = i * hitStride;
        }
        else if (shaderType == CALLABLE) {
            pRegions.srcOffset = shaderIndices[shaderType][i] * physicalRtProperties.shaderGroupHandleSize;
            pRegions.dstOffset = i * callableStride;
        }
        vkCmdCopyBuffer(commandBuffer, handleDataBuffer, shaderBindingTableBuffer[shaderType], 1, &pRegions);
    }
}

//==========================================================================================

bool RayTracingPipelineHandler::isEqual(const VkPhysicalDeviceRayTracingPipelinePropertiesKHR &traceValue, const VkPhysicalDeviceRayTracingPipelinePropertiesKHR &replayValue) {
    bool result = true;
    if (traceValue.shaderGroupHandleSize != replayValue.shaderGroupHandleSize) {
        result = false;
        vktrace_LogError("VkPhysicalDeviceRayTracingPipelinePropertiesKHR::shaderGroupHandleSize on trace platform is %d. However, it's %d on replay platform.", traceValue.shaderGroupHandleSize, replayValue.shaderGroupHandleSize);
    }
    if (traceValue.shaderGroupBaseAlignment != replayValue.shaderGroupBaseAlignment) {
        result = false;
        vktrace_LogError("VkPhysicalDeviceRayTracingPipelinePropertiesKHR::shaderGroupBaseAlignment on trace platform is %d. However, it's %d on replay platform.", traceValue.shaderGroupBaseAlignment, replayValue.shaderGroupBaseAlignment);
    }
    if (traceValue.shaderGroupHandleAlignment != replayValue.shaderGroupHandleAlignment) {
        result = false;
        vktrace_LogError("VkPhysicalDeviceRayTracingPipelinePropertiesKHR::shaderGroupHandleAlignment on trace platform is %d. However, it's %d on replay platform.", traceValue.shaderGroupHandleAlignment, replayValue.shaderGroupHandleAlignment);
    }
    return result;
}

VkPipeline RayTracingPipelineHandler::getCurrentRayTracingPipeline() {
    return m_currentRayTracingPipeline;
}

void RayTracingPipelineHandler::setCurrentRayTracingPipeline(VkPipeline pipeline) {
    m_currentRayTracingPipeline = pipeline;
}

// Some SBT buffer addresses are offset from other SBT buffer addresses in the same pipeline.
// So find the closest address of addr
std::unordered_map<VkDeviceAddress, VkBuffer>::iterator RayTracingPipelineHandler::findClosestAddress(VkDeviceAddress addr) {
    int delta_min = INT_MAX;
    auto it_result = sbtAddressToBuffer.end();
    for (auto it = sbtAddressToBuffer.begin(); it != sbtAddressToBuffer.end(); ++it) {
        int delta = addr - it->first;
        if (abs(delta) < delta_min) {
            delta_min = abs(delta);
            it_result = it;
        }
    }
    return it_result;
}

void RayTracingPipelineHandler::createSBT(RayTracingPipelineShaderInfo &shaderInfo, const VkCommandBuffer &remappedcommandBuffer, RayTracingPipelineShaderInfo::ShaderType shaderType) {
    VkDeviceSize bufferSize = 0;
    switch(shaderType) {
        case RayTracingPipelineShaderInfo::RAYGEN:
            bufferSize = shaderInfo.rayGenSize;
            break;
        case RayTracingPipelineShaderInfo::MISS:
            bufferSize = shaderInfo.missSize;
            break;
        case RayTracingPipelineShaderInfo::HIT:
            bufferSize = shaderInfo.hitSize;
            break;
        case RayTracingPipelineShaderInfo::CALLABLE:
            bufferSize = shaderInfo.callableSize;
            break;
        default:
            break;
    }
    VkBufferCreateInfo createInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                  NULL,
                                  0,
                                  bufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR,
                                  VK_SHARING_MODE_EXCLUSIVE,
                                  0,
                                  NULL
                                  };
    VkDevice replayDevice = 0;
    auto it_device = g_replay->replayCommandBufferToReplayDevice.find(remappedcommandBuffer);
    if (it_device != g_replay->replayCommandBufferToReplayDevice.end())
        replayDevice = it_device->second;
    VkResult result = vkCreateBuffer(replayDevice, &createInfo, NULL, &shaderInfo.shaderBindingTableBuffer[shaderType]);
    if (result != VK_SUCCESS) {
        vktrace_LogError("Error when vkCreateBuffer in line %d of %s, result = %d", __LINE__, __func__, result);
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(replayDevice, shaderInfo.shaderBindingTableBuffer[shaderType], &requirements);

    unsigned int mappableMemoryIdx = findMappableMemoryIdx(replayDevice);

    VkMemoryAllocateFlagsInfo flagsInfo {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        NULL,
        VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
        0
    };
    VkMemoryAllocateInfo allocateInfo {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        &flagsInfo,
        requirements.size,
        mappableMemoryIdx
    };
    result = vkAllocateMemory(replayDevice, &allocateInfo, NULL, &shaderInfo.shaderBindingTableMemory[shaderType]);
    if (result != VK_SUCCESS) {
        vktrace_LogError("Error when vkAllocateMemory in line %d of %s, result = %d", __LINE__, __func__, result);
    }

    result = vkBindBufferMemory(replayDevice, shaderInfo.shaderBindingTableBuffer[shaderType], shaderInfo.shaderBindingTableMemory[shaderType], 0);
    if (result != VK_SUCCESS) {
        vktrace_LogError("Error when vkBindBufferMemory in line %d of %s, result = %d", __LINE__, __func__, result);
    }

    VkBufferDeviceAddressInfo bufferDeviceInfo{
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        NULL,
        shaderInfo.shaderBindingTableBuffer[shaderType]
    };
    shaderInfo.shaderBindingTableDeviceAddress[shaderType] = vkGetBufferDeviceAddress(replayDevice, &bufferDeviceInfo);
}

//==========================================================================================

VkResult RayTracingPipelineHandlerVer1::getRayTracingShaderGroupHandles(packet_vkGetRayTracingShaderGroupHandlesKHR *pPacket) {
    VkPipeline remappedpipeline = g_replay->m_objMapper.remap_pipelines(pPacket->pipeline);
    VkDevice remappeddevice = g_replay->m_objMapper.remap_devices(pPacket->device);

    auto it = rayTracingPipelineShaderInfos.find(remappedpipeline);
    it->second.traceHandleSize = pPacket->dataSize;
    size_t dataSize = it->second.shaderGroupCount * RayTracingPipelineShaderInfo::physicalRtProperties.shaderGroupHandleSize;
    if (dataSize < pPacket->dataSize) {
        dataSize = pPacket->dataSize;
    }
    it->second.handleData.resize(dataSize);

    // No need to remap pData
    VkResult replayResult = g_replay->m_vkDeviceFuncs.GetRayTracingShaderGroupHandlesKHR(remappeddevice, remappedpipeline, 0, it->second.shaderGroupCount, dataSize, it->second.handleData.data());
    it->second.createHandleDataBuffer(remappeddevice);

    return replayResult;
}

VkResult RayTracingPipelineHandlerVer1::createRayTracingPipelinesKHR(packet_vkCreateRayTracingPipelinesKHR *pPacket) {
    VkDevice remappeddevice = g_replay->m_objMapper.remap_devices(pPacket->device);
    VkDeferredOperationKHR remappeddeferredOperation = g_replay->m_objMapper.remap_deferredoperationkhrs(pPacket->deferredOperation);
    VkPipelineCache remappedpipelineCache = g_replay->m_objMapper.remap_pipelinecaches(pPacket->pipelineCache);

    std::vector<RayTracingPipelineShaderInfo> rqInfos;
    for (uint32_t i = 0; i < pPacket->createInfoCount; i++) {
        const_cast<VkRayTracingPipelineCreateInfoKHR*>(pPacket->pCreateInfos)[i].layout = g_replay->m_objMapper.remap_pipelinelayouts(pPacket->pCreateInfos[i].layout);
        RayTracingPipelineShaderInfo rqInfo;
        for(uint32_t j = 0; j < pPacket->pCreateInfos[i].stageCount; j++) {
            ((VkPipelineShaderStageCreateInfo*)(((VkRayTracingPipelineCreateInfoKHR*)(pPacket->pCreateInfos))[i].pStages))[j].module = g_replay->m_objMapper.remap_shadermodules(pPacket->pCreateInfos[i].pStages[j].module);
            if (pPacket->pCreateInfos[i].pStages[j].stage & VK_SHADER_STAGE_RAYGEN_BIT_KHR) {
                rqInfo.rayGenCount++;
            }
            if (pPacket->pCreateInfos[i].pStages[j].stage & VK_SHADER_STAGE_ANY_HIT_BIT_KHR) {
                rqInfo.anyHitCount++;
            }
            if (pPacket->pCreateInfos[i].pStages[j].stage & VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) {
                rqInfo.closestHitCount++;
            }
            if (pPacket->pCreateInfos[i].pStages[j].stage & VK_SHADER_STAGE_MISS_BIT_KHR) {
                rqInfo.missCount++;
            }
            if (pPacket->pCreateInfos[i].pStages[j].stage & VK_SHADER_STAGE_INTERSECTION_BIT_KHR) {
                rqInfo.intersectionCount++;
            }
            if (pPacket->pCreateInfos[i].pStages[j].stage & VK_SHADER_STAGE_CALLABLE_BIT_KHR) {
                rqInfo.callableCount++;
            }
        }
        for (uint32_t j = 0; j < pPacket->pCreateInfos[i].groupCount; ++j) {
            if (pPacket->pCreateInfos[i].pGroups[j].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR) {
                uint32_t shaderIdx = pPacket->pCreateInfos[i].pGroups[j].generalShader;
                if (pPacket->pCreateInfos[i].pStages[shaderIdx].stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR)
                {
                    rqInfo.shaderIndices[RayTracingPipelineShaderInfo::RAYGEN].push_back(j);
                }
                else if (pPacket->pCreateInfos[i].pStages[shaderIdx].stage == VK_SHADER_STAGE_MISS_BIT_KHR)
                {
                    rqInfo.shaderIndices[RayTracingPipelineShaderInfo::MISS].push_back(j);
                }
                else if (pPacket->pCreateInfos[i].pStages[shaderIdx].stage == VK_SHADER_STAGE_CALLABLE_BIT_KHR)
                {
                    rqInfo.shaderIndices[RayTracingPipelineShaderInfo::CALLABLE].push_back(j);
                }
            }
            else
            {
                rqInfo.shaderIndices[RayTracingPipelineShaderInfo::HIT].push_back(j);
            }
        }
        rqInfo.shaderGroupCount = pPacket->pCreateInfos[i].groupCount;
        rqInfos.push_back(rqInfo);

        if (pPacket->pCreateInfos[i].pLibraryInfo != nullptr) {
            for (uint32_t j = 0; j < pPacket->pCreateInfos[i].pLibraryInfo->libraryCount; j++) {
               ((VkPipeline*)(((VkPipelineLibraryCreateInfoKHR*)(((VkRayTracingPipelineCreateInfoKHR*)(pPacket->pCreateInfos))[i].pLibraryInfo))->pLibraries))[j] = g_replay->m_objMapper.remap_pipelines(pPacket->pCreateInfos[i].pLibraryInfo->pLibraries[j]);
            }
        }
    }
    // No need to remap createInfoCount
    // No need to remap pAllocator
    VkPipeline *local_pPipelines = (VkPipeline *)vktrace_malloc(pPacket->createInfoCount * sizeof(VkPipeline));
    VkResult replayResult = g_replay->m_vkDeviceFuncs.CreateRayTracingPipelinesKHR(remappeddevice, remappeddeferredOperation, remappedpipelineCache, pPacket->createInfoCount, pPacket->pCreateInfos, pPacket->pAllocator, local_pPipelines);
    if (replayResult == VK_SUCCESS) {
        for (unsigned int i = 0; i < pPacket->createInfoCount; ++i) {
            g_replay->m_objMapper.add_to_pipelines_map((pPacket->pPipelines)[i], local_pPipelines[i]);
            replayRayTracingPipelinesKHRToDevice[local_pPipelines[i]] = remappeddevice;
            rayTracingPipelineShaderInfos[local_pPipelines[i]] = rqInfos[i];
        }
    }
    vktrace_free(local_pPipelines);

    return replayResult;
}

void RayTracingPipelineHandlerVer1::addSbtBufferFlag(VkBufferUsageFlags &usage) {
    if (usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) {
        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
}
void RayTracingPipelineHandlerVer1::addSbtBuffer(VkBuffer buffer, VkBufferUsageFlags usage) {
    if (usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) {
        sbtBuffer.insert(buffer);
    }
}

void RayTracingPipelineHandlerVer1::addSbtBufferSize(VkBuffer buffer, VkDeviceSize size) {
    auto it = sbtBuffer.find(buffer);
    if (it != sbtBuffer.end()) {
        sbtBufferSize.insert(size);
    }
}

void RayTracingPipelineHandlerVer1::addSbtCandidateMemory(VkDeviceMemory memory, VkDeviceSize origSize) {
    // TODO: the conditional statement is error on the AMD, but NV is OK.
    // if (sbtBufferSize.find(origSize) != sbtBufferSize.end())
    {
        sbtCandidateMemory.insert(memory);
    }
}

void RayTracingPipelineHandlerVer1::addSbtBufferAddress(VkBuffer buffer, VkDeviceAddress address) {
    auto it = sbtBufferToMemory.find(buffer);
    if (it != sbtBufferToMemory.end()) {
        it->second.addresses.insert(address);
        sbtAddressToBuffer[address] = buffer;
    }
}

void RayTracingPipelineHandlerVer1::addSbtBufferMemory(VkBuffer buffer, VkDeviceMemory memory) {
    if (sbtBuffer.find(buffer) != sbtBuffer.end() && sbtCandidateMemory.find(memory) != sbtCandidateMemory.end()) {
        MemoryAndAddress ma(memory);
        sbtBufferToMemory[buffer] = ma;
    }
}

void RayTracingPipelineHandlerVer1::cmdTraceRaysKHR(packet_vkCmdTraceRaysKHR *pPacket) {
    VkCommandBuffer remappedcommandBuffer = g_replay->m_objMapper.remap_commandbuffers(pPacket->commandBuffer);
    if (pPacket->commandBuffer != VK_NULL_HANDLE && remappedcommandBuffer == VK_NULL_HANDLE) {
        vktrace_LogError("Error detected in CmdTraceRaysKHR() due to invalid remapped VkCommandBuffer.");
        return;
    }
    bool found = false;
    auto it = rayTracingPipelineShaderInfos.begin();
    for (; it != rayTracingPipelineShaderInfos.end(); ++it) {
        if (it->first == m_currentRayTracingPipeline) {
            found = true;
            break;
        }
    }
    if (!found) {
        vktrace_LogError("%llu, %s: Cannot find ray tracing pipeline.", pPacket->header->global_packet_index, "vkCmdTraceRays");
    }
    it->second.rayGenSize = pPacket->pRaygenShaderBindingTable->size;
    it->second.rayGenStride = pPacket->pRaygenShaderBindingTable->stride;
    it->second.missSize = pPacket->pMissShaderBindingTable->size;
    it->second.missStride = pPacket->pMissShaderBindingTable->stride;
    it->second.hitSize = pPacket->pHitShaderBindingTable->size;
    it->second.hitStride = pPacket->pHitShaderBindingTable->stride;
    it->second.callableSize = pPacket->pCallableShaderBindingTable->size;
    it->second.callableStride = pPacket->pCallableShaderBindingTable->stride;
    if (pPacket->pRaygenShaderBindingTable != nullptr && pPacket->pRaygenShaderBindingTable->deviceAddress) {
        int offset = 0;
        auto it2 = sbtAddressToBuffer.find(pPacket->pRaygenShaderBindingTable->deviceAddress);
        if (it2 == sbtAddressToBuffer.end()) {
            vktrace_LogError("Cannot find address for raygen = %llu", pPacket->pRaygenShaderBindingTable->deviceAddress);
            return;
        }
        if (it->second.shaderBindingTableBuffer[RayTracingPipelineShaderInfo::RAYGEN] == 0) {
            createSBT(it->second, remappedcommandBuffer, RayTracingPipelineShaderInfo::RAYGEN);
        }
        it->second.copySBT_GPU(remappedcommandBuffer, it2->second, RayTracingPipelineShaderInfo::RAYGEN, offset);
        const_cast<VkStridedDeviceAddressRegionKHR*>(pPacket->pRaygenShaderBindingTable)->deviceAddress = it->second.shaderBindingTableDeviceAddress[RayTracingPipelineShaderInfo::RAYGEN];
    }
    if (pPacket->pMissShaderBindingTable != nullptr && pPacket->pMissShaderBindingTable->deviceAddress) {
        int offset = 0;
        auto it2 = sbtAddressToBuffer.find(pPacket->pMissShaderBindingTable->deviceAddress);
        if (it2 == sbtAddressToBuffer.end()) {
            vktrace_LogWarning("Cannot find address for miss = %llu", pPacket->pMissShaderBindingTable->deviceAddress);
            it2 = findClosestAddress(pPacket->pMissShaderBindingTable->deviceAddress);
        }
        if (it2 == sbtAddressToBuffer.end()) {
            vktrace_LogError("Cannot find address for miss = %llu", pPacket->pMissShaderBindingTable->deviceAddress);
            return;
        }
        offset = pPacket->pMissShaderBindingTable->deviceAddress - it2->first;
        if (it->second.shaderBindingTableBuffer[RayTracingPipelineShaderInfo::MISS] == 0) {
            createSBT(it->second, remappedcommandBuffer, RayTracingPipelineShaderInfo::MISS);
        }
        it->second.copySBT_GPU(remappedcommandBuffer, it2->second, RayTracingPipelineShaderInfo::MISS, offset);
        const_cast<VkStridedDeviceAddressRegionKHR*>(pPacket->pMissShaderBindingTable)->deviceAddress = it->second.shaderBindingTableDeviceAddress[RayTracingPipelineShaderInfo::MISS];
    }
    if (pPacket->pHitShaderBindingTable != nullptr && pPacket->pHitShaderBindingTable->deviceAddress) {
        int offset = 0;
        auto it2 = sbtAddressToBuffer.find(pPacket->pHitShaderBindingTable->deviceAddress);
        if (it2 == sbtAddressToBuffer.end()) {
            vktrace_LogWarning("Cannot find address for hit = %llu", pPacket->pHitShaderBindingTable->deviceAddress);
            it2 = findClosestAddress(pPacket->pHitShaderBindingTable->deviceAddress);
        }
        if (it2 == sbtAddressToBuffer.end()) {
            vktrace_LogError("Cannot find address for hit = %llu", pPacket->pHitShaderBindingTable->deviceAddress);
            return;
        }
        offset = pPacket->pHitShaderBindingTable->deviceAddress - it2->first;
        if (it->second.shaderBindingTableBuffer[RayTracingPipelineShaderInfo::HIT] == 0) {
            createSBT(it->second, remappedcommandBuffer, RayTracingPipelineShaderInfo::HIT);
        }
        it->second.copySBT_GPU(remappedcommandBuffer, it2->second, RayTracingPipelineShaderInfo::HIT, offset);
        const_cast<VkStridedDeviceAddressRegionKHR*>(pPacket->pHitShaderBindingTable)->deviceAddress = it->second.shaderBindingTableDeviceAddress[RayTracingPipelineShaderInfo::HIT];
    }
    if (pPacket->pCallableShaderBindingTable != nullptr && pPacket->pCallableShaderBindingTable->deviceAddress) {
        int offset = 0;
        auto it2 = sbtAddressToBuffer.find(pPacket->pCallableShaderBindingTable->deviceAddress);
        if (it2 == sbtAddressToBuffer.end()) {
            vktrace_LogWarning("Cannot find address for callable = %llu", pPacket->pCallableShaderBindingTable->deviceAddress);
            it2 = findClosestAddress(pPacket->pCallableShaderBindingTable->deviceAddress);
        }
        if (it2 == sbtAddressToBuffer.end()) {
            vktrace_LogError("Cannot find address for callable = %llu", pPacket->pCallableShaderBindingTable->deviceAddress);
            return;
        }
        offset = pPacket->pCallableShaderBindingTable->deviceAddress - it2->first;
        if (it->second.shaderBindingTableBuffer[RayTracingPipelineShaderInfo::CALLABLE] == 0) {
            createSBT(it->second, remappedcommandBuffer, RayTracingPipelineShaderInfo::CALLABLE);
        }
        it->second.copySBT_GPU(remappedcommandBuffer, it2->second, RayTracingPipelineShaderInfo::CALLABLE, offset);
        const_cast<VkStridedDeviceAddressRegionKHR*>(pPacket->pCallableShaderBindingTable)->deviceAddress = it->second.shaderBindingTableDeviceAddress[RayTracingPipelineShaderInfo::CALLABLE];
    }
    // No need to remap width
    // No need to remap height
    // No need to remap depth
    g_replay->m_vkDeviceFuncs.CmdTraceRaysKHR(remappedcommandBuffer, pPacket->pRaygenShaderBindingTable, pPacket->pMissShaderBindingTable, pPacket->pHitShaderBindingTable, pPacket->pCallableShaderBindingTable, pPacket->width, pPacket->height, pPacket->depth);
}
