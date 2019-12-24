#include <vkreplay_pipelinecache.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

//#include <vktrace_tracelog.h>

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

    std::pair<void*, size_t> PipelineCacheAccessor::GetPipelineCache(const VkPipelineCache &key) const {
        std::pair<void*, size_t> result({nullptr, 0});
        const auto iter = m_cachemap.find(key);
        if (m_cachemap.end() == iter) {
            return result;
        }
        result = iter->second;
        return result;
    }

    bool PipelineCacheAccessor::WritePipelineCache(const VkPipelineCache &key, void* cache_data, uint32_t cache_size) {
        bool result = false;
        const std::string file_name = GeneratePipelineCacheFileName(key);
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
                vktrace_LogAlways("Pipeline cache data file %s is saved.", full_path);
                free(full_path);
            }
        }
        file.close();

        return result;
    }

    bool PipelineCacheAccessor::LoadPipelineCache(const VkPipelineCache &key) {
        auto cache_result = ReadPipelineCache(key);
        if (nullptr == cache_result.first) {
            return false;
        }

        const std::string file_name = GeneratePipelineCacheFileName(key);
        char *full_path = realpath(file_name.c_str(), NULL);
        if (nullptr != full_path) {
            vktrace_LogAlways("Pipeline cache data %s is loaded.", full_path);
            free(full_path);
        }

        m_cachemap[key] = cache_result;
        return true;
    }

    bool PipelineCacheAccessor::FileExists(const VkPipelineCache &key) const {
        bool result = false;
        const std::string file_name = GeneratePipelineCacheFileName(key);
        char *full_path = realpath(file_name.c_str(), NULL);
        if (nullptr != full_path) {
            result = (0 == access(full_path, F_OK));
            free(full_path);
        }

        return result;
    }

    void PipelineCacheAccessor::SetPipelineCacheRootPath(const std::string &path) {
        assert(!path.empty());
        std::stringstream ss;
        ss << "ls " << path;
        std::string command = ss.str();
        if (0 != system(command.c_str())) {
            // Path does not exist, create path first.
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
            m_cachepath = path;
        }
    }

    void PipelineCacheAccessor::SetPhysicalDeviceProperties(uint32_t vendor_id, uint32_t device_id, const uint8_t *pipelinecache_uuid) {
        m_vendor_id = vendor_id;
        m_device_id = device_id;
        assert(nullptr != pipelinecache_uuid);
        memcpy(m_pipelinecache_uuid, pipelinecache_uuid, VK_UUID_SIZE);
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

    std::list<std::pair<VkDevice, VkPipelineCache>> PipelineCacheAccessor::GetCollectedPacketInfo() const {
        return m_collected_packetinfo_list;
    }

    std::pair<void*, size_t> PipelineCacheAccessor::ReadPipelineCache(const VkPipelineCache &key) {
        std::pair<void*, size_t> result({nullptr, 0});
        const std::string file_name = GeneratePipelineCacheFileName(key);
        std::ifstream file;
        file.open(file_name, std::ios::binary | std::ios::in | std::ios::ate);
        if (!file) {
            vktrace_LogWarning("Pipeline cache data file %s not found!", file_name.c_str());
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

    std::string PipelineCacheAccessor::GeneratePipelineCacheFileName(const VkPipelineCache &key) const {
        const uint64_t key_value = reinterpret_cast<uint64_t>(key);
        const std::string root_path = m_cachepath;

        std::stringstream ss;
        ss << root_path << "/" << key_value << "-" << m_vendor_id << "-" << m_device_id << "-";
        for (uint32_t i = 0; i < VK_UUID_SIZE; i++) {
            ss << static_cast<uint32_t>(m_pipelinecache_uuid[i]);
        }
        ss << ".dat";

        return ss.str();
    }

}