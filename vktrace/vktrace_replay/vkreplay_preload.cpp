/*
 * (C) COPYRIGHT 2019 ARM Limited
 * ALL RIGHTS RESERVED
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
 */

extern "C" {
#include "vktrace_filelike.h"
#include "vktrace_trace_packet_identifiers.h"
#include "vktrace_trace_packet_utils.h"
}

#include <cinttypes>
#include <sys/sysinfo.h>

#include <thread>
#include <mutex>
#include <condition_variable>

#include "vkreplay_factory.h"
#include "vkreplay_preload.h"
#include "vkreplay_vkreplay.h"
#include "vkreplay_main.h"

#define MAX_CHUNK_COUNT 16
#define SIZE_1K         1024
#define SIZE_1M         (SIZE_1K * SIZE_1K)

enum chunk_status {
    CHUNK_EMPTY,
    CHUNK_LOADING,
    CHUNK_READY,
    CHUNK_USING
};

struct mem_chunk_info {
    uint64_t     chunk_size      = 0;
    char*        base_address    = nullptr;
    char*        current_address = nullptr;
    chunk_status status          = CHUNK_EMPTY;
    std::mutex   mtx;
};

struct simple_sem {
    std::mutex              mtx;
    std::condition_variable cv;
    void wait() {
        std::unique_lock<std::mutex> lck(mtx);
        cv.wait(lck);
    }
    void notify() {
        std::unique_lock<std::mutex> lck(mtx);
        cv.notify_one();
    }
};

struct preload_context {
    mem_chunk_info chunks[MAX_CHUNK_COUNT];
    uint32_t    chunk_count   = MAX_CHUNK_COUNT;
    uint32_t    loading_idx   = MAX_CHUNK_COUNT;
    uint32_t    using_idx     = MAX_CHUNK_COUNT;
    uint64_t    next_pkt_size = 0;
    uint64_t    next_pkt_size_decompressed = 0;
    char*       preload_mem   = nullptr;
    FileLike*   tracefile     = nullptr;
    bool        exiting_thd   = false;
    std::thread thd_obj;
    simple_sem  replay_start_sem;
    bool        exceed_preloading_range = false;
    uint64_t    preload_waiting_time = 0;
} g_preload_context;

vktrace_trace_packet_header g_preload_header;
vktrace_trace_packet_header_compression_ext g_preload_header_ext;
static decompressor* g_decompressor = nullptr;

uint64_t get_preload_waiting_time_when_replaying()
{
    return g_preload_context.preload_waiting_time;
}

static uint64_t get_system_memory_size() {
    struct sysinfo si;
    if (sysinfo(&si)) {
        return 0;
    }
    uint64_t result = (uint64_t)si.totalram * (uint64_t)si.mem_unit;
    if (sizeof(void*) == 4) { // it is 32bit system
        const uint32_t max_size_of_mem_32b = ~0;
        result = result < max_size_of_mem_32b ? result : max_size_of_mem_32b;
    }
    return result;
}

static uint32_t calc_chunk_count(uint64_t sys_mem_size, uint64_t chunk_size, uint64_t file_size) {
    uint64_t preload_mem_size = sys_mem_size * replaySettings.memoryPercentage / 100;

    preload_mem_size = preload_mem_size < file_size ? preload_mem_size : file_size;
    uint32_t chunk_count = (preload_mem_size + chunk_size - 1) / chunk_size;

    return chunk_count;
}

static void get_packet_size(FileLike* file) {
    if (vktrace_FileLike_ReadRaw(file, &g_preload_header, sizeof(vktrace_trace_packet_header)) == FALSE) {
        g_preload_context.next_pkt_size = 0;
        g_preload_context.next_pkt_size_decompressed = 0;
        return;
    }
    g_preload_context.next_pkt_size = g_preload_header.size;
    if (g_preload_header.tracer_id == VKTRACE_TID_VULKAN_COMPRESSED) {      // a compressed packet
        if (vktrace_FileLike_ReadRaw(file, &g_preload_header_ext, sizeof(vktrace_trace_packet_header_compression_ext)) == FALSE) {
            g_preload_context.next_pkt_size = 0;
            g_preload_context.next_pkt_size_decompressed = 0;
            return;
        }
        else {
            g_preload_context.next_pkt_size_decompressed = g_preload_header_ext.decompressed_size + sizeof(vktrace_trace_packet_header);
        }
    }
    else {      // an uncompressed packet
        g_preload_context.next_pkt_size_decompressed = g_preload_header.size;
    }
}

vktrace_replay::vktrace_trace_packet_replay_library **replayerArray;
vktrace_replay::vktrace_trace_packet_replay_library *replayer = NULL;

