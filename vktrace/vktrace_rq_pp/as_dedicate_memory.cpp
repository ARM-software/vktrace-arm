#include <unordered_map>
#include <vector>

#include "vktrace_vk_packet_id.h"
#include "vktrace_rq_pp.h"
#include "vk_struct_size_helper.h"

using namespace std;

// For finding how many buffers are bound to each VkDeviceMemory
class Mem2Buf {
    struct MemInfo {
        uint64_t memAllocGid;
        list<VkBuffer> bufList;
        uint64_t memFreeGid;
        MemInfo(uint64_t allocGid) {
            memAllocGid = allocGid;
            memFreeGid = 0;
        }
    };
    unordered_map<VkDeviceMemory, list<MemInfo>> mem2memInfo;
public:
    void allocMem(VkDeviceMemory mem, uint64_t gid) {
        if (mem2memInfo.find(mem) != mem2memInfo.end()) {
            auto it = mem2memInfo[mem].rbegin();
            if (it->memFreeGid == 0) {
                vktrace_LogError("Device memory %llu was created again before being freed!", mem);
            }
            else {
                MemInfo memInfo(gid);
                mem2memInfo[mem].push_back(memInfo);
            }
        }
        else {
            MemInfo memInfo(gid);
            mem2memInfo[mem].push_back(memInfo);
        }
    }
    void freeMem(VkDeviceMemory mem, uint64_t gid) {
        if (mem2memInfo.find(mem) != mem2memInfo.end()) {
            auto it = mem2memInfo[mem].rbegin();
            if (it->memFreeGid != 0) {
                vktrace_LogError("Device memory %llu was freed twice!", mem);
            }
            else {
                it->memFreeGid = gid;
            }
        }
        else {
            vktrace_LogError("Trying to free a nonexistent device memory %llu!", mem);
        }
    }
    void freeAllMem(uint64_t gid) {
        for (auto it = mem2memInfo.begin(); it != mem2memInfo.end(); ++it) {
            for (auto it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
                if (it2->memFreeGid == 0)
                    it2->memFreeGid = gid;
            }
        }
    }
    void bindBufMem(VkBuffer buf, VkDeviceMemory mem) {
        if (mem2memInfo.find(mem) != mem2memInfo.end()) {
            auto it = mem2memInfo[mem].rbegin();
            if (it->memFreeGid != 0) {
                vktrace_LogError("Device memory %llu has already been freed. But now buffer %llu are trying to bind to it!", mem, buf);
            }
            else {
                it->bufList.push_back(buf);
            }
        }
        else {
            vktrace_LogError("Trying to bind buffer %llu to a nonexistent device memory %llu!", mem);
        }
    }
    int bufCount(VkDeviceMemory mem, VkBuffer buf, uint64_t gid) {
        if (mem2memInfo.find(mem) != mem2memInfo.end()) {
            for (auto it = mem2memInfo[mem].begin(); it != mem2memInfo[mem].end(); ++it) {
                if (it->memAllocGid <= gid && gid < it->memFreeGid) {
                    return it->bufList.size();
                }
            }
        }
        else {
            vktrace_LogError("Trying to calculate number of buffers bound to a nonexistent device memory %llu!", mem);
        }
        return 0;
    }
    void print() {
        for (auto it = mem2memInfo.begin(); it != mem2memInfo.end(); ++it) {
            vktrace_LogAlways("Device memory = %llu", it->first);
            if (it->second.size() > 1) {
                vktrace_LogAlways("This devicd memory was created and freed for more than 1 time.");
            }
            for (auto it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
                vktrace_LogAlways("\talloc = %llu, free = %llu", it2->memAllocGid, it2->memFreeGid);
                if (it2->bufList.size() > 1) {
                    vktrace_LogAlways("There are more than 1 buffer bound to this device memory.");
                }
                for (auto it3 = it2->bufList.begin(); it3 != it2->bufList.end(); ++it3) {
                    vktrace_LogAlways("\t\tbuffer = %llu", *it3);
                }
            }
        }
    }
};

Mem2Buf g_mem2Buf;
uint64_t g_packet_index;    // pHeader->global_packet_index in fast-forwarded trace files may not in order. This one gives them an ordered index.

struct move_memory_params {
    VkDevice device = VK_NULL_HANDLE;
    uint64_t packet_index = 0;
    std::unordered_map<uint64_t, vktrace_trace_packet_header*> packets_mov_from;
    std::unordered_map<uint64_t, std::vector<uint64_t>> packets_mov_to;
};
static move_memory_params g_mem_params;

typedef struct as_size_info {
    VkBuildAccelerationStructureFlagsKHR flags;
    VkAccelerationStructureBuildSizesInfoKHR sizes;
} as_size_info;

typedef struct remap_as_info {
    VkBuffer remapBuffer = 0;
    VkDeviceMemory remapMemory = 0;
    VkAccelerationStructureKHR remapAs = 0;
    VkDeviceSize remapSize = 0;
    VkDeviceAddress bufDeviceAddr = 0;
    VkDeviceAddress asDeviceAddr = 0;
    uint64_t handleCount = 0;
} remap_as_info;

typedef struct scratch_buffer_info {
    VkBuffer    buffer;
    VkDeviceMemory  memory;
    VkDeviceSize    size;
    uint32_t    triangle_prim_count;
    uint32_t    aabb_prim_count;
    uint32_t    instance_prim_count;
    VkDeviceAddress address;
    uint32_t    info_index;
    uint32_t    memoryTypeIndex;
}scratch_buffer_info;

static unordered_map<VkDevice, unordered_map<VkBuffer, buffer_info>> g_vkDevice2Buf2BufInfo;
static unordered_map<VkDevice, unordered_map<VkDeviceAddress, VkBuffer>> g_vkDevice2Addr2Buf;
static unordered_map<VkDevice, unordered_map<VkDeviceMemory, mem_info>> g_vkDevice2Mem2MemInfo;
static unordered_map<VkCommandBuffer, VkDevice> g_vkCmdBuf2Device;
static unordered_map<VkDevice, unordered_map<uint32_t, unordered_map<uint32_t, vector<as_size_info>>>> g_vkDevice2AsBuildType2AsType2AsSizeInfo;
static unordered_map<VkDevice, unordered_map<uint32_t, unordered_map<uint32_t, vector<unordered_map<uint32_t, uint32_t>>>>> g_vkDevice2AsBuildType2AsType2AsPrimitiveCount;
static unordered_map<uint64_t, unordered_map<VkBuffer, buffer_info>> g_fixAsBufSize_recreate_mem_gid;
static unordered_map<uint64_t, unordered_map<VkBuffer, buffer_info>> g_fixAsBufSize_potential_recreate_mem_gid;
static unordered_map<uint64_t, buffer_info> g_fixAsBufSize_recreate_buffer_mem_gid;
static unordered_map<VkBuffer, vector<VkDeviceAddress>> g_scratchBuffer2DeviceAddrs;
static unordered_map<VkBuffer, vector<VkAccelerationStructureKHR>> g_asBuffer2AccelerationStructures;
static unordered_map<VkBuffer, vector<VkBuffer>> g_extraAddedBuffer2Buffers;
static unordered_map<VkDeviceMemory, vector<VkDeviceMemory>>g_extraAddedMemory2Memorys;
static unordered_map<VkAccelerationStructureKHR, vector<VkBuffer>> g_extraAddedAS2Buffers;
static unordered_map<VkAccelerationStructureKHR, vector<VkDeviceMemory>>g_extraAddedAS2Memorys;
static remap_as_info g_Handles;
static uint64_t g_max_gid = 0;
static VkDeviceAddress g_max_bufferDeviceAddress = 0;

// recreate scratch buffer
static unordered_map<VkDevice, unordered_map<VkAccelerationStructureKHR, unordered_map<VkDeviceAddress, scratch_buffer_info>>> g_dev2AS2ScratchAddr2ScratchInfo;
static unordered_map<uint64_t, vector<scratch_buffer_info>> g_fixAsBufSize_recreate_scratch_gid;
static unordered_map<uint64_t, vector<uint32_t>>g_fixAsBufSize_replace_scratch_gid;

static int pre_fixasbufsize_create_buffer(vktrace_trace_packet_header*& pHeader) {
    packet_vkCreateBuffer* pPacket = interpret_body_as_vkCreateBuffer(pHeader);
    VkMemoryRequirements memReqs = {0, 0, 0};
    buffer_info bufInfo = {.device = pPacket->device,
                           .buffer = *pPacket->pBuffer,
                           .size = pPacket->pCreateInfo->size,
                           .usage = pPacket->pCreateInfo->usage,
                           .memory = VK_NULL_HANDLE,
                           .memoryOffset = 0,
                           .bind_packet_gid = 0,
                           .memoryTypeIndex = 0,
                           .packet_index = g_mem_params.packet_index,
                           .alloc_mem_packet_index = 0,
                           .memoryRequirements = memReqs};
    g_vkDevice2Buf2BufInfo[pPacket->device][bufInfo.buffer] = bufInfo;
    return 0;
}

static int pre_fixasbufsize_GetBufferMemoryRequirements(vktrace_trace_packet_header* &pHeader)
{
    if (pHeader->packet_id == VKTRACE_TPI_VK_vkGetBufferMemoryRequirements) {
        packet_vkGetBufferMemoryRequirements* pPacket = interpret_body_as_vkGetBufferMemoryRequirements(pHeader);
        g_vkDevice2Buf2BufInfo[pPacket->device][pPacket->buffer].memoryRequirements = *pPacket->pMemoryRequirements;
    } else if (pHeader->packet_id == VKTRACE_TPI_VK_vkGetBufferMemoryRequirements2) {
        packet_vkGetBufferMemoryRequirements2* pPacket = interpret_body_as_vkGetBufferMemoryRequirements2(pHeader);
        g_vkDevice2Buf2BufInfo[pPacket->device][pPacket->pInfo->buffer].memoryRequirements = pPacket->pMemoryRequirements->memoryRequirements;
    } else if (pHeader->packet_id == VKTRACE_TPI_VK_vkGetBufferMemoryRequirements2KHR) {
        packet_vkGetBufferMemoryRequirements2KHR* pPacket = interpret_body_as_vkGetBufferMemoryRequirements2KHR(pHeader);
        g_vkDevice2Buf2BufInfo[pPacket->device][pPacket->pInfo->buffer].memoryRequirements = pPacket->pMemoryRequirements->memoryRequirements;
    }

    return 0;
}

static void check_buffer_memory_call_sequence(buffer_info& bufInfo, mem_info& memInfo, VkDeviceSize offset)
{
    // vkAllocateMemory is ahead of vkCreateBuffer
    // This is not friendly when the buffer for many cases
    // a. if buffer is bound to accelerationStructure, we probably need to resize the buffer and memory size
    // b. if buffer size and alignment is different from trace device and replay device, we need resize the memory when replay
    // record such packets and adjust the vkAllocateMemory call right before the vkBundBufferMemory call in post pass
    // but for a memory bound with multiple buffers case, we can do nothing here
    // so just record the dedicate memory allocation cases
    if (bufInfo.packet_index > bufInfo.alloc_mem_packet_index &&
        bufInfo.size == memInfo.size &&
        offset == 0) {
        g_mem_params.packets_mov_from[memInfo.packet_index] = nullptr;
        g_mem_params.packets_mov_to[g_mem_params.packet_index].push_back(memInfo.packet_index);
    }
}

static int pre_fixasbufsize_bufMem_bind_buffer_memory(vktrace_trace_packet_header*& pHeader) {
    packet_vkBindBufferMemory* pPacket = interpret_body_as_vkBindBufferMemory(pHeader);
    g_mem2Buf.bindBufMem(pPacket->buffer, pPacket->memory);
    return 0;
}

