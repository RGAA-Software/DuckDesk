//
// Created by RGAA on 6/08/2025.
//

#ifndef GAMMARAYPREMIUM_PANEL_COMPANION_IMPL_H
#define GAMMARAYPREMIUM_PANEL_COMPANION_IMPL_H

#include "render_panel/companion/panel_companion.h"
#include <memory>
#include <functional>

#include "px_common_new/concurrent_type.h"

namespace px
{

    class Thread;
    class AuthManager;
    class ConsoleSettings;
    class SharedPreference;
    class StatManager;

    class PanelCompanionImpl : public PanelCompanion {
    public:
        ~PanelCompanionImpl() override;

        bool Init() override;

        void OnTimer100ms() override;
        void OnTimer1S() override;
        void OnTimer5S() override;

        // Console
        void UpdateConsoleServerConfig(const std::string &host, int port, bool ssl_enable) override;
        void UpdateAppkey(const std::string& appkey) override;
        std::shared_ptr<Authorization> RequestAuth() override;
        std::shared_ptr<Authorization> GetAuth() override;
        bool IsAuthFree() override;
        bool IsAuthPersonal() override;
        bool IsAuthEnterprise() override;
        bool IsAuthValid() override;

        // enc
        bool EncQRCode(std::string origin_content, std::vector<uint8_t>& cipher_data) override;

        void UpdateCurrentCpuFrequency(float freq) override;
        float GetCurrentCpuFrequency() override;
        std::shared_ptr<SysInfo> ParseHardwareInfo(const std::string& info) override;

        // console access
        std::shared_ptr<ConsoleAccessInfo> ParseConsoleAccessInfo(const std::string& info) override;

        // jump to github
        void JumpToGithub() override;
        bool HasUpdateForOffSite() override;

        void PostNetTask(std::function<void()>&& task) const;
        std::shared_ptr<SharedPreference> GetSP();

        // auth
        std::string GetAuthId() const;
        std::string GetAuthName() const;
        std::string GetMachineCode() const;

        // device id
        void UpdateDeviceId(const std::string& device_id) override;
        std::string GetDeviceId() const;

    private:
        void ReportWorkingAuthIfNeeded();
        void ReportOpenUpIfNeeded();

    private:
        ConsoleSettings* console_settings_ = nullptr;
        std::shared_ptr<AuthManager> auth_mgr_ = nullptr;
        std::shared_ptr<Thread> net_thread_ = nullptr;
        std::shared_ptr<SharedPreference> sp_ = nullptr;
        float current_cpu_frequency_ = 0.0f;
        // report or not...
        std::atomic_bool reported_working_auth_ = false;
        // system info
        px::Mutex<std::shared_ptr<SysInfo>> sys_info_;
        // stat manager
        std::shared_ptr<StatManager> stat_mgr_ = nullptr;
        // device id
        std::string device_id_;
        // has reported open up
        bool reported_open_up_ = false;
    };

}

#endif //GAMMARAYPREMIUM_PANEL_COMPANION_IMPL_H
