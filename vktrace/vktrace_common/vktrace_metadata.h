/*
 * Copyright (c) 2023 ARM Limited
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

#pragma once
#include <vector>
#include <unordered_map>
#include "vktrace_trace_packet_identifiers.h"
#include "vktrace_vk_vk_packets.h"
#include "vktrace_vk_packet_id.h"

extern uint32_t lastPacketThreadId;
extern uint64_t lastPacketIndex;
extern uint64_t lastPacketEndTime;
extern vktrace_trace_file_header trace_file_header;

void vktrace_appendPortabilityPacket(FILE* pTraceFile, std::vector<uint64_t>& portabilityTable);
uint32_t vktrace_appendMetaData(FILE* pTraceFile, const std::vector<uint64_t>& injectedData, uint64_t &meta_data_offset);
uint32_t vktrace_appendDeviceFeatures(FILE* pTraceFile, const std::unordered_map<VkDevice, uint32_t>& deviceToFeatures, uint64_t meta_data_offset);
void vktrace_resetFilesize(FILE* pTraceFile, uint64_t decompressFilesize);
void set_trace_file_header(vktrace_trace_file_header* pHeader);
bool vktrace_write_trace_packet_to_file(const vktrace_trace_packet_header* pHeader, FileLike* pFile);
void vktrace_write_trace_packet(const vktrace_trace_packet_header* pHeader, FileLike* pFile);