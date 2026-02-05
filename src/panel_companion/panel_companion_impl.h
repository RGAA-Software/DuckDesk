//
// Created by RGAA on 6/08/2025.
//

#ifndef GAMMARAYPREMIUM_PANEL_COMPANION_IMPL_H
#define GAMMARAYPREMIUM_PANEL_COMPANION_IMPL_H

#include "render_panel/companion/panel_companion.h"
#include <memory>
#include <functional>

namespace tc
{

    class Thread;
    class AuthManager;
    class SpvrSettings;
    class SharedPreference;

    class PanelCompanionImpl : public PanelCompanion {
    public:
        ~PanelCompanionImpl() override;

        bool Init() override;

        void OnTimer100ms() override;
        void OnTimer1S() override;
        void OnTimer5S() override;

        // Spvr
        void UpdateSpvrServerConfig(const std::string &host, int port) override;
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

        // spvr access
        std::shared_ptr<SpvrAccessInfo> ParseSpvrAccessInfo(const std::string& info) override;

        // jump to github
        void JumpToGithub() override;
        bool HasUpdateForOffSite() override;


    public:
        void PostNetTask(std::function<void()>&& task) const;
        std::shared_ptr<SharedPreference> GetSP();

    private:
        SpvrSettings* spvr_settings_ = nullptr;
        std::shared_ptr<AuthManager> auth_mgr_ = nullptr;
        std::shared_ptr<Thread> net_thread_ = nullptr;
        std::shared_ptr<SharedPreference> sp_ = nullptr;
        float current_cpu_frequency_ = 0.0f;
    };

}

extern "C" __declspec(dllexport) void* GetInstance();

#endif //GAMMARAYPREMIUM_PANEL_COMPANION_IMPL_H
