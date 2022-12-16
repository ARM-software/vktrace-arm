/*
 * (C) COPYRIGHT 2019 ARM Limited
 * ALL RIGHTS RESERVED
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     LICENSE-2.0" target="_blank" rel="nofollow">http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <sys/time.h>
#include <pthread.h>
#include <fstream>
#include <string>
#include <sstream>
#include <climits>
#include <algorithm>
#include <map>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "vk_loader_platform.h"
#include "vulkan/vk_layer.h"
#include "vk_layer_config.h"
#include "vk_layer_table.h"
#include "vk_layer_extension_utils.h"
#include "vk_layer_utils.h"
#include "vk_layer_utils.h"
#include "hwcpipe.h"
#include "json/json.h"

#if defined(__ANDROID__)
#define printf(...) __android_log_print(ANDROID_LOG_INFO, "hwcp", __VA_ARGS__);
#endif

#define HWCP_ENV_VAR "hwcprofiler"

class HWCProfilerInstance {
private:
    inline HWCProfilerInstance() {
        getPlatformEnvVar(HWCP_ENV_VAR);
        pthread_mutex_init(&s_hwcp_mutex,nullptr);
        std::string filePath = mOutputDir + mFileName + ".json";
        mOutputStream.open(filePath.c_str(), std::ofstream::out | std::ostream::trunc);
        printf("--------filePath = %s,file open %s \n",filePath.c_str(),mOutputStream.is_open() ? ("success!") : ("failed!"));
    }

    inline ~HWCProfilerInstance() {
        if (mOutputStream.rdstate() == std::ifstream::goodbit) {
            Json::StyledWriter writer;
            std::string recorder = writer.write(mSampleGpuCounters);
            mOutputStream.write(recorder.c_str(), recorder.size());
            mOutputStream.close();
        }
        pthread_mutex_destroy(&s_hwcp_mutex);
    }

    inline std::string toLowerString(const std::string &value) {
        std::string lower_value = value;
        std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
        return lower_value;
    }

public:
    uint64_t getCurrentTime() {
        struct timeval time = {};
        gettimeofday(&time, nullptr);
        uint64_t elapse = time.tv_sec * 1000000 + time.tv_usec;
        return elapse;
    }

    void enableCounters() {
        if (mHWCProfiler.cpu_profiler()) {
            for (hwcpipe::CpuCounter hwcpipeId : mHWCProfiler.cpu_profiler()->supported_counters()) {
                std::string name;
                for (auto &i : hwcpipe::cpu_counter_names) {
                    if (i.second == hwcpipeId) {
                        name = i.first;
                        break;
                    }
                }

                mSupportedCpuCounters[hwcpipeId] = name;
                printf("-------------supported cpu counter id = %u,name = %s \n",(uint32_t)hwcpipeId, name.c_str());
            }
        }

        if (mHWCProfiler.gpu_profiler()) {
            for (hwcpipe::GpuCounter hwcpipeId : mHWCProfiler.gpu_profiler()->supported_counters()) {
                std::string name;
                for (auto &i : hwcpipe::gpu_counter_names) {
                    if (i.second == hwcpipeId) {
                        name = i.first;
                        break;
                    }
                }

                mSupportedGpuCounters[hwcpipeId] = name;
                printf("-------------supported gpu counter id = %u,name = %s \n",(uint32_t)hwcpipeId, name.c_str());
            }
        }

        hwcpipe::CpuCounterSet cpuCounterSet;
        hwcpipe::GpuCounterSet gpuCounterSet;

        for (auto &counter : mSupportedCpuCounters) {
            cpuCounterSet.insert((hwcpipe::CpuCounter)counter.first);
        }

        for (auto &counter : mSupportedGpuCounters) {
            gpuCounterSet.insert((hwcpipe::GpuCounter)counter.first);
        }

        if (cpuCounterSet.size() > 0) {
            mHWCProfiler.set_enabled_cpu_counters(cpuCounterSet);
        }

        if (gpuCounterSet.size() > 0) {
            mHWCProfiler.set_enabled_gpu_counters(gpuCounterSet);
        }
    }

    void startCapture() {
        pthread_mutex_lock(&s_hwcp_mutex);
        printf("-------Start capture hw performance counter\n");
        enableCounters();
        mHWCProfiler.run();
        mHWCProfiler.sample();
        pthread_mutex_unlock(&s_hwcp_mutex);
    }

    void stopCapture() {
        pthread_mutex_lock(&s_hwcp_mutex);
        mHWCProfiler.stop();
        printf("-------Stop capture hw performance counter\n");
        pthread_mutex_unlock(&s_hwcp_mutex);
    }

    void sampleProcess() {
        pthread_mutex_lock(&s_hwcp_mutex);
        auto measurements = mHWCProfiler.sample();
        if (measurements.gpu) {
            if (mFirstGpuSample) {
                Json::Value gpuCounterNames;
                Json::Value gpuCounterOffset;
                uint32_t counterIndex = 0;
                for (const std::pair<hwcpipe::GpuCounter, hwcpipe::Value> &data : *measurements.gpu) {
                    gpuCounterNames[mSupportedGpuCounters[data.first]] = Json::Value(counterIndex);
                    counterIndex++;
                }

                gpuCounterOffset["sampleOffsets"] = gpuCounterNames;
                mSampleGpuCounters["captureGroups"].append(gpuCounterOffset);
                mFirstGpuSample = false;
            }

            Json::Value gpuCounters;
            for (const std::pair<hwcpipe::GpuCounter, hwcpipe::Value> &data : *measurements.gpu) {
                gpuCounters["data"].append(data.second.get<double>());
            }

            gpuCounters["frame"] = Json::Value(getFramesNumber());
            gpuCounters["ts"] = Json::Value(getCurrentTime());

            if (mFrameCount >= mFrameRange[0] && mFrameCount <= mFrameRange[1]) {
                mSampleGpuCounters["captureGroups"][0]["samples"].append(gpuCounters);
            }
        }

        pthread_mutex_unlock(&s_hwcp_mutex);
    }

    void nextFrame() {
        pthread_mutex_lock(&s_hwcp_mutex);
        mFrameCount++;
        pthread_mutex_unlock(&s_hwcp_mutex);
    }

    uint32_t getFramesNumber() {
        return mFrameCount;
    }

    void getPlatformEnvVar(const std::string &varName) {
        /*get output file directory,format,frame range,for example
        * hwcprofiler = path=d:/test/,range=1,1000000
        */
        std::string varValue = "";
        char result[1024] = { 0 };