static int pre_fixasbufsize_bind_buffer_memory(vktrace_trace_packet_header*& pHeader) {
    packet_vkBindBufferMemory* pPacket = interpret_body_as_vkBindBufferMemory(pHeader);
    buffer_info& bufInfo = g_vkDevice2Buf2BufInfo[pPacket->device][pPacket->buffer];
    mem_info& memInfo = g_vkDevice2Mem2MemInfo[pPacket->device][pPacket->memory];

    bufInfo.memory = pPacket->memory;
    bufInfo.memoryOffset = pPacket->memoryOffset;
    bufInfo.bind_packet_gid = pHeader->global_packet_index;
    bufInfo.memoryTypeIndex = memInfo.memoryTypeIndex;
    bufInfo.alloc_mem_packet_index = memInfo.packet_index;

    check_buffer_memory_call_sequence(bufInfo, memInfo, pPacket->memoryOffset);
    if ((bufInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) && (memInfo.flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT)) {
        if ((bufInfo.usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) &&
            !(bufInfo.usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
            !(bufInfo.usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) &&
            !(bufInfo.usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) &&
            !(bufInfo.usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR) &&
            (bufInfo.packet_index > memInfo.packet_index)) {
            g_fixAsBufSize_potential_recreate_mem_gid[bufInfo.bind_packet_gid][pPacket->buffer] = bufInfo;
        }

        if ((bufInfo.usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
            !(bufInfo.usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
            !(bufInfo.usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) &&
            !(bufInfo.usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) &&
            !(bufInfo.usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR) &&
            (bufInfo.packet_index > memInfo.packet_index)) {
            g_fixAsBufSize_potential_recreate_mem_gid[bufInfo.bind_packet_gid][pPacket->buffer] = bufInfo;
        }
    }
    return 0;
}

static int pre_fixasbufsize_bufMem_bind_buffer_memory2(vktrace_trace_packet_header*& pHeader) {
    packet_vkBindBufferMemory2* pPacket = interpret_body_as_vkBindBufferMemory2(pHeader);
    for (uint32_t index = 0; index < pPacket->bindInfoCount; index++) {
        g_mem2Buf.bindBufMem(pPacket->pBindInfos[index].buffer, pPacket->pBindInfos[index].memory);
    }
    return 0;
}

static int pre_fixasbufsize_bind_buffer_memory2(vktrace_trace_packet_header*& pHeader) {
    packet_vkBindBufferMemory2* pPacket = interpret_body_as_vkBindBufferMemory2(pHeader);
    for (uint32_t index = 0; index < pPacket->bindInfoCount; index++) {
        buffer_info& bufInfo = g_vkDevice2Buf2BufInfo[pPacket->device][pPacket->pBindInfos[index].buffer];
        mem_info& memInfo = g_vkDevice2Mem2MemInfo[pPacket->device][pPacket->pBindInfos[index].memory];

        bufInfo.memory = pPacket->pBindInfos[index].memory;
        bufInfo.memoryOffset = pPacket->pBindInfos[index].memoryOffset;
        bufInfo.bind_packet_gid = pHeader->global_packet_index;
        bufInfo.memoryTypeIndex = memInfo.memoryTypeIndex;

        check_buffer_memory_call_sequence(bufInfo, memInfo, pPacket->pBindInfos[index].memoryOffset);
        if ((bufInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) && (memInfo.flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT)) {
            if ((bufInfo.usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) &&
                !(bufInfo.usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
                !(bufInfo.usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) &&
                !(bufInfo.usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) &&
                !(bufInfo.usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR) &&
                (bufInfo.packet_index > memInfo.packet_index)) {
                g_fixAsBufSize_potential_recreate_mem_gid[bufInfo.bind_packet_gid][pPacket->pBindInfos[index].buffer] = bufInfo;
            }

            if ((bufInfo.usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
                !(bufInfo.usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
                !(bufInfo.usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) &&
                !(bufInfo.usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) &&
                !(bufInfo.usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR) &&
                (bufInfo.packet_index > memInfo.packet_index)) {
                g_fixAsBufSize_potential_recreate_mem_gid[bufInfo.bind_packet_gid][pPacket->pBindInfos[index].buffer] = bufInfo;
            }
        }
    }
    return 0;
}

static int pre_fixasbufsize_destroy_buffer(vktrace_trace_packet_header*& pHeader) {
    packet_vkDestroyBuffer* pPacket = interpret_body_as_vkDestroyBuffer(pHeader);
    g_vkDevice2Buf2BufInfo[pPacket->device].erase(pPacket->buffer);

    auto itAddr2Buf = g_vkDevice2Addr2Buf[pPacket->device].begin();
    while (itAddr2Buf != g_vkDevice2Addr2Buf[pPacket->device].end()) {
        if (itAddr2Buf->second == pPacket->buffer) {
            itAddr2Buf = g_vkDevice2Addr2Buf[pPacket->device].erase(itAddr2Buf);
        } else {
            itAddr2Buf++;
        }
    }
    return 0;
}

static int pre_fixasbufsize_create_as(vktrace_trace_packet_header*& pHeader) {
    packet_vkCreateAccelerationStructureKHR* pPacket = interpret_body_as_vkCreateAccelerationStructureKHR(pHeader);
    buffer_info& bufInfo = g_vkDevice2Buf2BufInfo[pPacket->device][pPacket->pCreateInfo->buffer];
    mem_info& memInfo = g_vkDevice2Mem2MemInfo[pPacket->device][bufInfo.memory];
    g_asBuffer2AccelerationStructures[pPacket->pCreateInfo->buffer].push_back(*pPacket->pAccelerationStructure);

    if (pPacket->pCreateInfo->offset == 0) {
        if (g_mem2Buf.bufCount(memInfo.memory, pPacket->pCreateInfo->buffer, g_packet_index) >= 2) {
            // There are more than one buffers bound to this memory.
            // We need the buffer to be bound to a dedicate memory,
            // so we should create a new memory and bind the buffer
            // to it in the post handle pass
            if (g_fixAsBufSize_recreate_mem_gid.find(bufInfo.bind_packet_gid) == g_fixAsBufSize_recreate_mem_gid.end()) {
                g_fixAsBufSize_recreate_mem_gid[bufInfo.bind_packet_gid][pPacket->pCreateInfo->buffer] = bufInfo;
            } else if (g_fixAsBufSize_recreate_mem_gid[bufInfo.bind_packet_gid].find(pPacket->pCreateInfo->buffer) ==
                       g_fixAsBufSize_recreate_mem_gid[bufInfo.bind_packet_gid].end()) {
                g_fixAsBufSize_recreate_mem_gid[bufInfo.bind_packet_gid][pPacket->pCreateInfo->buffer] = bufInfo;
            }
        }
        if (bufInfo.packet_index > memInfo.packet_index) {
            // There is createBuffer after allocateMemory, we need create new memory to be bound for adjusting size
            g_fixAsBufSize_recreate_mem_gid[bufInfo.bind_packet_gid][pPacket->pCreateInfo->buffer] = bufInfo;
        }
    } else {
        // buffer is bound to non-zero offset
        // we must recreate both buffer and memory
        g_fixAsBufSize_recreate_buffer_mem_gid[pHeader->global_packet_index] = bufInfo;
    }

    return 0;
}

static int pre_fixasbufsize_bufMem_alloc_mem(vktrace_trace_packet_header*& pHeader) {
    packet_vkAllocateMemory* pPacket = interpret_body_as_vkAllocateMemory(pHeader);
    g_mem2Buf.allocMem(*pPacket->pMemory, g_packet_index);

    return 0;
}

static int pre_fixasbufsize_alloc_mem(vktrace_trace_packet_header*& pHeader) {
    packet_vkAllocateMemory* pPacket = interpret_body_as_vkAllocateMemory(pHeader);
    mem_info memInfo = {.device = pPacket->device,
                        .memory = *pPacket->pMemory,
                        .size = pPacket->pAllocateInfo->allocationSize,
                        .memoryTypeIndex = pPacket->pAllocateInfo->memoryTypeIndex,
                        .didFlush = NoFlush,
                        .rangeSize = 0,
                        .rangeOffset = 0,
                        .packet_index = g_mem_params.packet_index
                        };

    VkMemoryAllocateFlagsInfo *allocateFlagInfo = (VkMemoryAllocateFlagsInfo*)find_ext_struct(
        (const vulkan_struct_header*)pPacket->pAllocateInfo->pNext,
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO);
    if (allocateFlagInfo == nullptr) {
        allocateFlagInfo = (VkMemoryAllocateFlagsInfo*)find_ext_struct(
            (const vulkan_struct_header*)pPacket->pAllocateInfo->pNext,
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR);
    }
    if (allocateFlagInfo != nullptr && (allocateFlagInfo->flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT)) {
        memInfo.flags = allocateFlagInfo->flags;
    }

    if (*pPacket->pMemory < (VkDeviceMemory)10000) {
        g_Handles.handleCount = 10000;
    }

    g_vkDevice2Mem2MemInfo[pPacket->device][*pPacket->pMemory] = memInfo;
    return 0;
}

static int pre_fixasbufsize_bufMem_free_mem(vktrace_trace_packet_header*& pHeader) {
    packet_vkFreeMemory* pPacket = interpret_body_as_vkFreeMemory(pHeader);

    g_mem2Buf.freeMem(pPacket->memory, g_packet_index);

    return 0;
}

static int pre_fixasbufsize_free_mem(vktrace_trace_packet_header*& pHeader) {
    packet_vkFreeMemory* pPacket = interpret_body_as_vkFreeMemory(pHeader);

    g_vkDevice2Mem2MemInfo[pPacket->device].erase(pPacket->memory);

    return 0;
}

static int fixasbuffersize_alloc_cmdbufs(vktrace_trace_packet_header*& pHeader) {
    packet_vkAllocateCommandBuffers* pPacket = interpret_body_as_vkAllocateCommandBuffers(pHeader);
    for (uint32_t index = 0; index < pPacket->pAllocateInfo->commandBufferCount; index++) {
        g_vkCmdBuf2Device[pPacket->pCommandBuffers[index]] = pPacket->device;
    }
    return 0;
}

static int fixasbuffersize_free_cmdbufs(vktrace_trace_packet_header*& pHeader) {
    packet_vkFreeCommandBuffers* pPacket = interpret_body_as_vkFreeCommandBuffers(pHeader);
    for (uint32_t index = 0; index < pPacket->commandBufferCount; index++) {
        g_vkCmdBuf2Device.erase(pPacket->pCommandBuffers[index]);
    }
    return 0;
}

static int pre_fixasbuffer_getbufdevaddr(vktrace_trace_packet_header*& pHeader) {
    packet_vkGetBufferDeviceAddress* pPacket = interpret_body_as_vkGetBufferDeviceAddress(pHeader);
    g_vkDevice2Addr2Buf[pPacket->device][pPacket->result] = pPacket->pInfo->buffer;
    g_max_bufferDeviceAddress = pPacket->result > g_max_bufferDeviceAddress ? pPacket->result : g_max_bufferDeviceAddress;
    return 0;
}

static void get_as_primitive_count(const VkAccelerationStructureBuildGeometryInfoKHR* pInfo, const uint32_t* pPrimCounts,
                                   unordered_map<uint32_t, uint32_t>& primitive_count) {
    for (uint32_t index = 0; index < pInfo->geometryCount; index++) {
        const VkAccelerationStructureGeometryKHR* pGeometry;
        if (pInfo->pGeometries) {
            pGeometry = pInfo->pGeometries + index;
        } else {
            pGeometry = pInfo->ppGeometries[index];
        }

        if (primitive_count.find(pGeometry->geometryType) == primitive_count.end()) {
            primitive_count[pGeometry->geometryType] = pPrimCounts[index];
        } else {
            primitive_count[pGeometry->geometryType] += pPrimCounts[index];
        }
    }
}

static int pre_fixasbuffer_getassize(vktrace_trace_packet_header*& pHeader) {
    packet_vkGetAccelerationStructureBuildSizesKHR* pPacket = interpret_body_as_vkGetAccelerationStructureBuildSizesKHR(pHeader);
    as_size_info asSizeInfo;
    asSizeInfo.flags = pPacket->pBuildInfo->flags;
    asSizeInfo.sizes = *pPacket->pSizeInfo;
    unordered_map<uint32_t, uint32_t> primitive_count;
    primitive_count.clear();

    get_as_primitive_count(pPacket->pBuildInfo, pPacket->pMaxPrimitiveCounts, primitive_count);

    if (pPacket->buildType == VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_OR_DEVICE_KHR) {
        if (pPacket->pBuildInfo->type == VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR) {
            g_vkDevice2AsBuildType2AsType2AsSizeInfo[pPacket->device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_KHR]
                                                    [VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR].push_back(asSizeInfo);
            g_vkDevice2AsBuildType2AsType2AsSizeInfo[pPacket->device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_KHR]
                                                    [VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR].push_back(asSizeInfo);
            g_vkDevice2AsBuildType2AsType2AsSizeInfo[pPacket->device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR]
                                                    [VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR].push_back(asSizeInfo);
            g_vkDevice2AsBuildType2AsType2AsSizeInfo[pPacket->device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR]
                                                    [VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR].push_back(asSizeInfo);

            g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[pPacket->device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_KHR]
                                                    [VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR].push_back(primitive_count);
            g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[pPacket->device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_KHR]
                                                    [VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR].push_back(primitive_count);
            g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[pPacket->device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR]
                                                    [VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR].push_back(primitive_count);
            g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[pPacket->device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR]
                                                    [VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR].push_back(primitive_count);
        } else {
            g_vkDevice2AsBuildType2AsType2AsSizeInfo[pPacket->device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_KHR]
                                                    [pPacket->pBuildInfo->type].push_back(asSizeInfo);
            g_vkDevice2AsBuildType2AsType2AsSizeInfo[pPacket->device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR]
                                                    [pPacket->pBuildInfo->type].push_back(asSizeInfo);

            g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[pPacket->device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_KHR]
                                                    [pPacket->pBuildInfo->type].push_back(primitive_count);
            g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[pPacket->device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR]
                                                    [pPacket->pBuildInfo->type].push_back(primitive_count);
        }
    } else if (pPacket->pBuildInfo->type == VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR) {
        g_vkDevice2AsBuildType2AsType2AsSizeInfo[pPacket->device][pPacket->buildType][VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR].push_back(asSizeInfo);
        g_vkDevice2AsBuildType2AsType2AsSizeInfo[pPacket->device][pPacket->buildType][VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR].push_back(asSizeInfo);

        g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[pPacket->device][pPacket->buildType][VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR].push_back(primitive_count);
        g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[pPacket->device][pPacket->buildType][VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR].push_back(primitive_count);
    } else {
        g_vkDevice2AsBuildType2AsType2AsSizeInfo[pPacket->device][pPacket->buildType][pPacket->pBuildInfo->type].push_back(asSizeInfo);

        g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[pPacket->device][pPacket->buildType][pPacket->pBuildInfo->type].push_back(primitive_count);
    }

    return 0;
}

void calc_primitive_count(const VkAccelerationStructureBuildGeometryInfoKHR& info,
                          const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfos,
                          uint32_t& triangle_prim_count,
                          uint32_t& aabb_prim_count,
                          uint32_t& instance_prim_count) {
    triangle_prim_count = 0;
    aabb_prim_count = 0;
    instance_prim_count = 0;
    for (uint32_t index = 0; index < info.geometryCount; index++) {
        const VkAccelerationStructureGeometryKHR* pGeometries;
        if (info.pGeometries != nullptr) {
            pGeometries = &info.pGeometries[index];
        }
        else {
            assert(info.ppGeometries != nullptr);
            pGeometries = info.ppGeometries[index];
        }
        switch (pGeometries->geometryType) {
            case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
            {
                triangle_prim_count += pBuildRangeInfos[index].primitiveCount;
                break;
            }
            case VK_GEOMETRY_TYPE_AABBS_KHR:
            {
                aabb_prim_count += pBuildRangeInfos[index].primitiveCount;
                break;
            }
            case VK_GEOMETRY_TYPE_INSTANCES_KHR:
            {
                instance_prim_count += pBuildRangeInfos[index].primitiveCount;
                break;
            }
            default:
            {
                vktrace_LogError("Unknown geometry type %d!\n", pGeometries->geometryType);
                assert(0);
            }
        }
    }
}

bool need_new_scratch_buffer(scratch_buffer_info& cur_scratch_info,
                             scratch_buffer_info& new_scratch_info,
                             VkBuildAccelerationStructureModeKHR mode) {
    bool needed = false;
    uint32_t changed_count = 0;
    // 1. If BOTTOM_LEVEL and TOP_LEVEL change, give new scratch_buffer_info
    // 2. If only BOTTOM_LEVEL change, modify triangle_prim_count and aabb_prim_count
    // 3. If only TOP_LEVEL change, modify instance_prim_count
    if ((new_scratch_info.instance_prim_count == 0 && cur_scratch_info.instance_prim_count > 0) ||
        (new_scratch_info.instance_prim_count > 0 && cur_scratch_info.instance_prim_count == 0)) {
        cur_scratch_info = new_scratch_info;
        needed = true;
    } else {
        if (new_scratch_info.triangle_prim_count > cur_scratch_info.triangle_prim_count) {
            needed = true;
            changed_count++;
        }
        if (new_scratch_info.aabb_prim_count > cur_scratch_info.aabb_prim_count) {
            needed = true;
            changed_count++;
        }
        if (new_scratch_info.instance_prim_count > cur_scratch_info.instance_prim_count) {
            needed = true;
            changed_count++;
        }
    }

    if (mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR) {
        assert (changed_count == 0);
        // By Vulkan spec, content cannot change the primitive count when update
        if (changed_count > 0) {
            vktrace_LogError("Content changes the primitive count when updating AS, why?\n");
            assert(0);
        }
    } else {
        assert (mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR);
        // Content rebuild the AS with different geometry type or bigger primitive count
        // update the scratch info to match the new one by which we probably can get the
        // matched vkGetAccelerationStructureBuildSize() call
        if (changed_count > 0) {
            cur_scratch_info.triangle_prim_count = new_scratch_info.triangle_prim_count;
            cur_scratch_info.aabb_prim_count = new_scratch_info.aabb_prim_count;
            cur_scratch_info.instance_prim_count = new_scratch_info.instance_prim_count;
        }
    }
    return needed;
}

VkDeviceSize find_matched_cmd_scratch_size(VkDevice device,
                                           const VkAccelerationStructureBuildGeometryInfoKHR& info,
                                           scratch_buffer_info& scratch_info) {
    if (g_vkDevice2AsBuildType2AsType2AsSizeInfo.find(device) == g_vkDevice2AsBuildType2AsType2AsSizeInfo.end() ||
        g_vkDevice2AsBuildType2AsType2AsPrimitiveCount.find(device) == g_vkDevice2AsBuildType2AsType2AsPrimitiveCount.end()) {
        return 0;
    }
    if (g_vkDevice2AsBuildType2AsType2AsSizeInfo[device].find(VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR) ==
        g_vkDevice2AsBuildType2AsType2AsSizeInfo[device].end() ||
        g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[device].find(VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR) ==
        g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[device].end()) {
            return 0;
    }
    if (g_vkDevice2AsBuildType2AsType2AsSizeInfo[device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR].find(info.type) ==
        g_vkDevice2AsBuildType2AsType2AsSizeInfo[device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR].end() ||
        g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR].find(info.type) ==
        g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR].end()) {
            return 0;
    }

    vector<as_size_info>& asSizeInfo = g_vkDevice2AsBuildType2AsType2AsSizeInfo[device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR][info.type];
    vector<as_size_info>::iterator it_as = asSizeInfo.begin();
    vector<unordered_map<uint32_t, uint32_t>>& primCount = g_vkDevice2AsBuildType2AsType2AsPrimitiveCount[device][VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR][info.type];
    vector<unordered_map<uint32_t, uint32_t>>::iterator it_primCount = primCount.begin();
    while (it_as != asSizeInfo.end()) {
        uint32_t flags = (uint32_t)it_as->flags & (uint32_t)info.flags &
                         (uint32_t)(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                                    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR);
        if ((flags == 0) &&
            ((uint32_t)info.flags &
            ((uint32_t)(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR))) != 0) {
            it_as++;
            it_primCount++;
            continue;
        }

        uint32_t match3 = 3;
        if ((it_primCount->find(VK_GEOMETRY_TYPE_TRIANGLES_KHR) != it_primCount->end() &&
             scratch_info.triangle_prim_count == (*it_primCount)[VK_GEOMETRY_TYPE_TRIANGLES_KHR])||
            (it_primCount->find(VK_GEOMETRY_TYPE_TRIANGLES_KHR) == it_primCount->end() &&
             scratch_info.triangle_prim_count == 0)) {
                match3--;
        }
        if ((it_primCount->find(VK_GEOMETRY_TYPE_AABBS_KHR) != it_primCount->end() &&
             scratch_info.aabb_prim_count == (*it_primCount)[VK_GEOMETRY_TYPE_AABBS_KHR])||
            (it_primCount->find(VK_GEOMETRY_TYPE_AABBS_KHR) == it_primCount->end() &&
             scratch_info.aabb_prim_count == 0)) {
                match3--;
        }
        if ((it_primCount->find(VK_GEOMETRY_TYPE_INSTANCES_KHR) != it_primCount->end() &&
             scratch_info.instance_prim_count == (*it_primCount)[VK_GEOMETRY_TYPE_INSTANCES_KHR])||
            (it_primCount->find(VK_GEOMETRY_TYPE_INSTANCES_KHR) == it_primCount->end() &&
             scratch_info.instance_prim_count == 0)) {
                match3--;
        }
        if (match3 == 0) {
            switch (info.mode) {
            case VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR: {
                return it_as->sizes.buildScratchSize;
            }
            case VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR: {
                return it_as->sizes.updateScratchSize;
            }
            default: {
                vktrace_LogError("Unknown device build mode: %d\n", info.mode);
                break;
            }
            }
        }
        it_as++;
        it_primCount++;
    }

    return 0;
}

static int pre_fixasbufsize_cmdbuild_as(vktrace_trace_packet_header* &pHeader) {
    packet_vkCmdBuildAccelerationStructuresKHR* pPacket = interpret_body_as_vkCmdBuildAccelerationStructuresKHR(pHeader);
    VkDevice device = g_vkCmdBuf2Device[pPacket->commandBuffer];
    auto &addr2Buf = g_vkDevice2Addr2Buf[device];

    // scratch buffer is part of a bigger buffer or memory
    // for such case, we must recreate a separate buffer and memory
    // as the new scratch buffer
    // The rules we use to create a new scratch buffer
    // 1. For each dst accelerationStructure, if there is no new
    //    created scratch buffer, we create a scratch buffer
    //      a. Use the current
    //         VkAccelerationStructureBuildGeometryInfoKHR
    //         to get the new created scratch buffer size
    //      b. The scratch buffer size depends on the geometry
    //         type and primitive number. More primitives means
    //         bigger buffer size
    //      c. For each accelerationStructure we maintain the map
    //         of <device, <as, scratch_buffer_info>>
    //         The purpose is to check whether the current scratch
    //         buffer size is big enough for the current device build
    //      d. Usually for each acclerationStructure there is only
    //         one type used. But there might be special cases
    //      e. Create a new buffer with the scratch buffer size
    //      f. Create a new memory with the scratch buffer size
    //      g. Bind buffer to memory
    //      h. Get new scratch buffer device address, and replace
    //         the old one used in
    //         VkAccelerationStructureBuildGeometryInfoKHR
    // 2.  If there is a new created scratch buffer
    //      a. Use the current
    //         VkAccelerationStructureBuildGeometryInfoKHR to estimate
    //         whether a bigger scratch buffer size is needed
    //      b. The estimation is based on geometry type and primitive
    //         number
    //          i.  Within the same geometry type, if the current
    //              primitive number is bigger, we'll create a new
    //              scratch buffer
    //          ii. If a new geotry type is met, we always create
    //              a new scratch buffer
    //          iii. Update  <device, <as, scratch_buffer_info>>
    //      c. Usually content builds AS once and update it multiple
    //         times.
    //          i.  In update mode, there are many limitations defined
    //              by Vulkan spec. Content cannot change the geometry
    //              type, VB/IB format and max primitive number.
    //          ii. This means scratch buffer size may not change
    //              within these limitations
    //      e. Content may rebuild the AS later to get a better
    //         performance than update, but it uses the same geometry and primitive
    //         number usually
    //      f. This means we have rare cases to need a bigger scratch
    //         buffer based on the above 2.a - 2.f analysis.
    //         But we still need the codes to support the rare case
    // 3. When we create a new scrath buffer within the same geometry
    //    type, we move the old one to a deferred free list.
    //    We have to free it in the end of the trace, as we don't know
    //    when the commands depend on it are finished.
    //    We may consider to free them earlier after several frames(3+)
    //    this might be safe
    for (uint32_t index = 0; index < pPacket->infoCount; index++) {
        const VkAccelerationStructureBuildGeometryInfoKHR& info = pPacket->pInfos[index];
        VkDeviceAddress scratchAddress = info.scratchData.deviceAddress;
        uint32_t memoryTypeIndex = UINT32_MAX;
        bool need_new_scratch = false;
        auto itAddr2Buf = addr2Buf.find(scratchAddress);
        VkBuffer orig_buffer = VK_NULL_HANDLE;
        VkDeviceMemory orig_memory = VK_NULL_HANDLE;
        if (itAddr2Buf == addr2Buf.end()) {
            // scratch buffer is probably bound to a memory with non-zero offset
            uint64_t delta_min = ULONG_LONG_MAX;
            itAddr2Buf = addr2Buf.end();
            for (auto iter = addr2Buf.begin(); iter != addr2Buf.end(); ++iter) {
                uint64_t delta = scratchAddress - iter->first;
                if (delta >= 0 && delta < delta_min) {
                    delta_min = delta;
                    itAddr2Buf = iter;
                }
            }

            if (itAddr2Buf != addr2Buf.end()) {
                // we found it's true scratch buffer is bound to a memory with non-zero offset
                // to resize the scratch buffer when replay, we need a new scratch buffer
                orig_buffer = itAddr2Buf->second;
                buffer_info& bufInfo = g_vkDevice2Buf2BufInfo[device][orig_buffer];
                orig_memory = bufInfo.memory;
                memoryTypeIndex = bufInfo.memoryTypeIndex;
                vktrace_LogDebug("scratch buffer is bound in gid=%llu size=%llu offset=%llu", bufInfo.bind_packet_gid, bufInfo.size, scratchAddress-delta_min);
                g_scratchBuffer2DeviceAddrs[itAddr2Buf->second].push_back(scratchAddress);
            } else {
                // we cannot find the real scratch buffer, what happens?
                // we should create a new scratch buffer
                vktrace_LogError("Cannot find the scratch buffer for device address %llu \n", scratchAddress);
                // assert(0);
            }
            need_new_scratch = true;
        } else {
            // scratch buffer is bound to the beginning of a buffer
            orig_buffer = itAddr2Buf->second;
            buffer_info &bufInfo = g_vkDevice2Buf2BufInfo[device][orig_buffer];
            mem_info &memInfo = g_vkDevice2Mem2MemInfo[device][bufInfo.memory];
            orig_memory = bufInfo.memory;
            memoryTypeIndex = bufInfo.memoryTypeIndex;
            if (g_mem2Buf.bufCount(memInfo.memory, orig_buffer, g_packet_index) >= 2) {
                // There are more than one buffers bound to the same memory.
                // To resize the scratch buffer when replaying, we need a new scratch buffer
                need_new_scratch = true;
            } else if (bufInfo.packet_index > memInfo.packet_index) {
                // There is createBuffer after allocateMemory,
                // we need create new memory to be bound for adjusting size
                need_new_scratch = true;
            } else {
                // the buffer size is equal to memory size
                // check whether the size is the result of vkGetAccelerationstructureBuildSize
                scratch_buffer_info scratch_buf_info = {.buffer = orig_buffer,
                                                        .memory = orig_memory,
                                                        .size = 0,
                                                        .triangle_prim_count = 0,
                                                        .aabb_prim_count = 0,
                                                        .instance_prim_count = 0,
                                                        .address = 0,
                                                        .info_index = 0,
                                                        .memoryTypeIndex = memoryTypeIndex};
                calc_primitive_count(info, pPacket->ppBuildRangeInfos[index], scratch_buf_info.triangle_prim_count,
                                     scratch_buf_info.aabb_prim_count, scratch_buf_info.instance_prim_count);
                VkDeviceSize size = find_matched_cmd_scratch_size(device, info, scratch_buf_info);
                if (size != bufInfo.size) {
                    need_new_scratch = true;
                }
            }
            g_scratchBuffer2DeviceAddrs[orig_buffer].push_back(scratchAddress);
        }

        if (need_new_scratch) {
            bool create_new = false;
            scratch_buffer_info scratch_buf_info = {.buffer = orig_buffer,
                                                    .memory = orig_memory,
                                                    .size = 0,
                                                    .triangle_prim_count = 0,
                                                    .aabb_prim_count = 0,
                                                    .instance_prim_count = 0,
                                                    .address = 0,
                                                    .info_index = 0,
                                                    .memoryTypeIndex = memoryTypeIndex};
            calc_primitive_count(info, pPacket->ppBuildRangeInfos[index], scratch_buf_info.triangle_prim_count,
                                 scratch_buf_info.aabb_prim_count, scratch_buf_info.instance_prim_count);
            if (g_dev2AS2ScratchAddr2ScratchInfo.find(device) == g_dev2AS2ScratchAddr2ScratchInfo.end() ||
                g_dev2AS2ScratchAddr2ScratchInfo[device].find(info.dstAccelerationStructure) == g_dev2AS2ScratchAddr2ScratchInfo[device].end() ||
                g_dev2AS2ScratchAddr2ScratchInfo[device][info.dstAccelerationStructure].find(scratchAddress) == g_dev2AS2ScratchAddr2ScratchInfo[device][info.dstAccelerationStructure].end()
                ) {
                create_new = true;
                g_dev2AS2ScratchAddr2ScratchInfo[device][info.dstAccelerationStructure][scratchAddress] = scratch_buf_info;
            } else {
                scratch_buffer_info& cur_scratch_info = g_dev2AS2ScratchAddr2ScratchInfo[device][info.dstAccelerationStructure][scratchAddress];
                if (need_new_scratch_buffer(cur_scratch_info, scratch_buf_info, info.mode)) {
                    create_new = true;
                }
            }
            if (create_new) {
                VkDeviceSize size = find_matched_cmd_scratch_size(device, info, scratch_buf_info);
                g_dev2AS2ScratchAddr2ScratchInfo[device][info.dstAccelerationStructure][scratchAddress].size = size;
                assert(size != 0);
                scratch_buffer_info new_scratch_info = g_dev2AS2ScratchAddr2ScratchInfo[device][info.dstAccelerationStructure][scratchAddress];
                new_scratch_info.info_index = index;
                g_fixAsBufSize_recreate_scratch_gid[pHeader->global_packet_index].push_back(new_scratch_info);
            }
            else {
                g_fixAsBufSize_replace_scratch_gid[pHeader->global_packet_index].push_back(index);
            }
        }
    }

    return 0;
}

static VkDeviceAddress getMaxBufferDeviceAddress(VkDevice device, VkDeviceSize size)
{
    static VkDeviceAddress maxBufferDeviceAddress = 0;
    VkDeviceAddress currentAddr = 0;
    const uint32_t alignedSize = 4096;
    const uint64_t maxHandleSize = (uint64_t)MAX_BUFFER_DEVICEADDRESS_SIZE;

    if (maxBufferDeviceAddress == 0) {
        auto &addr2Buf = g_vkDevice2Addr2Buf[device];
        auto itAddr2Buf = addr2Buf.find(g_max_bufferDeviceAddress);
        if (itAddr2Buf != addr2Buf.end()) {
            VkBuffer &buf = itAddr2Buf->second;
            buffer_info &bufInfo = g_vkDevice2Buf2BufInfo[device][buf];
            mem_info &memInfo = g_vkDevice2Mem2MemInfo[device][bufInfo.memory];
            if (bufInfo.size < memInfo.size) {
                maxBufferDeviceAddress = g_max_bufferDeviceAddress + memInfo.size;
            } else {
                maxBufferDeviceAddress = g_max_bufferDeviceAddress + bufInfo.size;
            }
        } else {
            maxBufferDeviceAddress = g_max_bufferDeviceAddress + maxHandleSize;
        }

        maxBufferDeviceAddress += alignedSize;
        maxBufferDeviceAddress = maxBufferDeviceAddress - (maxBufferDeviceAddress % alignedSize) + alignedSize;
    }

    currentAddr = maxBufferDeviceAddress;
    maxBufferDeviceAddress += size;
    maxBufferDeviceAddress += alignedSize;
    maxBufferDeviceAddress = maxBufferDeviceAddress - (maxBufferDeviceAddress % alignedSize) + alignedSize;
    return currentAddr;
}

static int pre_fixasbufsize_cmdbuild_as_indirect(vktrace_trace_packet_header*& pHeader) {
    // TODO:
    // to be implemented later
    assert(0);
    vktrace_LogError("pre_fixasbufsize_cmdbuild_as_indirect is not implemented!");
    return -1;
}

int pre_fix_asbuffer_size(vktrace_trace_file_header* pFileHeader, FileLike* traceFile) {
    int ret = 0;
    g_mem_params.packet_index = 0;
    g_packet_index = 0;

    // 1st pass of pre-process, find how many buffers are bound to each VkDeviceMemory
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
        g_packet_index++;
        pHeader->pBody = (uintptr_t)(((char*)pHeader) + sizeof(vktrace_trace_packet_header));
        if (pHeader->global_packet_index > g_max_gid) {
            g_max_gid = pHeader->global_packet_index;
        }

        if (
            pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory2 &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory2KHR &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkAllocateMemory &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkFreeMemory) {
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
            case VKTRACE_TPI_VK_vkBindBufferMemory: {
                ret = pre_fixasbufsize_bufMem_bind_buffer_memory(pHeader);
                g_mem_params.packet_index++;
                break;
            }
            case VKTRACE_TPI_VK_vkBindBufferMemory2:
            case VKTRACE_TPI_VK_vkBindBufferMemory2KHR: {
                ret = pre_fixasbufsize_bufMem_bind_buffer_memory2(pHeader);
                g_mem_params.packet_index++;
                break;
            }
            case VKTRACE_TPI_VK_vkAllocateMemory: {
                ret = pre_fixasbufsize_bufMem_alloc_mem(pHeader);
                g_mem_params.packet_index++;
                break;
            }
            case VKTRACE_TPI_VK_vkFreeMemory: {
                ret = pre_fixasbufsize_bufMem_free_mem(pHeader);
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
    g_mem2Buf.freeAllMem(g_packet_index);

    ret = 0;
    g_mem_params.packet_index = 0;

    // 2nd pass of pre-process
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

        if (pHeader->packet_id != VKTRACE_TPI_VK_vkCreateBuffer &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkGetBufferMemoryRequirements &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkGetBufferMemoryRequirements2 &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkGetBufferMemoryRequirements2KHR &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory2 &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyBuffer &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkCreateAccelerationStructureKHR &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkAllocateMemory &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkFreeMemory &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkAllocateCommandBuffers &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkFreeCommandBuffers &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkGetAccelerationStructureBuildSizesKHR &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkGetBufferDeviceAddress &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkGetBufferDeviceAddressKHR &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresKHR &&
            pHeader->packet_id != VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresIndirectKHR) {
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
            case VKTRACE_TPI_VK_vkCreateBuffer: {
                ret = pre_fixasbufsize_create_buffer(pHeader);
                g_mem_params.packet_index++;
                break;
            }
            case VKTRACE_TPI_VK_vkGetBufferMemoryRequirements:
            case VKTRACE_TPI_VK_vkGetBufferMemoryRequirements2:
            case VKTRACE_TPI_VK_vkGetBufferMemoryRequirements2KHR: {
                ret = pre_fixasbufsize_GetBufferMemoryRequirements(pHeader);
                break;
            }
            case VKTRACE_TPI_VK_vkBindBufferMemory: {
                ret = pre_fixasbufsize_bind_buffer_memory(pHeader);
                g_mem_params.packet_index++;
                break;
            }
            case VKTRACE_TPI_VK_vkBindBufferMemory2:
            case VKTRACE_TPI_VK_vkBindBufferMemory2KHR: {
                ret = pre_fixasbufsize_bind_buffer_memory2(pHeader);
                g_mem_params.packet_index++;
                break;
            }
            case VKTRACE_TPI_VK_vkDestroyBuffer: {
                ret = pre_fixasbufsize_destroy_buffer(pHeader);
                break;
            }
            case VKTRACE_TPI_VK_vkCreateAccelerationStructureKHR: {
                ret = pre_fixasbufsize_create_as(pHeader);
                break;
            }
            case VKTRACE_TPI_VK_vkAllocateMemory: {
                ret = pre_fixasbufsize_alloc_mem(pHeader);
                g_mem_params.packet_index++;
                break;
            }
            case VKTRACE_TPI_VK_vkFreeMemory: {
                ret = pre_fixasbufsize_free_mem(pHeader);
                break;
            }
            case VKTRACE_TPI_VK_vkAllocateCommandBuffers: {
                ret = fixasbuffersize_alloc_cmdbufs(pHeader);
                break;
            }
            case VKTRACE_TPI_VK_vkFreeCommandBuffers: {
                ret = fixasbuffersize_free_cmdbufs(pHeader);
                break;
            }
            case VKTRACE_TPI_VK_vkGetAccelerationStructureBuildSizesKHR: {
                ret = pre_fixasbuffer_getassize(pHeader);
                break;
            }
            case VKTRACE_TPI_VK_vkGetBufferDeviceAddress:
            case VKTRACE_TPI_VK_vkGetBufferDeviceAddressKHR: {
                ret = pre_fixasbuffer_getbufdevaddr(pHeader);
                break;
            }
            case VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresKHR: {
                ret = pre_fixasbufsize_cmdbuild_as(pHeader);
                break;
            }
            case VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresIndirectKHR: {
                ret = pre_fixasbufsize_cmdbuild_as_indirect(pHeader);
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

static int post_generate_vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, VkBuffer* pBuffer,
                                            FILE* newTraceFile, uint64_t* fileOffset, uint64_t* fileSize) {
    VkResult result = VK_SUCCESS;
    vktrace_trace_packet_header* pHeader;
    packet_vkCreateBuffer* pPacket = NULL;
    CREATE_TRACE_PACKET(vkCreateBuffer,
                        get_struct_chain_size((void*)pCreateInfo) + sizeof(VkAllocationCallbacks) + sizeof(VkBuffer));
    pHeader->global_packet_index = ++g_max_gid;
    pHeader->tracer_id = pHeader->tracer_id;
    pHeader->thread_id = pHeader->thread_id;
    pHeader->packet_id = VKTRACE_TPI_VK_vkCreateBuffer;
    pHeader->vktrace_begin_time = 0;
    pHeader->entrypoint_begin_time = 0;
    pHeader->entrypoint_end_time = 1;

    pPacket = interpret_body_as_vkCreateBuffer(pHeader);
    pPacket->device = device;
    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pCreateInfo), sizeof(VkBufferCreateInfo), pCreateInfo);
    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pCreateInfo->pQueueFamilyIndices),
                                       sizeof(uint32_t) * pCreateInfo->queueFamilyIndexCount, pCreateInfo->pQueueFamilyIndices);
    if (pCreateInfo) vktrace_add_pnext_structs_to_trace_packet(pHeader, (void*)pPacket->pCreateInfo, pCreateInfo);
    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pAllocator), sizeof(VkAllocationCallbacks), NULL);
    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pBuffer), sizeof(VkBuffer), pBuffer);
    pPacket->result = result;
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pCreateInfo->pQueueFamilyIndices));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pCreateInfo));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAllocator));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pBuffer));
    vktrace_finalize_trace_packet(pHeader);

    // inject the new packet to new trace file
    uint64_t bytesWritten = fwrite(pHeader, 1, pHeader->size, newTraceFile);
    fflush(newTraceFile);
    if (bytesWritten != pHeader->size) {
        vktrace_LogAlways("bytesWritten = %d, packet size = %d\n", bytesWritten, pHeader->size);
        vktrace_LogError("Failed to write the packet for packet_id = %hu", pHeader->packet_id);
        vktrace_delete_trace_packet(&pHeader);
        return -1;
    }

    // edit the portability table
    if (vktrace_append_portabilitytable(pHeader->packet_id)) {
        vktrace_LogDebug("Add packet to portability table: %s",
                         vktrace_vk_packet_id_name((VKTRACE_TRACE_PACKET_ID_VK)pHeader->packet_id));
        g_portabilityTable.push_back(*fileOffset);
    }
    *fileOffset += bytesWritten;
    *fileSize += bytesWritten;

    vktrace_delete_trace_packet(&pHeader);

    return 0;
}

