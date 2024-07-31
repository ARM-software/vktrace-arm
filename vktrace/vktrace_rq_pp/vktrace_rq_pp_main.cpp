#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdio>
#include <cstring>
#include <list>
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <unordered_map>

#include "vktrace_common.h"
#include "vktrace_tracelog.h"
#include "vktrace_filelike.h"
#include "vktrace_trace_packet_utils.h"
#include "vktrace_vk_packet_id.h"
#include "json/json.h"
#include "vktrace_pageguard_memorycopy.h"
#include "vktrace_rq_pp.h"

using namespace std;

static string commandTypeEnumToStringArray[] = {
    "COMMAND_TYPE_RQ",
};

parser_params g_params;
bool processBufDeviceAddr = false;
bool removeDummyBuildAS = false;

static void print_usage() {
    cout << "vktracerqpp " << VKTRACE_VERSION << " available options(NOTE:command must be placed at the beginning):" << endl;
    cout << "   rq                                                        Post process an ray query trace file." << endl << endl;
    cout << "   -ppbda                                                    Post process BufferDeviceAddress of an ray query trace file." << endl << endl;
    cout << "   --remove-dummy-build-as                                   Post process remove dummy build AS of an ray query trace file." << endl << endl;
    cout << "   -in    src_tracefile                                      The src_tracefile to open. the parameter must exist and be placed after the command." << endl << endl;
    cout << "   -o     dst_traceFile                                      The dst_tracefile to generate. the parameter must exist and be placed after the command." << endl << endl;
#if defined(_DEBUG)
    cout << "   -v     Verbosity mode. Modes are \"quiet\", \"errors\", \"warnings\", \"full\", \"debug\"" << endl << endl;
#else
    cout << "   -v     Verbosity mode. Modes are \"quiet\", \"errors\", \"warnings\", \"full\"" << endl << endl;
#endif
}

static bool changeLogLevel(string level) {
    bool successful = true;
    if (level == "errors")
        vktrace_LogSetLevel(VKTRACE_LOG_ERROR);
    else if (level == "quiet")
        vktrace_LogSetLevel(VKTRACE_LOG_NONE);
    else if (level == "warnings")
        vktrace_LogSetLevel(VKTRACE_LOG_WARNING);
    else if (level == "full")
        vktrace_LogSetLevel(VKTRACE_LOG_VERBOSE);
#if defined(_DEBUG)
    else if (level == "debug") {
        vktrace_LogSetLevel(VKTRACE_LOG_DEBUG);
    }
#endif
    else {
        successful = false;
    }
    return successful;
}

static int parse_args(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0) {
        print_usage();
        exit(0);
    }

    string commandArg = argv[1];
    if (commandArg.compare("rq") == 0) {
        g_params.command = COMMAND_TYPE_RQ;
    } else {
        vktrace_LogError("Input command '%s' doesn't supported", commandArg.c_str());
        return -1;
    }

    for (int i = 2; i < argc;) {
        string arg(argv[i]);
        if (arg.compare("-o") == 0) {
            g_params.dstTraceFile = argv[i + 1];
            i = i + 2;
        } else if (arg.compare("-ppbda") == 0) {
            processBufDeviceAddr = true;
            vktrace_LogAlways("Post process BufferDeviceAddress of an ray query trace file.");
            i = i + 1;
        } else if (arg.compare("--remove-dummy-build-as") == 0) {
            removeDummyBuildAS = true;
            vktrace_LogAlways("Post process remove dummy build AS of an ray query trace file..");
            i = i + 1;
        } else if (arg.compare("-v") == 0) {
            char* logLevel = argv[i + 1];
            if (!changeLogLevel(logLevel)) {
                vktrace_LogError("Invalid verbosity mode: \"%s\".", logLevel);
                return -1;
            }
            i = i + 2;
        } else if (arg.compare("-in") == 0) {
            g_params.srcTraceFile = argv[i + 1];
            i = i + 2;
        } else {
            vktrace_LogError("Input param %s does not exist.",argv[i]);
            return -1;
        }
    }

    if (g_params.srcTraceFile == nullptr || g_params.dstTraceFile == nullptr) {
        vktrace_LogError("Please specify src tracefile and dst tracefile.");
        return -1;
    }

    const char* src_extension_name = strstr(g_params.srcTraceFile, ".vktrace");
    if (src_extension_name == nullptr || (strcmp(src_extension_name, ".vktrace") != 0 && strcmp(src_extension_name, ".vktrace.gz") != 0)) {
        vktrace_LogError("Input src file is not a vktrace file.");
        return -1;
    }

    if (g_params.dstTraceFile) {
        const char* dst_extension_name = strstr(g_params.dstTraceFile, ".vktrace");
        if (dst_extension_name == nullptr || strcmp(dst_extension_name, ".vktrace") != 0) {
            vktrace_LogError("Input dst file is not a vktrace file.");
            return -1;
        }
    }

    return 0;
}

