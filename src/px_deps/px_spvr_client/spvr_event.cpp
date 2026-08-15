//
// Created by RGAA on 23/01/2026.
//

#include "spvr_event.h"

#include <nlohmann/json.hpp>

using namespace nlohmann;

namespace tc
{

    static void ToJson(json& j, const SpvrEvent& e) {
        j = json{
            {"event_id", e.event_id_},
            {"total", e.total_},
            {"timestamp", e.timestamp_},
            {"readable_timestamp", e.readable_timestamp_},
            {"event_type", e.event_type_},
            {"device_id", e.device_id_},
            {"device_ip", e.device_ip_},
            {"device_name", e.device_name_},
            {"message", e.message_},

            {"uid", e.user_id_},
            {"user_name", e.user_name_},
            {"log_path", e.log_path_},
            {"cpu_usage", e.cpu_usage_},
            {"memory_usage", e.mem_usage_},
            {"disk_usage", e.disk_usage_},
            {"disk_path", e.disk_path_},
            {"gpu_usage", e.gpu_usage_},
            {"gpu_id", e.gpu_id_},
            {"gpu_name", e.gpu_name_}
        };
    }

    static void FromJson(const json& j, SpvrEvent& e) {
        j.at("event_id").get_to(e.event_id_);
        j.at("total").get_to(e.total_);
        j.at("timestamp").get_to(e.timestamp_);
        j.at("readable_timestamp").get_to(e.readable_timestamp_);
        j.at("event_type").get_to(e.event_type_);
        j.at("device_id").get_to(e.device_id_);
        j.at("device_ip").get_to(e.device_ip_);
        j.at("device_name").get_to(e.device_name_);
        j.at("message").get_to(e.message_);

        j.at("uid").get_to(e.user_id_);
        j.at("user_name").get_to(e.user_name_);
        j.at("log_path").get_to(e.log_path_);
        j.at("cpu_usage").get_to(e.cpu_usage_);
        j.at("memory_usage").get_to(e.mem_usage_);
        j.at("disk_usage").get_to(e.disk_usage_);
        j.at("disk_path").get_to(e.disk_path_);
        j.at("gpu_usage").get_to(e.gpu_usage_);
        j.at("gpu_id").get_to(e.gpu_id_);
        j.at("gpu_name").get_to(e.gpu_name_);
    }

    std::string SpvrEvent::AsJson() {
        nlohmann::json j;
        ToJson(j, *this);
        return j.dump();
    }


    std::shared_ptr<SpvrEvent> SpvrEvent::CpuOverload(const std::string& device_id,
                                            const std::string& device_ip,
                                            const std::string& device_name,
                                            const std::string& uid,
                                            const std::string& username,
                                            int cpu_usage) {
        auto event = std::make_shared<SpvrEvent>();
        event->event_type_ = "cpu";
        event->device_id_ = device_id;
        event->device_ip_ = device_ip;
        event->device_name_ = device_name;
        event->user_id_ = uid;
        event->user_name_ = username;
        event->cpu_usage_ = cpu_usage;
        return event;
    }

    std::shared_ptr<SpvrEvent> SpvrEvent::MemoryOverload(const std::string& device_id,
                                                    const std::string& device_ip,
                                                    const std::string& device_name,
                                                    const std::string& uid,
                                                    const std::string& username,
                                                    int mem_usage_) {
        auto event = std::make_shared<SpvrEvent>();
        event->event_type_ = "memory";
        event->device_id_ = device_id;
        event->device_ip_ = device_ip;
        event->device_name_ = device_name;
        event->user_id_ = uid;
        event->user_name_ = username;
        event->mem_usage_ = mem_usage_;
        return event;
    }

    std::shared_ptr<SpvrEvent> SpvrEvent::DiskOverload(const std::string& device_id,
                                                const std::string& device_ip,
                                                const std::string& device_name,
                                                const std::string& uid,
                                                const std::string& username,
                                                int disk_usage,
                                                const std::string& disk_path) {
        auto event = std::make_shared<SpvrEvent>();
        event->event_type_ = "disk";
        event->device_id_ = device_id;
        event->device_ip_ = device_ip;
        event->device_name_ = device_name;
        event->user_id_ = uid;
        event->user_name_ = username;
        event->disk_usage_ = disk_usage;
        event->disk_path_ = disk_path;
        return event;
    }

    std::shared_ptr<SpvrEvent> SpvrEvent::GpuOverload(const std::string& device_id,
                                                  const std::string& device_ip,
                                                  const std::string& device_name,
                                                  const std::string& uid,
                                                  const std::string& username,
                                                  int gpu_usage,
                                                  const std::string& gpu_id,
                                                  const std::string& gpu_name) {
        auto event = std::make_shared<SpvrEvent>();
        event->event_type_ = "gpu";
        event->device_id_ = device_id;
        event->device_ip_ = device_ip;
        event->device_name_ = device_name;
        event->user_id_ = uid;
        event->user_name_ = username;
        event->gpu_usage_ = gpu_usage;
        event->gpu_id_ = gpu_id;
        event->gpu_name_ = gpu_name;
        return event;
    }

}
