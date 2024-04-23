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

#ifndef _VKTRACE_PRELOAD_H_
#define _VKTRACE_PRELOAD_H_
#include <cinttypes>
#include "vkreplay_factory.h"
#include "decompressor.h"

bool init_preload(FileLike* file, vktrace_replay::vktrace_trace_packet_replay_library *replayer_array[], decompressor* decompressor, uint64_t filesize);
vktrace_trace_packet_header* preload_get_next_packet();
void exit_preload();
uint64_t get_preload_waiting_time_when_replaying();
bool preloaded_whole();

#endif /* _VKTRACE_PRELOAD_H_ */
