//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_EVENT_REPLAYER_PLUGIN_H
#define PX_EVENT_REPLAYER_PLUGIN_H

#include "px_render/plugin_interface/px_plugin_interface.h"
#include <map>

namespace px
{

    class WinEventReplayer;

    class EventReplayerPlugin : public PxPluginInterface {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        bool OnCreate(const px::PxPluginParam& param) override;
        void On1Second() override;
        void OnMessage(std::shared_ptr<Message> msg) override;
        void OnClientDisconnected(const std::string &visitor_device_id, const std::string &stream_id) override;
        void UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage& msg) override;

    private:
        void ProcessMouseEvent(std::shared_ptr<Message> msg) const;
        void ProcessKeyboardEvent(std::shared_ptr<Message> msg) const;

    private:
        std::shared_ptr<WinEventReplayer> replayer_ = nullptr;

    };

}


#endif //PX_UDP_PLUGIN_H
