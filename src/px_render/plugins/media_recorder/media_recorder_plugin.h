//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_MEDIA_RECORDER_PLUGIN_H
#define PX_RENDER_MEDIA_RECORDER_PLUGIN_H

#include "px_render/plugin_interface/px_stream_plugin.h"

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace px
{

    class RecordWriter;

    // Render 端录屏插件:复用编码产物(H264/H265 + Opus)直接 remux 成 MP4,
    // 零重编码。滚动式录制(默认 1GB/段, 24 个文件上限, 删除最旧)。
    // 编码音频通过 PxEncodedAudioSink 可选接口接收(不改变基类虚表, ABI 兼容)。
    class MediaRecorderPlugin : public PxStreamPlugin, public PxEncodedAudioSink {
    public:
        MediaRecorderPlugin() { plugin_enabled_ = true; }

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

        // auto record: 有人连接自动录, 无人连接自动停
        void OnNewClientConnected(const std::string& visitor_device_id, const std::string& stream_id, const std::string& conn_type) override;
        void OnClientDisconnected(const std::string& visitor_device_id, const std::string& stream_id) override;

        // stream hooks(在共享 Stream 插件任务线程上串行回调)
        void OnEncodedVideoFrame(const std::string& mon_name,
                                 const PxPluginEncodedVideoType& video_type,
                                 const std::shared_ptr<Data>& data,
                                 uint64_t frame_index,
                                 int frame_width,
                                 int frame_height,
                                 bool key) override;
        void OnEncodedAudioFrame(const std::shared_ptr<Data>& data, int samples, int channels, int bits, int frame_size) override;

    private:        struct Entry {
            bool is_video = false;
            std::shared_ptr<Data> data;
            std::string mon_name;
            PxPluginEncodedVideoType video_type = PxPluginEncodedVideoType::kH264;
            uint64_t frame_index = 0;
            int width = 0;
            int height = 0;
            bool key = false;
        };

        void StartRecord();
        void StopRecord();
        void Enqueue(Entry&& e);
        void ProcessBatch(std::deque<Entry>& batch);
        void Drain();
        void Finalize();

        // config(OnCreate 由 exe 侧经 param cluster 下发)
        std::string record_dir_;
        bool auto_enabled_ = false;
        int64_t max_segment_bytes_ = 1024LL * 1024 * 1024;
        int max_file_count_ = 24;

        // state
        std::atomic<bool> recording_{false};
        std::atomic<bool> stopping_{false};
        std::atomic<bool> drain_scheduled_{false};

        // queue: 回调线程只入队, 写盘在工作线程(不阻塞共享 Stream 任务线程)
        static constexpr size_t kMaxQueuedEntries = 512;
        std::mutex queue_mtx_;
        std::deque<Entry> queue_;
        std::atomic<uint64_t> dropped_frames_{0};
        int64_t last_drop_log_ts_ = 0;

        // writers: 只允许在工作线程访问
        std::map<std::string, std::shared_ptr<RecordWriter>> writers_;

        // auto mode: 已连接客户端集合。
        // 按 visitor_device_id 键控(断开事件已与连接事件保持一致, 可配对)。
        std::mutex clients_mtx_;
        std::set<std::string> client_devices_;
    };

}

PX_PLUGIN_EXPORT(px::MediaRecorderPlugin)

#endif //PX_RENDER_MEDIA_RECORDER_PLUGIN_H
