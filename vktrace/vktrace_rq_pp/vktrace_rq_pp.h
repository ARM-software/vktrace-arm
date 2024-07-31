#pragma once

#include <list>
#include <unordered_map>
#include <vector>

#include "vktrace_common.h"
#include "compressor.h"
#include "decompressor.h"

enum command_type {
    COMMAND_TYPE_RQ = 0xf,
};

struct parser_params {
    command_type command;
    const char* paramFlag = nullptr;
    const char* srcTraceFile = nullptr;
    const char* dstTraceFile = nullptr;
    uint64_t shaderIndex = UINT64_MAX;
    uint64_t srcGlobalPacketIndexStart = UINT64_MAX;
    uint64_t srcGlobalPacketIndexEnd = UINT64_MAX;
};
extern parser_params g_params;

enum FlushStatus {
    NoFlush,
    ApiFlush,
    InternalFlush,
};

typedef struct mem_info {
    VkDevice device;
    VkDeviceMemory memory;
    VkDeviceSize size; // totalSize
    VkMemoryAllocateFlags flags;
    uint32_t memoryTypeIndex;
    FlushStatus     didFlush;
    VkDeviceSize    rangeSize;
    VkDeviceSize    rangeOffset;
    uint64_t packet_index;
} mem_info;

// data structures for rmid command to remove invalid data
typedef struct buffer_info {
    VkDevice device;
    VkBuffer buffer;
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    VkDeviceMemory memory;
    VkDeviceSize memoryOffset;
    uint64_t bind_packet_gid;
    uint32_t memoryTypeIndex;
    uint64_t packet_index;
    uint64_t alloc_mem_packet_index;
    VkMemoryRequirements memoryRequirements;
} buffer_info;

typedef struct packet_info {
    uint64_t position;
    uint64_t size;
} packet_info;

extern compressor* g_compressor;
extern decompressor* g_decompressor;
extern std::list<uint64_t> g_globalPacketIndexList;
extern std::unordered_map<uint64_t, packet_info> g_globalPacketIndexToPacketInfo;
extern std::vector<uint64_t> g_portabilityTable;
extern bool processBufDeviceAddr;
