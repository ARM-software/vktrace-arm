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

#include "compressor.h"
#include "lz4compressor.h"
#include "snpcompressor.h"

compressor::~compressor() {

}

compressor* create_compressor(VKTRACE_COMPRESS_TYPE type) {
    if (type == VKTRACE_COMPRESS_TYPE_LZ4) {
        return new lz4compressor;
    }
    else if (type == VKTRACE_COMPRESS_TYPE_SNAPPY) {
        return new snpcompressor;
    }

    return nullptr;
}

int compress_packet(compressor *g_compressor, vktrace_trace_packet_header* &pPacketHeader) {
    if (pPacketHeader->tracer_id == VKTRACE_TID_VULKAN_COMPRESSED) {
        vktrace_LogWarning("Packet %d is already a compressed one, so it won't be compressed.", pPacketHeader->global_packet_index);
        return 0;
    }
    int orig_data_size = pPacketHeader->size - sizeof(vktrace_trace_packet_header);
    int buffer_size = g_compressor->getMaxCompressedLength(orig_data_size);
    vktrace_trace_packet_header* pCompressPacketHeader = (vktrace_trace_packet_header*)vktrace_malloc(sizeof(vktrace_trace_packet_header) + sizeof(vktrace_trace_packet_header_compression_ext) + buffer_size);
    pPacketHeader->pBody = (uintptr_t)(pPacketHeader + 1);
    char *compress_buffer = (char *)pCompressPacketHeader + sizeof(vktrace_trace_packet_header) + sizeof(vktrace_trace_packet_header_compression_ext);
    int compressed_data_size = g_compressor->compress((char*)pPacketHeader->pBody, orig_data_size, compress_buffer, buffer_size);
    if (compressed_data_size <= 0) {
        vktrace_LogError("Compression error: %d\n", compressed_data_size);
        return -1;
    }
    else if (compressed_data_size >= orig_data_size) {
        vktrace_LogWarning("The data after compression becomes even larger (%d bytes to %d bytes), so it won't be compressed.\n", orig_data_size, compressed_data_size);
        return 0;
    }
    else {
        uint64_t compressed_packet_size = sizeof(vktrace_trace_packet_header) + sizeof(vktrace_trace_packet_header_compression_ext) + compressed_data_size;
        memcpy(pCompressPacketHeader, pPacketHeader, sizeof(vktrace_trace_packet_header));
        pCompressPacketHeader->pBody = (uintptr_t)(pCompressPacketHeader + 1);
        pCompressPacketHeader->tracer_id = VKTRACE_TID_VULKAN_COMPRESSED;

        pCompressPacketHeader->size = compressed_packet_size;
        reinterpret_cast<vktrace_trace_packet_header_compression_ext*>(pCompressPacketHeader->pBody)->decompressed_size = orig_data_size;
        reinterpret_cast<vktrace_trace_packet_header_compression_ext*>(pCompressPacketHeader->pBody)->pBody = (uintptr_t)compress_buffer;

        g_compressor->compress_packet_counter++;
        vktrace_free(pPacketHeader);
        pPacketHeader = pCompressPacketHeader;
        return 0;
    }
}
