//
// Created by RGAA on 6/08/2025.
//

#include "panel_companion_impl.h"
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include "spvr/auth_manager.h"
#include "spvr/spvr_setting.h"
#include "tc_common_new/log.h"
#include "tc_common_new/thread.h"
#include "tc_common_new/http_client.h"
#include "tc_common_new/tc_aes.h"
#include "tc_common_new/md5.h"
#include "tc_common_new/folder_util.h"
#include "tc_common_new/shared_preference.h"
#include "crypto/auth_aes.h"
#include "tc_3rdparty/json/json.hpp"
#include "hw_info/hw_info_parser.h"
#include "spvr/spvr_access_info_parser.h"
#include "version_config.h"
#include "stat/stat_manager.h"
#include "spvr/auth_defs.h"

using namespace nlohmann;

namespace tc
{

    PanelCompanionImpl::~PanelCompanionImpl() {

    }

    bool PanelCompanionImpl::Init() {
        auto base_path = FolderUtil::GetProgramDataPath();
        auto log_path = std::format(L"{}/gr_logs/panel_companion.log", base_path);
        Logger::InitLog(log_path, true);
        LOGI("PanelCompanion Init");

        net_thread_ = Thread::Make("companion_net", 1024);
        net_thread_->Poll();

        sp_ = std::make_shared<SharedPreference>();
        auto sp_dir = base_path + L"/gr_data";
        if (!sp_->Init(sp_dir, "panel_companion.dat")) {
            //QMessageBox::critical(nullptr, "Error", "You may already run a instance.");
            return false;
        }

        spvr_settings_ = SpvrSettings::Instance();
        // auth
        auth_mgr_ = std::make_shared<AuthManager>(this);
        auth_mgr_->LoadFromStorage();

        // stat
        stat_mgr_ = std::make_shared<StatManager>(this);

        //
        return true;
    }

    void PanelCompanionImpl::OnTimer100ms() {

    }

    void PanelCompanionImpl::OnTimer1S() {

    }

