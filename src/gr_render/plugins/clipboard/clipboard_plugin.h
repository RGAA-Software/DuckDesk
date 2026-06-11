//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_CLIPBOARD_PLUGIN_H
#define GAMMARAY_CLIPBOARD_PLUGIN_H

#include "gr_render/plugin_interface/gr_plugin_interface.h"
#include <atomic>
#include <memory>

namespace tc
{

    class Data;
    class ClipboardManager;
    class CpVirtualFile;

    class ClipboardPlugin : public GrPluginInterface {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;

        bool OnCreate(const tc::GrPluginParam& param) override;
        bool OnDestroy() override;
        std::shared_ptr<std::atomic_bool> GetLifetimeToken() const { return lifetime_token_; }

        void OnMessage(std::shared_ptr<Message> msg) override;
        void DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event) override;

        // client -> request buffer
        void OnRequestFileBuffer(std::shared_ptr<Message> in_msg);

    private:
        void OnRequestFileBegin(std::shared_ptr<Message> msg);
        void OnRequestFileEnd(std::shared_ptr<Message> msg);

    private:
        std::shared_ptr<ClipboardManager> clipboard_mgr_ = nullptr;
        std::shared_ptr<std::atomic_bool> lifetime_token_ = std::make_shared<std::atomic_bool>(true);
//        CpVirtualFile* virtual_file_ = nullptr;
//        IDataObject* data_object_ = nullptr;
    };

}


#endif //GAMMARAY_UDP_PLUGIN_H
