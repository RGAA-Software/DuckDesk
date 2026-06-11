//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_RTC_PLUGIN_H
#define GAMMARAY_RTC_PLUGIN_H
#include <memory>
#include "gr_render/plugin_interface/gr_plugin_interface.h"
#include "file_transmission_server/tc_translator_stub.h"

namespace tc
{

    class Message;
    class FileTransmitMsgInterface;

    class FileTransferPlugin : public GrPluginInterface {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;

        bool OnCreate(const GrPluginParam& param) override;
        void OnMessage(std::shared_ptr<Message> msg) override;
        void OnSyncPluginSettingsInfo(const GrPluginSettingsInfo& settings) override;
        LanguageKind GetCurrentLanguage();

    private:
        std::shared_ptr<FileTransmitMsgInterface> file_trans_msg_interface_ = nullptr;
    };

}


#endif //GAMMARAY_UDP_PLUGIN_H
