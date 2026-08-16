//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_FILE_TRANSFER_PLUGIN_H
#define PX_RENDER_FILE_TRANSFER_PLUGIN_H
#include <memory>
#include "px_render/plugin_interface/px_plugin_interface.h"
#include "file_transmission_server/px_translator_stub.h"

namespace px
{

    class Message;
    class FileTransmitMsgInterface;

    class FileTransferPlugin : public PxPluginInterface {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;

        bool OnCreate(const PxPluginParam& param) override;
        void OnMessage(std::shared_ptr<Message> msg) override;
        void OnSyncPluginSettingsInfo(const PxPluginSettingsInfo& settings) override;
        LanguageKind GetCurrentLanguage();

    private:
        std::shared_ptr<FileTransmitMsgInterface> file_trans_msg_interface_ = nullptr;
    };

}


#endif //PX_UDP_PLUGIN_H
