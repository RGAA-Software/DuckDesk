//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_CLIENT_FILE_TRANSFER_PLUGIN_H
#define PX_CLIENT_FILE_TRANSFER_PLUGIN_H

#include "px_client/plugin_interface/ct_plugin_interface.h"
#include <map>

namespace px
{

    class FileTransInterface;

    class FileTransferPlugin : public ClientPluginInterface {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        void ShowRootWidget() override;
        void HideRootWidget() override;
        bool OnCreate(const px::ClientPluginParam& param) override;
        void On1Second() override;
        void OnMessage(std::shared_ptr<Message> msg) override;
        void DispatchAppEvent(const std::shared_ptr<ClientAppBaseEvent> &event) override;
        bool HasProcessingTasks() override;
        void SyncClientPluginSettings(const px::ClientPluginSettings &st) override;

    private:
        std::shared_ptr<FileTransInterface> file_trans_interface_ = nullptr;

    };

}


#endif //PX_UDP_PLUGIN_H