#ifdef _WIN32
        int bytes = GetEnvironmentVariableA(varName.c_str(), result, 1024 - 1);
        if (bytes > 0) {
            varValue = result;
        }
#elif defined(__ANDROID__)
        std::string command = "getprop debug." + varName;
        FILE *pipe = popen(command.c_str(), "r");
        if (pipe != nullptr) {
            fgets(result, 1024, pipe);
            pclose(pipe);
            size_t count = strcspn(result, "\r\n");
            printf("-------------environment variable result = %s,length = %d \n", result, static_cast<int>(count));
            if (count > 0) {
                varValue = std::string(result, count);
            }
        } else {
            printf("-------environment variable Name: %s open failed \n", varName.c_str());
        }
#else
        const char *ret_value = getenv(varName.c_str());
        if (ret_value != nullptr) {
            varValue = ret_value;
        }
#endif
        parseEnvVar(varValue);
    }

    void parseEnvVar(const std::string& envString) {
        printf("-------environment variable = %s\n", envString.c_str());
        //parse mOutputDir
        std::string findString = "path=";
        int offset = findString.size();
        auto pathBeginPos = envString.find(findString);
        if (pathBeginPos != std::string::npos) {
            auto pathEndPos = envString.find(",",pathBeginPos + offset);
            if (pathEndPos != std::string::npos) {
                mOutputDir = envString.substr(pathBeginPos + offset, pathEndPos - pathBeginPos - offset);
            } else {
                mOutputDir = envString.substr(pathBeginPos + offset);
            }
            mOutputDir = mOutputDir + "/";
        }

        //parse out frame range
        findString = "range=";
        offset = findString.size();
        std::string rangeString = "";
        auto rangeBeginPos = envString.find(findString);
        if (rangeBeginPos != std::string::npos) {
            auto rangeEndPos = envString.find(" ",rangeBeginPos + offset);
            if (rangeEndPos != std::string::npos) {
                rangeString = envString.substr(rangeBeginPos + offset, rangeEndPos - rangeBeginPos - offset);
            } else {
                rangeString = envString.substr(rangeBeginPos + offset);
            }
            auto commaPos = rangeString.find(",");
            if (commaPos != std::string::npos) {
                std::string minFrame = rangeString.substr(0,commaPos);
                mFrameRange[0] = strtoul(minFrame.c_str(),nullptr,0);
                std::string maxFrame = rangeString.substr(commaPos + 1);
                mFrameRange[1] = strtoul(maxFrame.c_str(),nullptr,0);
            }
        }
    }

    static inline HWCProfilerInstance &current() { return s_hwcp_instance; }

private:
    static HWCProfilerInstance s_hwcp_instance;
    static pthread_mutex_t s_hwcp_mutex;
    hwcpipe::HWCPipe mHWCProfiler;
    std::unordered_map<hwcpipe::CpuCounter, std::string, hwcpipe::CpuCounterHash> mSupportedCpuCounters;
    std::unordered_map<hwcpipe::GpuCounter, std::string, hwcpipe::GpuCounterHash> mSupportedGpuCounters;

#if defined(__ANDROID__)
    std::string mOutputDir = "/sdcard/Android/";
#else
    std::string mOutputDir = "./";
#endif

    std::string mFileName = "hw_profiler_counters";
    std::ofstream mOutputStream;
    uint64_t mFrameRange[2] = {0,ULONG_MAX};
    uint32_t mFrameCount = 0;
    Json::Value mSampleGpuCounters;
    bool mFirstGpuSample = true;
};

HWCProfilerInstance HWCProfilerInstance::s_hwcp_instance;
pthread_mutex_t HWCProfilerInstance::s_hwcp_mutex;