// Common global resources
unordered_map<uint64_t, packet_info> g_globalPacketIndexToPacketInfo;
list<uint64_t> g_globalPacketIndexList;
vector<uint64_t> g_portabilityTable;
static vktrace_trace_packet_header g_portabilityTableHeader = {};
static vktrace_trace_packet_header *g_pMetaData = nullptr;
compressor* g_compressor = nullptr;
decompressor* g_decompressor = nullptr;
static int g_compress_packet_counter = 0;

static void append_portability_packet(FILE* pTraceFile) {
    uint64_t one_64 = 1;

    if (pTraceFile == NULL) {
        vktrace_LogError("Trace file was not created");
        return;
    }

    vktrace_LogDebug("Post processing trace file");

    // Add a word containing the size of the table to the table.
    // This will be the last word in the file.
    g_portabilityTable.push_back(g_portabilityTable.size());

    // Append the table packet to the trace file.
    g_portabilityTableHeader.size = sizeof(g_portabilityTableHeader) + g_portabilityTable.size() * sizeof(uint64_t);

    if (0 == Fseek(pTraceFile, 0, SEEK_END) &&
        1 == fwrite(&g_portabilityTableHeader, sizeof(g_portabilityTableHeader), 1, pTraceFile) &&
        g_portabilityTable.size() == fwrite(&g_portabilityTable[0], sizeof(uint64_t), g_portabilityTable.size(), pTraceFile)) {
        // Set the flag in the file header that indicates the portability table has been written
        if (0 == Fseek(pTraceFile, offsetof(vktrace_trace_file_header, portability_table_valid), SEEK_SET)) {
            fwrite(&one_64, sizeof(uint64_t), 1, pTraceFile);
        }
    } else {
        // Set the flag in the file header that indicates the portability table has not been written
        if (0 == Fseek(pTraceFile, offsetof(vktrace_trace_file_header, portability_table_valid), SEEK_SET)) {
            fwrite(&one_64, sizeof(uint64_t), 0, pTraceFile);
        }
    }
    g_portabilityTable.clear();
    vktrace_LogDebug("Post processing of trace file completed");
}

static void save_packet_info(vktrace_trace_packet_header* packet, uint64_t currentPosition) {
    if (packet->packet_id == VKTRACE_TPI_PORTABILITY_TABLE) {
        memcpy(&g_portabilityTableHeader, packet, sizeof(vktrace_trace_packet_header));
    } else if (packet->packet_id == VKTRACE_TPI_META_DATA) {
        if (g_pMetaData == nullptr) {
            g_pMetaData = reinterpret_cast<vktrace_trace_packet_header*>(new char[packet->size]);
        }
        memcpy(g_pMetaData, packet, packet->size);
    } else if (packet->packet_id > VKTRACE_TPI_PORTABILITY_TABLE) {
        packet_info packetInfo = {};
        packetInfo.position = currentPosition;
        packetInfo.size = packet->size;
        g_globalPacketIndexToPacketInfo[packet->global_packet_index] = packetInfo;
        g_globalPacketIndexList.push_back(packet->global_packet_index);
    } else {
        vktrace_LogWarning("Unsupported packet id: %llu", packet->packet_id);
    }
}

static int pre_handle_command(vktrace_trace_file_header* pFileHeader, FileLike *traceFile);
static int post_handle_command(vktrace_trace_file_header* pFileHeader, FileLike* traceFile);

Json::Value metaRoot;
Json::Reader reader;

