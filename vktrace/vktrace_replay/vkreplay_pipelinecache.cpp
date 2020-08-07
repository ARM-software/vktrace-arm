#include <vkreplay_pipelinecache.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <vector>

namespace {

    std::vector<std::string> GetFileList(const std::string &path) {
        std::vector<std::string> list;
        DIR *dp;
        struct dirent *dirp;
        if (nullptr == (dp  = opendir(path.c_str()))) {
            return list;
        }

        struct stat64 st;
        char *full_path = nullptr;
        while ((dirp = readdir(dp)) != NULL) {
            std::string p = path;
            if (0 == stat64(p.append("/").append(dirp->d_name).c_str(), &st)) {
                if (S_ISREG(st.st_mode)) {
                    full_path = realpath(p.c_str(), nullptr);
                    if (nullptr != full_path) {
                        p = full_path;
                        list.push_back(p);
                        free(full_path);
                        full_path = nullptr;
                    }
                }
            }
        }

        closedir(dp);
        return list;
    }

    std::string PipelinecacheUUIDToString(const uint8_t *pipelinecache_uuid) {
        std::stringstream ss_pipelinecache_uuid;
        for (uint32_t i = 0; i < VK_UUID_SIZE; i++) {
            ss_pipelinecache_uuid << static_cast<uint16_t>(pipelinecache_uuid[i]);
        }
        return ss_pipelinecache_uuid.str();
    }

    uint64_t ParsePipelineCacheHandle(const std::string &full_path) {
        const std::size_t pos_begin = full_path.find_last_of("/") + 1;
        if (std::string::npos == pos_begin) {
            return 0;
        }

        const std::size_t pos_end = full_path.find_first_of("-", pos_begin);
        if (std::string::npos == pos_end) {
            return 0;
        }

        const std::string pipelinecache_handle = full_path.substr(pos_begin, pos_end - pos_begin);
        const uint64_t result = std::stoull(pipelinecache_handle.c_str());

        return result;
    }

}

namespace vktrace_replay {

    std::string g_default_pipelinecache_rootpath;
    const std::string& GetDefaultPipelineCacheRootPath() {
        if (g_default_pipelinecache_rootpath.empty()) {
        #ifdef ANDROID
            g_default_pipelinecache_rootpath =  "/sdcard/Android/data/com.example.vkreplay/pipelinecache";
            const bool is_x64 = (8 == sizeof(void*));
            if (!is_x64) {
                char *full_path = realpath("/sdcard/Android/data/com.example.vkreplay32", NULL);
                if (nullptr != full_path) {
                    // vkreplay32 full path exists on disk, use the following path as the pipeline cache path.
                    if (0 == access(full_path, F_OK)) {
                        g_default_pipelinecache_rootpath = "/sdcard/Android/data/com.example.vkreplay32/pipelinecache";
                    }
                    free(full_path);
                    full_path = nullptr;
                }
                // If user uses a flag --abi for apk installing, it may cause the 32 bit vkreplay still be installed
                // under path /sdcard/Android/data/com.example.vkreplay without "32" suffix.
            }
        #else // LINUX
            g_default_pipelinecache_rootpath = "./pipelinecache";
        #endif 
        }

        return g_default_pipelinecache_rootpath;
    }

    PipelineCacheAccessor::PipelineCacheAccessor() {

    }

    PipelineCacheAccessor::~PipelineCacheAccessor() {

        for (auto &info : m_cachemap) {
            if (nullptr != info.second.first) {
                VKTRACE_DELETE(info.second.first);
            }
        }
    }

    std::pair<void*, size_t> PipelineCacheAccessor::GetPipelineCache(const VkPipelineCache &cache_handle) const {
        const uint64_t key = reinterpret_cast<uint64_t>(cache_handle);
        std::pair<void*, size_t> result({nullptr, 0});
        const auto iter = m_cachemap.find(key);
        if (m_cachemap.end() == iter) {
            return result;
        }
        result = iter->second;
        return result;
    }

    bool PipelineCacheAccessor::WritePipelineCache(const VkPipelineCache &cache_handle, void* cache_data, const uint32_t &cache_size, const uint64_t &gpu_info, const uint8_t *pipelinecache_uuid) {
        bool result = false;
        const uint64_t cache_handle_value = reinterpret_cast<uint64_t>(cache_handle);
        const std::string root_path = m_cachepath;
        std::stringstream ss;
        std::string &&uuid = PipelinecacheUUIDToString(pipelinecache_uuid);
        ss << root_path << "/" << cache_handle_value << "-" << gpu_info << "-" << uuid << ".dat";

        const std::string file_name = ss.str();
        std::ofstream file;
        file.open(file_name, std::ios::binary | std::ios::out);
        if (!file) {
            return result;
        }

        file.write(reinterpret_cast<const char*>(cache_data), cache_size);
        if (!file.fail()) {
            result = true;
            char *full_path = realpath(file_name.c_str(), NULL);
            if (nullptr != full_path) {
                vktrace_LogAlways("Pipeline cache data file %s has been saved.", full_path);
                free(full_path);
                full_path = nullptr;
            }
        }
        file.close();

        return result;
    }

