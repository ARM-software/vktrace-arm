#include "vktrace_metadata.h"
#include "vktrace_common.h"
#include "vktrace_trace_packet_utils.h"
#include "compressor.h"
#include <cstddef>
#include "json/json.h"
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>

uint32_t lastPacketThreadId = 0;
uint64_t lastPacketIndex = 0;
uint64_t lastPacketEndTime = 0;
vktrace_trace_file_header trace_file_header;

void vktrace_appendPortabilityPacket(FILE* pTraceFile, std::vector<uint64_t>& portabilityTable) {
    vktrace_trace_packet_header hdr;
    uint64_t one_64 = 1;

    if (pTraceFile == NULL) {
        vktrace_LogError("tracefile was not created");
        return;
    }

    vktrace_LogVerbose("Post processing trace file");

    // Add a word containing the size of the table to the table.
    // This will be the last word in the file.
    portabilityTable.push_back(portabilityTable.size());

    // Append the table packet to the trace file.
    hdr.size = sizeof(hdr) + portabilityTable.size() * sizeof(uint64_t);
    hdr.global_packet_index = lastPacketIndex + 1;
    hdr.tracer_id = VKTRACE_TID_VULKAN;
    hdr.packet_id = VKTRACE_TPI_PORTABILITY_TABLE;
    hdr.thread_id = lastPacketThreadId;
    hdr.vktrace_begin_time = hdr.entrypoint_begin_time = hdr.entrypoint_end_time = hdr.vktrace_end_time = lastPacketEndTime;
    hdr.next_buffers_offset = 0;
    hdr.pBody = (uintptr_t)NULL;
    if (0 == Fseek(pTraceFile, 0, SEEK_END) && 1 == fwrite(&hdr, sizeof(hdr), 1, pTraceFile) &&
        portabilityTable.size() == fwrite(&portabilityTable[0], sizeof(uint64_t), portabilityTable.size(), pTraceFile)) {
        // Set the flag in the file header that indicates the portability table has been written
        if (0 == Fseek(pTraceFile, offsetof(vktrace_trace_file_header, portability_table_valid), SEEK_SET))
            fwrite(&one_64, sizeof(uint64_t), 1, pTraceFile);
    }
    portabilityTable.clear();
    vktrace_LogVerbose("Post processing of trace file completed");
}

void vktrace_resetFilesize(FILE* pTraceFile, uint64_t decompressFilesize) {
    if (0 == Fseek(pTraceFile, offsetof(vktrace_trace_file_header, decompress_file_size), SEEK_SET)) {
        fwrite(&decompressFilesize, sizeof(uint64_t), 1, pTraceFile);
    }
}

uint32_t vktrace_appendMetaData(FILE* pTraceFile, const std::vector<uint64_t>& injectedData, uint64_t &meta_data_offset) {
    Json::Value root;
    Json::Value injectedCallList;
    for (uint32_t i = 0; i < injectedData.size(); i++) {
        injectedCallList.append(injectedData[i]);
    }
    root["injectedCalls"] = injectedCallList;
    auto str = root.toStyledString();
    vktrace_LogVerbose("Meta data string: %s", str.c_str());

    vktrace_trace_packet_header hdr;
    uint64_t meta_data_file_offset = 0;
    uint32_t meta_data_size = str.size() + 1;
    meta_data_size = ROUNDUP_TO_8(meta_data_size);
    char *meta_data_str_json = new char[meta_data_size];
    memset(meta_data_str_json, 0, meta_data_size);
    strcpy(meta_data_str_json, str.c_str());

    hdr.size = sizeof(hdr) + meta_data_size;
    hdr.global_packet_index = lastPacketIndex++;
    hdr.tracer_id = VKTRACE_TID_VULKAN;
    hdr.packet_id = VKTRACE_TPI_META_DATA;
    hdr.thread_id = lastPacketThreadId;
    hdr.vktrace_begin_time = hdr.entrypoint_begin_time = hdr.entrypoint_end_time = hdr.vktrace_end_time = lastPacketEndTime;
    hdr.next_buffers_offset = 0;
    hdr.pBody = (uintptr_t)NULL;

    if (0 == Fseek(pTraceFile, 0, SEEK_END)) {
        meta_data_file_offset = Ftell(pTraceFile);
        if (1 == fwrite(&hdr, sizeof(hdr), 1, pTraceFile) &&
            meta_data_size == fwrite(meta_data_str_json, sizeof(char), meta_data_size, pTraceFile)) {
            if (0 == Fseek(pTraceFile, offsetof(vktrace_trace_file_header, meta_data_offset), SEEK_SET)) {
                fwrite(&meta_data_file_offset, sizeof(uint64_t), 1, pTraceFile);
                vktrace_LogVerbose("Meta data at the file offset %llu", meta_data_file_offset);
            }
        }
        meta_data_offset = meta_data_file_offset;
    } else {
        vktrace_LogError("File operation failed during append the meta data");
    }
    delete[] meta_data_str_json;
    return meta_data_size;
}