static int post_generate_vkDestroyBuffer(VkDevice device, VkBuffer buffer,
                                          FILE* newTraceFile, uint64_t* fileOffset, uint64_t* fileSize) {
    VkResult result = VK_SUCCESS;
    vktrace_trace_packet_header* pHeader;
    CREATE_TRACE_PACKET(vkDestroyBuffer, sizeof(VkAllocationCallbacks));
    packet_vkDestroyBuffer* pPacket = interpret_body_as_vkDestroyBuffer(pHeader);

    pHeader->global_packet_index = ++g_max_gid;
    pHeader->tracer_id = pHeader->tracer_id;
    pHeader->thread_id = pHeader->thread_id;
    pHeader->packet_id = VKTRACE_TPI_VK_vkDestroyBuffer;
    pHeader->vktrace_begin_time = 0;
    pHeader->entrypoint_begin_time = 0;
    pHeader->entrypoint_end_time = 1;

    pPacket->device = device;
    pPacket->buffer = buffer;
    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pAllocator), sizeof(VkAllocationCallbacks), NULL);
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAllocator));
    vktrace_finalize_trace_packet(pHeader);

    // inject the new packet to new trace file
    uint64_t bytesWritten = fwrite(pHeader, 1, pHeader->size, newTraceFile);
    fflush(newTraceFile);
    if (bytesWritten != pHeader->size) {
        vktrace_LogAlways("bytesWritten = %d, packet size = %d\n", bytesWritten, pHeader->size);
        vktrace_LogError("Failed to write the packet for packet_id = %hu", pHeader->packet_id);
        vktrace_delete_trace_packet(&pHeader);
        return -1;
    }

    // edit the portability table
    if (vktrace_append_portabilitytable(pHeader->packet_id)) {
        vktrace_LogDebug("Add packet to portability table: %s",
                         vktrace_vk_packet_id_name((VKTRACE_TRACE_PACKET_ID_VK)pHeader->packet_id));
        g_portabilityTable.push_back(*fileOffset);
    }
    *fileOffset += bytesWritten;
    *fileSize += bytesWritten;

    vktrace_delete_trace_packet(&pHeader);

    return 0;
}

