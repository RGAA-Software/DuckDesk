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

    class AuthManager;
    class SpvrSettings;
    class Thread;

    class PanelCompanionImpl : public PanelCompanion {
    public:
        ~PanelCompanionImpl() override;

        bool Init() override;

        void OnTimer100ms() override;
        void OnTimer1S() override;
        void OnTimer5S() override;

        // Spvr
        void UpdateSpvrServerConfig(const std::string &host, int port) override;

    public:
        void PostNetTask(std::function<void()>&& task);

    private:
        SpvrSettings* spvr_settings_ = nullptr;
        std::shared_ptr<AuthManager> auth_mgr_ = nullptr;
        std::shared_ptr<Thread> net_thread_ = nullptr;
    };

}

extern "C" __declspec(dllexport) void* GetInstance();

#endif //GAMMARAYPREMIUM_PANEL_COMPANION_IMPL_H
