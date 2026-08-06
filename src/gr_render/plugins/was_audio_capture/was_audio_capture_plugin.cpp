//
// Created RGAA on 15/11/2024.
//

#include "was_audio_capture_plugin.h"
#include "audio_capture.h"
#include "gr_render/plugins/plugin_ids.h"
#include "miniaudio_audio_capture.h"
#include "tc_common_new/log.h"
#include "tc_common_new/memory_stat.h"
#include "gr_render/plugin_interface/gr_plugin_events.h"
#include "gr_render/plugin_interface/gr_plugin_context.h"

namespace tc
{
    std::string WasAudioCapturePlugin::GetPluginId() {
        return kWasAudioCapturePluginId;
    }

    std::string WasAudioCapturePlugin::GetPluginName() {
        return "MiniAudio";
    }

    std::string WasAudioCapturePlugin::GetVersionName() {
        return "1.2.0";
    }

    uint32_t WasAudioCapturePlugin::GetVersionCode() {
        return 120;
    }

    void WasAudioCapturePlugin::On1Second() {
#if MEMORY_STST_ON
        plugin_context_->PostWorkTask([=, this]() {
            auto info = MemoryStat::Instance()->GetStatInfo();
            LOGI("Memory usage: {}", info.Dump());
        });
#endif
    }

    std::string WasAudioCapturePlugin::GetPluginDescription() {
        return "MiniAudio WASAPI default-device loopback capture";
    }

    bool WasAudioCapturePlugin::OnCreate(const tc::GrPluginParam& param) {
        GrDataProviderPlugin::OnCreate(param);
        MemoryStat::Instance();
        LOGI("[WasAudioCapturePlugin] OnCreate (follow OS default playback device)");
        return true;
    }

    void WasAudioCapturePlugin::OnCommand(const std::string& command) {
        LOGI("[WasAudioCapturePlugin] OnCommand: {}", command);
    }

    void WasAudioCapturePlugin::StartProviding() {
        LOGI("[WasAudioCapturePlugin] StartProviding, audio_enabled={}",
             sys_settings_.audio_enabled_);

        if (audio_capture_) {
            LOGW("[WasAudioCapturePlugin] previous capture still alive, stopping it first");
            audio_capture_->Stop();
            audio_capture_.reset();
        }

        audio_capture_ = MiniAudioCapture::Make();
        if (!audio_capture_) {
            LOGE("[WasAudioCapturePlugin] MiniAudioCapture::Make failed");
            return;
        }

        audio_capture_->RegisterFormatCallback([=, this](int samples, int channels, int bits) {
            this->samples_ = samples;
            this->channels_ = channels;
            this->bits_ = bits;
            LOGI("[WasAudioCapturePlugin] format ready: {}Hz {}ch {}bit", samples, channels, bits);
        });

        audio_capture_->RegisterDataCallback([=, this](const std::shared_ptr<Data>& data) {
            if (!sys_settings_.audio_enabled_) {
                return;
            }
            if (!data || data->Size() <= 0) {
                return;
            }
            auto event = std::make_shared<GrPluginRawAudioFrameEvent>();
            event->full_data_ = data;
            event->sample_rate_ = this->samples_;
            event->channels_ = this->channels_;
            event->bits_ = this->bits_;
            CallbackEvent(event);
        });

        audio_capture_->RegisterSplitDataCallback([=, this](const auto& left, const auto& right) {
            if (!sys_settings_.audio_enabled_) {
                return;
            }
            auto event = std::make_shared<GrPluginSplitRawAudioFrameEvent>();
            event->left_ch_data_ = left;
            event->right_ch_data_ = right;
            event->sample_rate_ = this->samples_;
            event->channels_ = this->channels_;
            event->bits_ = this->bits_;
            CallbackEvent(event);
        });

        const int start_ret = audio_capture_->Start();
        if (start_ret != 0) {
            LOGE("[WasAudioCapturePlugin] Start failed, ret={}", start_ret);
            audio_capture_.reset();
            return;
        }
        LOGI("[WasAudioCapturePlugin] StartProviding OK");
    }

    void WasAudioCapturePlugin::StopProviding() {
        LOGI("[WasAudioCapturePlugin] StopProviding");
        if (!audio_capture_) {
            LOGW("[WasAudioCapturePlugin] StopProviding: capture already null");
            return;
        }
        audio_capture_->Pause();
        audio_capture_->Stop();
        audio_capture_.reset();
        LOGI("[WasAudioCapturePlugin] StopProviding done");
    }

}
