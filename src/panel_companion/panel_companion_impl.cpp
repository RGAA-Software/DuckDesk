//
// Created by RGAA on 6/08/2025.
//

#include "panel_companion_impl.h"
#include <QApplication>
#include "spvr/auth_manager.h"
#include "spvr/spvr_setting.h"
#include "tc_common_new/log.h"
#include "tc_common_new/thread.h"
#include "tc_common_new/tc_aes.h"
#include "tc_common_new/md5.h"
#include "tc_common_new/shared_preference.h"
#include "crypto/auth_aes.h"
#include "tc_3rdparty/json/json.hpp"
#include "tc_common_new/string_util.h"

using namespace nlohmann;

void* GetInstance() {
    static tc::PanelCompanionImpl impl;
    return (void*)&impl;
}

namespace tc
{

    PanelCompanionImpl::~PanelCompanionImpl() {

    }

    bool PanelCompanionImpl::Init() {
        std::string base_path = ".";
        Logger::InitLog(base_path + "/gr_logs/panel_companion.log", true);
        LOGI("PanelCompanion Init");

        net_thread_ = Thread::Make("companion_net", 1024);
        net_thread_->Poll();

        sp_ = std::make_shared<SharedPreference>();
        auto sp_dir = qApp->applicationDirPath() + "/gr_data";
        if (!sp_->Init(sp_dir.toStdString(), "panel_companion.dat")) {
            //QMessageBox::critical(nullptr, "Error", "You may already run a instance.");
            return -1;
        }

        spvr_settings_ = SpvrSettings::Instance();
        auth_mgr_ = std::make_shared<AuthManager>(this);
        auth_mgr_->LoadFromStorage();
        return true;
    }

    void PanelCompanionImpl::OnTimer100ms() {

    }

    void PanelCompanionImpl::OnTimer1S() {

    }

    void PanelCompanionImpl::OnTimer5S() {
        auth_mgr_->OnTimer5S();
    }

    void PanelCompanionImpl::UpdateSpvrServerConfig(const std::string &host, int port) {
        spvr_settings_->UpdateServerConfig(host, port);
    }

    std::shared_ptr<Authorization> PanelCompanionImpl::RequestAuth() {
        return auth_mgr_->RequestAuth();
    }

    std::shared_ptr<Authorization> PanelCompanionImpl::GetAuth() {
        return auth_mgr_->GetAuth();
    }

    ///

    void PanelCompanionImpl::PostNetTask(std::function<void()> &&task) {
        net_thread_->Post(std::move(task));
    }

    bool PanelCompanionImpl::EncQRCode(std::string origin_content, std::vector<uint8_t>& cipher_data) {
        const std::string user_key = MD5::Hex("U1J892%$m5s");
        std::string key = user_key.substr(0, 16);
        std::string iv = user_key.substr(user_key.length() - 16);
        return AesEncryptPcks7Cbc128(reinterpret_cast<const unsigned char*>(origin_content.c_str()), origin_content.size(),
            reinterpret_cast<const unsigned char*>(key.c_str()), reinterpret_cast<const unsigned char*>(iv.c_str()), cipher_data);
    }

    std::shared_ptr<SharedPreference> PanelCompanionImpl::GetSP() {
        return sp_;
    }

    void PanelCompanionImpl::UpdateCurrentCpuFrequency(float freq) {
        current_cpu_frequency_ = freq;
    }