uint32_t last_packet_thread_id;
uint64_t last_packet_end_time;

string editCommand = "";
extern "C" BOOL vktrace_pageguard_init_multi_threads_memcpy();

static void release(FILE* tracefp, FileLike* traceFile, vktrace_trace_file_header* pFileHeader, const char* tmpfile) {
    if (tracefp != nullptr) { fclose(tracefp); }
    if (traceFile != nullptr) { vktrace_free(traceFile); }
    if (pFileHeader != nullptr) { vktrace_free(pFileHeader); }
    if (g_pMetaData != nullptr) { delete g_pMetaData; g_pMetaData = nullptr; }
    if (g_compressor != nullptr) { delete g_compressor; g_compressor = nullptr; }
    if (g_decompressor != nullptr) { delete g_decompressor; g_decompressor = nullptr; }
    if (tmpfile != nullptr) { remove(tmpfile); }
    g_compress_packet_counter = 0;
}

int main(int argc, char** argv) {
    if (parse_args(argc, argv) < 0) {
        vktrace_LogError("Invalid parameters!");
        print_usage();
        return -1;
    }
    for (int i = 1; i < argc; i++) {
        char commandParam[512] = {0};
        sprintf(commandParam, "%s ", argv[i]);
        editCommand += commandParam;
    }

    FILE* tracefp = fopen(g_params.srcTraceFile, "rb");
    if (tracefp == NULL) {
        vktrace_LogError("Cannot open trace file: '%s'.", g_params.srcTraceFile);
        return -1;
    }

    vktrace_pageguard_init_multi_threads_memcpy();

    // Decompress trace file if it is a gz file.
    const char* tmpfile = NULL;
    if (vktrace_File_IsCompressed(tracefp)) {
#if defined(ANDROID)
        tmpfile = "/sdcard/tmp.vktrace";
#elif defined(PLATFORM_LINUX)
        tmpfile = "/tmp/tmp.vktrace";
#else
        tmpfile = "tmp.vktrace";
#endif
        // Close the fp for the gz file and open the decompressed file.
        fclose(tracefp);
        if (!vktrace_File_Decompress(g_params.srcTraceFile, tmpfile)) {
            return -1;
        }
        tracefp = fopen(tmpfile, "rb");
        if (tracefp == NULL) {
            vktrace_LogError("Cannot open trace file: '%s'.", tmpfile);
            return -1;
        }
    }

    FileLike* traceFile = NULL;
    traceFile = vktrace_FileLike_create_file(tracefp);

    vktrace_trace_file_header fileHeader = {};
    if (!vktrace_FileLike_ReadRaw(traceFile, &fileHeader, sizeof(fileHeader))) {
        vktrace_LogError("Fail to read file header!");
        release(tracefp, traceFile, nullptr, tmpfile);
        return -1;
    }

    if (fileHeader.magic != VKTRACE_FILE_MAGIC) {
        vktrace_LogError("%s does not appear to be a valid Vulkan trace file.", g_params.srcTraceFile);
        release(tracefp, traceFile, nullptr, tmpfile);
        return -1;
    }

    if (sizeof(void*) != fileHeader.ptrsize) {
        vktrace_LogError("%llu-bit trace file is not supported by %zu-bit vkeditor.", (fileHeader.ptrsize * 8),
                         (sizeof(void*) * 8));
        release(tracefp, traceFile, nullptr, tmpfile);
        return -1;
    }

    vktrace_trace_file_header* pFileHeader = nullptr;
    if (!(pFileHeader = (vktrace_trace_file_header*)vktrace_malloc(sizeof(vktrace_trace_file_header) +
                                                                   (size_t)(fileHeader.n_gpuinfo * sizeof(struct_gpuinfo))))) {
        vktrace_LogError("Can't allocate space for trace file header.");
        release(tracefp, traceFile, nullptr, tmpfile);
        return -1;
    }

    *pFileHeader = fileHeader;
    if (!vktrace_FileLike_ReadRaw(traceFile, pFileHeader + 1, fileHeader.n_gpuinfo * sizeof(struct_gpuinfo))) {
        vktrace_LogError("Unable to read header from file.");
        release(tracefp, traceFile, pFileHeader, tmpfile);
        return -1;
    }
    if (pFileHeader->trace_file_version > VKTRACE_TRACE_FILE_VERSION) {
        release(tracefp, traceFile, nullptr, tmpfile);
        vktrace_LogError("Trace file version %d is larger than vkeditor version %d. You need a newer vkeditor to edit it.", pFileHeader->trace_file_version, VKTRACE_TRACE_FILE_VERSION);
        return -1;
    }

    vktrace_trace_packet_header* packet = NULL;
    uint64_t currentPosition = vktrace_FileLike_GetCurrentPosition(traceFile);
    // Construct mapping from global packet index to packet in trace file
    while ((packet = vktrace_read_trace_packet(traceFile))) {
        if (!packet) break;
        save_packet_info(packet, currentPosition);
        vktrace_delete_trace_packet_no_lock(&packet);
        currentPosition = vktrace_FileLike_GetCurrentPosition(traceFile);
    }
    vktrace_LogDebug("Read trace file completed.");
    if (fileHeader.bit_flags & VKTRACE_USE_ACCELERATION_STRUCTURE_API_BIT) {
        vktrace_LogAlways("There are AS related functions in the input trace file.");
    }
    vktrace_set_trace_version(pFileHeader->trace_file_version);
    // Read and parse meta data
    if (pFileHeader->trace_file_version > VKTRACE_TRACE_FILE_VERSION_9) {
        vktrace_trace_packet_header hdr;
        uint64_t originalFilePos = vktrace_FileLike_GetCurrentPosition(traceFile);
        if (vktrace_FileLike_SetCurrentPosition(traceFile, fileHeader.meta_data_offset)
            && vktrace_FileLike_ReadRaw(traceFile, &hdr, sizeof(hdr))
            && hdr.packet_id == VKTRACE_TPI_META_DATA) {
            uint64_t meta_data_json_str_size = hdr.size - sizeof(hdr);
            char* meta_data_json_str = new char[meta_data_json_str_size];
            if (!meta_data_json_str || !vktrace_FileLike_ReadRaw(traceFile, meta_data_json_str, meta_data_json_str_size)) {
                vktrace_LogError("Reading meta data of the original file failed");
                return -1;
            }

            if (!reader.parse( meta_data_json_str, metaRoot)) {
                vktrace_LogError("Parsing meta data of the original file failed");
                delete[] meta_data_json_str;
                return -1;
            }

            delete[] meta_data_json_str;

        } else {
            vktrace_LogWarning("Dump the Meta data failed");
        }
        vktrace_FileLike_SetCurrentPosition(traceFile, originalFilePos);
    }

    if (pre_handle_command(pFileHeader, traceFile) == -1) {
        release(tracefp, traceFile, pFileHeader, tmpfile);
        return -1;
    }
    if (post_handle_command(pFileHeader, traceFile) == -1) {
        release(tracefp, traceFile, pFileHeader, tmpfile);
        return -1;
    }
    release(tracefp, traceFile, pFileHeader, tmpfile);

    return 0;
}

