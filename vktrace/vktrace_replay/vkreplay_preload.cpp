/*
 * (C) COPYRIGHT 2019-2023 ARM Limited
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
#include <fstream>

#include "vkreplay_factory.h"
#include "vkreplay_preload.h"
#include "vkreplay_vkreplay.h"
#include "vkreplay_main.h"

#define MAX_CHUNK_COUNT 600
#define SIZE_1K         1024
#define SIZE_1M         (SIZE_1K * SIZE_1K)

enum chunk_status {
    CHUNK_EMPTY,
    CHUNK_LOADING,
    CHUNK_READY,
    CHUNK_USING,
    CHUNK_SKIPPED
};

// skipped_to_first, 0: no skipping; 1: skipping detected; 2: skipping notify
enum skip_status {
    SKIPPING_NONE,
    SKIPPING_DETECTED,
    SKIPPING_NOTIFY,
    SKIPPING_DONE
};

enum skip_notify_status {
    SKIP_NOTIFY_NONE,
    SKIP_NOTIFY
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
    template<typename Predicate>
    void wait(Predicate pred) {
        std::unique_lock<std::mutex> lck(mtx);
        cv.wait(lck, pred);
    }
    void notify() {
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
    simple_sem  preload_sem;
    simple_sem  preload_sem_get;
    simple_sem  preload_sem_skip;
    simple_sem  preload_sem_head;
    simple_sem  preload_sem_ready;
    bool        exceed_preloading_range = false;
    uint64_t    preload_waiting_time = 0;

} g_preload_context;

vktrace_trace_packet_header g_preload_header;
vktrace_trace_packet_header_compression_ext g_preload_header_ext;
static decompressor* g_decompressor = nullptr;
static char*        tmp_address     = nullptr;

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

static uint64_t get_free_memory_size() {
     uint64_t free_mem = 0;
#if defined(ANDROID) || defined(__linux__)
        std::string token;
        std::ifstream file("/proc/meminfo");
        while(file >> token) {
            if(token == "MemFree:") {
                unsigned long mem;
                if(file >> mem) {
                    free_mem += mem;
                }
            }
            else if(token == "Buffers:") {
                unsigned long mem;
                if(file >> mem) {
                    free_mem += mem;
                }
            }
            else if(token == "Cached:") {
                unsigned long mem;
                if(file >> mem) {
                    free_mem += mem;
                }
            }
            // ignore rest of the line
            file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
        free_mem = free_mem * 1024; // To bytes
#endif
    return free_mem;
}

static uint32_t calc_chunk_count(uint64_t sys_mem_size, uint64_t chunk_size, uint64_t file_size) {
    uint32_t chunk_count = 0;
    uint64_t system_total_mem_size = get_system_memory_size();
    uint64_t preload_mem_size = sys_mem_size * replaySettings.memoryPercentage / 100;
    if (preload_mem_size > system_total_mem_size / 2) {
        vktrace_LogAlways("Init preload: The system total memory size = %llu, the free memory size greater than system_total_mem_size / 2.", system_total_mem_size);
        preload_mem_size = system_total_mem_size / 2;
    }
    preload_mem_size = std::min(preload_mem_size, MAX_CHUNK_COUNT * chunk_size);
    if (file_size <= preload_mem_size) {
        chunk_count = (file_size + chunk_size - 1) / chunk_size;
    } else {
        chunk_count = preload_mem_size / chunk_size;
    }
    if (chunk_count == 1 && file_size > chunk_size) {
        chunk_count = 0;   //Only 1 chunk free memory size, and it can't preload the whole file, so the preloading is meaningless.
    }
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
    if(g_preload_context.next_pkt_size_decompressed > replaySettings.preloadChunkSize * SIZE_1M)
        vktrace_LogDebug("The size of packet (global id: %llu, size %llu) is larger than the chunk size!", g_preload_header.global_packet_index, g_preload_context.next_pkt_size_decompressed);
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
        if (frame_counter + vktrace_replay::getStartFrame() >= vktrace_replay::getEndFrame() + 1) {
            g_preload_context.exceed_preloading_range = true;
            g_preload_context.preload_sem.notify();
        }
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

// skipped_to_first, 0: no skipping; 1: skipping detected; 2: skipping notify
volatile uint32_t skipped_to_first = SKIPPING_NONE;
volatile uint32_t skipped_notify = SKIP_NOTIFY_NONE;
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
            if(skipped_to_first == SKIPPING_DETECTED) {
                skipped_to_first = SKIPPING_NOTIFY;
                vktrace_LogDebug("Skipped to the first chunk!");
            }
        }
        if(skipped_to_first == SKIPPING_NOTIFY){
            vktrace_LogDebug("Chunk: %d notify when loading, skip status(%d)", g_preload_context.loading_idx, skipped_to_first);
            g_preload_context.preload_sem_skip.notify();
            skipped_to_first = SKIPPING_NONE;
            skipped_notify = SKIP_NOTIFY;
        }

        mem_chunk_info* cur_chunk = &g_preload_context.chunks[g_preload_context.loading_idx];
        if (cur_chunk->status == CHUNK_EMPTY || cur_chunk->status == CHUNK_USING) {
            cur_chunk->mtx.lock();
            vktrace_LogDebug("Chunk %llu is locked when loading.", g_preload_context.loading_idx);

            if (!first_full) {
                preloaded_whole_range = false;
            }
            // loading
            vktrace_LogDebug("Loading chunk %d !", g_preload_context.loading_idx);
            uint32_t loaded_packet_count = 0;
            char* boundary_addr = cur_chunk->base_address + cur_chunk->chunk_size;

            uint32_t chunks_needed = 1;

            // if the loading packet size is larger than the size of a chunk
            if(g_preload_context.next_pkt_size_decompressed > cur_chunk->chunk_size){
                // ceil(chunks_needed)
                chunks_needed =uint32_t((g_preload_context.next_pkt_size_decompressed - 1) * 1.0 / cur_chunk->chunk_size) + 1;
                assert(chunks_needed > 1);
                vktrace_LogDebug("The packet size is larger than the chunk size! %d chunks are needed!", chunks_needed);

                // if the remaining chunks size is not enough, skip to the first chunk, and set those chunk status as SKIPPED
                if(g_preload_context.loading_idx + chunks_needed > g_preload_context.chunk_count){
                    cur_chunk->status = CHUNK_SKIPPED;
                    // it is illegal to skip the first chunks
                    assert(g_preload_context.loading_idx != 0);
                    vktrace_LogDebug("Chunk %llu is SKIPPED !", g_preload_context.loading_idx);

                    for(uint32_t chunk_idx = g_preload_context.loading_idx + 1; chunk_idx < g_preload_context.chunk_count; ++chunk_idx){
                        mem_chunk_info* tmp_chunk = &g_preload_context.chunks[chunk_idx];
                        // wait if the chunk status is READY
                        if(tmp_chunk->status == CHUNK_READY || tmp_chunk->status == CHUNK_SKIPPED){
                            vktrace_LogDebug("Chunk %llu need to wait until its status is EMPTY or USING when loading (during skipping).", g_preload_context.loading_idx);
                            g_preload_context.preload_sem_get.wait([&]{
                                return tmp_chunk->status == CHUNK_EMPTY || tmp_chunk->status == CHUNK_USING;
                            });
                        }
                        tmp_chunk->mtx.lock();
                        vktrace_LogDebug("Chunk %llu is locked when loading.", chunk_idx);
                        tmp_chunk->status = CHUNK_SKIPPED;
                        tmp_chunk->mtx.unlock();
                        vktrace_LogDebug("Chunk %llu is unlocked when loading.", chunk_idx);
                        skipped_to_first = SKIPPING_DETECTED;
                        vktrace_LogDebug("Chunk %llu is SKIPPED!", chunk_idx);
                    }
                    cur_chunk->mtx.unlock();
                    vktrace_LogDebug("Chunk %llu is unlocked when loading.", g_preload_context.loading_idx);
                    // loading_idx will be 0 on next loop
                    g_preload_context.loading_idx = g_preload_context.chunk_count;
                    continue;

                } else if(g_preload_context.loading_idx + chunks_needed == g_preload_context.chunk_count) {
                    // a special case: notify the waiting thread
                   skipped_notify = SKIP_NOTIFY;
                }

                // if no chunks are skipped, then lock other chunks
                for(uint32_t i = 1; i < chunks_needed; ++i) {
                    mem_chunk_info* tmp_chunk = &g_preload_context.chunks[g_preload_context.loading_idx + i];
                    // lock directly if the following chunk status is EMPTY or USING
                    // wait if the chunk status is READY
                    // set these chunk status as LOADING
                    if(tmp_chunk->status == CHUNK_READY || tmp_chunk->status == CHUNK_SKIPPED){
                        vktrace_LogDebug("Chunk %llu need to wait until its status is EMPTY or USING when loading.", g_preload_context.loading_idx + i);
                        g_preload_context.preload_sem_get.wait([&]{
                            return tmp_chunk->status == CHUNK_EMPTY || tmp_chunk->status == CHUNK_USING;
                        });
                        vktrace_LogDebug("Chunk %llu finished waiting because of status(%llu) when loading.", g_preload_context.loading_idx + i, tmp_chunk->status);
                    }
                    tmp_chunk->mtx.lock();
                    vktrace_LogDebug("Chunk %llu is locked when loading.", g_preload_context.loading_idx + i);
                    tmp_chunk->status = CHUNK_LOADING;
                    vktrace_LogDebug("Loading chunk - %d !", g_preload_context.loading_idx + i);
                }

                // set a new boudary address
                boundary_addr = cur_chunk->base_address + cur_chunk->chunk_size * chunks_needed;
            }

            cur_chunk->status = CHUNK_LOADING;
            if(skipped_notify == SKIP_NOTIFY) {
                g_preload_context.preload_sem_head.notify();
                vktrace_LogDebug("Chunk %llu notify after skipping to the first chunk.", g_preload_context.using_idx);
                skipped_notify = SKIP_NOTIFY_NONE;
            }
            vktrace_LogDebug("Chunk %llu is LOADING when loading.", g_preload_context.loading_idx);

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
            vktrace_LogDebug("Chunk %d is READY when loading!", g_preload_context.loading_idx);
            vktrace_LogDebug("%d packets are loaded", loaded_packet_count);
            cur_chunk->mtx.unlock();
            vktrace_LogDebug("Chunk %llu is unlocked when loading.", g_preload_context.loading_idx);
            // after loading, update other chunks' current address, status and unlock those chunks
            for(uint32_t i = 1; i < chunks_needed; ++i){
                mem_chunk_info* tmp_chunk = &g_preload_context.chunks[g_preload_context.loading_idx + i];
                tmp_chunk->current_address = cur_chunk->current_address;
                tmp_chunk->status = CHUNK_READY;
                g_preload_context.preload_sem_ready.notify();
                vktrace_LogDebug("Chunk %d is also READY, notify !", g_preload_context.loading_idx + i);
                tmp_chunk->mtx.unlock();
                vktrace_LogDebug("Chunk %llu is unlocked when loading.", g_preload_context.loading_idx + i);
            }
            g_preload_context.loading_idx += chunks_needed;
            g_preload_context.preload_sem.notify();
        } else if (cur_chunk->status != CHUNK_READY && cur_chunk->status != CHUNK_SKIPPED) {
            vktrace_LogWarning("Chunk loading thread: Not right, the chunk (%llu) status(%llu) confused !", g_preload_context.loading_idx, cur_chunk->status);
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
    uint64_t system_free_mem_size = get_free_memory_size();
    vktrace_LogAlways("Init preload: the free memory size = %llu, the decompress file size = %llu, original file size = %llu.", system_free_mem_size, filesize, file->mFileLen);

    vktrace_LogAlways("Init preload: chunk size: %llu (MB) ", replaySettings.preloadChunkSize);
    if(replaySettings.preloadChunkSize < 200){
        replaySettings.preloadChunkSize = 200;
        vktrace_LogAlways("Init preload: the chunk size is too small, adjusted to 200 (MB)!");
    }

    static const uint64_t chunk_size  = replaySettings.preloadChunkSize * SIZE_1M;
    uint32_t chunk_count = calc_chunk_count(system_free_mem_size, chunk_size, filesize);
    if (chunk_count < 1) {
        return false;
    }

    char* preload_mem = (char*)vktrace_malloc(chunk_size * chunk_count);
    while (preload_mem == nullptr && chunk_count > 1) {
        // Allocate memory failed, then decrease the chunk count
        chunk_count--;
        preload_mem = (char*)vktrace_malloc(chunk_size * chunk_count);
    }
    if (preload_mem == nullptr) {
        return false;
    }

    g_preload_context.chunk_count = chunk_count;
    g_preload_context.tracefile   = file;
    g_preload_context.loading_idx = chunk_count;
    g_preload_context.using_idx   = chunk_count;

    if (preload_mem) {
        vktrace_LogAlways("Init preload: the chunk size = %llu and the chunk count = %d ...", chunk_size, chunk_count);

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
    if (cur_chunk->status == CHUNK_SKIPPED){
        vktrace_LogDebug("Chunk %llu is waiting to be preloaded !", g_preload_context.using_idx);
        // wait until the loading chunk is skipped to the first
        g_preload_context.preload_sem_skip.wait([&]{
            return skipped_to_first == SKIPPING_NONE || !g_preload_context.next_pkt_size;
        });
        vktrace_LogDebug("Chunk %llu finished waiting because of status(%llu) when preloading.", g_preload_context.using_idx, cur_chunk->status);
        cur_chunk->status = CHUNK_EMPTY;
        vktrace_LogDebug("Chunk %llu is EMPTY when preloading.", g_preload_context.using_idx);
        for(uint32_t chunk_idx = g_preload_context.using_idx + 1; chunk_idx < g_preload_context.chunk_count; ++chunk_idx){
            mem_chunk_info* tmp_chunk = &g_preload_context.chunks[chunk_idx];
            // all the following chunks status should be SKIPPED
            if(tmp_chunk->status != CHUNK_SKIPPED){
                vktrace_LogWarning("Skipping not right, the chunk status(%d) confused !", cur_chunk->status);
            }
            tmp_chunk->mtx.lock();
            vktrace_LogDebug("Chunk %llu is locked when preloading.", chunk_idx);
            tmp_chunk->status = CHUNK_EMPTY;
            vktrace_LogDebug("Chunk %llu is EMPTY now !", chunk_idx);
            tmp_chunk->mtx.unlock();
            vktrace_LogDebug("Chunk %llu is unlocked when preloading.", chunk_idx);
        }
        g_preload_context.preload_sem_get.notify();
        g_preload_context.using_idx = 0;
        cur_chunk = &g_preload_context.chunks[g_preload_context.using_idx];
        // skip to the first while the first chunk is still empty
        if(cur_chunk->status == CHUNK_EMPTY){
            vktrace_LogDebug("Chunk %llu is waiting to be preloaded because of skipping!", g_preload_context.using_idx);
            g_preload_context.preload_sem_head.wait([&]{
                return cur_chunk->status == CHUNK_LOADING || cur_chunk->status == CHUNK_READY || !g_preload_context.next_pkt_size;
            });
            vktrace_LogDebug("Chunk %llu finished waiting because of status(%llu) when preloading.", g_preload_context.using_idx, cur_chunk->status);
        }
    }

    if (cur_chunk->status == CHUNK_READY || cur_chunk->status == CHUNK_LOADING) {
        uint64_t start_time = vktrace_get_time();
        cur_chunk->mtx.lock();
        vktrace_LogDebug("Chunk %llu is locked when preloading !", g_preload_context.using_idx);
        uint64_t end_time = vktrace_get_time();
        if (vktrace_replay::timerStarted())
            g_preload_context.preload_waiting_time += end_time - start_time;
        assert(cur_chunk->status == CHUNK_READY);
        if(boundary_addr == cur_chunk->current_address){
            vktrace_LogDebug("This chunk has nothing! Too many chunks when preloading !");
        }
        boundary_addr = cur_chunk->current_address;
        if(tmp_address != nullptr){
            cur_chunk->current_address = tmp_address;
        } else {
            cur_chunk->current_address = cur_chunk->base_address;
        }
        cur_chunk->status = CHUNK_USING;
        vktrace_LogDebug("Chunk %llu is USING now when preloading !", g_preload_context.using_idx);
    } else if (cur_chunk->status == CHUNK_EMPTY) {
        vktrace_LogDebug("Chunk %llu is EMPY when preloading !!", g_preload_context.using_idx);
        assert(!g_preload_context.next_pkt_size);
        return NULL;
    }

    vktrace_trace_packet_header* pHeader = (vktrace_trace_packet_header*)cur_chunk->current_address;
    if (pHeader->size == 0) {
        vktrace_LogError("This is a wrong packetage !");
    }
    cur_chunk->current_address += pHeader->size;
    if(replaySettings.printCurrentPacketIndex > 1) {
        vktrace_LogDebug("Chunk (%llu), pHeader: %p,id %llu, size: %llu, boundary_addr: %p",
            g_preload_context.using_idx, pHeader, pHeader->global_packet_index, pHeader->size, boundary_addr);
    }

    bool update_tmp_address = false;
    if(cur_chunk->current_address >= cur_chunk->base_address + cur_chunk->chunk_size)
        update_tmp_address = true;

    if (cur_chunk->current_address >= boundary_addr) {
        cur_chunk->status = CHUNK_EMPTY;
        g_preload_context.preload_sem_get.notify();
        vktrace_LogDebug("Chunk %llu is EMPTY when preloading, notify !", g_preload_context.using_idx);
        uint32_t chunks_occupied = 1;
        chunks_occupied = uint32_t((cur_chunk->current_address - cur_chunk->base_address - 1) * 1.0 / cur_chunk->chunk_size) + 1;

        cur_chunk->current_address = cur_chunk->base_address;
        cur_chunk->mtx.unlock();
        vktrace_LogDebug("Chunk %llu is unlocked when preloading !", g_preload_context.using_idx);

        if(update_tmp_address == true) {
            vktrace_LogDebug("Chunk %llu ~ %llu are used by the packet(id: %llu, size: %llu) when preloading ! %llu chunks are occupied,",
                g_preload_context.using_idx, g_preload_context.using_idx + chunks_occupied -1, pHeader->global_packet_index, pHeader->size, chunks_occupied);
            for(uint32_t chunk_idx = g_preload_context.using_idx + 1; chunk_idx < g_preload_context.using_idx + chunks_occupied; ++chunk_idx){
                mem_chunk_info* tmp_chunk = &g_preload_context.chunks[chunk_idx];
                // wait if the chunk status is not READY
                if(tmp_chunk->status != CHUNK_READY ){
                    vktrace_LogDebug("Chunk %llu need to wait until its status is READY when preloading !", chunk_idx);
                    g_preload_context.preload_sem_ready.wait([&]{
                        return tmp_chunk->status == CHUNK_READY;
                    });
                }
                tmp_chunk->mtx.lock();
                vktrace_LogDebug("Chunk %llu is locked when preloading !", chunk_idx);
                tmp_chunk->status = CHUNK_EMPTY;
                g_preload_context.preload_sem_get.notify();
                vktrace_LogDebug("Chunk %llu is also EMPTY when preloading, notify !", chunk_idx);
                tmp_chunk->current_address = tmp_chunk->base_address;
                tmp_chunk->mtx.unlock();
                vktrace_LogDebug("Chunk %llu is unlocked when preloading !", chunk_idx);
            }
            update_tmp_address = false;
        }
        vktrace_LogDebug("Chunk %llu (+ %llu) when preloading !", g_preload_context.using_idx, chunks_occupied);
        if(update_tmp_address == false) {
            g_preload_context.using_idx += chunks_occupied;
        }

        if (g_preload_context.loading_idx == g_preload_context.using_idx % g_preload_context.chunk_count) {
            g_preload_context.preload_sem.wait([&]{
                return cur_chunk->status != CHUNK_EMPTY || g_preload_context.exceed_preloading_range || !g_preload_context.next_pkt_size;
            });
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

