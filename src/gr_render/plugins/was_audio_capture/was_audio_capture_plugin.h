//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_MEDIA_RECORDER_PLUGIN_H
#define GAMMARAY_MEDIA_RECORDER_PLUGIN_H

#include <mutex>

#include "gr_render/plugin_interface/gr_data_provider_plugin.h"

namespace tc
{

    class IAudioCapture;

    class WasAudioCapturePlugin : public GrDataProviderPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;

        bool OnCreate(const tc::GrPluginParam &param) override;
        void OnCommand(const std::string &command) override;
        void StartProviding() override;
        void StopProviding() override;

        void SetAudioLoopbackProcessId(uint32_t pid) override;
        uint32_t GetAudioLoopbackProcessId() const override;
        bool IsProviding() const override;
        int GetLastStartError() const override;

    private:
        mutable std::mutex provide_mu_;
        int samples_ = 0;
        int channels_ = 0;
        int bits_ = 0;
        int last_start_error_ = 0;
        uint32_t loopback_process_id_ = 0;
        std::shared_ptr<IAudioCapture> audio_capture_ = nullptr;
    };

}


GR_PLUGIN_EXPORT(tc::WasAudioCapturePlugin)


#endif //GAMMARAY_UDP_PLUGIN_H
