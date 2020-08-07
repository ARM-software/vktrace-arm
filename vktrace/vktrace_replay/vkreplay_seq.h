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
#pragma once

extern "C" {
#include "vktrace_filelike.h"
#include "vktrace_trace_packet_identifiers.h"
#include "vktrace_trace_packet_utils.h"
}
#include <mutex>
#include <memory>

#include "vkreplay_preload.h"
#include "vkreplay_factory.h"
#include "decompressor.h"

/* Class to handle fetching and sequencing packets from a tracefile.
 * Contains no knowledge of type of tracer needed to process packet.
 * Requires low level file/stream reading/seeking support. */
namespace vktrace_replay {

struct seqBookmark {
    uint64_t file_offset;
};

// replay Sequencer interface
class AbstractSequencer {
   public:
    virtual ~AbstractSequencer() {}
    virtual vktrace_trace_packet_header *get_next_packet() = 0;
    virtual void get_bookmark(seqBookmark &bookmark) = 0;
    virtual void set_bookmark(const seqBookmark &bookmark) = 0;
};

class Sequencer : public AbstractSequencer {
   public:
    Sequencer(FileLike *pFile, decompressor* decom, uint64_t filesize) : m_lastPacket(NULL), m_pFile(pFile), m_chunkEnabled(false), m_decompressor(decom), m_decompressFilesize(filesize) {}
    ~Sequencer() { this->clean_up(); }

    void clean_up() {
        if (m_chunkEnabled) {
            exit_preload();
        } else if (m_lastPacket) {
            free(m_lastPacket);
            m_lastPacket = NULL;
        }
    }

    vktrace_trace_packet_header *get_next_packet();
    void get_bookmark(seqBookmark &bookmark);
    void set_bookmark(const seqBookmark &bookmark);
    void record_bookmark();
    void set_lastPacket(vktrace_trace_packet_header *newPacket);
    bool start_preload(vktrace_replay::vktrace_trace_packet_replay_library *replayer_array[], decompressor* decompressor) {
        m_chunkEnabled = init_preload(m_pFile, replayer_array, decompressor, m_decompressFilesize);
        return m_chunkEnabled;
    };

   private:
    vktrace_trace_packet_header *m_lastPacket;
    seqBookmark m_bookmark;
    FileLike *m_pFile;
    bool m_chunkEnabled;
    decompressor* m_decompressor;
    uint64_t m_decompressFilesize = 0;
};

} /* namespace vktrace_replay */
