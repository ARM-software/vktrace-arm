/**************************************************************************
 *
 * Copyright 2015-2016 Valve Corporation
 * Copyright (C) 2015-2016 LunarG, Inc.
 * Copyright (C) 2019 ARM Limited.
 * All Rights Reserved.
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
 * Author: Jon Ashburn <jon@lunarg.com>
 **************************************************************************/
#include "vkreplay_seq.h"
#include "vkreplay_main.h"

extern "C" {
#include "vktrace_trace_packet_utils.h"
}

namespace vktrace_replay {


vktrace_trace_packet_header *Sequencer::get_next_packet() {
    if (!m_pFile) return (NULL);
    if (!m_chunkEnabled) {  // do not use preload
        vktrace_delete_trace_packet_no_lock(&m_lastPacket);
        m_lastPacket = vktrace_read_trace_packet(m_pFile);
        if (!m_lastPacket)
            return 0;
        if (m_lastPacket->tracer_id == VKTRACE_TID_VULKAN_COMPRESSED) {
            int ret = decompress_packet(m_decompressor, m_lastPacket);
            if (ret != 0)
                return 0;
        }
    } else {
        if (timerStarted()) // preload, and already in the preloading range
        {
            m_lastPacket = preload_get_next_packet();
        }
        else {              // preload, but not in the preloading range
            vktrace_delete_trace_packet_no_lock(&m_lastPacket);
            m_lastPacket = vktrace_read_trace_packet(m_pFile);
        }
    }
    return m_lastPacket;
}

void Sequencer::set_lastPacket(vktrace_trace_packet_header *newPacket) {
    m_lastPacket = newPacket;
}

void Sequencer::get_bookmark(seqBookmark &bookmark) { bookmark.file_offset = m_bookmark.file_offset; }

void Sequencer::set_bookmark(const seqBookmark &bookmark) { vktrace_FileLike_SetCurrentPosition(m_pFile, m_bookmark.file_offset); }

void Sequencer::record_bookmark() { m_bookmark.file_offset = vktrace_FileLike_GetCurrentPosition(m_pFile); }

} /* namespace vktrace_replay */