    std::shared_ptr<SysInfo> PanelCompanionImpl::ParseHardwareInfo(const std::string& info) {
        std::string json_info;
        try {
            json_info = AuthAes::AesDecrypt(info, AES_DEPLOY_AUTH);
            LOGI("Decoded: {}", json_info);
        }
        catch(std::exception& e) {
            LOGE("Decoded error: {}", e.what());
            return nullptr;
        }

        try {
            auto value = std::make_shared<SysInfo>();
            auto obj = json::parse(json_info);
            // CPU
            if (obj.contains("cpu")) {
                auto cpu_obj = obj["cpu"];
                // usage
                value->cpu_.usage_ = cpu_obj["usage"].get<float>();
                // vendor
                value->cpu_.vendor_ = cpu_obj["vendor"].get<std::string>();
                //brand
                value->cpu_.brand_ = StringUtil::Trim(cpu_obj["brand"].get<std::string>());
                // base_frequency
                value->cpu_.base_frequency_ = cpu_obj["base_frequency"].get<float>();
                // current_frequency
                value->cpu_.current_frequency_ = current_cpu_frequency_;
                // max_frequency
                value->cpu_.max_frequency_ = cpu_obj["max_frequency"].get<float>();
                // cpus
                if (cpu_obj.contains("cpus") && cpu_obj["cpus"].is_array()) {
                    for (const auto& sub : cpu_obj["cpus"]) {
                        value->cpu_.cpus_.push_back(SysSingleCpuInfo {
                            .name_ = sub["name"].get<std::string>(),
                            .usage_ = sub["usage"].get<float>(),
                        });
                    }
                }

            }

            // Memory
            if (obj.contains("mem")) {
                auto mem_obj = obj["mem"];
                // total
                value->mem_.total_ = mem_obj["total"].get<uint64_t>();
                value->mem_.total_gb_ = mem_obj["total_gb"].get<uint64_t>();
                // used
                value->mem_.used_ = mem_obj["used"].get<uint64_t>();
                value->mem_.used_gb_ = mem_obj["used_gb"].get<uint64_t>();
                // available
                value->mem_.available_ = mem_obj["available"].get<uint64_t>();
                value->mem_.available_gb_ = mem_obj["available"].get<uint64_t>();
            }

            // Disks
            if (obj.contains("disks") && obj["disks"].is_array()) {
                for (const auto& disk : obj["disks"]) {
                    value->disks_.push_back(SysDiskInfo {
                        .disk_type_ = disk["disk_type"].get<std::string>(),
                        .mount_on_ = disk["mount_on"].get<std::string>(),
                        .filesystem_ = disk["filesystem"].get<std::string>(),
                        .available_ = disk["available"].get<uint64_t>(),
                        .available_gb_ = disk["available_gb"].get<uint64_t>(),
                        .total_ = disk["total"].get<uint64_t>(),
                        .total_gb_ = disk["total_gb"].get<uint64_t>(),
                    });
                }
            }

            // Network
            if (obj.contains("networks") && obj["networks"].is_array()) {
                for (const auto& network : obj["networks"]) {
                    auto nts = network["ip_networks"];
                    std::vector<SysIpNetwork> ip_networks;
                    for (const auto& nt : nts) {
                        ip_networks.push_back(SysIpNetwork {
                            .addr_ = nt["addr"].get<std::string>(),
                            .prefix_ = nt["prefix"].get<uint8_t>(),
                        });
                    }
                    value->networks_.push_back(SysNetworkInfo {
                        .name_ = network["name"].get<std::string>(),
                        .mac_ = network["mac"].get<std::string>(),
                        .ip_networks_ = ip_networks,
                        .received_data_ = network["received_data"].get<uint64_t>(),
                        .sent_data_ = network["sent_data"].get<uint64_t>(),
                    });
                }
            }

            // OS
            value->os_.sys_name_ = obj["os"]["sys_name"].get<std::string>();
            value->os_.sys_kernel_version_ = obj["os"]["sys_kernel_version"].get<std::string>();
            value->os_.sys_os_version_ = obj["os"]["sys_os_version"].get<std::string>();
            value->os_.sys_os_long_version_ = obj["os"]["sys_os_long_version"].get<std::string>();
            value->os_.sys_host_name_ = obj["os"]["sys_host_name"].get<std::string>();
            value->os_.sys_kernel_ = obj["os"]["sys_kernel"].get<std::string>();

            // Uptime
            value->uptime_ = obj["uptime"].get<std::string>();

            // GPU
            if (obj.contains("gpus") && obj["gpus"].is_array()) {
                auto gpus_obj = obj["gpus"];
                for (const auto& gpu : gpus_obj) {
                    value->gpus_.push_back(SysGpuInfo {
                       .brand_ = gpu["brand"].get<std::string>(),
                       .fan_speed_ = gpu["fan_speed"].get<uint32_t>(),
                        .power_limit_ = gpu["power_limit"].get<uint32_t>(),
                        .encoder_utilization_ = gpu["encoder_utilization"].get<uint32_t>(),
                        .gpu_utilization_ = gpu["gpu_utilization"].get<uint32_t>(),
                        .mem_utilization_ = gpu["mem_utilization"].get<uint32_t>(),
                        .temperature_ = gpu["temperature"].get<uint32_t>(),
                        .mem_used_ = gpu["mem_used"].get<uint64_t>(),
                        .mem_used_gb_ = gpu["mem_used_gb"].get<float>(),
                        .mem_total_ = gpu["mem_total"].get<uint64_t>(),
                        .mem_total_gb_ = gpu["mem_total_gb"].get<float>(),
                    });
                }
            }

            auto print_info = to_string(*value.get());
            LOGI("SysInfo: {}", print_info);

            return value;
        }
        catch(std::exception& e) {
            LOGE("parse json failed: {}", e.what());
            return nullptr;
        }
    }

}