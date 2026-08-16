//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_JOYSTICK_PLUGIN_H
#define PX_JOYSTICK_PLUGIN_H

#include <map>
#include "px_render/plugin_interface/px_plugin_interface.h"

namespace px
{

    class VigemController;

    class JoystickPlugin : public PxPluginInterface {
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

    private:
        void PrepareConnection();
        void ReplayJoystickEvent(const std::string& stream_id, std::shared_ptr<Message> msg);

    private:
        std::shared_ptr<VigemController> controller_;
    };

}


#endif
