//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_CLIPBOARD_PLUGIN_H
#define PX_CLIPBOARD_PLUGIN_H

#include "px_render/plugin_interface/px_plugin_interface.h"
#include <memory>

namespace px
{

    class Data;
    class CpVirtualFile;

    class ClipboardPlugin : public PxPluginInterface {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;

        bool OnCreate(const px::PxPluginParam& param) override;
        bool OnDestroy() override;

        void OnMessage(std::shared_ptr<Message> msg) override;
        void DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event) override;

        // client -> request buffer
        void OnRequestFileBuffer(std::shared_ptr<Message> in_msg);

    private:
        void OnRequestFileBegin(std::shared_ptr<Message> msg);
        void OnRequestFileEnd(std::shared_ptr<Message> msg);

    };

}


#endif //PX_UDP_PLUGIN_H
