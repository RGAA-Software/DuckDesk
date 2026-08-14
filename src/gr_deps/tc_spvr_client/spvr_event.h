//
// Created by RGAA on 23/01/2026.
//

#ifndef GAMMARAYPREMIUM_SPVR_EVENT_H
#define GAMMARAYPREMIUM_SPVR_EVENT_H

#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>
#include <memory>

namespace tc
{

    const std::string kEventType = "event_type";
    const std::string kCpuUsage = "cpu_usage";
    const std::string kMemoryUsage = "memory_usage";
    const std::string kDiskUsage = "disk_usage";
    const std::string kDiskPath = "disk_path";
    const std::string kGpuUsage = "gpu_usage";
    const std::string kGpuId = "gpu_id";
    const std::string kGpuName = "gpu_name";

    class SpvrEvent {
    public:
        static std::shared_ptr<SpvrEvent> CpuOverload(const std::string& device_id,
                                                      const std::string& device_ip,
                                                      const std::string& device_name,
                                                      const std::string& uid,
                                                      const std::string& username,
                                                      int cpu_usage);

        static std::shared_ptr<SpvrEvent> MemoryOverload(const std::string& device_id,
                                                         const std::string& device_ip,
                                                         const std::string& device_name,
                                                         const std::string& uid,
                                                         const std::string& username,
                                                         int mem_usage);

        static std::shared_ptr<SpvrEvent> DiskOverload(const std::string& device_id,
                                                       const std::string& device_ip,
                                                       const std::string& device_name,
                                                       const std::string& uid,
                                                       const std::string& username,
                                                       int disk_usage,
                                                       const std::string& disk_path);

        static std::shared_ptr<SpvrEvent> GpuOverload(const std::string& device_id,
                                                      const std::string& device_ip,
                                                      const std::string& device_name,
                                                      const std::string& uid,
                                                      const std::string& username,
                                                      int gpu_usage,
                                                      const std::string& gpu_id,
                                                      const std::string& gpu_name);

        std::string AsJson();

    public:
        std::string event_id_;
        uint64_t total_;
        int64_t timestamp_;
        std::string readable_timestamp_;
        std::string event_type_;
        std::string device_id_;
        std::string device_ip_;
        std::string device_name_;
        std::string message_;
        //std::optional<SysInfo> current_sys_info_;
        std::string user_id_;
        std::string user_name_;
        std::unordered_map<std::string, std::string> user_update_values_;
        std::string log_path_;
        uint32_t cpu_usage_;
        uint32_t mem_usage_;
        uint32_t disk_usage_;
        std::string disk_path_;
        uint32_t gpu_usage_;
        std::string gpu_id_;
        std::string gpu_name_;
    };

}

#endif //GAMMARAYPREMIUM_SPVR_EVENT_H