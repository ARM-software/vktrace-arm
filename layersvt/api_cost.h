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

#if defined(__ANDROID__)
#define printf(...) __android_log_print(ANDROID_LOG_DEBUG, "APICOST ", __VA_ARGS__);
#endif

#define API_COST_ENV_VAR "APICOST"
#define OUTPUT_LENGTH 4096

enum class ApiCostFormat {
    Text,
    Html,
    Csv,
};

typedef struct ApiStatInfo_ {
    uint64_t callcount = 0;
    uint64_t costsum = 0;
} ApiStatInfo;

/* the api_cost ouput file is vk_apicost.txt */
class ApiCostInstance {
private:
    inline ApiCostInstance() {
        getPlatformEnvVar(API_COST_ENV_VAR);
        pthread_mutex_init(&s_cost_mutex,nullptr);
        func_stat.clear();
        std::string filePath = "";
        if (output_format == ApiCostFormat::Csv) {
            filePath = output_dir + file_name + ".csv";
        } else if (output_format == ApiCostFormat::Text) {
            filePath = output_dir + file_name + ".txt";
        } else {
            filePath = output_dir + file_name + ".html";
        }
        output_stream.open(filePath.c_str(), std::ofstream::out | std::ostream::trunc);
        printf("--------filePath = %s,file open %s \n",filePath.c_str(),output_stream.is_open() ? ("success!") : ("failed!"));
        if (output_stream.rdstate() == std::ifstream::goodbit && output_format == ApiCostFormat::Html) {
            stream() <<
                "<!doctype html>"
                "<html>"
                    "<head>"
                        "<title>Vulkan API Cost</title>"
                        "<style type='text/css'>"
                        "html {"
                            "background-color: #0b1e48;"
                            "background-image: url('https://vulkan.lunarg.com/img/bg-starfield.jpg');"
                            "background-position: center;"
                            "-webkit-background-size: cover;"
                            "-moz-background-size: cover;"
                            "-o-background-size: cover;"
                            "background-size: cover;"
                            "background-attachment: fixed;"
                            "background-repeat: no-repeat;"
                            "height: 100%;"
                        "}"
                        "#header {"
                            "z-index: -1;"
                        "}"
                        "#header>img {"
                            "position: absolute;"
                            "width: 160px;"
                            "margin-left: -280px;"
                            "tostd::ostream &stream()p: -10px;"
                            "left: 50%;"
                        "}"
                        "#header>h1 {"
                            "font-family: Arial, 'Helvetica Neue', Helvetica, sans-serif;"
                            "font-size: 44px;"
                            "font-weight: 200;"
                            "text-shadow: 4px 4px 5px #000;"
                            "color: #eee;"
                            "position: absolute;"
                            "width: 400px;"
                            "margin-left: -80px;"
                            "top: 8px;"
                            "left: 50%;"
                        "}"
                        "body {"
                            "font-family: Consolas, monaco, monospace;"
                            "font-size: 14px;"
                            "line-height: 20px;"
                            "color: #eee;"
                            "height: 100%;"
                            "margin: 0;"
                            "overflow: hidden;"
                        "}"
                        "#wrapper {"
                            "background-color: rgba(0, 0, 0, 0.7);"
                            "border: 1px solid #446;"
                            "box-shadow: 0px 0px 10px #000;"
                            "padding: 8px 12px;"
                            "display: inline-block;"
                            "position: absolute;"
                            "top: 80px;"
                            "bottom: 25px;"
                            "left: 50px;"
                            "right: 50px;"
                            "overflow: auto;"
                        "}"
                        "details>*:not(summary) {"
                            "margin-left: 22px;"
                        "}"
                        "summary:only-child {"
                          "display: block;"
                          "padding-left: 15px;"
                        "}"
                        "details>summary:only-child::-webkit-details-marker {"
                            "display: none;"
                            "padding-left: 15px;"
                        "}"
                        ".var, .type, .val {"
                            "display: inline;"
                            "margin: 0 6px;"
                        "}"
                        ".type {"
                            "color: #acf;"
                        "}"
                        ".val {"
                            "color: #afa;"
                            "text-align: right;"
                        "}"
                        ".thd {"
                            "color: #888;"
                        "}"
                        "</style>"
                    "</head>"
                    "<body>"
                        "<div id='header'>"
                            "<img src='https://lunarg.com/wp-content/uploads/2016/02/LunarG-wReg-150.png' />"
                            "<h1>Vulkan API Cost</h1>"
                        "</div>"
                        "<div id='wrapper'>";
        }
    }

    inline ~ApiCostInstance() {
        if (output_stream.rdstate() == std::ifstream::goodbit) {
            if (output_format == ApiCostFormat::Csv) {
                char costinfo[256] = {0};
                sprintf(costinfo,"function,count,time(us)\r\n");
                cost_string = cost_string + costinfo;
                for(auto e: func_stat) {
                    memset(costinfo,0,sizeof(costinfo));
                    sprintf(costinfo,"%s,%lu,%lu\r\n",e.first.c_str(),static_cast<unsigned long>(e.second.callcount),static_cast<unsigned long>(e.second.costsum));
                    cost_string = cost_string + costinfo;
                }
                output_stream.write(cost_string.c_str(),cost_string.size());
            } else if (output_format == ApiCostFormat::Text) {
                output_stream.write(cost_string.c_str(),cost_string.size());
            } else {
                stream() << "</div></body></html>";
                stream().flush();
            }
            output_stream.close();
        }
        func_stat.clear();
        pthread_mutex_destroy(&s_cost_mutex);
    }

    inline std::ostream &stream() const {
        return *(std::ofstream *)&output_stream;
    }