static int post_generate_vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset,
                                                FILE* newTraceFile, uint64_t* fileOffset, uint64_t* fileSize) {
    VkResult result = VK_SUCCESS;
    vktrace_trace_packet_header* pHeader;
    packet_vkBindBufferMemory* pPacket = NULL;
    CREATE_TRACE_PACKET(vkBindBufferMemory, 0);
    pHeader->global_packet_index = ++g_max_gid;
    pHeader->tracer_id = pHeader->tracer_id;
    pHeader->thread_id = pHeader->thread_id;
    pHeader->packet_id = VKTRACE_TPI_VK_vkBindBufferMemory;
    pHeader->vktrace_begin_time = 0;
    pHeader->entrypoint_begin_time = 0;
    pHeader->entrypoint_end_time = 1;

    pPacket = interpret_body_as_vkBindBufferMemory(pHeader);
    pPacket->device = device;
    pPacket->buffer = buffer;
    pPacket->memory = memory;
    pPacket->memoryOffset = memoryOffset;
    pPacket->result = result;
    vktrace_finalize_trace_packet(pHeader);

    // inject the new packet to new trace file
    uint64_t bytesWritten = fwrite(pHeader, 1, pHeader->size, newTraceFile);
    fflush(newTraceFile);
    if (bytesWritten != pHeader->size) {
        vktrace_LogAlways("bytesWritten = %d, packet size = %d\n", bytesWritten, pHeader->size);
        vktrace_LogError("Failed to write the packet for packet_id = %hu", pHeader->packet_id);
        vktrace_delete_trace_packet(&pHeader);
        return -1;
    }

    // edit the portability table
    if (vktrace_append_portabilitytable(pHeader->packet_id)) {
        vktrace_LogDebug("Add packet to portability table: %s",
                         vktrace_vk_packet_id_name((VKTRACE_TRACE_PACKET_ID_VK)pHeader->packet_id));
        g_portabilityTable.push_back(*fileOffset);
    }
    *fileOffset += bytesWritten;
    *fileSize += bytesWritten;

    vktrace_delete_trace_packet(&pHeader);

    return 0;
}

