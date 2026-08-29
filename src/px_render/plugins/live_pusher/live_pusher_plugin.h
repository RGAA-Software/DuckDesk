#pragma once

#include <memory>

#include "px_render/plugin_interface/px_stream_plugin.h"

namespace px {

class LivePusherRuntime;

class LivePusherPlugin final : public PxStreamPlugin {
public:
    LivePusherPlugin();
    ~LivePusherPlugin() override;

    std::string GetPluginId() override;
    std::string GetPluginName() override;
    std::string GetVersionName() override;
    uint32_t GetVersionCode() override;
    std::string GetPluginDescription() override;
    bool OnCreate(const PxPluginParam& param) override;
    bool OnStop() override;
    bool OnDestroy() override;
    void On1Second() override;
    void OnEncodedVideoFrame(
        const std::string& monitor_name,
        const PxPluginEncodedVideoType& video_type,
        const std::shared_ptr<Data>& data,
        uint64_t frame_index,
        int frame_width,
        int frame_height,
        bool key) override;
    void OnRawAudioData(
        const std::shared_ptr<Data>& data,
        int samples,
        int channels,
        int bits) override;

private:
    void RefreshKeyframeRequester();
    std::shared_ptr<LivePusherRuntime> runtime_;
};

}  // namespace px

PX_PLUGIN_EXPORT(px::LivePusherPlugin)
