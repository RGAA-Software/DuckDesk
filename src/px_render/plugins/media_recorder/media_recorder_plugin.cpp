#include "media_recorder_plugin.h"

#include <filesystem>
#include <utility>

#include "media_recorder_runtime.h"
#include "px_common_new/log.h"
#include "px_render/plugin_interface/px_plugin_context.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugins/plugin_ids.h"

namespace px {

MediaRecorderPlugin::~MediaRecorderPlugin() {
    if (runtime_) {
        runtime_->Shutdown();
        runtime_.reset();
    }
}

std::string MediaRecorderPlugin::GetPluginId() { return kMediaRecorderPluginId; }
std::string MediaRecorderPlugin::GetPluginName() { return "Media Recorder(Server)"; }
std::string MediaRecorderPlugin::GetVersionName() { return "1.2.0"; }
uint32_t MediaRecorderPlugin::GetVersionCode() { return 120; }
std::string MediaRecorderPlugin::GetPluginDescription() {
    return "Media recorder in server side";
}

bool MediaRecorderPlugin::OnCreate(const PxPluginParam& param) {
    if (!PxStreamPlugin::OnCreate(param)) {
        return false;
    }
    MediaRecorderRuntime::Config config;
    config.record_dir = GetConfigParam<std::string>("record_dir");
    config.auto_enabled = GetConfigBoolParam("record_auto_enabled");
    config.max_segment_bytes = GetConfigIntParam("record_max_segment_bytes");
    config.max_file_count = static_cast<int>(GetConfigIntParam("record_max_file_count"));
    if (config.max_segment_bytes <= 0) {
        config.max_segment_bytes = 1024LL * 1024 * 1024;
    }
    if (config.max_file_count <= 0) {
        config.max_file_count = 24;
    }
    if (config.record_dir.empty()) {
        config.record_dir = (std::filesystem::path(base_data_path_) /
                             "px_render_records").string();
    }
    LOGI("MediaRecorderPlugin config: auto_enabled={}, dir={}, "
         "max_segment_bytes={}, max_file_count={}",
         config.auto_enabled, config.record_dir,
         config.max_segment_bytes, config.max_file_count);
    runtime_ = MediaRecorderRuntime::Make(std::move(config));
    RefreshKeyframeRequester();
    return true;
}

bool MediaRecorderPlugin::OnStop() {
    if (runtime_) {
        runtime_->ClearKeyframeRequester();
        runtime_->StopRecord();
    }
    return PxStreamPlugin::OnStop();
}

bool MediaRecorderPlugin::OnDestroy() {
    if (runtime_) {
        runtime_->Shutdown();
        runtime_.reset();
    }
    return PxStreamPlugin::OnDestroy();
}

void MediaRecorderPlugin::On1Second() {
    RefreshKeyframeRequester();
    if (runtime_) {
        runtime_->On1Second();
    }
}

void MediaRecorderPlugin::OnCommand(const std::string& command) {
    if (!runtime_) {
        return;
    }
    if (runtime_->IsAutoEnabled()) {
        if (command == "record:start" || command == "record:stop") {
            LOGI("MediaRecord: auto mode enabled, ignore manual command {}", command);
        }
        return;
    }
    if (command == "record:start") {
        RefreshKeyframeRequester();
        runtime_->StartRecord();
    } else if (command == "record:stop") {
        runtime_->StopRecord();
    }
}

void MediaRecorderPlugin::OnNewClientConnected(
    const std::string& visitor_device_id,
    const std::string& stream_id,
    const std::string& conn_type) {
    PxPluginInterface::OnNewClientConnected(visitor_device_id, stream_id, conn_type);
    if (!runtime_ || !IsPluginEnabled()) {
        return;
    }
    RefreshKeyframeRequester();
    runtime_->OnClientConnected(visitor_device_id, stream_id);
}

void MediaRecorderPlugin::OnClientDisconnected(
    const std::string& visitor_device_id,
    const std::string& stream_id) {
    if (runtime_ && IsPluginEnabled()) {
        runtime_->OnClientDisconnected(visitor_device_id, stream_id);
    }
}

void MediaRecorderPlugin::OnEncodedVideoFrame(
    const std::string& mon_name,
    const PxPluginEncodedVideoType& video_type,
    const std::shared_ptr<Data>& data,
    uint64_t frame_index,
    int frame_width,
    int frame_height,
    bool key) {
    if (runtime_ && IsPluginEnabled()) {
        runtime_->EnqueueVideo(mon_name, video_type, data, frame_index,
                               frame_width, frame_height, key);
    }
}

void MediaRecorderPlugin::OnEncodedAudioFrame(
    const std::shared_ptr<Data>& data,
    int samples,
    int channels,
    int bits,
    int frame_size) {
    (void)samples;
    (void)channels;
    (void)bits;
    (void)frame_size;
    if (runtime_ && IsPluginEnabled()) {
        runtime_->EnqueueAudio(data);
    }
}

void MediaRecorderPlugin::RefreshKeyframeRequester() {
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
        event->plugin_name_ = kMediaRecorderPluginId;
        context->PostWorkTask([weak_context, callback, event] {
            if (weak_context.lock()) {
                callback(event);
            }
        });
    });
}

}  // namespace px