static int post_generate_vkGetBufferDeviceAddressKHR(VkDevice device, const VkBufferDeviceAddressInfo* pInfo,
                                                         VkDeviceAddress* addr, FILE* newTraceFile, uint64_t* fileOffset,
                                                         uint64_t* fileSize) {
    vktrace_trace_packet_header* pHeader;
    packet_vkGetBufferDeviceAddressKHR* pPacket = NULL;
    CREATE_TRACE_PACKET(vkGetBufferDeviceAddressKHR, sizeof(VkBufferDeviceAddressInfo));
    pHeader->global_packet_index = ++g_max_gid;
    pHeader->tracer_id = pHeader->tracer_id;
    pHeader->thread_id = pHeader->thread_id;
    pHeader->packet_id = VKTRACE_TPI_VK_vkGetBufferDeviceAddressKHR;
    pHeader->vktrace_begin_time = 0;
    pHeader->entrypoint_begin_time = 0;
    pHeader->entrypoint_end_time = 1;

    pPacket = interpret_body_as_vkGetBufferDeviceAddressKHR(pHeader);
    pPacket->device = device;
    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pInfo), sizeof(VkBufferDeviceAddressInfo), pInfo);
    pPacket->result = *addr;
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pInfo));

    // inject the new packet to new trace file
    uint64_t bytesWritten = fwrite(pHeader, 1, pHeader->size, newTraceFile);
    fflush(newTraceFile);
    if (bytesWritten != pHeader->size) {
        vktrace_LogAlways("bytesWritten = %d, packet size = %d\n", bytesWritten, pHeader->size);
        vktrace_LogError("Failed to write the packet for packet_id = %hu", pHeader->packet_id);
        vktrace_delete_trace_packet(&pHeader);
        return -1;
    }

    *fileOffset += bytesWritten;
    *fileSize += bytesWritten;

    vktrace_delete_trace_packet(&pHeader);
    return 0;
}

static int post_generate_vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
    VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
    const VkImageMemoryBarrier* pImageMemoryBarriers, FILE* newTraceFile, uint64_t* fileOffset, uint64_t* fileSize) {
    vktrace_trace_packet_header* pHeader;
    packet_vkCmdPipelineBarrier* pPacket = NULL;
    size_t customSize;
    customSize = (memoryBarrierCount * sizeof(VkMemoryBarrier)) + (bufferMemoryBarrierCount * sizeof(VkBufferMemoryBarrier)) +
                 (imageMemoryBarrierCount * sizeof(VkImageMemoryBarrier));
    CREATE_TRACE_PACKET(vkCmdPipelineBarrier, customSize);
    pHeader->global_packet_index = ++g_max_gid;
    pHeader->tracer_id = pHeader->tracer_id;
    pHeader->thread_id = pHeader->thread_id;
    pHeader->packet_id = VKTRACE_TPI_VK_vkCmdPipelineBarrier;
    pHeader->vktrace_begin_time = 0;
    pHeader->entrypoint_begin_time = 0;
    pHeader->entrypoint_end_time = 1;

    pPacket = interpret_body_as_vkCmdPipelineBarrier(pHeader);
    pPacket->commandBuffer = commandBuffer;
    pPacket->srcStageMask = srcStageMask;
    pPacket->dstStageMask = dstStageMask;
    pPacket->dependencyFlags = dependencyFlags;
    pPacket->memoryBarrierCount = memoryBarrierCount;
    pPacket->bufferMemoryBarrierCount = bufferMemoryBarrierCount;
    pPacket->imageMemoryBarrierCount = imageMemoryBarrierCount;
    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pMemoryBarriers), memoryBarrierCount * sizeof(VkMemoryBarrier),
                                       pMemoryBarriers);
    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pBufferMemoryBarriers),
                                       bufferMemoryBarrierCount * sizeof(VkBufferMemoryBarrier), pBufferMemoryBarriers);
    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pImageMemoryBarriers),
                                       imageMemoryBarrierCount * sizeof(VkImageMemoryBarrier), pImageMemoryBarriers);

    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pMemoryBarriers));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pBufferMemoryBarriers));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pImageMemoryBarriers));
    vktrace_finalize_trace_packet(pHeader);

    // inject the new packet to new trace file
    uint64_t bytesWritten = fwrite(pHeader, 1, pHeader->size, newTraceFile);
    fflush(newTraceFile);
    if (bytesWritten != pHeader->size) {
        vktrace_LogAlways("bytesWritten = %d, packet size = %d\n", bytesWritten, pHeader->size);
        vktrace_LogError("Failed to write the packet for packet_id = %hu", pHeader->packet_id);
        vktrace_delete_trace_packet(&pHeader);
        return -1;
    }

    *fileOffset += bytesWritten;
    *fileSize += bytesWritten;

    vktrace_delete_trace_packet(&pHeader);
    return 0;
}

