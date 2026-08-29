//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_MEDIA_RECORDER_PLUGIN_H
#define PX_RENDER_MEDIA_RECORDER_PLUGIN_H

#include "px_render/plugin_interface/px_stream_plugin.h"

#include <memory>

namespace px {

class MediaRecorderRuntime;

class MediaRecorderPlugin final : public PxStreamPlugin, public PxEncodedAudioSink {
public:
    MediaRecorderPlugin() { plugin_enabled_ = true; }
    ~MediaRecorderPlugin() override;

    std::string GetPluginId() override;
    std::string GetPluginName() override;
    std::string GetVersionName() override;
    uint32_t GetVersionCode() override;
    std::string GetPluginDescription() override;

    bool OnCreate(const PxPluginParam& param) override;
    bool OnStop() override;
    bool OnDestroy() override;
    void On1Second() override;
    void OnCommand(const std::string& command) override;
    void OnNewClientConnected(
        const std::string& visitor_device_id,
        const std::string& stream_id,
        const std::string& conn_type) override;
    void OnClientDisconnected(
        const std::string& visitor_device_id,
        const std::string& stream_id) override;
    void OnEncodedVideoFrame(
        const std::string& mon_name,
        const PxPluginEncodedVideoType& video_type,
        const std::shared_ptr<Data>& data,
        uint64_t frame_index,
        int frame_width,
        int frame_height,
        bool key) override;
    void OnEncodedAudioFrame(
        const std::shared_ptr<Data>& data,
        int samples,
        int channels,
        int bits,
        int frame_size) override;

private:
    void RefreshKeyframeRequester();

    // The plug-in remains loader-owned; asynchronous work never captures it.
    std::shared_ptr<MediaRecorderRuntime> runtime_;
};

}  // namespace px

PX_PLUGIN_EXPORT(px::MediaRecorderPlugin)

#endif  // PX_RENDER_MEDIA_RECORDER_PLUGIN_H