static uint64_t load_packet(FileLike* file, void* load_addr, uint64_t packet_size) {
    vktrace_trace_packet_header* pHeader = (vktrace_trace_packet_header*)load_addr;

    static unsigned int frame_counter = 0;
    if (g_preload_header.tracer_id == VKTRACE_TID_VULKAN_COMPRESSED) {
        vktrace_trace_packet_header* preload_mem = (vktrace_trace_packet_header*)vktrace_malloc(g_preload_header.size);
        memcpy(preload_mem, &g_preload_header, sizeof(vktrace_trace_packet_header));
        preload_mem->pBody = (uintptr_t)(preload_mem + 1);
        memcpy(preload_mem + 1, &g_preload_header_ext, sizeof(vktrace_trace_packet_header_compression_ext));
        vktrace_trace_packet_header_compression_ext *tmp = (vktrace_trace_packet_header_compression_ext *)(preload_mem + 1);
        tmp->pBody = (uintptr_t)tmp + sizeof(vktrace_trace_packet_header_compression_ext);
        if (vktrace_FileLike_ReadRaw(file, (char *)preload_mem + sizeof(vktrace_trace_packet_header) + sizeof(vktrace_trace_packet_header_compression_ext),
                    (size_t)g_preload_header.size - sizeof(vktrace_trace_packet_header) - sizeof(vktrace_trace_packet_header_compression_ext)) == FALSE) {
            vktrace_LogError("Failed to read trace packet with size of %llu.", g_preload_header.size);
            return 0;
        }
        if (decompress_packet(g_decompressor, preload_mem) != 0) {
            vktrace_LogError("Failed to decompress trace packet with size of %llu.", g_preload_header.size);
            return 0;
        }
        memcpy(pHeader, preload_mem, preload_mem->size);
        vktrace_free(preload_mem);
    }
    else {
        pHeader->size = g_preload_header.size;
        memcpy(pHeader, &g_preload_header, sizeof(vktrace_trace_packet_header));
        if (vktrace_FileLike_ReadRaw(file,
                (char*)pHeader + sizeof(vktrace_trace_packet_header),
                (size_t)(pHeader->size) - sizeof(vktrace_trace_packet_header)) == FALSE) {
            vktrace_LogError("Failed to read trace packet with size of %llu.", packet_size);
            return 0;
        }
     }

    if (pHeader->packet_id == VKTRACE_TPI_VK_vkQueuePresentKHR) {
        ++frame_counter;
        if (frame_counter + vktrace_replay::getStartFrame() >= vktrace_replay::getEndFrame())
            g_preload_context.exceed_preloading_range = true;
    }

    pHeader->pBody = (uintptr_t)pHeader + sizeof(vktrace_trace_packet_header);

    // interpret this packet
    switch (pHeader->packet_id) {
        case VKTRACE_TPI_MESSAGE:
#if defined(ANDROID) || !defined(ARM_ARCH)
            vktrace_trace_packet_message* msgPacket;
            msgPacket = vktrace_interpret_body_as_trace_packet_message(pHeader);
            vktrace_LogAlways("Packet %lu: Traced Message (%s): %s", pHeader->global_packet_index,
                              vktrace_LogLevelToShortString(msgPacket->type), msgPacket->message);
#endif
            break;
        case VKTRACE_TPI_MARKER_CHECKPOINT:
            break;
        case VKTRACE_TPI_MARKER_API_BOUNDARY:
            break;
        case VKTRACE_TPI_MARKER_API_GROUP_BEGIN:
            break;
        case VKTRACE_TPI_MARKER_API_GROUP_END:
            break;
        case VKTRACE_TPI_MARKER_TERMINATE_PROCESS:
            break;
        case VKTRACE_TPI_META_DATA:
        case VKTRACE_TPI_PORTABILITY_TABLE:
            break;
        case VKTRACE_TPI_VK_vkQueuePresentKHR: {
            vktrace_trace_packet_header* res = replayer->Interpret(pHeader);
            if (res == NULL) {
                vktrace_LogError("Failed to interpret QueuePresent().");
            }
            break;
        }
        // TODO processing code for all the above cases
        default: {
            if (pHeader->tracer_id >= VKTRACE_MAX_TRACER_ID_ARRAY_SIZE || pHeader->tracer_id == VKTRACE_TID_RESERVED) {
                vktrace_LogError("Tracer_id from packet num packet %d invalid.", pHeader->packet_id);
            }
            replayer = replayerArray[pHeader->tracer_id];
            if (replayer == NULL) {
                vktrace_LogWarning("Tracer_id %d has no valid replayer.", pHeader->tracer_id);
            }
            if (pHeader->packet_id >= VKTRACE_TPI_VK_vkApiVersion) {
                // replay the API packet
                vktrace_trace_packet_header* res = replayer->Interpret(pHeader);
                if (res == NULL) {
                    vktrace_LogError("Failed to replay packet_id %d, with global_packet_index %d.", pHeader->packet_id,
                                     pHeader->global_packet_index);
                }
            } else {
                vktrace_LogError("Bad packet type id=%d, index=%d.", pHeader->packet_id, pHeader->global_packet_index);
            }
            extern vkReplay* g_pReplayer;
            switch (pHeader->packet_id) {
                case VKTRACE_TPI_VK_vkFlushMappedMemoryRanges: {
                    if (replaySettings.premapping && g_pReplayer->premap_FlushMappedMemoryRanges(pHeader)) {
                        pHeader->packet_id += PREMAP_SHIFT;
                    }
                    break;
                }
                case VKTRACE_TPI_VK_vkUpdateDescriptorSets: {
                    if (replaySettings.premapping && g_pReplayer->premap_UpdateDescriptorSets(pHeader)) {
                        pHeader->packet_id += PREMAP_SHIFT;
                    }
                    break;
                }
                case VKTRACE_TPI_VK_vkCmdBindDescriptorSets: {
                    if (replaySettings.premapping && g_pReplayer->premap_CmdBindDescriptorSets(pHeader)) {
                        pHeader->packet_id += PREMAP_SHIFT;
                    }
                    break;
                }
                case VKTRACE_TPI_VK_vkCmdDrawIndexed: {
                    if (replaySettings.premapping && g_pReplayer->premap_CmdDrawIndexed(pHeader)) {
                        pHeader->packet_id += PREMAP_SHIFT;
                    }
                    break;
                }
                case VKTRACE_TPI_VK_vkCreatePipelineCache: {
                    if (replaySettings.enablePipelineCache) {
                        packet_vkCreatePipelineCache *pPacket = reinterpret_cast<packet_vkCreatePipelineCache *>(pHeader->pBody);
                        if (NULL != pPacket) {
                            auto accessor = g_pReplayer->get_pipelinecache_accessor();
                            assert(NULL != accessor);
                            const VkPipelineCache cache_handle = *(pPacket->pPipelineCache);
                            // We don't have gpu info and pipeline cache uuid info in this period
                            // so we only use cache handle to search cache file.
                            std::string &&full_path = accessor->FindFile(cache_handle);
                            if (false == full_path.empty()) {
                                if (accessor->LoadPipelineCache(full_path)) {
                                    auto cache_data = accessor->GetPipelineCache(cache_handle);
                                    if (nullptr != cache_data.first) {
                                        VkPipelineCacheCreateInfo *pCreateInfo = const_cast<VkPipelineCacheCreateInfo *>(pPacket->pCreateInfo);
                                        pCreateInfo->pInitialData = cache_data.first;
                                        pCreateInfo->initialDataSize = cache_data.second;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
                default: {
                    break;
                }
            }
            break;
        }
    }

    return packet_size;
}

bool preloaded_whole_range = true;
bool preloaded_whole()
{
    return preloaded_whole_range;
}

static void chunk_loading() {
    bool first_full = true;
    g_preload_context.loading_idx = 0;
    while (!g_preload_context.exiting_thd) {
        if (g_preload_context.loading_idx == g_preload_context.chunk_count) {
            g_preload_context.loading_idx = 0;
            if (first_full) {
                g_preload_context.replay_start_sem.notify();
                first_full = false;
            }
        }

        mem_chunk_info* cur_chunk = &g_preload_context.chunks[g_preload_context.loading_idx];

        if (cur_chunk->status == CHUNK_EMPTY || cur_chunk->status == CHUNK_USING) {
            cur_chunk->mtx.lock();
            cur_chunk->status = CHUNK_LOADING;

            if (!first_full) {
                preloaded_whole_range = false;
            }
            // loading
            vktrace_LogDebug("Loading chunk %d !", g_preload_context.loading_idx);
            uint32_t loaded_packet_count = 0;
            char* boundary_addr = cur_chunk->base_address + cur_chunk->chunk_size;

            while (!g_preload_context.exiting_thd
                   && g_preload_context.next_pkt_size_decompressed
                   && !g_preload_context.exceed_preloading_range
                   && (cur_chunk->current_address + g_preload_context.next_pkt_size_decompressed) < boundary_addr) {
                if (!load_packet(g_preload_context.tracefile, cur_chunk->current_address, g_preload_context.next_pkt_size)) {
                    break;
                }
                cur_chunk->current_address += g_preload_context.next_pkt_size_decompressed;
                get_packet_size(g_preload_context.tracefile);
                loaded_packet_count++;
            }
            cur_chunk->status = CHUNK_READY;
            g_preload_context.loading_idx++;
            vktrace_LogDebug("%d packets are loaded", loaded_packet_count);
            cur_chunk->mtx.unlock();
        } else if (cur_chunk->status != CHUNK_READY) {
            vktrace_LogWarning("Chunk loading thread: Not right, the chunk status(%d) confused !", cur_chunk->status);
        }

        if (!g_preload_context.next_pkt_size || g_preload_context.exceed_preloading_range) {
            // no more packets or exceeds the preloading range, exiting...
            if (first_full) g_preload_context.replay_start_sem.notify();
            break;
        }
    }
}

bool init_preload(FileLike* file, vktrace_replay::vktrace_trace_packet_replay_library *replayer_array[], decompressor* decompressor, uint64_t filesize) {

    g_decompressor = decompressor;
    replayerArray = replayer_array;
    bool ret = true;
    uint64_t system_mem_size = get_system_memory_size();
    vktrace_LogAlways("Init preload: the total size of system memory = 0x%llX, the decompress size = 0x%llX, original size = 0x%llX of the trace file.", system_mem_size, filesize, file->mFileLen);

    static const uint64_t chunk_size  = 400 * SIZE_1M;
    uint32_t chunk_count = calc_chunk_count(system_mem_size, chunk_size, filesize);
    chunk_count = chunk_count < MAX_CHUNK_COUNT ? chunk_count : MAX_CHUNK_COUNT;

    char* preload_mem = (char*)vktrace_malloc(chunk_size * chunk_count);
    while (preload_mem == nullptr && chunk_count > 1) {
        // Allocate memory failed, then decrease the chunk count
        chunk_count--;
        preload_mem = (char*)vktrace_malloc(chunk_size * chunk_count);
    }

    g_preload_context.chunk_count = chunk_count;
    g_preload_context.tracefile   = file;
    g_preload_context.loading_idx = chunk_count;
    g_preload_context.using_idx   = chunk_count;

    if (preload_mem) {
        vktrace_LogAlways("Init preload: the chunk size = 0x%llX and the chunk count = %d ...", chunk_size, chunk_count);

        g_preload_context.preload_mem = preload_mem;

        for (uint32_t i = 0; i < chunk_count; i++) {
            g_preload_context.chunks[i].chunk_size = chunk_size;
            g_preload_context.chunks[i].base_address = preload_mem + chunk_size * i;
            g_preload_context.chunks[i].current_address = g_preload_context.chunks[i].base_address;
        }

        get_packet_size(g_preload_context.tracefile);
        g_preload_context.thd_obj = std::thread(chunk_loading);

        // waiting for the loading thread full all the chunks
        g_preload_context.replay_start_sem.wait();
    } else {
        vktrace_LogError("Failed to allocate memory for preload trace file !");
        ret = false;
    }

    return ret;
}

vktrace_trace_packet_header* preload_get_next_packet()
{
    if (g_preload_context.using_idx == g_preload_context.chunk_count) {
        g_preload_context.using_idx = 0;
    }

    mem_chunk_info* cur_chunk = &g_preload_context.chunks[g_preload_context.using_idx];
    static char* boundary_addr = 0;
    if (cur_chunk->status == CHUNK_READY || cur_chunk->status == CHUNK_LOADING) {
        uint64_t start_time = vktrace_get_time();
        cur_chunk->mtx.lock();
        uint64_t end_time = vktrace_get_time();
        if (vktrace_replay::timerStarted())
            g_preload_context.preload_waiting_time += end_time - start_time;
        assert(cur_chunk->status == CHUNK_READY);
        cur_chunk->status = CHUNK_USING;
        boundary_addr = cur_chunk->current_address;
        cur_chunk->current_address = cur_chunk->base_address;
    } else if (cur_chunk->status == CHUNK_EMPTY) {
        assert(!g_preload_context.next_pkt_size);
        return NULL;
    }

    vktrace_trace_packet_header* pHeader = (vktrace_trace_packet_header*)cur_chunk->current_address;
    cur_chunk->current_address += pHeader->size;

    if (cur_chunk->current_address >= boundary_addr) {
        cur_chunk->status = CHUNK_EMPTY;
        cur_chunk->current_address = cur_chunk->base_address;
        g_preload_context.using_idx++;
        cur_chunk->mtx.unlock();
        if (g_preload_context.loading_idx == g_preload_context.using_idx) {
            while (cur_chunk->status == CHUNK_EMPTY && !g_preload_context.exceed_preloading_range && g_preload_context.next_pkt_size) {
                std::this_thread::yield();
            }
        }
    }
    return pHeader;
}

void exit_preload() {
    g_preload_context.exiting_thd = true;
    if (g_preload_context.preload_mem) {
        for (uint32_t i = 0; i < g_preload_context.chunk_count; i++) {
            if (g_preload_context.chunks[i].status == CHUNK_USING) {
                g_preload_context.chunks[i].status = CHUNK_EMPTY;
                g_preload_context.chunks[i].mtx.unlock();
            }
        }
        g_preload_context.thd_obj.join();
        vktrace_free(g_preload_context.preload_mem);
        g_preload_context.preload_mem = nullptr;
    }
}

