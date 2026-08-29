//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_CLIENT_MEDIA_RECORD_PLUGIN_H
#define PX_CLIENT_MEDIA_RECORD_PLUGIN_H

#include "px_client/plugin_interface/ct_media_record_plugin_interface.h"
#include <memory>

namespace px
{
    class MediaRecordRuntime;

    class MediaRecordPluginClient : public MediaRecordPluginClientInterface {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;

        bool OnCreate(const px::ClientPluginParam& param) override;
        bool OnStop() override;
        bool OnDestroy() override;
        void On1Second() override;
        void OnMessage(std::shared_ptr<Message> msg) override;
        void DispatchAppEvent(const std::shared_ptr<ClientAppBaseEvent> &event) override;

        void StartRecord() override;
        void EndRecord() override;

        [[nodiscard]] std::string GetScreenRecordingPath() const;
    private:
        std::shared_ptr<MediaRecordRuntime> runtime_;
    };
}





#endif //PX_UDP_PLUGIN_H