uint32_t vktrace_appendDeviceFeatures(FILE* pTraceFile, const std::unordered_map<VkDevice, uint32_t>& deviceToFeatures, uint64_t meta_data_offset) {
    /**************************************************************
     * JSON format:
     * "deviceFeatures" : {
     *      "device" : [
     *          {  // device 0
     *              "accelerationStructureCaptureReplay" : 1,
     *              "bufferDeviceAddressCaptureReplay" : 0,
     *              "deviceHandle" : "0xaaaaaaaa"
     *          }
     *          {  // device 1
     *              "accelerationStructureCaptureReplay" : 1,
     *              "bufferDeviceAddressCaptureReplay" : 1,
     *              "deviceHandle" : "0xbbbbbbbb"
     *          }
     *      ]
     * }
     * ***********************************************************/
    Json::Reader reader;
    Json::Value metaRoot;
    Json::Value featuresRoot;
    for (auto e : deviceToFeatures) {
        char deviceHandle[32] = {0};
        sprintf(deviceHandle, "%p", e.first);
        Json::Value features;
        features["deviceHandle"] = Json::Value(deviceHandle);
        features["accelerationStructureCaptureReplay"] = Json::Value((e.second & PACKET_TAG_ASCAPTUREREPLAY) ? 1 : 0);
        features["bufferDeviceAddressCaptureReplay"] = Json::Value((e.second & PACKET_TAG_BUFFERCAPTUREREPLAY) ? 1 : 0);
        features["rayTracingPipelineShaderGroupHandleCaptureReplay"] = Json::Value((e.second & PACKET_TAG_RTPSGHCAPTUREREPLAY) ? 1 : 0);
        featuresRoot["device"].append(features);
    }
    FileLike fileLike = {FileLike::File, pTraceFile, 0, nullptr };
    vktrace_trace_packet_header hdr = {};
    uint32_t device_features_string_size = 0;
    if (vktrace_FileLike_SetCurrentPosition(&fileLike, meta_data_offset)
        && vktrace_FileLike_ReadRaw(&fileLike, &hdr, sizeof(hdr))
        && hdr.packet_id == VKTRACE_TPI_META_DATA) {
        uint64_t meta_data_json_str_size = hdr.size - sizeof(hdr);
        char* meta_data_json_str = new char[meta_data_json_str_size];
        memset(meta_data_json_str, 0, meta_data_json_str_size);
        if (!meta_data_json_str || !vktrace_FileLike_ReadRaw(&fileLike, meta_data_json_str, meta_data_json_str_size)) {
            vktrace_LogError("Reading meta data of the original file failed");
            return 0;
        }
        reader.parse(meta_data_json_str, metaRoot);
        metaRoot["deviceFeatures"] = featuresRoot;
        delete[] meta_data_json_str;
        auto metaStr = metaRoot.toStyledString();
        uint64_t metaDataStrSize = metaStr.size();
        metaDataStrSize = ROUNDUP_TO_8(metaDataStrSize);
        char* metaDataStr = new char[metaDataStrSize];
        memset(metaDataStr, 0, metaDataStrSize);
        memcpy(metaDataStr, metaStr.c_str(), metaStr.size());
        hdr.size = sizeof(hdr) + metaDataStrSize;
        device_features_string_size = metaDataStrSize - meta_data_json_str_size;
        vktrace_FileLike_SetCurrentPosition(&fileLike, meta_data_offset);
        if (1 == fwrite(&hdr, sizeof(hdr), 1, pTraceFile)) {
            fwrite(metaDataStr, sizeof(char), metaDataStrSize, pTraceFile);
        }
        delete[] metaDataStr;
        auto featureStr = featuresRoot.toStyledString();
        vktrace_LogVerbose("Device features: %s", featureStr.c_str());
    } else {
        vktrace_LogAlways("Dump device features failed");
        return 0;
    }

    return device_features_string_size;
}

void set_trace_file_header(vktrace_trace_file_header* pHeader) {
    trace_file_header = *pHeader;
}