    inline std::string toLowerString(const std::string &value) {
        std::string lower_value = value;
        std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
        return lower_value;
    }

    void writeCsv(std::string& functionname,uint64_t& cost) {
        if (frame_count >= frame_range[0] && frame_count <= frame_range[1]) {
            apiStat(functionname,cost);
        }
    }

    void writeTxt(uint32_t& threadid,std::string& functionname,uint64_t& cost) {
        if (frame_count >= frame_range[0] && frame_count <= frame_range[1]) {
            char costinfo[256] = {0};
            sprintf(costinfo,"frameid = %-7lu threadid = %-12u funcname = %-48s cost = %-6lu us \r\n",\
                static_cast<unsigned long>(frame_count), threadid,functionname.c_str(),static_cast<unsigned long>(cost));
            cost_string = cost_string + costinfo;
        }
        if (cost_string.size() >= OUTPUT_LENGTH) {
            output_stream.write(cost_string.c_str(),cost_string.size());
            cost_string = "";
        }
    }

    void writeHtml(uint32_t& threadid,std::string& functionname,uint64_t& cost) {
        // <summary><div class='var'>frameid = 0 threadid = 0 funcname = vkWaitForFences cost = 0</div></summary>
        if (frame_count >= frame_range[0] && frame_count <= frame_range[1]) {
            stream() << "<summary><div class='var'>";
            stream() << "frameid = " << frame_count << "    threadid = " << threadid << "    funcname = " \
                << functionname << "    cost = " << cost << "    us \r\n";
            stream() << "</div></summary>";
        }
        long pos = stream().tellp();
        if (pos >= OUTPUT_LENGTH) {
            stream().flush();
        }
    }

    void inline apiStat(std::string& functionname,uint64_t& cost) {
        func_stat[functionname].callcount++;
        func_stat[functionname].costsum += cost;
    }

public:
    uint64_t getCurrentTime() {
        pthread_mutex_lock(&s_cost_mutex);
        struct timeval time = {};
        gettimeofday(&time,nullptr);
        uint64_t elapse = time.tv_sec * 1000000 + time.tv_usec;
        pthread_mutex_unlock(&s_cost_mutex);
        return elapse;
    }

    void nextFrame() {
        pthread_mutex_lock(&s_cost_mutex);
        frame_count++;
        pthread_mutex_unlock(&s_cost_mutex);
    }

    void writeFile(std::string functionname,uint64_t cost) {
        pthread_mutex_lock(&s_cost_mutex);
        if (output_stream.rdstate() != std::ifstream::goodbit) {
            pthread_mutex_unlock(&s_cost_mutex);
            return ;
        }
        uint32_t threadid = static_cast<uint32_t>(pthread_self());
        if (output_format == ApiCostFormat::Csv) {
            writeCsv(functionname,cost);
        } else if (output_format == ApiCostFormat::Text) {
            writeTxt(threadid,functionname,cost);
        } else {
            writeHtml(threadid,functionname,cost);
        }
        pthread_mutex_unlock(&s_cost_mutex);
    }

    void getPlatformEnvVar(const std::string &varName) {
        /*get output file directory,format,frame range,for example
        * APICOST = path=d:/test/,format=Text,range=1,1000000
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
            printf("-------------environment variable result = %s,length = %d \n",result,static_cast<int>(count));
            if (count > 0) {
                varValue = std::string(result, count);
            }
        } else {
            printf("-------environment variable Name: %s open failed \n",varName.c_str());
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
        printf("-------environment variable = %s\n",envString.c_str());
        //parse output_dir
        std::string findString = "path=";
        int offset = findString.size();
        auto pathBeginPos = envString.find(findString);
        if (pathBeginPos != std::string::npos) {
            auto pathEndPos = envString.find(",",pathBeginPos + offset);
            if (pathEndPos != std::string::npos) {
                output_dir = envString.substr(pathBeginPos + offset, pathEndPos - pathBeginPos - offset);
            } else {
                output_dir = envString.substr(pathBeginPos + offset);
            }
            output_dir = output_dir + "/";
        }

        //parse output_format
        findString = "format=";
        offset = findString.size();
        std::string formatString = "";
        auto formatBeginPos = envString.find(findString);
        if (formatBeginPos != std::string::npos) {
            auto formatEndPos = envString.find(",",formatBeginPos + offset);
            if (formatEndPos != std::string::npos) {
                formatString = envString.substr(formatBeginPos + offset, formatEndPos - formatBeginPos - offset);
            } else {
                formatString = envString.substr(formatBeginPos + offset);
            }
        }
        std::string temp = toLowerString(formatString);
        if (temp == "html")
        {
            output_format = ApiCostFormat::Html;
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
                frame_range[0] = strtoul(minFrame.c_str(),nullptr,0);
                std::string maxFrame = rangeString.substr(commaPos + 1);
                frame_range[1] = strtoul(maxFrame.c_str(),nullptr,0);
            }
        }
    }

    static inline ApiCostInstance &current() { return s_cost_instance; }

private:
    static ApiCostInstance s_cost_instance;
    static pthread_mutex_t s_cost_mutex;
    std::map<std::string,ApiStatInfo> api_statinfo;
    std::string output_dir = "./";
    std::string file_name = "vk_apicost";
    ApiCostFormat output_format = ApiCostFormat::Csv;
    std::string cost_string = "";
    std::ofstream output_stream;
    std::map<std::string,ApiStatInfo> func_stat;
    uint64_t frame_range[2] = {0,ULONG_MAX};
    uint64_t frame_count = 0;
};

ApiCostInstance ApiCostInstance::s_cost_instance;
pthread_mutex_t ApiCostInstance::s_cost_mutex;