int pre_find_as_flush(vktrace_trace_file_header* pFileHeader, FileLike* traceFile);
int pre_find_as_unmap(vktrace_trace_file_header* pFileHeader, FileLike* traceFile);
int pre_fix_asbuffer_size(vktrace_trace_file_header* pFileHeader, FileLike* traceFile);
int pre_remove_unused_memory_handle(vktrace_trace_file_header* pFileHeader, FileLike* traceFile);
int pre_remove_unused_handle(vktrace_trace_file_header* pFileHeader, FileLike* traceFile);
int pre_remove_dummy_build_as(vktrace_trace_file_header* pFileHeader, FileLike* traceFile);
int pre_remove_all_dummy_as(vktrace_trace_file_header* pFileHeader, FileLike* traceFile);
int pre_find_sbt(vktrace_trace_file_header* pFileHeader, FileLike *traceFile);

static int pre_handle_command(vktrace_trace_file_header* pFileHeader, FileLike *traceFile) {
    if (pFileHeader == nullptr) {
        return -1;
    }

    int ret;
    switch (g_params.command) {
        case COMMAND_TYPE_RQ: {
            if (removeDummyBuildAS) {
                ret = pre_remove_dummy_build_as(pFileHeader, traceFile);
                ret |= pre_remove_all_dummy_as(pFileHeader, traceFile);
            } else {
                ret = pre_remove_unused_memory_handle(pFileHeader, traceFile);
                ret |= pre_remove_unused_handle(pFileHeader, traceFile);
                ret |= pre_find_as_unmap(pFileHeader, traceFile);
                ret |= pre_find_as_flush(pFileHeader, traceFile);
                ret |= pre_find_sbt(pFileHeader, traceFile);
                ret |= pre_fix_asbuffer_size(pFileHeader, traceFile);

            }
        } break;

        default: {
            vktrace_LogError("Input command %s does not exist.", commandTypeEnumToStringArray[g_params.command].c_str());
            return -1;
        } break;
    }

    return ret;
}