static int post_generate_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, VkDeviceMemory* pMemory,
                                              FILE* newTraceFile, uint64_t* fileOffset, uint64_t* fileSize) {
    VkResult result = VK_SUCCESS;
    vktrace_trace_packet_header* pHeader;
    CREATE_TRACE_PACKET(vkAllocateMemory, get_struct_chain_size((void*)pAllocateInfo) + sizeof(VkMemoryAllocateInfo) +
                                              sizeof(VkMemoryAllocateFlagsInfo) + sizeof(VkAllocationCallbacks) +
                                              sizeof(VkDeviceMemory));
    packet_vkAllocateMemory* pPacket = interpret_body_as_vkAllocateMemory(pHeader);

    pHeader->global_packet_index = ++g_max_gid;
    pHeader->tracer_id = pHeader->tracer_id;
    pHeader->thread_id = pHeader->thread_id;
    pHeader->packet_id = VKTRACE_TPI_VK_vkAllocateMemory;
    pHeader->vktrace_begin_time = 0;
    pHeader->entrypoint_begin_time = 0;
    pHeader->entrypoint_end_time = 1;

    pPacket->device = device;
    pPacket->result = VK_SUCCESS;

    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pAllocateInfo), sizeof(VkMemoryAllocateInfo), pAllocateInfo);
    if (pAllocateInfo) vktrace_add_pnext_structs_to_trace_packet(pHeader, (void*)pPacket->pAllocateInfo, pAllocateInfo);
    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pAllocator), sizeof(VkAllocationCallbacks), NULL);
    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pMemory), sizeof(VkDeviceMemory), pMemory);
    pPacket->result = result;
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAllocateInfo));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAllocator));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pMemory));
    vktrace_finalize_trace_packet(pHeader);

    // inject the new packet to new trace file
    uint64_t bytesWritten = fwrite(pHeader, 1, pHeader->size, newTraceFile);
    fflush(newTraceFile);
    if (bytesWritten != pHeader->size) {
        vktrace_LogAlways("bytesWritten = %d, packet size = %d\n", bytesWritten, pHeader->size);
        vktrace_LogError("Failed to write the packet for packet_id = %hu", pHeader->packet_id);
        vktrace_delete_trace_packet(&pHeader);
        return -1;
    }

    // edit the portability table
    if (vktrace_append_portabilitytable(pHeader->packet_id)) {
        vktrace_LogDebug("Add packet to portability table: %s",
                         vktrace_vk_packet_id_name((VKTRACE_TRACE_PACKET_ID_VK)pHeader->packet_id));
        g_portabilityTable.push_back(*fileOffset);
    }
    *fileOffset += bytesWritten;
    *fileSize += bytesWritten;

    vktrace_delete_trace_packet(&pHeader);

    return 0;
}

static int post_generate_vkFreeMemory(VkDevice device, VkDeviceMemory memory,
                                          FILE* newTraceFile, uint64_t* fileOffset, uint64_t* fileSize) {
    VkResult result = VK_SUCCESS;
    vktrace_trace_packet_header* pHeader;
    CREATE_TRACE_PACKET(vkFreeMemory, sizeof(VkAllocationCallbacks));
    packet_vkFreeMemory* pPacket = interpret_body_as_vkFreeMemory(pHeader);

    pHeader->global_packet_index = ++g_max_gid;
    pHeader->tracer_id = pHeader->tracer_id;
    pHeader->thread_id = pHeader->thread_id;
    pHeader->packet_id = VKTRACE_TPI_VK_vkFreeMemory;
    pHeader->vktrace_begin_time = 0;
    pHeader->entrypoint_begin_time = 0;
    pHeader->entrypoint_end_time = 1;

    pPacket->device = device;
    pPacket->memory = memory;
    vktrace_add_buffer_to_trace_packet(pHeader, (void**)&(pPacket->pAllocator), sizeof(VkAllocationCallbacks), NULL);
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAllocator));
    vktrace_finalize_trace_packet(pHeader);

    // inject the new packet to new trace file
    uint64_t bytesWritten = fwrite(pHeader, 1, pHeader->size, newTraceFile);
    fflush(newTraceFile);
    if (bytesWritten != pHeader->size) {
        vktrace_LogAlways("bytesWritten = %d, packet size = %d\n", bytesWritten, pHeader->size);
        vktrace_LogError("Failed to write the packet for packet_id = %hu", pHeader->packet_id);
        vktrace_delete_trace_packet(&pHeader);
        return -1;
    }

    // edit the portability table
    if (vktrace_append_portabilitytable(pHeader->packet_id)) {
        vktrace_LogDebug("Add packet to portability table: %s",
                         vktrace_vk_packet_id_name((VKTRACE_TRACE_PACKET_ID_VK)pHeader->packet_id));
        g_portabilityTable.push_back(*fileOffset);
    }
    *fileOffset += bytesWritten;
    *fileSize += bytesWritten;

    vktrace_delete_trace_packet(&pHeader);

    return 0;
}

static int post_fixasbufsize_vkCreateBuffer(vktrace_trace_file_header* pFileHeader,  vktrace_trace_packet_header* &pHeader, FILE* newTraceFile, uint64_t* fileOffset, uint64_t* fileSize)
{
    int ret = 0;
    packet_vkCreateBuffer* pPacket = interpret_body_as_vkCreateBuffer(pHeader);
    VkExportFenceCreateInfo usage1;
    VkExportFenceCreateInfo usage2;
    if (pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        // created buffer for AS
        if (g_asBuffer2AccelerationStructures.find(*pPacket->pBuffer) != g_asBuffer2AccelerationStructures.end() &&
            pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) {
            // According to Vulkan Spec, VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO is not a valid structure of VkBufferCreateInfo::pNext.
            // Here we added it to specify this buffer is an AS buffer.
            usage1 = {
                .sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO,
                .pNext = pPacket->pCreateInfo->pNext,
                .handleTypes = VK_BUFFER_USAGE_EXTRA_MARK_ASBUFFER,
            };
            const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->pNext = &usage1;
            const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->usage &= ~VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->usage &= ~VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->usage &= ~VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
            const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->usage &= ~VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        }

        // created buffer for scratch data
        if (g_scratchBuffer2DeviceAddrs.find(*pPacket->pBuffer) != g_scratchBuffer2DeviceAddrs.end() &&
            pPacket->pCreateInfo->usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
            // According to Vulkan Spec, VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO is not a valid structure of VkBufferCreateInfo::pNext.
            // Here we added it to specify this buffer is a scratch buffer.
            usage2 = {
                .sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO,
                .pNext = pPacket->pCreateInfo->pNext,
                .handleTypes = VK_BUFFER_USAGE_EXTRA_MARK_SCRATCH,
            };
            const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->pNext = &usage2;
            const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->usage &= ~VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->usage &= ~VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->usage &= ~VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
            const_cast<VkBufferCreateInfo*>(pPacket->pCreateInfo)->usage &= ~VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        }
    }

    VkResult result = VK_SUCCESS;
    uint64_t newSize = pHeader->size + ROUNDUP_TO_8(2 * (sizeof(VkExportFenceCreateInfo) + sizeof(void*)));
    vktrace_trace_packet_header* pMemoryNew = static_cast<vktrace_trace_packet_header*>(malloc(newSize));
    memset(pMemoryNew, 0, newSize);
    memcpy(pMemoryNew, pHeader, sizeof(vktrace_trace_packet_header));
    vktrace_trace_packet_header* pHeaderNew = (vktrace_trace_packet_header*)pMemoryNew;
    pHeaderNew->size = newSize;
    pHeaderNew->pBody = (uintptr_t)(((char*)pMemoryNew) + sizeof(vktrace_trace_packet_header));
    pHeaderNew->next_buffers_offset = sizeof(vktrace_trace_packet_header) +
                               ROUNDUP_TO_8(sizeof(packet_vkCreateBuffer));  // Initial offset is from start of header to after the packet body.
    packet_vkCreateBuffer* pPacketNew = interpret_body_as_vkCreateBuffer(pHeaderNew);

    vktrace_add_buffer_to_trace_packet(pHeaderNew, (void **)&(pPacketNew->pCreateInfo), sizeof(VkBufferCreateInfo), pPacket->pCreateInfo);
    vktrace_add_buffer_to_trace_packet(pHeaderNew, (void **)&(pPacketNew->pCreateInfo->pQueueFamilyIndices),
                                       sizeof(uint32_t) * pPacket->pCreateInfo->queueFamilyIndexCount, pPacket->pCreateInfo->pQueueFamilyIndices);
    if (pPacket->pCreateInfo)  {
        vktrace_add_pnext_structs_to_trace_packet(pHeaderNew, (void*)pPacketNew->pCreateInfo, pPacket->pCreateInfo);
    }
    vktrace_add_buffer_to_trace_packet(pHeaderNew, (void **)&(pPacketNew->pAllocator), sizeof(VkAllocationCallbacks), NULL);
    vktrace_add_buffer_to_trace_packet(pHeaderNew, (void **)&(pPacketNew->pBuffer), sizeof(VkBuffer), pPacket->pBuffer);
    pPacketNew->device = pPacket->device;
    pPacketNew->result = pPacket->result;
    vktrace_finalize_buffer_address(pHeaderNew, (void **)&(pPacketNew->pCreateInfo->pQueueFamilyIndices));
    vktrace_finalize_buffer_address(pHeaderNew, (void **)&(pPacketNew->pCreateInfo));
    vktrace_finalize_buffer_address(pHeaderNew, (void **)&(pPacketNew->pAllocator));
    vktrace_finalize_buffer_address(pHeaderNew, (void **)&(pPacketNew->pBuffer));

    pHeader = pHeaderNew;

    return ret;
}

