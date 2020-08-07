/*
 * (C) COPYRIGHT 2020 ARM Limited
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
 *
 */

#include "decompressor.h"
#include "lz4decompressor.h"
#include "snpdecompressor.h"

decompressor::~decompressor() {

}

decompressor* create_decompressor(VKTRACE_COMPRESS_TYPE type) {
    if (type == VKTRACE_COMPRESS_TYPE_LZ4) {
        return new lz4decompressor;
    }
    else if (type == VKTRACE_COMPRESS_TYPE_SNAPPY) {
        return new snpdecompressor;
    }
    return nullptr;
}

int decompress_packet(decompressor *g_decompressor, vktrace_trace_packet_header* &pPacketHeader) {
    if (pPacketHeader->tracer_id != VKTRACE_TID_VULKAN_COMPRESSED) {
        vktrace_LogWarning("packet %d is not a compressed one, so it'won't be decompressed.", pPacketHeader->global_packet_index);
        return 0;
    }
    pPacketHeader->pBody = (uintptr_t)(pPacketHeader + 1);
    uint64_t decompressed_data_size = reinterpret_cast<vktrace_trace_packet_header_compression_ext*>(pPacketHeader->pBody)->decompressed_size;
    reinterpret_cast<vktrace_trace_packet_header_compression_ext*>(pPacketHeader->pBody)->pBody = pPacketHeader->pBody + sizeof(vktrace_trace_packet_header_compression_ext);
    uint64_t compressed_data_size = pPacketHeader->size - sizeof(vktrace_trace_packet_header) - sizeof(vktrace_trace_packet_header_compression_ext);

    vktrace_trace_packet_header* pDecompressPacketHeader = (vktrace_trace_packet_header*)vktrace_malloc(sizeof(vktrace_trace_packet_header) + decompressed_data_size);
    char *decompress_data = (char *)(pDecompressPacketHeader + 1);
    size_t decompressed_data_size_actual = (size_t)g_decompressor->decompress((char *)(reinterpret_cast<vktrace_trace_packet_header_compression_ext*>(pPacketHeader->pBody)->pBody), compressed_data_size, decompress_data, decompressed_data_size);
    if (decompressed_data_size_actual != decompressed_data_size) {
        vktrace_free(pDecompressPacketHeader);
        vktrace_LogError("Decompress error! The size of the uncompression result (%lu) doesn't match that recorded in the packet header (%lu).\n", decompressed_data_size_actual, decompressed_data_size);
        return -1;
    }

    memcpy(pDecompressPacketHeader, pPacketHeader, sizeof(vktrace_trace_packet_header));
    pDecompressPacketHeader->size = sizeof(vktrace_trace_packet_header) + decompressed_data_size;
    pDecompressPacketHeader->tracer_id = VKTRACE_TID_VULKAN;
    pDecompressPacketHeader->pBody = (uintptr_t)(pDecompressPacketHeader + 1);

    vktrace_free(pPacketHeader);
    pPacketHeader = pDecompressPacketHeader;
    return 0;
}