BOOL isvkFlushMappedMemoryRangesSpecial(PBYTE pOPTPackageData) {
    BOOL bRet = FALSE;
    PageGuardChangedBlockInfo *pChangedInfoArray = (PageGuardChangedBlockInfo *)pOPTPackageData;
    if ((pOPTPackageData == nullptr) ||
        ((static_cast<uint64_t>(pChangedInfoArray[0].reserve0)) &
         PAGEGUARD_SPECIAL_FORMAT_PACKET_FOR_VKFLUSHMAPPEDMEMORYRANGES))
    {
        bRet = TRUE;
    }
    return bRet;
}

int post_find_as_reference(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader);
int post_find_as_unmap(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header*& pHeader);
int post_rm_capture_replay(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header*& pHeader);
int post_rm_invalid_data(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader);
int post_as_dedicate_mem(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header*& pHeader, FILE* newTraceFile,
                uint64_t* fileOffset, uint64_t* fileSize);
int post_move_allocate_memory(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header*& pHeader, FILE* newTraceFile,
                              uint64_t* fileOffset, uint64_t* fileSize, bool &rmdp);
int post_remove_unused_handle(vktrace_trace_file_header* pFileHeader,  vktrace_trace_packet_header* &pHeader, FILE* newTraceFile,
                              uint64_t* fileOffset, uint64_t* fileSize, bool &rmdp);
int post_remove_dummy_build_as(vktrace_trace_file_header* pFileHeader,  vktrace_trace_packet_header* &pHeader, FILE* newTraceFile,
                               uint64_t* fileOffset, uint64_t* fileSize, bool &rmdp);
int post_find_sbt(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header* &pHeader);

static int handle_packet(vktrace_trace_file_header* pFileHeader, vktrace_trace_packet_header*& pPacketHeader, FILE* newTraceFile,
                         uint64_t* fileOffset, uint64_t* fileSize, bool& rmdp) {
    if (pPacketHeader == nullptr) {
        return -1;
    }

    int ret = 0;
    switch (g_params.command) {
        case COMMAND_TYPE_RQ: {
            if (removeDummyBuildAS) {
                ret = post_remove_dummy_build_as(pFileHeader, pPacketHeader, newTraceFile, fileOffset, fileSize, rmdp);
            } else {
                ret = post_rm_invalid_data(pFileHeader, pPacketHeader);
                ret |= post_rm_capture_replay(pFileHeader, pPacketHeader);
                ret |= post_find_as_reference(pFileHeader, pPacketHeader);
                ret |= post_find_as_unmap(pFileHeader, pPacketHeader);
                ret |= post_find_sbt(pFileHeader, pPacketHeader);
                ret |= post_as_dedicate_mem(pFileHeader, pPacketHeader, newTraceFile, fileOffset, fileSize);
                ret |= post_remove_unused_handle(pFileHeader, pPacketHeader, newTraceFile, fileOffset, fileSize, rmdp);
            }
        } break;

        default: {
            vktrace_LogError("Input command %s does not exist.", commandTypeEnumToStringArray[g_params.command].c_str());
            return -1;
        } break;
    }

    return ret;
}