static int post_fixscratchbufsize_cmd_build_as(vktrace_trace_file_header* pFileHeader,  vktrace_trace_packet_header* &pHeader, FILE* newTraceFile, uint64_t* fileOffset, uint64_t* fileSize)
{
    int ret = 0;

    bool recreate_scratch = g_fixAsBufSize_recreate_scratch_gid.find(pHeader->global_packet_index) != g_fixAsBufSize_recreate_scratch_gid.end();
    bool replace_scratch = g_fixAsBufSize_replace_scratch_gid.find(pHeader->global_packet_index) != g_fixAsBufSize_replace_scratch_gid.end();

    if (!recreate_scratch && !replace_scratch) {
       return ret;
    }

    packet_vkCmdBuildAccelerationStructuresKHR* pPacket = (packet_vkCmdBuildAccelerationStructuresKHR*)pHeader->pBody;
    pPacket->header = pHeader;
    intptr_t pInfosOffset = (intptr_t)pPacket->pInfos;
    pPacket->pInfos = (VkAccelerationStructureBuildGeometryInfoKHR*)vktrace_trace_packet_interpret_buffer_pointer(pHeader, (intptr_t)pPacket->pInfos);
    for (uint32_t i = 0; i < pPacket->infoCount; ++i) {
        vktrace_trace_packet_interpret_buffer_pointer(pHeader, (intptr_t)(pInfosOffset + sizeof(VkAccelerationStructureBuildGeometryInfoKHR) * i));
    }

    VkDevice device = g_vkCmdBuf2Device[pPacket->commandBuffer];

    if (recreate_scratch) {
        vktrace_LogAlways("gid %llu, recreate scratch buffer and memory in vkCmdBuildAccelerationStructuresKHR", pHeader->global_packet_index);
        vector<scratch_buffer_info>& scratches_info = g_fixAsBufSize_recreate_scratch_gid[pHeader->global_packet_index];

        for (vector<scratch_buffer_info>::iterator it_scratch = scratches_info.begin(); it_scratch != scratches_info.end(); ++it_scratch) {
            const VkAccelerationStructureBuildGeometryInfoKHR& info = pPacket->pInfos[it_scratch->info_index];

            // create a new buffer for scratch
            VkExportFenceCreateInfo usage;
            usage = {
                .sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO,
                .pNext = NULL,
                .handleTypes = VK_BUFFER_USAGE_EXTRA_MARK_SCRATCH,
            };

            VkBufferCreateInfo createInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                          &usage,
                                          0,
                                          it_scratch->size,
                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                          VK_SHARING_MODE_EXCLUSIVE,
                                          0,
                                          NULL};
            g_Handles.handleCount++;
            g_Handles.remapBuffer = (VkBuffer)g_Handles.handleCount;
            ret = post_generate_vkCreateBuffer(device, &createInfo, &g_Handles.remapBuffer, newTraceFile, fileOffset, fileSize);
            if (it_scratch->buffer != VK_NULL_HANDLE) {
                g_extraAddedBuffer2Buffers[it_scratch->buffer].push_back(g_Handles.remapBuffer);
            }
            if (ret != 0) {
                return ret;
            }

            // create a new memory for scratch
            VkMemoryAllocateFlagsInfo flagsInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, NULL,
                                                VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, 0};

            // TODO: if size == 0, we have to inject a new vkGetAccelerationStructureBuildSize call to get the size
            assert(it_scratch->size != 0);
            // TODO: if memoryType is unknown, we have to inject calls to query the required memory type index
            assert(it_scratch->memoryTypeIndex != UINT32_MAX);

            VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flagsInfo, it_scratch->size,
                                              it_scratch->memoryTypeIndex};
            g_Handles.handleCount++;
            g_Handles.remapMemory = (VkDeviceMemory)g_Handles.handleCount;
            ret = post_generate_vkAllocateMemory(device, &allocateInfo, &g_Handles.remapMemory, newTraceFile, fileOffset,
                                                     fileSize);
            if (it_scratch->memory != VK_NULL_HANDLE) {
                g_extraAddedMemory2Memorys[it_scratch->memory].push_back(g_Handles.remapMemory);
            }
            if (ret != 0) {
                return ret;
            }

            // bind buffer to memory
            ret = post_generate_vkBindBufferMemory(device, g_Handles.remapBuffer, g_Handles.remapMemory, 0, newTraceFile,
                                                       fileOffset, fileSize);
            if (ret != 0) {
                return ret;
            }

            // get buffer device address
            VkBufferDeviceAddressInfo bufferDeviceInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, NULL, g_Handles.remapBuffer};
            g_Handles.bufDeviceAddr = getMaxBufferDeviceAddress(device, it_scratch->size);
            ret = post_generate_vkGetBufferDeviceAddressKHR(device, &bufferDeviceInfo, &g_Handles.bufDeviceAddr, newTraceFile,
                                                                fileOffset, fileSize);
            if (ret != 0) {
                return ret;
            }
            auto scratchAddress = info.scratchData.deviceAddress;
            const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(&info)->scratchData.deviceAddress = g_Handles.bufDeviceAddr;
            it_scratch->address = info.scratchData.deviceAddress;

            g_dev2AS2ScratchAddr2ScratchInfo[device][info.dstAccelerationStructure][scratchAddress] = *it_scratch;
        } //  for (vector<scratch_buffer_info>::iterator it_scratch = scratches_info.begin(); it_scratch != scratches_info.end(); ++it_scratch)
    } // if (recreate_scratch)
    else {
        assert(replace_scratch == true);
        vector<uint32_t>& indices = g_fixAsBufSize_replace_scratch_gid[pHeader->global_packet_index];
        vector<uint32_t>::iterator it_index = indices.begin();
        while (it_index != indices.end()) {
            const VkAccelerationStructureBuildGeometryInfoKHR& info = pPacket->pInfos[*it_index];
            assert(g_dev2AS2ScratchAddr2ScratchInfo.find(device) != g_dev2AS2ScratchAddr2ScratchInfo.end());
            assert(g_dev2AS2ScratchAddr2ScratchInfo[device].find(info.dstAccelerationStructure) != g_dev2AS2ScratchAddr2ScratchInfo[device].end());
            assert(g_dev2AS2ScratchAddr2ScratchInfo[device][info.dstAccelerationStructure].find(info.scratchData.deviceAddress) != g_dev2AS2ScratchAddr2ScratchInfo[device][info.dstAccelerationStructure].end());
            auto temp = info.scratchData.deviceAddress;
            const_cast<VkAccelerationStructureBuildGeometryInfoKHR*>(&info)->scratchData.deviceAddress = g_dev2AS2ScratchAddr2ScratchInfo[device][info.dstAccelerationStructure][info.scratchData.deviceAddress].address;
            it_index++;
        }
    }

    for (uint32_t i = 0; i < pPacket->infoCount; ++i) {
        VkAccelerationStructureBuildGeometryInfoKHR* temp = (VkAccelerationStructureBuildGeometryInfoKHR* )&(pPacket->pInfos[i]);
        vktrace_finalize_buffer_address(pHeader, (void**)&temp);
    }
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pInfos));

    return ret;
}

static int post_fixscratchbufsize_cmd_build_as_indirect(vktrace_trace_file_header* pFileHeader,
                                                        vktrace_trace_packet_header*& pHeader, FILE* newTraceFile,
                                                        uint64_t* fileOffset, uint64_t* fileSize) {
    // TODO:
    // to be implemented later
    assert(0);
    vktrace_LogError("post_fixscratchbufsize_cmd_build_as_indirect is not implemented!");
    return -1;
}

static int post_fixasbufsize_mov_alloc_mem_call(vktrace_trace_file_header* pFileHeader, FILE* newTraceFile,
                                                uint64_t* fileOffset, uint64_t* fileSize)
{
    int ret = 0;
    if (g_mem_params.packets_mov_to.find(g_mem_params.packet_index) == g_mem_params.packets_mov_to.end())
    {
        return ret;
    }
    std::vector<uint64_t>& packets_index = g_mem_params.packets_mov_to[g_mem_params.packet_index];
    for (uint32_t index = 0; index < packets_index.size(); index++) {
        uint64_t packet_id = packets_index[index];
        if (g_mem_params.packets_mov_from.find(packet_id) == g_mem_params.packets_mov_from.end()) {
            vktrace_LogError(
                "post_fixasbufsize_mov_alloc_mem_call: failed to find packet to be moved with packet id = %lld\n", packet_id);
            ret = -1;
            continue;
        }
        vktrace_trace_packet_header* pHeader = g_mem_params.packets_mov_from[packet_id];
        packet_vkAllocateMemory* pPacket = interpret_body_as_vkAllocateMemory(pHeader);
        int retv = post_generate_vkAllocateMemory(pPacket->device, pPacket->pAllocateInfo, pPacket->pMemory, newTraceFile,
                                                      fileOffset, fileSize);
        if (retv != 0) {
            vktrace_LogError(
                "post_fixasbufsize_mov_alloc_mem_call: failed to move vkAllocateMemory packet by packet index: %lld\n", packet_id);
            ret = -1;
        }
        free(pHeader);
        g_mem_params.packets_mov_from.erase(packet_id);
    }
    return ret;
}

static int post_fixasbufsize_bind_buffer_memory(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header*& pHeader,
                                                FILE* newTraceFile, uint64_t* fileOffset, uint64_t* fileSize) {
    int ret = 0;
    packet_vkBindBufferMemory* pPacket = interpret_body_as_vkBindBufferMemory(pHeader);
    bool recreate_mem = false;
    VkDeviceSize allocationSize = 0;
    uint32_t memoryTypeIndex = 0;

    if (g_fixAsBufSize_recreate_mem_gid.find(pHeader->global_packet_index) != g_fixAsBufSize_recreate_mem_gid.end()) {
        buffer_info& bufInfo = g_fixAsBufSize_recreate_mem_gid[pHeader->global_packet_index][pPacket->buffer];
        recreate_mem = true;
        allocationSize = bufInfo.size;
        memoryTypeIndex = bufInfo.memoryTypeIndex;
    } else if (g_fixAsBufSize_potential_recreate_mem_gid.find(pHeader->global_packet_index) != g_fixAsBufSize_potential_recreate_mem_gid.end()) {
        buffer_info& bufInfo = g_fixAsBufSize_potential_recreate_mem_gid[pHeader->global_packet_index][pPacket->buffer];
        recreate_mem = true;
        allocationSize = bufInfo.size;
        memoryTypeIndex = bufInfo.memoryTypeIndex;
    }
    // we should create a new memory and bound to the current buffer
    if (recreate_mem) {
        VkMemoryAllocateFlagsInfo flagsInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, NULL,
                                            VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, 0};

        VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                          .pNext = &flagsInfo,
                                          .allocationSize = allocationSize,
                                          .memoryTypeIndex = memoryTypeIndex};
        g_Handles.handleCount++;
        g_Handles.remapMemory = (VkDeviceMemory)g_Handles.handleCount;
        ret = post_generate_vkAllocateMemory(pPacket->device, &allocInfo, &g_Handles.remapMemory, newTraceFile, fileOffset,
                                                 fileSize);
        g_extraAddedMemory2Memorys[pPacket->memory].push_back(g_Handles.remapMemory);

        vktrace_LogAlways("gid %llu, recreate memory vkBindBufferMemory ", pHeader->global_packet_index);

        if (ret == 0) {
            pPacket->memory = (VkDeviceMemory)g_Handles.remapMemory;
            pPacket->memoryOffset = 0;
        } else {
            return ret;
        }
    }
    return ret;
}

static int post_fixasbufsize_bind_buffer_memory2(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header*& pHeader,
                                                 FILE* newTraceFile, uint64_t* fileOffset, uint64_t* fileSize) {
    int ret = 0;
    packet_vkBindBufferMemory2* pPacket = (packet_vkBindBufferMemory2*)pHeader->pBody;
    pPacket->pBindInfos =
        (const VkBindBufferMemoryInfo*)vktrace_trace_packet_interpret_buffer_pointer(pHeader, (intptr_t)pPacket->pBindInfos);

    for (uint32_t index = 0; index < pPacket->bindInfoCount; index++) {
        bool recreate_mem = false;
        VkDeviceSize allocationSize = 0;
        uint32_t memoryTypeIndex = 0;

        if (g_fixAsBufSize_recreate_mem_gid.find(pHeader->global_packet_index) != g_fixAsBufSize_recreate_mem_gid.end()) {
            buffer_info& bufInfo = g_fixAsBufSize_recreate_mem_gid[pHeader->global_packet_index][pPacket->pBindInfos[index].buffer];
            recreate_mem = true;
            allocationSize = bufInfo.size;
            memoryTypeIndex = bufInfo.memoryTypeIndex;
        } else if (g_fixAsBufSize_potential_recreate_mem_gid.find(pHeader->global_packet_index) != g_fixAsBufSize_potential_recreate_mem_gid.end()) {
            buffer_info& bufInfo = g_fixAsBufSize_potential_recreate_mem_gid[pHeader->global_packet_index][pPacket->pBindInfos[index].buffer];
            recreate_mem = true;
            allocationSize = bufInfo.size;
            memoryTypeIndex = bufInfo.memoryTypeIndex;
        }

        if (!recreate_mem) {
            continue;
        }

        // we should create a new memory and bound to the current buffer
        vktrace_LogAlways("gid %llu, recreate memory in vkBindBufferMemory2", pHeader->global_packet_index);
        VkMemoryAllocateFlagsInfo flagsInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, NULL,
                                            VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, 0};

        VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                          .pNext = &flagsInfo,
                                          .allocationSize = allocationSize,
                                          .memoryTypeIndex = memoryTypeIndex};
        g_Handles.handleCount++;
        g_Handles.remapMemory = (VkDeviceMemory)g_Handles.handleCount;
        ret = post_generate_vkAllocateMemory(pPacket->device, &allocInfo, &g_Handles.remapMemory, newTraceFile, fileOffset,
                                                 fileSize);
        g_extraAddedMemory2Memorys[pPacket->pBindInfos[index].memory].push_back(g_Handles.remapMemory);
        if (ret != 0) {
            return ret;
        }
        (const_cast<VkBindBufferMemoryInfo*>(pPacket->pBindInfos))[index].memory = (VkDeviceMemory)g_Handles.remapMemory;
        (const_cast<VkBindBufferMemoryInfo*>(pPacket->pBindInfos))[index].memoryOffset = 0;
    }

    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pBindInfos));
    return ret;
}

