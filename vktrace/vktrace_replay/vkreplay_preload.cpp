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

#include "vkreplay_preload.h"

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
    char*       preload_mem   = nullptr;
    FileLike*   tracefile     = nullptr;
    bool        exiting_thd   = false;
    std::thread thd_obj;
    simple_sem  replay_start_sem;
} g_preload_context;

static uint64_t get_system_memory_size() {
    struct sysinfo si;
    if (sysinfo(&si)) {
        return 0;
    }
    return si.totalram * si.mem_unit;
}

static uint32_t calc_chunk_count(uint64_t sys_mem_size, uint64_t chunk_size, uint64_t file_size) {
    uint64_t preload_mem_size = sys_mem_size / 2;

    preload_mem_size = preload_mem_size < file_size ? preload_mem_size : file_size;
    uint32_t chunk_count = (preload_mem_size + chunk_size - 1) / chunk_size;

    return chunk_count;
}

static uint64_t get_packet_size(FileLike* file) {
    uint64_t packet_size = 0;
    if (vktrace_FileLike_ReadRaw(file, &packet_size, sizeof(uint64_t)) == FALSE) {
        return 0;
    }
    return packet_size;
}

static uint64_t load_packet(FileLike* file, void* load_addr, uint64_t packet_size) {
    vktrace_trace_packet_header* pHeader = (vktrace_trace_packet_header*)load_addr;

    pHeader->size = packet_size;
    if (vktrace_FileLike_ReadRaw(file,
            (char*)pHeader + sizeof(uint64_t),
            (size_t)packet_size - sizeof(uint64_t)) == FALSE) {
        vktrace_LogError("Failed to read trace packet with size of %llu.", packet_size);
        return 0;
    }

    pHeader->pBody = (uintptr_t)pHeader + sizeof(vktrace_trace_packet_header);

    return packet_size;
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
            // loading
            vktrace_LogDebug("Loading chunk %d !", g_preload_context.loading_idx);
            cur_chunk->status = CHUNK_LOADING;

            uint32_t loaded_packet_count = 0;
            char* boundary_addr = cur_chunk->base_address + cur_chunk->chunk_size;

            while (!g_preload_context.exiting_thd
                   && g_preload_context.next_pkt_size
                   && (cur_chunk->current_address + g_preload_context.next_pkt_size) < boundary_addr) {
                if (!load_packet(g_preload_context.tracefile, cur_chunk->current_address, g_preload_context.next_pkt_size)) {
                    break;
                }
                cur_chunk->current_address += g_preload_context.next_pkt_size;
                g_preload_context.next_pkt_size = get_packet_size(g_preload_context.tracefile);
                loaded_packet_count++;
            }
            cur_chunk->status = CHUNK_READY;
            g_preload_context.loading_idx++;
            vktrace_LogDebug("%d packets are loaded", loaded_packet_count);
            cur_chunk->mtx.unlock();
        } else if (cur_chunk->status != CHUNK_READY) {
            vktrace_LogWarning("Chunk loading thread: Not right, the chunk status(%d) confused !", cur_chunk->status);
        }

        if (!g_preload_context.next_pkt_size) {
            // no more packets, exiting...
            if (first_full) g_preload_context.replay_start_sem.notify();
            break;
        }
    }
}

bool init_preload(FileLike* file) {

    bool ret = true;
    uint64_t system_mem_size = get_system_memory_size();
    vktrace_LogAlways("Init preload: the total size of system memory = 0x%llX and the size of trace file = 0x%llX", system_mem_size, file->mFileLen);

    static const uint64_t chunk_size  = 384 * SIZE_1M;
    uint32_t chunk_count = calc_chunk_count(system_mem_size, chunk_size, file->mFileLen);
    chunk_count = chunk_count < MAX_CHUNK_COUNT ? chunk_count : MAX_CHUNK_COUNT;

    vktrace_LogAlways("Init preload: the chunk size = 0x%llX and the chunk count = %d ...", chunk_size, chunk_count);

    g_preload_context.chunk_count = chunk_count;
    g_preload_context.tracefile   = file;
    g_preload_context.loading_idx = chunk_count;
    g_preload_context.using_idx   = chunk_count;

    char* preload_mem = (char*)vktrace_malloc(chunk_size * chunk_count);
    if (preload_mem) {
        g_preload_context.preload_mem = preload_mem;

        for (uint32_t i = 0; i < chunk_count; i++) {
            g_preload_context.chunks[i].chunk_size = chunk_size;
            g_preload_context.chunks[i].base_address = preload_mem + chunk_size * i;
            g_preload_context.chunks[i].current_address = g_preload_context.chunks[i].base_address;
        }

        g_preload_context.next_pkt_size = get_packet_size(g_preload_context.tracefile);
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
        cur_chunk->mtx.lock();
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