    bool PipelineCacheAccessor::LoadPipelineCache(const std::string &full_path) {
        const uint64_t key = ParsePipelineCacheHandle(full_path);
        if (0 == key) {
            // Cannot parse the pipeline cache handler value from the path, the input file name may not
            // a correct cache file for reading.
            vktrace_LogWarning("Pipelinecache data file name format error. Cannot load cache file %s", full_path.c_str());
            return false;
        }

        auto cache_result = ReadPipelineCache(full_path);
        if (nullptr == cache_result.first) {
            return false;
        }

        m_cachemap[key] = cache_result;
        vktrace_LogAlways("Pipeline cache data file %s has been loaded.", full_path.c_str());
        return true;
    }

    void PipelineCacheAccessor::SetPipelineCacheRootPath(const std::string &path) {
        assert(!path.empty());
        char *full_path = realpath(path.c_str(), nullptr);
        if (nullptr == full_path) {
            // Path does not exist, create path first.
            std::stringstream ss;
            ss.str("");
            ss << "mkdir -p " << path;
            std::string command = ss.str();
            if (0 == system(command.c_str())) {
                m_cachepath = path;
            } else {
                m_cachepath = GetDefaultPipelineCacheRootPath();
                vktrace_LogWarning("Path %s is invalid, use default path %s .", path.c_str(), m_cachepath.c_str());
            }
        } else {
            // Path exists, directly assign value.
            m_cachepath = full_path;
            free(full_path);
            full_path = nullptr;
        }
    }

    void PipelineCacheAccessor::CollectPacketInfo(const VkDevice &device, const VkPipelineCache &cache_key) {
        m_collected_packetinfo_list.push_back(std::make_pair(device, cache_key));
    }

    void PipelineCacheAccessor::RemoveCollectedPacketInfo(const VkDevice &device, const VkPipelineCache &cache_key) {
        auto pair = std::make_pair(device, cache_key);
        auto iter = std::find(m_collected_packetinfo_list.begin(), m_collected_packetinfo_list.end(), pair);
        if (iter != m_collected_packetinfo_list.end()) {
            m_collected_packetinfo_list.erase(iter);
        }
    }

    std::string PipelineCacheAccessor::FindFile(const VkPipelineCache &cache_handle) const {
        std::string result;
        std::vector<std::string> &&file_list = GetFileList(m_cachepath);
        if (file_list.empty()) {
            return result;
        }

        const uint64_t cache_handle_value = reinterpret_cast<uint64_t>(cache_handle);
        std::stringstream ss;
        ss << cache_handle_value;
        for (const auto &full_path : file_list) {
            if (std::string::npos == full_path.find_first_of(ss.str())) {
                continue;
            }
            result = full_path;
        }

        return result;
    }

    std::string PipelineCacheAccessor::FindFile(const VkPipelineCache &cache_handle, const uint64_t &gpu_info, const uint8_t *pipelinecache_uuid) const {
        std::string result;
        std::vector<std::string> &&file_list = GetFileList(m_cachepath);
        if (file_list.empty()) {
            return result;
        }

        const uint64_t cache_handle_value = reinterpret_cast<uint64_t>(cache_handle);
        std::stringstream ss_cache_handle;
        std::stringstream ss_gpu_info;
        std::stringstream ss_pipelinecache_uuid;
        ss_cache_handle << cache_handle_value;
        ss_gpu_info << gpu_info;
        std::string &&uuid = PipelinecacheUUIDToString(pipelinecache_uuid);
        for (const auto &full_path : file_list) {
            if (std::string::npos != full_path.find_first_of(ss_cache_handle.str()) &&
                std::string::npos != full_path.find_first_of(ss_gpu_info.str()) &&
                std::string::npos != full_path.find_first_of(uuid)) {
                result = full_path;
                break;
            }
        }

        return result;
    }

    std::list<std::pair<VkDevice, VkPipelineCache>> PipelineCacheAccessor::GetCollectedPacketInfo() const {
        return m_collected_packetinfo_list;
    }

    std::pair<void*, size_t> PipelineCacheAccessor::ReadPipelineCache(const std::string &full_path) {
        std::pair<void*, size_t> result({nullptr, 0});
        std::ifstream file;
        file.open(full_path, std::ios::binary | std::ios::in | std::ios::ate);
        if (!file) {
            vktrace_LogWarning("Pipeline cache data file %s not found!", full_path.c_str());
            return result;
        }

// +-------------------------------------------------------------------------------------------------------------------------------------------------------+
// + Offset | Size         | Meaning                                                                                                                       +
// +      0 |            4 | length in bytes of the entire pipeline cache header written as a stream of bytes, with the least significant byte first       +
// +      4 |            4 | a VkPipelineCacheHeaderVersion value written as a stream of bytes, with the least significant byte first                      +
// +      8 |            4 | a vendor ID equal to VkPhysicalDeviceProperties::vendorID written as a stream of bytes, with the least significant byte first +
// +     12 |            4 | a device ID equal to VkPhysicalDeviceProperties::deviceID written as a stream of bytes, with the least significant byte first +
// +     16 | VK_UUID_SIZE | a pipeline cache ID equal to VkPhysicalDeviceProperties::pipelineCacheUUID                                                    +
// +-------------------------------------------------------------------------------------------------------------------------------------------------------+

        const uint32_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        char *cache_data = VKTRACE_NEW_ARRAY(char, file_size);
        file.read(cache_data, file_size);
        file.close();

        result.first = cache_data;
        result.second = file_size;

        return result;
    }

}