uint32_t vktrace_appendMetaData(FILE* pTraceFile, vktrace_trace_file_header *pFileHeader) {
    Json::Value command;
    command["command"] = Json::Value(editCommand);
    Json::Value &editProcess = metaRoot["editProcess"];
    editProcess.append(command);

    auto str = metaRoot.toStyledString();
    vktrace_LogVerbose("Meta data string: %s", str.c_str());
    uint32_t meta_data_size = str.size() + 1;
    meta_data_size = ROUNDUP_TO_8(meta_data_size);
    char *meta_data_str_json = new char[meta_data_size];
    memset(meta_data_str_json, 0, meta_data_size);
    strcpy(meta_data_str_json, str.c_str());
    vktrace_trace_packet_header hdr;
    if (g_pMetaData) {
        hdr = *g_pMetaData;
        hdr.size = sizeof(hdr) + meta_data_size;
    }
    else {
        hdr.size = sizeof(hdr) + meta_data_size;
        hdr.global_packet_index = g_globalPacketIndexList.size();
        hdr.tracer_id = VKTRACE_TID_VULKAN;
        hdr.packet_id = VKTRACE_TPI_META_DATA;
        hdr.thread_id = last_packet_thread_id;
        hdr.vktrace_begin_time = hdr.entrypoint_begin_time = hdr.entrypoint_end_time = hdr.vktrace_end_time = last_packet_end_time;
        hdr.next_buffers_offset = 0;
        hdr.pBody = (uintptr_t)NULL;
    }

    if (0 == Fseek(pTraceFile, 0, SEEK_END)) {
        uint64_t meta_data_file_offset = Ftell(pTraceFile);
        if (1 == fwrite(&hdr, sizeof(hdr), 1, pTraceFile) &&
            meta_data_size == fwrite(meta_data_str_json, sizeof(char), meta_data_size, pTraceFile)) {
            pFileHeader->meta_data_offset = meta_data_file_offset;
        }
    } else {
        vktrace_LogError("File operation failed during append the meta data");
    }
    delete[] meta_data_str_json;
    return meta_data_size;
}

