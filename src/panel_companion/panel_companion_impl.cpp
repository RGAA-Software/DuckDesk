//
// Created by RGAA on 6/08/2025.
//

#include "panel_companion_impl.h"
#include "spvr/auth_manager.h"
#include "spvr/spvr_setting.h"
#include "tc_common_new/log.h"
#include "tc_common_new/thread.h"

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

        spvr_settings_ = SpvrSettings::Instance();
        auth_mgr_ = std::make_shared<AuthManager>(this);
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
        auth_mgr_->RequestAuth();
    }

    ///

    void PanelCompanionImpl::PostNetTask(std::function<void()> &&task) {
        net_thread_->Post(std::move(task));
    }
}