bool vktrace_write_trace_packet_to_file(const vktrace_trace_packet_header* pHeader, FileLike* pFile) {
    static std::vector<uint64_t> portabilityTable;
    static std::vector<uint64_t> injectedCalls;
    static std::unordered_map<VkDevice, uint32_t> deviceToFeatures;
    static uint64_t decompress_file_size = 0;
    static uint64_t fileOffset = 0;
    static bool useAsApi = false;
    static compressor* g_compressor = NULL;
    static bool firstRun = true;

    if (pFile->mMessageStream != NULL || pFile->mFile == NULL) {
        return true;
    }

    if (firstRun) {
        if (0 != Fseek(pFile->mFile, 0, SEEK_END)) {
            vktrace_LogError("File operation failed during vktrace_write_trace_packet_to_file");
        }

        fileOffset = Ftell(pFile->mFile);
        decompress_file_size = fileOffset;
        g_compressor = create_compressor(VKTRACE_COMPRESS_TYPE_LZ4);
        firstRun = false;
    }

    if (pHeader->packet_id <= VKTRACE_TPI_MARKER_CHECKPOINT) {
        return false;
    }

    if (pHeader->packet_id == VKTRACE_TPI_MARKER_TERMINATE_PROCESS || pHeader->packet_id == VKTRACE_TPI_VK_vkDestroyInstance) {
        if (pHeader->packet_id == VKTRACE_TPI_VK_vkDestroyInstance) {
            vktrace_FileLike_WriteRaw(pFile, pHeader, (size_t)pHeader->size);
        }

        uint64_t meta_data_offset = 0;
        decompress_file_size += (sizeof(vktrace_trace_packet_header) + (portabilityTable.size() + 1)* sizeof(uint64_t));
        if (trace_file_header.trace_file_version > VKTRACE_TRACE_FILE_VERSION_9) {
            uint32_t meta_data_str_size = vktrace_appendMetaData(pFile->mFile, injectedCalls, meta_data_offset);
            decompress_file_size += sizeof(vktrace_trace_packet_header) + meta_data_str_size;
        }
        if (trace_file_header.trace_file_version > VKTRACE_TRACE_FILE_VERSION_10 && meta_data_offset > 0) {
            uint32_t device_features_str_size = vktrace_appendDeviceFeatures(pFile->mFile, deviceToFeatures, meta_data_offset);
            decompress_file_size += device_features_str_size;
        }

        vktrace_appendPortabilityPacket(pFile->mFile, portabilityTable);
        vktrace_resetFilesize(pFile->mFile, decompress_file_size);
        vktrace_LogAlways("Capturing of trace file completed.");

        if (useAsApi) {
            trace_file_header.bit_flags = trace_file_header.bit_flags | VKTRACE_USE_ACCELERATION_STRUCTURE_API_BIT;
            fseek(pFile->mFile, offsetof(vktrace_trace_file_header, bit_flags), SEEK_SET);
            fwrite(&trace_file_header.bit_flags, sizeof(uint16_t), 1, pFile->mFile);
            vktrace_LogAlways("There are AS related functions in the trace file.");
        }
        if (g_compressor && g_compressor->compress_packet_counter > 0) {
            fseek(pFile->mFile, offsetof(vktrace_trace_file_header, compress_type), SEEK_SET);
            VKTRACE_COMPRESS_TYPE type = VKTRACE_COMPRESS_TYPE_LZ4;
            fwrite(&type, sizeof(uint16_t), 1, pFile->mFile);
        }

        delete g_compressor;
        g_compressor = NULL;
        fclose(pFile->mFile);
        pFile->mFile = NULL;
        portabilityTable.clear();
        injectedCalls.clear();
        deviceToFeatures.clear();
        useAsApi = false;
        lastPacketIndex = 0;
        lastPacketThreadId = 0;
        lastPacketEndTime = 0;
        decompress_file_size = 0;
        fileOffset = 0;
        return false;
    }

    if ((trace_file_header.trace_file_version > VKTRACE_TRACE_FILE_VERSION_9)
        && (vktrace_get_trace_packet_tag(pHeader) & PACKET_TAG__INJECTED)) {
            injectedCalls.push_back(pHeader->global_packet_index);
    }
    if ((trace_file_header.trace_file_version > VKTRACE_TRACE_FILE_VERSION_10)
        && (pHeader->packet_id == VKTRACE_TPI_VK_vkCreateDevice)) {
        vktrace_trace_packet_header *pCreateDeviceHeader = static_cast<vktrace_trace_packet_header *>(malloc((size_t)pHeader->size));
        if (pCreateDeviceHeader == nullptr) {
            vktrace_LogError("DeviceHeader memory malloc failed.");
        }
        memcpy(pCreateDeviceHeader, pHeader, (size_t)pHeader->size);
        pCreateDeviceHeader->pBody = (uintptr_t)(((char*)pCreateDeviceHeader) + sizeof(vktrace_trace_packet_header));
        VkDevice* pDevice = nullptr;
        if (trace_file_header.ptrsize == sizeof(void*)) {
            packet_vkCreateDevice* pPacket = (packet_vkCreateDevice*)pCreateDeviceHeader->pBody;
            pDevice = (VkDevice*)vktrace_trace_packet_interpret_buffer_pointer(pCreateDeviceHeader, (intptr_t)pPacket->pDevice);
        } else {
            uint32_t devicePos = trace_file_header.ptrsize * 4;
            intptr_t offset = 0;
            if (trace_file_header.ptrsize == 4) {
                uint32_t* device = (uint32_t*)(pCreateDeviceHeader->pBody + devicePos);
                offset = (intptr_t)(*device);
            } else if (trace_file_header.ptrsize == 8){
                uint64_t* device = (uint64_t*)(pCreateDeviceHeader->pBody + devicePos);
                offset = (intptr_t)(*device);
            }
            pDevice = (VkDevice*)vktrace_trace_packet_interpret_buffer_pointer(pCreateDeviceHeader, offset);
        }
        deviceToFeatures[*pDevice] = vktrace_get_trace_packet_tag(pCreateDeviceHeader);
        free(pCreateDeviceHeader);
    }

    decompress_file_size += pHeader->size;
    if (pHeader->size - sizeof(vktrace_trace_packet_header) > 1024) {
        vktrace_trace_packet_header *pCompressHeader = static_cast<vktrace_trace_packet_header *>(malloc((size_t)pHeader->size));
        if (pCompressHeader == nullptr) {
            vktrace_LogError("pCompressHeader memory malloc failed.");
        }
        memcpy(pCompressHeader, pHeader, (size_t)pHeader->size);
        if (g_compressor && compress_packet(g_compressor, (vktrace_trace_packet_header*&)pCompressHeader) != 0) {
            vktrace_LogError("Failed to compress the packet for packet_id = %hu", pHeader->packet_id);
        }
        if (pHeader->size > pCompressHeader->size) {
            memcpy((vktrace_trace_packet_header*)pHeader, pCompressHeader, (size_t)pCompressHeader->size);
        }
        free(pCompressHeader);
    }

    if (pHeader->packet_id == VKTRACE_TPI_VK_vkBuildAccelerationStructuresKHR || pHeader->packet_id == VKTRACE_TPI_VK_vkCreateAccelerationStructureKHR ||
        pHeader->packet_id == VKTRACE_TPI_VK_vkGetAccelerationStructureBuildSizesKHR || pHeader->packet_id == VKTRACE_TPI_VK_vkCmdBuildAccelerationStructuresKHR) {
        useAsApi = true;
    }

    // If the packet is one we need to track, add it to the table
    if (vktrace_append_portabilitytable(pHeader->packet_id)) {
        vktrace_LogDebug("Add packet to portability table: %s",
                            vktrace_vk_packet_id_name((VKTRACE_TRACE_PACKET_ID_VK)pHeader->packet_id));
        portabilityTable.push_back(fileOffset);
    }
    lastPacketIndex = pHeader->global_packet_index;
    lastPacketThreadId = pHeader->thread_id;
    lastPacketEndTime = pHeader->vktrace_end_time;
    fileOffset += pHeader->size;

    return true;
}

void vktrace_write_trace_packet(const vktrace_trace_packet_header* pHeader, FileLike* pFile) {
    if (pHeader == nullptr || pFile == nullptr || !((pFile->mFile != 0) ^ (pFile->mMessageStream != 0))) {
        return;
    }

    if (!vktrace_write_trace_packet_to_file(pHeader, pFile)) {
        return;
    }

    BOOL res = vktrace_FileLike_WriteRaw(pFile, pHeader, (size_t)pHeader->size);
    if (!res && pHeader->packet_id != VKTRACE_TPI_MARKER_TERMINATE_PROCESS) {
        // We don't retry on failure because vktrace_FileLike_WriteRaw already retried and gave up.
        vktrace_LogWarning("Failed to write trace packet.");
        exit(1);
    }
}
