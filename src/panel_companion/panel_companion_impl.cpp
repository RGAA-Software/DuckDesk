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
#include "hw_info_parser.h"

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
        return HWInfoParser::ParseHWInfo(json_info, current_cpu_frequency_);
    }

}