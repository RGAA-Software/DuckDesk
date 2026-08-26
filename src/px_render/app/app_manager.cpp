//
// Created by RGAA on 2023-12-20.
//

#include "app_manager.h"
#include "rd_context.h"
#include "px_common_new/log.h"
#include "px_steam_manager_new/steam_manager.h"

namespace px
{

    AppManager::AppManager(const std::shared_ptr<RdContext>& ctx) {
        context_ = ctx;
    }

    AppManager::~AppManager() {

    }

    void AppManager::Init() {
        msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kControl);
        state_msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kState);
    }

    bool AppManager::StartProcess() {
        return true;
    }

    bool AppManager::StartProcessWithHook() {
        return true;
    }

    void AppManager::Exit() {
        if (msg_listener_) {
            msg_listener_->UnListenAll();
        }
        if (state_msg_listener_) {
            state_msg_listener_->UnListenAll();
        }
    }

    void AppManager::OnCapturedVideoFrame() {
    }

    void AppManager::CloseCurrentApp() {

    }

}
