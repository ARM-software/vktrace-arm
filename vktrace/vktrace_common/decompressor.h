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

class decompressor {
public:
    /* decompresses a compress block 'input' of size 'inputLength' bytes
     * into already allocated 'output' buffer of size 'outputLength'.
     * returns the number of bytes decompressed into 'output' buffer
     * or 0 or negative if decompression fails
     */
    virtual int decompress(const char* input, size_t inputLength, char* output, size_t outputLength) = 0;
    virtual ~decompressor() = 0;
};

decompressor* create_decompressor(VKTRACE_COMPRESS_TYPE type);

int decompress_packet(decompressor *g_decompressor, vktrace_trace_packet_header* &pPacketHeader);
