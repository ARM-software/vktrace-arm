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

// vktrace version word define
// +------+------+----+
// | Major| Minor| PT |
// +------+------+----+
//  15  10 9    4 3  0
#define ENCODE_VKTRACE_VER(major, minor, point) (major << 10 | minor << 4 | point)

#define DECODE_VKTRACE_VER_MAJOR(ver) ((ver >> 10) & 0x3f)
#define DECODE_VKTRACE_VER_MINOR(ver) ((ver >> 4) & 0x3f)
#define DECODE_VKTRACE_VER_POINT(ver) (ver & 0xf)

#define VKTRACE_VER ENCODE_VKTRACE_VER(VKTRACE_VER_MAJOR, VKTRACE_VER_MINOR, VKTRACE_VER_POINT)

#if defined(__cplusplus)
extern "C" {
#endif
uint16_t vktrace_version();
const char* version_word_to_str(uint16_t ver);
#if defined(__cplusplus)
}
#endif
