//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_CLIENT_CLIPBOARD_PLUGIN_H
#define GAMMARAY_CLIENT_CLIPBOARD_PLUGIN_H

#include "gr_client/plugin_interface/ct_plugin_interface.h"
#include <map>
#include <atomic>
#include <memory>

namespace tc
{

    class ClipboardManager;

    class ClientClipboardPlugin : public ClientPluginInterface {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        bool OnCreate(const tc::ClientPluginParam& param) override;
        bool OnDestroy() override;
        void On1Second() override;
        void OnMessage(std::shared_ptr<Message> msg) override;
        void DispatchAppEvent(const std::shared_ptr<ClientAppBaseEvent> &event) override;

        void OnLocalClipboardUpdated();
        bool IsClipboardEnabled();
        std::shared_ptr<std::atomic_bool> GetLifetimeToken() const { return lifetime_token_; }

    private:
        void OnRequestFileBegin(const std::shared_ptr<Message>& msg);
        void OnRequestFileBuffer(const std::shared_ptr<Message>& msg);
        void OnRequestFileEnd(const std::shared_ptr<Message>& msg);

    private:
        std::shared_ptr<ClipboardManager> clipboard_mgr_ = nullptr;
        std::shared_ptr<std::atomic_bool> lifetime_token_ = std::make_shared<std::atomic_bool>(true);
    };

}



#endif //GAMMARAY_UDP_PLUGIN_H
