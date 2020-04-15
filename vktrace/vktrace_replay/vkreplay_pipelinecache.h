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
 * Author: Yuchen Tong <yuchen.tong@arm.com>
 **************************************************************************/

#pragma once

extern "C" {
#include "vktrace_filelike.h"
#include "vktrace_trace_packet_identifiers.h"
#include "vktrace_trace_packet_utils.h"
}

#include <map>
#include <list>
#include <memory>
#include <string>

/* Class to handle fetching and sequencing packets from a tracefile.
 * Contains no knowledge of type of tracer needed to process packet.
 * Requires low level file/stream reading/seeking support. */
namespace vktrace_replay {

    const std::string& GetDefaultPipelineCacheRootPath();

    class PipelineCacheAccessor {
        public:
            typedef std::shared_ptr<PipelineCacheAccessor> Ptr;
            PipelineCacheAccessor();
            ~PipelineCacheAccessor();

            std::pair<void*, size_t> GetPipelineCache(const VkPipelineCache &cache_handle) const;
            bool                     WritePipelineCache(const VkPipelineCache &cache_handle, void* cache_data, const uint32_t &cache_size, const uint64_t &gpu_info, const uint8_t *pipelinecache_uuid);
            bool                     LoadPipelineCache(const std::string &full_path);

            void                     SetPipelineCacheRootPath(const std::string &path);

            void                     CollectPacketInfo(const VkDevice &device, const VkPipelineCache &cache_key);
            void                     RemoveCollectedPacketInfo(const VkDevice &device, const VkPipelineCache &cache_key);

            std::string              FindFile(const VkPipelineCache &cache_handle) const;
            std::string              FindFile(const VkPipelineCache &cache_handle, const uint64_t &gpu_info, const uint8_t *pipelinecache_uuid) const;

            std::list<std::pair<VkDevice, VkPipelineCache>> GetCollectedPacketInfo() const;
        private:
            std::pair<void*, size_t> ReadPipelineCache(const std::string &full_path);

        private:
            std::map<uint64_t, std::pair<void*, size_t>>           m_cachemap;
            std::list<std::pair<VkDevice, VkPipelineCache>>        m_collected_packetinfo_list;
            std::string                                            m_cachepath;
    };




} 