/*
 * Copyright (c) 2019 ARM Limited. All rights reserved.
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

#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdio>
#include <cstring>

#include <unordered_map>

#include "vktrace_common.h"
#include "vktrace_tracelog.h"
#include "vktrace_filelike.h"
#include "vktrace_trace_packet_utils.h"
#include "vktrace_vk_packet_id.h"
#include "decompressor.h"

#include "vktracedump_main.h"

using namespace std;

struct vktracedump_params {
    const char* traceFile = NULL;
    const char* simpleDumpFile = NULL;
    const char* fullDumpFile = NULL;
    const char* dumpFileFrameNum = nullptr;
    bool onlyHeaderInfo = false;
    bool noAddr = false;
    bool dumpShader = false;
    bool saveAsHtml = false;
    bool saveAsJson = false;
} g_params;

char g_dump_file_name[1024] = {0};
const char* DUMP_SETTING_FILE = "vk_layer_settings.txt";
const char* FULL_DUMP_FILE_UNSET = "full_dump_file_unset";
const char* SEPARATOR = " : ";
const char* SPACES = "               ";
const uint32_t COLUMN_WIDTH = 26;

static void print_usage() {
    cout << "vktracedump available options:" << endl;
    cout << "    -o <traceFile>        The trace file to open and parse" << endl;
    cout << "    -hd                   Only dump the file header and meta data." << endl;
    cout << "    -s <simpleDumpFile>   (Optional) The file to save the outputs of simple/brief API dump. Use 'stdout' to send "
            "outputs to stdout."
         << endl;
    cout << "    -f <fullDumpFile>     (Optional) The file to save the outputs of full/detailed API dump. Use 'stdout' to send "
            "outputs to stdout."
         << endl;
    cout << "    -fn <dumpFileFrameNum>   (Optional) Set dump file frame number, The default is 0."
         << endl;
    cout << "    -ds                   Dump the shader binary code in pCode to shader dump files shader_<index>.hex (when "
            "<fullDumpFile> is a file) or to stdout (when <fullDumpFile> is stdout).  Only works with \"-f <fullDumpFile>\" option."
         << endl;
    cout << "                          The file name shader_<index>.hex can be found in pCode in the <fullDumpFile> to associate "
            "with vkCreateShaderModule."
         << endl;
    cout << "    -dh                   Save full/detailed API dump as HTML format. (Default is text format)  Only works with \"-f "
            "<fullDumpFile>\" option."
         << endl;
    cout << "    -dj                   Save full/detailed API dump as JSON format. (Default is text format)  Only works with \"-f "
            "<fullDumpFile>\" option."
         << endl;
    cout << "    -na                   Dump string \"address\" in place of hex addresses. (Default is false)  Only works with \"-f "
            "<fullDumpFile>\" option."
         << endl;
}

static int parse_args(int argc, char** argv) {
    for (int i = 1; i < argc;) {
        string arg(argv[i]);
        if (arg.compare("-o") == 0) {
            const char* traceFile = argv[i + 1];
            g_params.traceFile = traceFile;
            i = i + 2;
        } else if (arg.compare("-s") == 0) {
            g_params.simpleDumpFile = argv[i + 1];
            i = i + 2;
        } else if (arg.compare("-f") == 0) {
            g_params.fullDumpFile = argv[i + 1];
            i = i + 2;
        } else if (arg.compare("-fn") == 0) {
            g_params.dumpFileFrameNum = argv[i + 1];
            i = i + 2;
        } else if (arg.compare("-ds") == 0) {
            g_params.dumpShader = true;
            i++;
        } else if (arg.compare("-dh") == 0) {
            g_params.saveAsHtml = true;
            i++;
        } else if (arg.compare("-dj") == 0) {
            g_params.saveAsJson = true;
            i++;
        } else if (arg.compare("-na") == 0) {
            g_params.noAddr = true;
            i++;
        } else if (arg.compare("-hd") == 0) {
            g_params.onlyHeaderInfo = true;
            i++;
        } else if (arg.compare("-h") == 0) {
            print_usage();
            exit(0);
        } else {
            return -1;
        }
    }
    if ((g_params.saveAsHtml || g_params.saveAsJson || g_params.dumpShader || g_params.noAddr) && (g_params.fullDumpFile == NULL)) {
        // saveAsHtml, saveAsJson, dumpShader and noAddress should be used with valid fullDumpFile option.
        return -1;
    }
    if (g_params.traceFile == NULL) {
        // traceFile option must be specified.
        return -1;
    }

    return 0;
}

static void dump_packet_brief(ostream& dumpFile, uint32_t frameNumber, vktrace_trace_packet_header* packet,
                              uint64_t currentPosition) {
    static size_t index = 0;
    static bool skipApi = false;
    if (index == 0) {
        dumpFile << setw(COLUMN_WIDTH) << "frame" << SEPARATOR << setw(COLUMN_WIDTH) << "thread id" << SEPARATOR
                 << setw(COLUMN_WIDTH) << "packet index" << SEPARATOR << setw(COLUMN_WIDTH) << "global pack id" << SEPARATOR
                 << setw(COLUMN_WIDTH) << "pack position" << SEPARATOR << setw(COLUMN_WIDTH) << "pack byte size" << SEPARATOR
                 << "API Call" << endl;
    }
    if (packet->packet_id != VKTRACE_TPI_VK_vkGetInstanceProcAddr && packet->packet_id != VKTRACE_TPI_VK_vkGetDeviceProcAddr) {
        skipApi = false;
        dumpFile << setw(COLUMN_WIDTH) << dec << frameNumber << SEPARATOR << setw(COLUMN_WIDTH) << dec << packet->thread_id
                 << SEPARATOR << setw(COLUMN_WIDTH) << dec << index << SEPARATOR << setw(COLUMN_WIDTH) << dec
                 << packet->global_packet_index << SEPARATOR << setw(COLUMN_WIDTH) << dec << currentPosition << SEPARATOR
                 << setw(COLUMN_WIDTH) << dec << packet->size << SEPARATOR
                 << vktrace_stringify_vk_packet_id((VKTRACE_TRACE_PACKET_ID_VK)packet->packet_id, packet) << endl;
    } else {
        if (!skipApi) {
            dumpFile << setw(COLUMN_WIDTH) << dec << frameNumber << SEPARATOR << SPACES << SEPARATOR << SPACES << SEPARATOR
                     << SPACES << SEPARATOR << SPACES << SEPARATOR << SPACES << SEPARATOR << "Skip "
                     << vktrace_vk_packet_id_name((VKTRACE_TRACE_PACKET_ID_VK)packet->packet_id) << " call(s)!" << endl;
            skipApi = true;
        }
    }
    index++;
}

static void dump_full_setup() {
    if (g_params.fullDumpFile) {
        // Remove existing dump setting file before creating a new one
        remove(DUMP_SETTING_FILE);

        vktrace_set_global_var("VK_LAYER_PATH", "");
        vktrace_set_global_var("VK_LAYER_SETTINGS_PATH", "");
        // Generate a default vk_layer_settings.txt if it does not exist or failed to be opened.
        ofstream settingFile;
        settingFile.open(DUMP_SETTING_FILE);
        settingFile << "#  ==============" << endl;
        settingFile << "#  lunarg_api_dump.output_format : Specifies the format used for output, can be Text (default -- "
                       "outputs plain text), Html, or Json."
                    << endl;
        settingFile << "#  lunarg_api_dump.detailed : Setting this to TRUE causes parameter details to be dumped in addition "
                       "to API calls."
                    << endl;
        settingFile
            << "#  lunarg_api_dump.no_addr : Setting this to TRUE causes \"address\" to be dumped in place of hex addresses."
            << endl;
        settingFile << "#  lunarg_api_dump.file : Setting this to TRUE indicates that output should be written to file instead "
                       "of STDOUT."
                    << endl;
        settingFile << "#  lunarg_api_dump.log_filename : Specifies the file to dump to when \"file = TRUE\"." << endl;
        settingFile << "#  lunarg_api_dump.flush : Setting this to TRUE causes IO to be flushed each API call that is written."
                    << endl;
        settingFile << "#  lunarg_api_dump.indent_size : Specifies the number of spaces that a tab is equal to." << endl;
        settingFile << "#  lunarg_api_dump.show_types : Setting this to TRUE causes types to be dumped in addition to values."
                    << endl;
        settingFile << "#  lunarg_api_dump.name_size : The number of characters the name of a variable should consume, "
                       "assuming more are not required."
                    << endl;
        settingFile << "#  lunarg_api_dump.type_size : The number of characters the type of a variable should consume, "
                       "assuming more are not requires."
                    << endl;
        settingFile << "#  lunarg_api_dump.use_spaces : Setting this to TRUE causes all tabs characters to be replaced with spaces."
                    << endl;
        settingFile << "#  lunarg_api_dump.show_shader : Setting this to TRUE causes the shader binary code in pCode to be "
                       "also written to output."
                    << endl;
        settingFile << "#  ==============" << endl;
        if (g_params.saveAsHtml) {
            settingFile << "lunarg_api_dump.output_format = Html" << endl;
        } else if (g_params.saveAsJson) {
            settingFile << "lunarg_api_dump.output_format = Json" << endl;
        } else {
            settingFile << "lunarg_api_dump.output_format = Text" << endl;
        }
        settingFile << "lunarg_api_dump.detailed = TRUE" << endl;
        settingFile << "lunarg_api_dump.no_addr = " << (g_params.noAddr ? "TRUE" : "FALSE") << endl;
        if (g_params.fullDumpFile && (!strcmp(g_params.fullDumpFile, "STDOUT") || !strcmp(g_params.fullDumpFile, "stdout"))) {
            settingFile << "lunarg_api_dump.file = FALSE" << endl;
        } else {
            settingFile << "lunarg_api_dump.file = TRUE" << endl;
        }
        settingFile << "lunarg_api_dump.log_filename = " << g_dump_file_name << endl;
        settingFile << "lunarg_api_dump.flush = TRUE" << endl;
        settingFile << "lunarg_api_dump.indent_size = 4" << endl;
        settingFile << "lunarg_api_dump.show_types = TRUE" << endl;
        settingFile << "lunarg_api_dump.name_size = 32" << endl;
        settingFile << "lunarg_api_dump.type_size = 0" << endl;
        settingFile << "lunarg_api_dump.use_spaces = TRUE" << endl;
        if (g_params.dumpShader) {
            settingFile << "lunarg_api_dump.show_shader = TRUE" << endl;
        } else {
            settingFile << "lunarg_api_dump.show_shader = FALSE" << endl;
        }
        settingFile.close();
    }
}

static bool change_dump_file_name(const char* dump_file_name_org, uint32_t cur_frame_index) {
    uint32_t dump_file_frame_num = 0;
    if (cur_frame_index == 0) {
        strncpy(g_dump_file_name, dump_file_name_org, strlen(dump_file_name_org));
    }
    if (g_params.dumpFileFrameNum != nullptr) {
        dump_file_frame_num = atoi(g_params.dumpFileFrameNum);
    }
    if (dump_file_frame_num == 0) {
        if (cur_frame_index == 0) {
            return true;
        } else {
            return false;
        }
    } else {
        int remainder = cur_frame_index % dump_file_frame_num;
        if (remainder != 0) {
            return false;
        }

        memset(g_dump_file_name, 0, 1024);
        char buffer[32] = {0};
        sprintf(buffer, "_%d", cur_frame_index / dump_file_frame_num);
        char* dotpos = strrchr(const_cast<char*>(dump_file_name_org), '.');
        if (dotpos == nullptr) {
            strncpy(g_dump_file_name, dump_file_name_org, strlen(dump_file_name_org));
            strcat(g_dump_file_name, buffer);
        } else {
            strncpy(g_dump_file_name, dump_file_name_org, strlen(dump_file_name_org) - strlen(dotpos));
            strcat(g_dump_file_name, buffer);
            strcat(g_dump_file_name, dotpos);
        }
        return true;
    }
    return false;
}

static string enabled_features_to_str(uint64_t enabled_features) {
    string str = "null";

    if (enabled_features & TRACER_FEAT_FORCE_FIFO) {
        str = "TRACER_FEAT_FORCE_FIFO";
    }

    if (enabled_features & TRACER_FEAT_PG_SYNC_GPU_DATA_BACK) {
        if (str == "null") {
            str = "TRACER_FEAT_PG_SYNC_GPU_DATA_BACK";
        } else {
            str += " | TRACER_FEAT_PG_SYNC_GPU_DATA_BACK";
        }
    }

    if (enabled_features & TRACER_FEAT_DELAY_SIGNAL_FENCE) {
        if (str == "null") {
            str = "TRACER_FEAT_DELAY_SIGNAL_FENCE";
        } else {
            str += " | TRACER_FEAT_DELAY_SIGNAL_FENCE";
        }
    }

    if ((enabled_features & ~0x7) > 0) {
        if (str == "null") {
            str = "TRACER_FEAT_UNKNOWN";
        } else {
            str += " | TRACER_FEAT_UNKNOWN";
        }
    }

    return str;
}

int main(int argc, char** argv) {
    if (parse_args(argc, argv) < 0) {
        cout << "Error: invalid parameters!" << endl;
        print_usage();
        return -1;
    }
    if (g_params.fullDumpFile != nullptr && (!strcmp(g_params.fullDumpFile, "STDOUT") || !strcmp(g_params.fullDumpFile, "stdout"))) {
        g_params.dumpFileFrameNum = nullptr;
    }
    if (g_params.simpleDumpFile != nullptr && (!strcmp(g_params.simpleDumpFile, "STDOUT") || !strcmp(g_params.simpleDumpFile, "stdout"))) {
        g_params.dumpFileFrameNum = nullptr;
    }

    FILE* tracefp = fopen(g_params.traceFile, "rb");
    if (tracefp == NULL) {
        cout << "Error: Open trace file \"" << g_params.traceFile << "\" fail !" << endl;
        return -1;
    }

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
        if (!vktrace_File_Decompress(g_params.traceFile, tmpfile)) {
            return -1;
        }
        tracefp = fopen(tmpfile, "rb");
        if (tracefp == NULL) {
            vktrace_LogError("Cannot open trace file: '%s'.", tmpfile);
            return -1;
        }
    }

    int ret = 0;
    FileLike* traceFile = NULL;
    traceFile = vktrace_FileLike_create_file(tracefp);

    vktrace_trace_file_header fileHeader;
    struct_gpuinfo gpuInfo;

    if (vktrace_FileLike_ReadRaw(traceFile, &fileHeader, sizeof(fileHeader))) {
        if (fileHeader.magic != VKTRACE_FILE_MAGIC) {
            cout << "\"" << g_params.traceFile << "\" "
                 << "is not a vktrace file !" << endl;
            ret = -1;
        } else if (sizeof(void*) != fileHeader.ptrsize) {
            cout << "Error: " << fileHeader.ptrsize * 8 << "bit trace file cannot be parsed by " << sizeof(void*) * 8
                 << "bit vktracedump!" << endl;
            ret = -1;
        } else {
            streambuf* buf = NULL;
            ostream* pSimpleDumpFile = nullptr;
            ofstream fileOutput;
            if (g_params.simpleDumpFile && change_dump_file_name(g_params.simpleDumpFile, 0)) {
                if (!strcmp(g_dump_file_name, "STDOUT") || !strcmp(g_dump_file_name, "stdout")) {
                    buf = cout.rdbuf();
                } else {
                    fileOutput.open(g_dump_file_name);
                    buf = fileOutput.rdbuf();
                }
                pSimpleDumpFile = new ostream(buf);
                if (pSimpleDumpFile == nullptr) {
                    vktrace_LogError("Cannot new ostream.");
                    ret = -1;
                }
            }
            if (g_params.fullDumpFile && change_dump_file_name(g_params.fullDumpFile, 0)) {
                dump_full_setup();
            }
            bool hideBriefInfo = false;
            if ((g_params.fullDumpFile && (!strcmp(g_params.fullDumpFile, "STDOUT") || !strcmp(g_params.fullDumpFile, "stdout"))) ||
                (g_params.simpleDumpFile &&
                 (!strcmp(g_params.simpleDumpFile, "STDOUT") || !strcmp(g_params.simpleDumpFile, "stdout")))) {
                hideBriefInfo = true;
            }
            if (!hideBriefInfo) {
                cout << setw(COLUMN_WIDTH) << left << "File Version:" << fileHeader.trace_file_version << endl;
                if (fileHeader.trace_file_version > VKTRACE_TRACE_FILE_VERSION_8) {
                    cout << setw(COLUMN_WIDTH) << left << "Tracer Version:"
                         << version_word_to_str(fileHeader.tracer_version) << endl;
                }
                if (fileHeader.trace_file_version > VKTRACE_TRACE_FILE_VERSION_9) {
                    cout << setw(COLUMN_WIDTH) << left << "Enabled Tracer Features:"
                         << enabled_features_to_str(fileHeader.enabled_tracer_features) << endl;
                }
                cout << setw(COLUMN_WIDTH) << left << "File Type:" << fileHeader.ptrsize * 8 << "bit" << endl;
                cout << setw(COLUMN_WIDTH) << left << "Arch:" << (char*)&fileHeader.arch << endl;
                cout << setw(COLUMN_WIDTH) << left << "OS:" << (char*)&fileHeader.os << endl;
                cout << setw(COLUMN_WIDTH) << left << "Endianess:" << (fileHeader.endianess ? "Big" : "Little") << endl;
                if (fileHeader.n_gpuinfo < 1 || fileHeader.n_gpuinfo > 1) {
                    cout << "Warning: number of gpu info = " << fileHeader.n_gpuinfo << endl;
                }
            }
            for (uint32_t i = 0; i < fileHeader.n_gpuinfo; i++) {
                if (vktrace_FileLike_ReadRaw(traceFile, &gpuInfo, sizeof(gpuInfo))) {
                    if (!hideBriefInfo) {
                        cout << setw(COLUMN_WIDTH) << left << "Vendor ID:"
                             << "0x" << hex << uppercase << (gpuInfo.gpu_id >> 32) << endl;
                        cout << setw(COLUMN_WIDTH) << left << "Device ID:"
                             << "0x" << hex << uppercase << (gpuInfo.gpu_id & UINT32_MAX) << endl;
                        cout << setw(COLUMN_WIDTH) << left << "Driver Ver:"
                             << "0x" << hex << uppercase << gpuInfo.gpu_drv_vers << endl;
                    }
                } else {
                    cout << "Error: Read gpu info fail!" << endl;
                    ret = -1;
                }
            }
            // Dump the meta data
            if (fileHeader.trace_file_version > VKTRACE_TRACE_FILE_VERSION_9) {
                uint64_t originalFilePos;
                vktrace_trace_packet_header hdr;
                originalFilePos = vktrace_FileLike_GetCurrentPosition(traceFile);
                if (vktrace_FileLike_SetCurrentPosition(traceFile, fileHeader.meta_data_offset)
                    && vktrace_FileLike_ReadRaw(traceFile, &hdr, sizeof(hdr))
                    && hdr.packet_id == VKTRACE_TPI_META_DATA) {
                    uint64_t meta_data_json_str_size = hdr.size - sizeof(hdr);
                    char* meta_data_json_str = new char[meta_data_json_str_size];
                    if (meta_data_json_str && vktrace_FileLike_ReadRaw(traceFile, meta_data_json_str, meta_data_json_str_size)) {
                        cout << "Meta Data: " << meta_data_json_str;
                    }
                    delete[] meta_data_json_str;
                } else {
                    vktrace_LogWarning("Dump the Meta data failed");
                }
                vktrace_FileLike_SetCurrentPosition(traceFile, originalFilePos);
            }
            if (ret > -1 && !g_params.onlyHeaderInfo) {
                uint32_t frameNumber = 0;
                uint32_t deviceApiVersion = UINT32_MAX;
                uint32_t appApiVersion = UINT32_MAX;
                char* pApplicationName = NULL;
                uint32_t applicationVersion = 0;
                char* pEngineName = NULL;
                uint32_t engineVersion = 0;
                char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "";
                decompressor* decomp = nullptr;
                if (fileHeader.compress_type != VKTRACE_COMPRESS_TYPE_NONE) {
                    decomp = create_decompressor((VKTRACE_COMPRESS_TYPE)fileHeader.compress_type);
                    if (decomp == nullptr) {
                        vktrace_LogError("Create decompressor error.");
                        fclose(tracefp);
                        vktrace_free(traceFile);
                        if (tmpfile) {
                            remove(tmpfile);
                        }
                        return -1;
                    }
                }
                while (true) {
                    uint64_t currentPosition = vktrace_FileLike_GetCurrentPosition(traceFile);
                    vktrace_trace_packet_header* packet = vktrace_read_trace_packet(traceFile);
                    if (!packet) break;

                    if (packet->tracer_id == VKTRACE_TID_VULKAN_COMPRESSED) {
                        ret = decompress_packet(decomp,packet);
                        if (ret != 0) {
                            vktrace_LogError("Decompress packet error.");
                            break;
                        }
                    }

                    if (packet->packet_id >= VKTRACE_TPI_VK_vkApiVersion && packet->packet_id < VKTRACE_TPI_META_DATA) {
                        vktrace_trace_packet_header* pInterpretedHeader = interpret_trace_packet_vk(packet);
                        if (g_params.simpleDumpFile) {
                            dump_packet_brief(*pSimpleDumpFile, frameNumber, pInterpretedHeader, currentPosition);
                        }
                        if (g_params.fullDumpFile) {
                            dump_packet(pInterpretedHeader);
                        }
                        switch (pInterpretedHeader->packet_id) {
                            case VKTRACE_TPI_VK_vkQueuePresentKHR: {
                                frameNumber++;
                                if (g_params.simpleDumpFile && change_dump_file_name(g_params.simpleDumpFile, frameNumber)) {
                                    fileOutput.close();
                                    fileOutput.open(g_dump_file_name);
                                    buf = fileOutput.rdbuf();
                                    pSimpleDumpFile->rdbuf(buf);
                                }
                                if (g_params.fullDumpFile && change_dump_file_name(g_params.fullDumpFile, frameNumber)) {
                                    reset_dump_file_name(g_dump_file_name);
                                }

                            } break;
                            case VKTRACE_TPI_VK_vkGetPhysicalDeviceProperties: {
                                if (!hideBriefInfo) {
                                    if (deviceApiVersion == UINT32_MAX) {
                                        packet_vkGetPhysicalDeviceProperties* pPacket =
                                            (packet_vkGetPhysicalDeviceProperties*)(pInterpretedHeader->pBody);
                                        deviceApiVersion = pPacket->pProperties->apiVersion;
                                        memcpy(deviceName, pPacket->pProperties->deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
                                    }
                                }
                            } break;
                            case VKTRACE_TPI_VK_vkCreateInstance: {
                                if (!hideBriefInfo) {
                                    packet_vkCreateInstance* pPacket = (packet_vkCreateInstance*)(pInterpretedHeader->pBody);
                                    if (pPacket->pCreateInfo->pApplicationInfo) {
                                        if (pApplicationName == NULL && pEngineName == NULL) {
                                            if (pPacket->pCreateInfo->pApplicationInfo->pApplicationName) {
                                                size_t applicationNameLen =
                                                    strlen(pPacket->pCreateInfo->pApplicationInfo->pApplicationName);
                                                pApplicationName = (char*)malloc(applicationNameLen + 1);
                                                memcpy(pApplicationName, pPacket->pCreateInfo->pApplicationInfo->pApplicationName,
                                                   applicationNameLen);
                                                pApplicationName[applicationNameLen] = '\0';
                                            }
                                            applicationVersion = pPacket->pCreateInfo->pApplicationInfo->applicationVersion;
                                            if (pPacket->pCreateInfo->pApplicationInfo->pEngineName) {
                                                size_t engineNameLen = strlen(pPacket->pCreateInfo->pApplicationInfo->pEngineName);
                                                pEngineName = (char*)malloc(engineNameLen + 1);
                                                memcpy(pEngineName, pPacket->pCreateInfo->pApplicationInfo->pEngineName, engineNameLen);
                                                pEngineName[engineNameLen] = '\0';
                                            }
                                            engineVersion = pPacket->pCreateInfo->pApplicationInfo->engineVersion;
                                        }
                                        if (appApiVersion == UINT32_MAX) {
                                            appApiVersion = pPacket->pCreateInfo->pApplicationInfo->apiVersion;
                                        }
                                    }
                                }
                            } break;
                            default:
                                break;
                        }
                    }
                    vktrace_delete_trace_packet_no_lock(&packet);
                }
                if (decomp != nullptr) {
                    delete decomp;
                }
                if (!hideBriefInfo) {
                    if (deviceApiVersion != UINT32_MAX) {
                        cout << setw(COLUMN_WIDTH) << left << "Device Name:" << deviceName << endl;
                        cout << setw(COLUMN_WIDTH) << left << "Device API Ver:" << dec << VK_VERSION_MAJOR(deviceApiVersion) << "." << dec
                             << VK_VERSION_MINOR(deviceApiVersion) << "." << dec << VK_VERSION_PATCH(deviceApiVersion) << endl;
                    }
                    if (appApiVersion != UINT32_MAX) {
                        cout << setw(COLUMN_WIDTH) << left << "App API Ver:" << dec << VK_VERSION_MAJOR(appApiVersion) << "." << dec
                             << VK_VERSION_MINOR(appApiVersion) << "." << dec << VK_VERSION_PATCH(appApiVersion) << endl;
                    }
                    if (pApplicationName) {
                        cout << setw(COLUMN_WIDTH) << left << "App Name:" << pApplicationName << endl;
                        free(pApplicationName);
                        pApplicationName = NULL;
                    } else {
                        cout << setw(COLUMN_WIDTH) << left << "App Name:"
                             << "NULL" << endl;
                    }
                    cout << setw(COLUMN_WIDTH) << left << "App Ver:" << applicationVersion << endl;
                    if (pEngineName) {
                        cout << setw(COLUMN_WIDTH) << left << "Engine Name:" << pEngineName << endl;
                        free(pEngineName);
                        pEngineName = NULL;
                    } else {
                        cout << setw(COLUMN_WIDTH) << left << "Engine Name:"
                             << "NULL" << endl;
                    }
                    cout << setw(COLUMN_WIDTH) << left << "Engine Ver:" << engineVersion << endl;
                    cout << setw(COLUMN_WIDTH) << left << "Frames:" << frameNumber << endl;
                }
            }
            if (g_params.simpleDumpFile && strcmp(g_params.simpleDumpFile, "STDOUT") && strcmp(g_params.simpleDumpFile, "stdout")) {
                if (pSimpleDumpFile != nullptr) delete pSimpleDumpFile;
                fileOutput.close();
            }
            if (g_params.fullDumpFile) {
                remove(DUMP_SETTING_FILE);
            }
        }
    } else {
        cout << "Error: Fail to read file header !" << endl;
        ret = -1;
    }

    fclose(tracefp);
    vktrace_free(traceFile);
    if (!ret && tmpfile) {
        remove(tmpfile);
    }

    return ret;
}