static int post_handle_command(vktrace_trace_file_header* pFileHeader, FileLike* traceFile) {
    int ret = 0;

    // Write out to new trace file.
    FILE* newfp = fopen(g_params.dstTraceFile, "wb");
    if (newfp == NULL) {
        vktrace_LogError("Fail to open trace file %s to write!", g_params.dstTraceFile);
        return -1;
    }

    // Writes file header.
    if (!removeDummyBuildAS) {
        pFileHeader->bit_flags |= VKTRACE_RQ_POSTPROCESSED_BIT;
    }
    uint64_t bytesWritten = fwrite(pFileHeader, 1, sizeof(vktrace_trace_file_header) + (size_t)(pFileHeader->n_gpuinfo * sizeof(struct_gpuinfo)), newfp);
    fflush(newfp);
    if (bytesWritten != sizeof(vktrace_trace_file_header) + pFileHeader->n_gpuinfo * sizeof(struct_gpuinfo)) {
        vktrace_LogError("Unable to write trace file header - fwrite failed.");
        fclose(newfp);
        return -1;
    }

    uint64_t fileOffset = pFileHeader->first_packet_offset;
    uint64_t filesize = fileOffset;
    // Loop through the packet list and write packet by packet to the new trace file.
    for (auto it = g_globalPacketIndexList.begin(); it != g_globalPacketIndexList.end(); ++it) {
        uint64_t gpIndex = (uint64_t)*it;

        if (g_globalPacketIndexToPacketInfo.find(gpIndex) == g_globalPacketIndexToPacketInfo.end()) {
            vktrace_LogError("Incorrect global packet index: %llu.", gpIndex);
            ret = -1;
            break;
        }

        // Read packet from the existing trace file
        packet_info packetInfo = g_globalPacketIndexToPacketInfo[gpIndex];
        vktrace_trace_packet_header* pHeader = (vktrace_trace_packet_header*)vktrace_malloc((size_t)packetInfo.size);
        vktrace_FileLike_SetCurrentPosition(traceFile, packetInfo.position);
        if (!vktrace_FileLike_ReadRaw(traceFile, pHeader, (size_t)packetInfo.size)) {
            vktrace_LogError("Failed to read trace packet with size of %llu.", (size_t)packetInfo.size);
            vktrace_free(pHeader);
            ret = -1;
            break;
        }
        bool rmdp = false;
        pHeader->pBody = (uintptr_t)(((char*)pHeader) + sizeof(vktrace_trace_packet_header));
        if (handle_packet(pFileHeader, pHeader, newfp, &fileOffset, &filesize, rmdp) == -1) {
            vktrace_free(pHeader);
            ret = -1;
            break;
        }
        if (rmdp) {
            vktrace_free(pHeader);
            continue;
        }

        if (pHeader->tracer_id == VKTRACE_TID_VULKAN_COMPRESSED)
            g_compress_packet_counter++;
        if (pHeader->tracer_id == VKTRACE_TID_VULKAN_COMPRESSED) {
            uint64_t decompressed_data_size = reinterpret_cast<vktrace_trace_packet_header_compression_ext*>(pHeader->pBody)->decompressed_size;
            uint64_t decompressed_packet_size = decompressed_data_size + sizeof(vktrace_trace_packet_header) + sizeof(vktrace_trace_packet_header_compression_ext);
            filesize += decompressed_packet_size;
        } else {
            filesize += pHeader->size;
        }

        // Write packet to the new trace file
        size_t copy_size = (size_t)pHeader->size;
        bytesWritten = fwrite(pHeader, 1, copy_size, newfp);
        fflush(newfp);
        if (bytesWritten != copy_size) {
            vktrace_LogAlways("bytesWritten = %d, copy_size = %d\n", bytesWritten, copy_size);
            vktrace_LogError("Failed to write the packet for packet_id = %hu", pHeader->packet_id);
            vktrace_free(pHeader);
            ret = -1;
            break;
        }
        // Generate new portability table
        if (vktrace_append_portabilitytable(pHeader->packet_id)) {
            vktrace_LogDebug("Add packet to portability table: %s",
                              vktrace_vk_packet_id_name((VKTRACE_TRACE_PACKET_ID_VK)pHeader->packet_id));
            g_portabilityTable.push_back(fileOffset);
        }
        fileOffset += bytesWritten;
        last_packet_thread_id = pHeader->thread_id;
        last_packet_end_time = pHeader->vktrace_end_time;
        vktrace_free(pHeader);
    }

    int metaSize = 0;
    if (ret != -1 && pFileHeader->trace_file_version > VKTRACE_TRACE_FILE_VERSION_9) {
        // Append meta data
        metaSize = vktrace_appendMetaData(newfp, pFileHeader);
        if (metaSize < 0) {
            vktrace_LogError("Error on appending meadata\n");
            fclose(newfp);
            return -1;
        }
    }

    if (ret != -1) {
        // Append new portability table
        append_portability_packet(newfp);
        vktrace_LogDebug("Done generating new trace file %s", g_params.dstTraceFile);
    }

    // Writes file header again to fresh the compress information.
    if (fseek(newfp, 0, SEEK_SET) != 0) {
        vktrace_LogError("Error on fseek, ferror = %d\n", ferror(newfp));
        fclose(newfp);
        return -1;
    }
    if (g_compress_packet_counter == 0) {   // There are no compress packets in this file
        pFileHeader->compress_type = VKTRACE_COMPRESS_TYPE_NONE;
    }
    else if (g_compress_packet_counter < 0) {
        vktrace_LogError("The compress packet number is negative, please report this bug to vkeditor developers.\n");
        fclose(newfp);
        return -1;
    }
    if (pFileHeader->trace_file_version > VKTRACE_TRACE_FILE_VERSION_9) {
        filesize += (metaSize + sizeof(vktrace_trace_packet_header));
    }
    filesize += g_portabilityTableHeader.size;
    pFileHeader->decompress_file_size = filesize;
    bytesWritten = fwrite(pFileHeader, 1, sizeof(vktrace_trace_file_header) + (size_t)(pFileHeader->n_gpuinfo * sizeof(struct_gpuinfo)), newfp);
    fflush(newfp);
    if (bytesWritten != sizeof(vktrace_trace_file_header) + pFileHeader->n_gpuinfo * sizeof(struct_gpuinfo)) {
        vktrace_LogError("Unable to write trace file header - fwrite failed.");
        fclose(newfp);
        return -1;
    }
    fclose(newfp);

    vktrace_LogAlways("Post processing finished.");

    return ret;
}
