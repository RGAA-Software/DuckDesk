#pragma once

#include "px_render/plugin_interface/px_stream_plugin.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AVFormatContext;
struct AVStream;
struct AVCodecContext;
struct AVAudioFifo;
struct SwrContext;

namespace px {

// Passive RTMP pusher. It deliberately does not implement PxNetPlugin: its
// connection to ZLM must not make RdApplication believe there is a remote
// control client and therefore must not alter capture-on-demand semantics.
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

    void OnEncodedVideoFrame(const std::string& mon_name,
                             const PxPluginEncodedVideoType& video_type,
                             const std::shared_ptr<Data>& data,
                             uint64_t frame_index,
                             int frame_width,
                             int frame_height,
                             bool key) override;
    void OnRawAudioData(const std::shared_ptr<Data>& data, int samples, int channels, int bits) override;

private:
    enum class EntryKind { Video, Audio };
    struct Entry {
        EntryKind kind{};
        std::shared_ptr<Data> data;
        PxPluginEncodedVideoType video_type = PxPluginEncodedVideoType::kH264;
        int width = 0;
        int height = 0;
        bool key = false;
        int sample_rate = 0;
        int channels = 0;
        int bits = 0;
        int64_t timestamp_ms = 0;
    };

    void Enqueue(Entry&& entry);
    void WorkerLoop();
    void ProcessVideo(const Entry& entry);
    void ProcessAudio(const Entry& entry);
    bool InitAudio(int sample_rate, int channels, int bits);
    void ResetAudioState();
    bool OpenOutput();
    void CloseOutput();
    void ResetVideoState();
    void Shutdown();
    bool IsSelectedMonitor(const std::string& mon_name);
    std::string BuildUrl() const;

    bool enabled_ = false;
    std::string rtmp_url_;
    std::string stream_id_;
    std::string primary_monitor_;
    // Game-hook encoded frames do not carry a monitor name.  Keep the
    // selection state separate from the name so an empty-but-valid name is
    // selected once instead of causing a log message for every frame.
    bool primary_monitor_selected_ = false;
    int audio_bitrate_ = 96000;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Entry> queue_;
    bool stopping_ = false;
    std::thread worker_;
    uint64_t dropped_ = 0;
    int64_t last_drop_log_ms_ = 0;

    // worker-thread-only mux state
    AVFormatContext* fmt_ = nullptr;
    AVStream* video_stream_ = nullptr;
    AVStream* audio_stream_ = nullptr;
    // av_write_trailer is valid only after avformat_write_header succeeds.
    // Output setup may fail before the muxer has installed its private state.
    bool header_written_ = false;
    AVCodecContext* aac_ = nullptr;
    AVAudioFifo* audio_fifo_ = nullptr;
    SwrContext* swr_ = nullptr;
    PxPluginEncodedVideoType codec_ = PxPluginEncodedVideoType::kH264;
    bool codec_known_ = false;
    bool have_key_ = false;
    int video_width_ = 0;
    int video_height_ = 0;
    int64_t session_start_ms_ = 0;
    int64_t last_video_dts_ = -1;
    int64_t last_idr_request_ms_ = 0;
    int64_t pending_key_ts_ = 0;
    int64_t next_audio_pts_ = 0;
    std::vector<uint8_t> vps_;
    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
    std::vector<uint8_t> pending_key_;
};

} // namespace px

PX_PLUGIN_EXPORT(px::LivePusherPlugin)