static int post_fixasbufsize_create_as(vktrace_trace_file_header* pFileHeader,  vktrace_trace_packet_header* &pHeader, FILE* newTraceFile, uint64_t* fileOffset, uint64_t* fileSize)
{
    int ret = 0;
    packet_vkCreateAccelerationStructureKHR* pPacket = (packet_vkCreateAccelerationStructureKHR*)pHeader->pBody;
    pPacket->header = pHeader;
    pPacket->pCreateInfo = (const VkAccelerationStructureCreateInfoKHR*)vktrace_trace_packet_interpret_buffer_pointer(pHeader, (intptr_t)pPacket->pCreateInfo);
    pPacket->pAccelerationStructure = (VkAccelerationStructureKHR*)vktrace_trace_packet_interpret_buffer_pointer(pHeader, (intptr_t)pPacket->pAccelerationStructure);
    if (g_fixAsBufSize_recreate_buffer_mem_gid.find(pHeader->global_packet_index) != g_fixAsBufSize_recreate_buffer_mem_gid.end()) {
        if (pPacket->pCreateInfo->offset == 0) {
            vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pCreateInfo));
            vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAccelerationStructure));
            return ret;
        }

        buffer_info& bufInfo = g_fixAsBufSize_recreate_buffer_mem_gid[pHeader->global_packet_index];
        VkDevice device = bufInfo.device;
        unsigned int mappableMemoryIdx = bufInfo.memoryTypeIndex;
        VkDeviceSize asBufSize = pPacket->pCreateInfo->size;

        VkExportFenceCreateInfo usage;
        usage = {
            .sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO,
            .pNext = NULL,
            .handleTypes = VK_BUFFER_USAGE_EXTRA_MARK_ASBUFFER,
        };

        VkBufferCreateInfo createInfo {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, &usage, 0, asBufSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
            VK_SHARING_MODE_EXCLUSIVE, 0, NULL
        };

        g_Handles.handleCount++;
        g_Handles.remapBuffer = (VkBuffer)g_Handles.handleCount;
        ret = post_generate_vkCreateBuffer(device, &createInfo, &g_Handles.remapBuffer, newTraceFile, fileOffset, fileSize);
        g_extraAddedAS2Buffers[*pPacket->pAccelerationStructure].push_back(g_Handles.remapBuffer);
        if (ret != 0) {
            vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pCreateInfo));
            vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAccelerationStructure));
            return ret;
        }

        VkMemoryAllocateFlagsInfo flagsInfo {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, NULL, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, 0
        };

        VkMemoryAllocateInfo allocateInfo {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flagsInfo, asBufSize, mappableMemoryIdx
        };
        g_Handles.handleCount++;
        g_Handles.remapMemory = (VkDeviceMemory)g_Handles.handleCount;
        ret = post_generate_vkAllocateMemory(device, &allocateInfo, &g_Handles.remapMemory, newTraceFile, fileOffset, fileSize);
        g_extraAddedAS2Memorys[*pPacket->pAccelerationStructure].push_back(g_Handles.remapMemory);
        if (ret != 0) {
            vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pCreateInfo));
            vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAccelerationStructure));
            return ret;
        }

        ret = post_generate_vkBindBufferMemory(device, g_Handles.remapBuffer, g_Handles.remapMemory, 0, newTraceFile, fileOffset, fileSize);
        if (ret != 0) {
            vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pCreateInfo));
            vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAccelerationStructure));
            return ret;
        }

        VkBufferDeviceAddressInfo bufferDeviceInfo {
            VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, NULL, g_Handles.remapBuffer
        };
        g_Handles.bufDeviceAddr = getMaxBufferDeviceAddress(device, asBufSize);
        ret = post_generate_vkGetBufferDeviceAddressKHR(device, &bufferDeviceInfo, &g_Handles.bufDeviceAddr, newTraceFile, fileOffset, fileSize);
        if (ret != 0) {
            vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pCreateInfo));
            vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAccelerationStructure));
            return ret;
        }

        const_cast<VkAccelerationStructureCreateInfoKHR*>(pPacket->pCreateInfo)->buffer = g_Handles.remapBuffer;
        const_cast<VkAccelerationStructureCreateInfoKHR*>(pPacket->pCreateInfo)->offset = 0;
    }

    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pCreateInfo));
    vktrace_finalize_buffer_address(pHeader, (void**)&(pPacket->pAccelerationStructure));
    return ret;
}

int post_as_dedicate_mem(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header*& pHeader, FILE* newTraceFile,
                uint64_t* fileOffset, uint64_t* fileSize) {
    if (pHeader->packet_id != VKTRACE_TPI_VK_vkGetDeviceQueue &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkGetAccelerationStructureBuildSizesKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCreateBuffer &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory2 &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory2KHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCreateAccelerationStructureKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresIndirectKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkFreeMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyBuffer &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyAccelerationStructureKHR &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkDestroyDevice &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkAllocateCommandBuffers &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkFreeCommandBuffers) {
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
    static VkDevice traceDevice = VK_NULL_HANDLE;
    switch (pHeader->packet_id) {
        case VKTRACE_TPI_VK_vkCreateBuffer: {
            ret = post_fixasbufsize_vkCreateBuffer(pFileHeader, pHeader, newTraceFile, fileOffset, fileSize);
            break;
        }
        case VKTRACE_TPI_VK_vkGetDeviceQueue: {
            packet_vkGetDeviceQueue* pPacket = (packet_vkGetDeviceQueue*)pHeader->pBody;
            traceDevice = pPacket->device;
            break;
        }
        case VKTRACE_TPI_VK_vkAllocateCommandBuffers: {
            ret = fixasbuffersize_alloc_cmdbufs(pCopy);
            break;
        }
        case VKTRACE_TPI_VK_vkFreeCommandBuffers: {
            ret = fixasbuffersize_free_cmdbufs(pCopy);
            break;
        }
        case VKTRACE_TPI_VK_vkGetAccelerationStructureBuildSizesKHR: {
            packet_vkGetAccelerationStructureBuildSizesKHR* pPacket =
                (packet_vkGetAccelerationStructureBuildSizesKHR*)pHeader->pBody;
            if (traceDevice != VK_NULL_HANDLE) {
                pPacket->device = traceDevice;
            }
            break;
        }
        case VKTRACE_TPI_VK_vkBindBufferMemory: {
            ret = post_fixasbufsize_bind_buffer_memory(pFileHeader, pHeader, newTraceFile, fileOffset, fileSize);
            break;
        }
        case VKTRACE_TPI_VK_vkBindBufferMemory2:
        case VKTRACE_TPI_VK_vkBindBufferMemory2KHR: {
            ret = post_fixasbufsize_bind_buffer_memory2(pFileHeader, pHeader, newTraceFile, fileOffset, fileSize);
            break;
        }
        case VKTRACE_TPI_VK_vkCreateAccelerationStructureKHR: {
            ret = post_fixasbufsize_create_as(pFileHeader, pHeader, newTraceFile, fileOffset, fileSize);
            break;
        }
        case VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresKHR: {
            ret = post_fixscratchbufsize_cmd_build_as(pFileHeader, pHeader, newTraceFile, fileOffset, fileSize);
            break;
        }
        case VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresIndirectKHR: {
            ret = post_fixscratchbufsize_cmd_build_as_indirect(pFileHeader, pHeader, newTraceFile, fileOffset, fileSize);
            break;
        }
        case VKTRACE_TPI_VK_vkFreeMemory: {
            packet_vkFreeMemory* pPacket = (packet_vkFreeMemory*)pHeader->pBody;
            if (g_extraAddedMemory2Memorys.find(pPacket->memory) != g_extraAddedMemory2Memorys.end()) {
                for (uint32_t i = 0; i < g_extraAddedMemory2Memorys[pPacket->memory].size(); i++) {
                    post_generate_vkFreeMemory(pPacket->device, g_extraAddedMemory2Memorys[pPacket->memory][i], newTraceFile,
                                                   fileOffset, fileSize);
                }
                g_extraAddedMemory2Memorys[pPacket->memory].clear();
                g_extraAddedMemory2Memorys.erase(pPacket->memory);
            }
            break;
        }
        case VKTRACE_TPI_VK_vkDestroyBuffer: {
            packet_vkDestroyBuffer* pPacket = (packet_vkDestroyBuffer*)pHeader->pBody;
            if (g_extraAddedBuffer2Buffers.find(pPacket->buffer) != g_extraAddedBuffer2Buffers.end()) {
                for (uint32_t i = 0; i < g_extraAddedBuffer2Buffers[pPacket->buffer].size(); i++) {
                    post_generate_vkDestroyBuffer(pPacket->device, g_extraAddedBuffer2Buffers[pPacket->buffer][i], newTraceFile,
                                                      fileOffset, fileSize);
                }
                g_extraAddedBuffer2Buffers[pPacket->buffer].clear();
                g_extraAddedBuffer2Buffers.erase(pPacket->buffer);
            }
            break;
        }
        case VKTRACE_TPI_VK_vkDestroyAccelerationStructureKHR: {
            packet_vkDestroyAccelerationStructureKHR* pPacket = (packet_vkDestroyAccelerationStructureKHR*)pHeader->pBody;
            if (g_extraAddedAS2Buffers.find(pPacket->accelerationStructure) != g_extraAddedAS2Buffers.end()) {
                for (uint32_t i = 0; i < g_extraAddedAS2Buffers[pPacket->accelerationStructure].size(); i++) {
                    post_generate_vkDestroyBuffer(pPacket->device, g_extraAddedAS2Buffers[pPacket->accelerationStructure][i], newTraceFile, fileOffset, fileSize);
                }
                g_extraAddedAS2Buffers[pPacket->accelerationStructure].clear();
                g_extraAddedAS2Buffers.erase(pPacket->accelerationStructure);
            }
            if (g_extraAddedAS2Memorys.find(pPacket->accelerationStructure) != g_extraAddedAS2Memorys.end()) {
                for (uint32_t i = 0; i < g_extraAddedAS2Memorys[pPacket->accelerationStructure].size(); i++) {
                    post_generate_vkFreeMemory(pPacket->device, g_extraAddedAS2Memorys[pPacket->accelerationStructure][i], newTraceFile, fileOffset, fileSize);
                }
                g_extraAddedAS2Memorys[pPacket->accelerationStructure].clear();
                g_extraAddedAS2Memorys.erase(pPacket->accelerationStructure);
            }
            break;
        }
        case VKTRACE_TPI_VK_vkDestroyDevice: {
            packet_vkDestroyDevice* pPacket = (packet_vkDestroyDevice*)pHeader->pBody;
            for (auto itBuf = g_extraAddedBuffer2Buffers.begin(); itBuf != g_extraAddedBuffer2Buffers.end(); ++itBuf) {
                for (uint32_t i = 0; i < g_extraAddedBuffer2Buffers[itBuf->first].size(); i++) {
                    post_generate_vkDestroyBuffer(pPacket->device, g_extraAddedBuffer2Buffers[itBuf->first][i], newTraceFile,
                                                      fileOffset, fileSize);
                }
            }

            for (auto itMem = g_extraAddedMemory2Memorys.begin(); itMem != g_extraAddedMemory2Memorys.end(); ++itMem) {
                for (uint32_t i = 0; i < g_extraAddedMemory2Memorys[itMem->first].size(); i++) {
                    post_generate_vkFreeMemory(pPacket->device, g_extraAddedMemory2Memorys[itMem->first][i], newTraceFile,
                                                   fileOffset, fileSize);
                }
            }
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

int post_move_allocate_memory(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header*& pHeader, FILE* newTraceFile,
                              uint64_t* fileOffset, uint64_t* fileSize, bool &rmdp) {
    if (pHeader->packet_id != VKTRACE_TPI_VK_vkAllocateMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkCreateBuffer &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory2 &&
        pHeader->packet_id != VKTRACE_TPI_VK_vkBindBufferMemory2KHR) {
        return 0;
    }

    static bool firstRun = true;
    if (firstRun) {
        g_mem_params.packet_index = 0;
        firstRun = false;
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
    static VkDevice traceDevice = VK_NULL_HANDLE;
    switch (pHeader->packet_id) {
        case VKTRACE_TPI_VK_vkAllocateMemory: {
            if (g_mem_params.packets_mov_from.find(g_mem_params.packet_index) != g_mem_params.packets_mov_from.end()) {
                assert(g_mem_params.packets_mov_from[g_mem_params.packet_index] == nullptr);
                vktrace_trace_packet_header* pNewPacket = (vktrace_trace_packet_header*)malloc(pHeader->size);
                memcpy(pNewPacket, pHeader, pHeader->size);
                pNewPacket->pBody = (uintptr_t)(((char*)pNewPacket) + sizeof(vktrace_trace_packet_header));
                g_mem_params.packets_mov_from[g_mem_params.packet_index] = pNewPacket;
                rmdp = true;
            }
            g_mem_params.packet_index++;
            break;
        }
        case VKTRACE_TPI_VK_vkCreateBuffer: {
            g_mem_params.packet_index++;
            break;
        }
        case VKTRACE_TPI_VK_vkBindBufferMemory: {
            post_fixasbufsize_mov_alloc_mem_call(pFileHeader, newTraceFile, fileOffset, fileSize);
            g_mem_params.packet_index++;
            break;
        }
        case VKTRACE_TPI_VK_vkBindBufferMemory2:
        case VKTRACE_TPI_VK_vkBindBufferMemory2KHR: {
            post_fixasbufsize_mov_alloc_mem_call(pFileHeader, newTraceFile, fileOffset, fileSize);
            g_mem_params.packet_index++;
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
