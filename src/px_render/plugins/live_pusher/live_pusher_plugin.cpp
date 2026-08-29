#include "live_pusher_plugin.h"

#include <chrono>
#include <string_view>

#include "live_pusher_ffmpeg.h"
#include "live_pusher_runtime.h"
#include "px_common_new/log.h"
#include "px_render/plugin_interface/px_plugin_context.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugins/plugin_ids.h"

namespace px {
namespace {

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string BuildPublishUrl(std::string url, const std::string& stream_id) {
    constexpr std::string_view placeholder = "{live_stream_id}";
    if (const auto position = url.find(placeholder); position != std::string::npos) {
        url.replace(position, placeholder.size(), stream_id);
    }
    return url;
}

}  // namespace

LivePusherPlugin::LivePusherPlugin() { plugin_enabled_ = true; }

LivePusherPlugin::~LivePusherPlugin() {
    if (runtime_) {
        runtime_->Shutdown();
        runtime_.reset();
    }
}

std::string LivePusherPlugin::GetPluginId() { return kLivePusherPluginId; }
std::string LivePusherPlugin::GetPluginName() { return "Live Pusher"; }
std::string LivePusherPlugin::GetVersionName() { return "0.2.0"; }
uint32_t LivePusherPlugin::GetVersionCode() { return 20; }
std::string LivePusherPlugin::GetPluginDescription() {
    return "Passive RTMP live pusher";
}

bool LivePusherPlugin::OnCreate(const PxPluginParam& param) {
    if (!PxStreamPlugin::OnCreate(param)) {
        return false;
    }
    if (!GetConfigBoolParam("push_enabled")) {
        LOGI("LivePusher disabled by [push].enabled");
        return true;
    }
    const auto rtmp_url = GetConfigParam<std::string>("push_rtmp_url");
    const auto stream_id = GetConfigParam<std::string>("live_stream_id");
    if (rtmp_url.empty() || stream_id.empty()) {
        LOGE("LivePusher disabled: rtmp_url or live_stream_id is empty");
        return true;
    }

    LivePusherRuntime::Config config;
    config.publish_url = BuildPublishUrl(rtmp_url, stream_id);
    config.primary_monitor = GetConfigParam<std::string>("push_primary_monitor");
    config.audio_bitrate = static_cast<int>(GetConfigIntParam("push_audio_bitrate"));
    if (config.audio_bitrate <= 0) {
        config.audio_bitrate = 96000;
    }
    runtime_ = LivePusherRuntime::Make(config, MakeFfmpegLivePushProcessor);
    if (!runtime_) {
        LOGE("LivePusher disabled: failed to create its owned runtime");
        return true;
    }
    RefreshKeyframeRequester();
    LOGI("LivePusher enabled: stream={}, selected_monitor={}", stream_id,
         config.primary_monitor.empty() ? "<first-active>" : config.primary_monitor);
    return true;
}

bool LivePusherPlugin::OnStop() {
    if (runtime_) {
        runtime_->ClearKeyframeRequester();
        runtime_->Shutdown();
    }
    return PxStreamPlugin::OnStop();
}

bool LivePusherPlugin::OnDestroy() {
    if (runtime_) {
        runtime_->Shutdown();
        runtime_.reset();
    }
    return PxStreamPlugin::OnDestroy();
}

void LivePusherPlugin::On1Second() {
    if (runtime_) {
        RefreshKeyframeRequester();
        runtime_->On1Second(NowMs());
    }
}

void LivePusherPlugin::OnEncodedVideoFrame(
    const std::string& monitor_name,
    const PxPluginEncodedVideoType& video_type,
    const std::shared_ptr<Data>& data,
    uint64_t frame_index,
    int frame_width,
    int frame_height,
    bool key) {
    (void)frame_index;
    if (runtime_ && IsPluginEnabled()) {
        RefreshKeyframeRequester();
        runtime_->EnqueueVideo(monitor_name, video_type, data,
                               frame_width, frame_height, key, NowMs());
    }
}

void LivePusherPlugin::OnRawAudioData(
    const std::shared_ptr<Data>& data,
    int samples,
    int channels,
    int bits) {
    if (runtime_ && IsPluginEnabled()) {
        runtime_->EnqueueAudio(data, samples, channels, bits, NowMs());
    }
}

void LivePusherPlugin::RefreshKeyframeRequester() {
    if (!runtime_) {
        return;
    }
    const auto weak_context = std::weak_ptr<PxPluginContext>(plugin_context_);
    const auto callback = event_cbk_;
    if (!callback) {
        runtime_->ClearKeyframeRequester();
        return;
    }
    runtime_->SetKeyframeRequester([weak_context, callback] {
        const auto context = weak_context.lock();
        if (!context) {
            return;
        }
        auto event = std::make_shared<PxPluginInsertIdrEvent>();
        event->plugin_name_ = kLivePusherPluginId;
        context->PostWorkTask([weak_context, callback, event] {
            if (weak_context.lock()) {
                callback(event);
            }
        });
    });
}

}  // namespace px