    void PanelCompanionImpl::OnTimer5S() {
        auth_mgr_->OnTimer5S();
        this->PostNetTask([this]() {
            ReportWorkingAuthIfNeeded();
        });

        this->PostNetTask([this]() {
            ReportOpenUpIfNeeded();
        });
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

    bool PanelCompanionImpl::IsAuthFree() {
        const auto auth = GetAuth();
        return auth->IsFree();
    }

    bool PanelCompanionImpl::IsAuthPersonal() {
        const auto auth = GetAuth();
        return auth->IsPersonal();
    }

    bool PanelCompanionImpl::IsAuthEnterprise() {
        auto auth = GetAuth();
        return auth->IsEnterprise();
    }

    bool PanelCompanionImpl::IsAuthValid() {
        return auth_mgr_->IsAuthValid();
    }

    ///

    void PanelCompanionImpl::PostNetTask(std::function<void()> &&task) const {
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

    float PanelCompanionImpl::GetCurrentCpuFrequency() {
        return current_cpu_frequency_;
    }

    std::shared_ptr<SysInfo> PanelCompanionImpl::ParseHardwareInfo(const std::string& info) {
        if (info.empty()) {
            return nullptr;
        }
        std::string json_info;
        try {
            json_info = AuthAes::AesDecrypt(info, AES_DEPLOY_AUTH);
            //LOGI("Decoded: {}", json_info);
        }
        catch(std::exception& e) {
            LOGE("Decoded error: {}, info: {}", e.what(), info);
            return nullptr;
        }
        auto sys_info = HWInfoParser::ParseHWInfo(json_info, current_cpu_frequency_);
        sys_info->raw_json_msg_ = json_info;

        // save it
        sys_info_ = sys_info;

        return sys_info;
    }

    std::shared_ptr<SpvrAccessInfo> PanelCompanionImpl::ParseSpvrAccessInfo(const std::string& info) {
        std::string real_info;
        try {
            std::string head = "spvr://access##";
            real_info = info.substr(head.size(), info.size());
            auto json_info = AuthAes::AesDecrypt(real_info, AES_DEPLOY_AUTH);
            auto parsed_info = SpvrAccessInfoParser::ParseInfo(json_info);
            if (!parsed_info) {
                LOGI("Parsed AccessInfo is null");
                return nullptr;
            }
            if (!parsed_info->IsValid()) {
                LOGI("Parsed AccessInfo invalid: {}", json_info);
                return nullptr;
            }
            return parsed_info;
        }
        catch(std::exception& e) {
            LOGE("Decoded error: {}, info: {}", e.what(), real_info);
            return nullptr;
        }
    }

    void PanelCompanionImpl::JumpToGithub() {
        QDesktopServices::openUrl(QUrl("https://github.com/RGAA-Software/GammaRay"));
    }

    // version1 == version2 return 0;  version1 > version2 return 1; version1 < version2 return -1;
    int CompareVersion(const QString& version1, const QString& version2) {
        QStringList parts1 = version1.split('.');
        QStringList parts2 = version2.split('.');
        int numParts = qMax(parts1.size(), parts2.size());
        for (int i = 0; i < numParts; ++i) {
            int part1 = (i < parts1.size()) ? parts1[i].toInt() : 0;
            int part2 = (i < parts2.size()) ? parts2[i].toInt() : 0;

            if (part1 < part2) {
                return -1;
            }
            else if (part1 > part2) {
                return 1;
            }
        }
        return 0;
    }

    bool PanelCompanionImpl::HasUpdateForOffSite() {
        auto client = HttpClient::MakeSSL("godesk.uk", 443, "/api/v1/query/product/version", 2000);
        auto resp = client->Request();
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("response failed: {}", resp.status);
            return false;
        }
        try { 
            nlohmann::json json = nlohmann::json::parse(resp.body);
            if (!json.contains("data")) {
                LOGE("json parse error: miss data field");
                return false;
            }
            auto data_obj = json["data"];
            if (!data_obj.contains("version")) {
                LOGE("json parse error: miss version field");
                return false;
            }
            auto version = data_obj["version"].get<std::string>();
            std::string cur_version_str = PROJECT_VERSION;
            int res = CompareVersion(QString::fromStdString(version), QString::fromStdString(cur_version_str));
            LOGI("Current version: {}, offsite version: {}, res: {}", cur_version_str, version, res);
            return res > 0;
        } catch (std::exception& e) {
            LOGE("json parse error: {}", e.what());
            return false;
        }
    }

    // In network thread
    void PanelCompanionImpl::ReportWorkingAuthIfNeeded() {
        if (reported_working_auth_ || !sys_info_.HasValue() || !stat_mgr_) {
            return;
        }
        if (stat_mgr_->ReportWorkingAuth(sys_info_.Clone())) {
            reported_working_auth_ = true;
            LOGI("Report working auth!");
        }
    }

    void PanelCompanionImpl::ReportOpenUpIfNeeded() {
        if (reported_open_up_ || !sys_info_.HasValue() || !stat_mgr_) {
            return;
        }
        if (stat_mgr_->ReportOpenUp(sys_info_.Clone())) {
            reported_open_up_ = true;
            LOGI("Report OpenUp!");
        }
    }

    std::string PanelCompanionImpl::GetAuthId() const {
        return sp_->Get(kAuthId);
    }

    std::string PanelCompanionImpl::GetAuthName() const {
        return sp_->Get(kAuthName);
    }

    std::string PanelCompanionImpl::GetMachineCode() const {
        return sp_->Get(kAuthMachineCode);
    }

    std::string PanelCompanionImpl::GetAppkey() const {
        return sp_->Get(kAuthAppkey);
    }

    std::string PanelCompanionImpl::GetDeviceId() const {
        return device_id_;
    }

    void PanelCompanionImpl::UpdateDeviceId(const std::string& device_id) {
        device_id_ = device_id;
    }

}