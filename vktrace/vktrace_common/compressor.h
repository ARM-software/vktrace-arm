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

#pragma once

#include "vktrace_trace_packet_identifiers.h"

class compressor {
public:
    /* returns the maximum size the compressor may output in a "worst case" scenario.
     */
    virtual int getMaxCompressedLength(size_t size) = 0;

    /* Compresses 'inputLength' bytes from buffer 'input'
     * into already allocated 'output' buffer of size 'outputLength'.
     * snappy compressor doesn't need outputLength parameter, but lz4 does.
     * returns the number of bytes written into buffer 'output'
     * or 0 if compression fails
     */
    virtual int compress(const char* input, size_t inputLength, char* output, size_t outputLength) = 0;

    virtual ~compressor() = 0;
    int compress_packet_counter = 0;
};

compressor* create_compressor(VKTRACE_COMPRESS_TYPE type);

int compress_packet(compressor *g_compressor, vktrace_trace_packet_header* &pPacketHeader);
