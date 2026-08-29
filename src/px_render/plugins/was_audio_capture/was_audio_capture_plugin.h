//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_WAS_AUDIO_CAPTURE_PLUGIN_H
#define PX_RENDER_WAS_AUDIO_CAPTURE_PLUGIN_H

#include <memory>

#include "px_render/plugin_interface/px_data_provider_plugin.h"

namespace px
{

    class WasAudioCaptureRuntime;

    class WasAudioCapturePlugin : public PxDataProviderPlugin {
    public:
        ~WasAudioCapturePlugin() override;

        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;

        bool OnCreate(const px::PxPluginParam &param) override;
        bool OnStop() override;
        bool OnDestroy() override;
        void OnCommand(const std::string &command) override;
        void OnSyncPluginSettingsInfo(const PxPluginSettingsInfo& settings) override;
        void StartProviding() override;
        void StopProviding() override;

        void SetAudioLoopbackProcessId(uint32_t pid) override;
        uint32_t GetAudioLoopbackProcessId() const override;
        bool IsProviding() const override;
        int GetLastStartError() const override;

    private:
        void RefreshRuntimeDelivery();

        // The loader-owned plug-in instance remains an ABI singleton. All
        // asynchronous capture/restart work lives in this independently
        // reference-counted runtime and never retains or captures the plug-in.
        std::shared_ptr<WasAudioCaptureRuntime> runtime_;
    };

}


PX_PLUGIN_EXPORT(px::WasAudioCapturePlugin)


#endif //PX_UDP_PLUGIN_H
