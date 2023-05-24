#pragma once

#include "vulkan/vulkan.h"
#include "vktrace_vk_vk_packets.h"
#include <vector>
#include <unordered_map>
#include <set>

struct RayTracingPipelineShaderInfo {
    enum ShaderType{
        RAYGEN,
        MISS,
        HIT,
        CALLABLE
    };

    static VkPhysicalDeviceRayTracingPipelinePropertiesKHR physicalRtPropertiesTrace;
    static VkPhysicalDeviceRayTracingPipelinePropertiesKHR physicalRtProperties;

    uint32_t rayGenCount = 0;
    uint32_t rayGenSize = 0;
    uint32_t rayGenStride = 0;
    uint32_t rayGenSizeTrace = 0;
    uint32_t rayGenStrideTrace = 0;

    uint32_t anyHitCount = 0;
    uint32_t closestHitCount = 0;
    uint32_t intersectionCount = 0;

    uint32_t hitCount = 0;
    uint32_t hitSize = 0;
    uint32_t hitStride = 0;
    uint32_t hitSizeTrace = 0;
    uint32_t hitStrideTrace = 0;

    uint32_t missCount = 0;
    uint32_t missSize = 0;
    uint32_t missStride = 0;
    uint32_t missSizeTrace = 0;
    uint32_t missStrideTrace = 0;

    uint32_t callableCount = 0;
    uint32_t callableSize = 0;
    uint32_t callableStride = 0;
    uint32_t callableSizeTrace = 0;
    uint32_t callableStrideTrace = 0;

    std::vector<int> shaderIndices[4];

    uint32_t traceHandleSize = 0;

    uint32_t shaderGroupCount = 0;

    VkBuffer shaderBindingTableBuffer[4] = { 0, 0, 0, 0 };
    VkDeviceMemory shaderBindingTableMemory[4] = { 0, 0, 0, 0 };
    VkDeviceAddress shaderBindingTableDeviceAddress[4] = { 0, 0, 0, 0 };

    std::vector<uint8_t> handleData;
    VkBuffer handleDataBuffer = VK_NULL_HANDLE;
    VkDeviceMemory handleDataMemory = 0;

    uint32_t handleCount() {
        return rayGenCount + anyHitCount + closestHitCount + missCount + intersectionCount + callableCount;
    }

    uint32_t align_up(uint32_t num, uint32_t a) {
        return (num + (a - 1)) / a * a;
    }

    void createHandleDataBuffer(VkDevice device);
    void transitionBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                          VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkDeviceSize offset, VkDeviceSize size);
    void copySBT_CPU(const VkCommandBuffer &remappedcommandBuffer, const VkDeviceMemory &srcMemory, ShaderType shaderType, int offset);
    void copySBT_GPU(const VkCommandBuffer &remappedCommandBuffer, const VkBuffer &srcBuffer, ShaderType shaderType, int offset);
    void writeSBT_CPU(void *pData, ShaderType shaderType);
    void writeSBT_GPU(VkCommandBuffer commandBuffer, ShaderType shaderType);
};

class RayTracingPipelineHandler {
protected:
    struct MemoryAndAddress {
        VkDeviceMemory memory;
        std::set<VkDeviceAddress> addresses;
        MemoryAndAddress() {
            memory = 0;
        }
        MemoryAndAddress(VkDeviceMemory mem) {
            memory = mem;
        }
        MemoryAndAddress(const MemoryAndAddress &r) {
            memory = r.memory;
            addresses = r.addresses;
        }
        MemoryAndAddress &operator=(const MemoryAndAddress &r) {
            memory = r.memory;
            addresses = r.addresses;
            return *this;
        }
    };

    VkPipeline m_currentRayTracingPipeline;
    std::set<VkBuffer> sbtBuffer;
    std::set<VkDeviceSize> sbtBufferSize;
    std::set<VkDeviceMemory> sbtCandidateMemory;
    std::unordered_map<VkBuffer, MemoryAndAddress> sbtBufferToMemory;
    std::unordered_map<VkDeviceAddress, VkBuffer> sbtAddressToBuffer;
    std::unordered_map<VkPipeline, RayTracingPipelineShaderInfo> rayTracingPipelineShaderInfos;
    std::unordered_map<VkPipeline, VkDevice> replayRayTracingPipelinesKHRToDevice;
    std::unordered_map<VkDeviceAddress, VkBuffer>::iterator findClosestAddress(VkDeviceAddress addr);
    void createSBT(RayTracingPipelineShaderInfo &shaderInfo, const VkCommandBuffer &remappedcommandBuffer, RayTracingPipelineShaderInfo::ShaderType shaderType);
public:
    virtual ~RayTracingPipelineHandler() {}
    static bool platformsCompatible();
    VkPipeline getCurrentRayTracingPipeline();
    void setCurrentRayTracingPipeline(VkPipeline pipeline);
    virtual void addSbtBufferFlag(VkBufferUsageFlags &usage) = 0;
    virtual void addSbtBuffer(VkBuffer buffer, VkBufferUsageFlags usage) = 0;
    virtual void addSbtBufferSize(VkBuffer buffer, VkDeviceSize size) = 0;
    virtual void addSbtCandidateMemory(VkDeviceMemory memory, VkDeviceSize origSize) = 0;
    virtual void addSbtBufferAddress(VkBuffer buffer, VkDeviceAddress address) = 0;
    virtual void delSbtBufferAddress(VkBuffer buffer, VkDeviceAddress address) = 0;
    virtual void addSbtBufferMemory(VkBuffer buffer, VkDeviceMemory memory) = 0;
    virtual VkResult getRayTracingShaderGroupHandles(packet_vkGetRayTracingShaderGroupHandlesKHR *pPacket) = 0;
    virtual VkResult createRayTracingPipelinesKHR(packet_vkCreateRayTracingPipelinesKHR *pPacket) = 0;
    virtual void cmdTraceRaysKHR(packet_vkCmdTraceRaysKHR *pPacket) = 0;
};

class RayTracingPipelineHandlerVer1 : public RayTracingPipelineHandler {
    ~RayTracingPipelineHandlerVer1() {}
    void addSbtBufferFlag(VkBufferUsageFlags &usage);
    void addSbtBuffer(VkBuffer buffer, VkBufferUsageFlags usage);
    void addSbtBufferSize(VkBuffer buffer, VkDeviceSize size);
    void addSbtCandidateMemory(VkDeviceMemory memory, VkDeviceSize origSize);
    void addSbtBufferAddress(VkBuffer buffer, VkDeviceAddress address);
    void delSbtBufferAddress(VkBuffer buffer, VkDeviceAddress address);
    void addSbtBufferMemory(VkBuffer buffer, VkDeviceMemory memory);
    VkResult getRayTracingShaderGroupHandles(packet_vkGetRayTracingShaderGroupHandlesKHR *pPacket);
    VkResult createRayTracingPipelinesKHR(packet_vkCreateRayTracingPipelinesKHR *pPacket);
    void cmdTraceRaysKHR(packet_vkCmdTraceRaysKHR *pPacket);
};
