//
// Created RGAA on 15/11/2024.
//

#include "media_recorder_plugin.h"
#include "px_render/plugins/plugin_ids.h"
#include "px_media_record_new/record_writer.h"
#include "px_common_new/log.h"
#include "px_common_new/string_util.h"
#include "px_common_new/time_util.h"

#include <filesystem>

namespace px
{
    std::string MediaRecorderPlugin::GetPluginId() {
        return kMediaRecorderPluginId;
    }

    std::string MediaRecorderPlugin::GetPluginName() {
        return "Media Recorder(Server)";
    }

    std::string MediaRecorderPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t MediaRecorderPlugin::GetVersionCode() {
        return 110;
    }

    std::string MediaRecorderPlugin::GetPluginDescription() {
        return "Media recorder in server side";
    }

    bool MediaRecorderPlugin::OnCreate(const px::PxPluginParam& param) {
        PxStreamPlugin::OnCreate(param);

        record_dir_ = GetConfigParam<std::string>("record_dir");
        auto_enabled_ = GetConfigBoolParam("record_auto_enabled");
        max_segment_bytes_ = GetConfigIntParam("record_max_segment_bytes");
        max_file_count_ = (int)GetConfigIntParam("record_max_file_count");
        if (max_segment_bytes_ <= 0) {
            max_segment_bytes_ = 1024LL * 1024 * 1024;
        }
        if (max_file_count_ <= 0) {
            max_file_count_ = 24;
        }
        if (record_dir_.empty()) {
            // 默认: C:\Users\Public\Pixels\recordings (与 px_data 同约定)
            record_dir_ = (std::filesystem::path(base_data_path_) / "recordings").string();
        }

        LOGI("MediaRecorderPlugin config: auto_enabled={}, dir={}, max_segment_bytes={}, max_file_count={}",
             auto_enabled_, record_dir_, max_segment_bytes_, max_file_count_);
        return true;
    }

    bool MediaRecorderPlugin::OnStop() {
        StopRecord();
        return PxStreamPlugin::OnStop();
    }

    bool MediaRecorderPlugin::OnDestroy() {
        StopRecord();
        return PxStreamPlugin::OnDestroy();
    }

    void MediaRecorderPlugin::On1Second() {
        if (dropped_frames_.load() > 0) {
            auto now = px::TimeUtil::GetCurrentTimestamp();
            if (now - last_drop_log_ts_ >= 10000) {
                last_drop_log_ts_ = now;
                LOGW("MediaRecord: dropped {} frames (queue full), recording: {}",
                     dropped_frames_.exchange(0), recording_.load());
            }
        }
    }

    void MediaRecorderPlugin::OnCommand(const std::string& command) {
        if (command == "record:start") {
            if (auto_enabled_) {
                LOGI("MediaRecord: auto mode enabled, ignore manual start");
                return;
            }
            StartRecord();
        }
        else if (command == "record:stop") {
            if (auto_enabled_) {
                LOGI("MediaRecord: auto mode enabled, ignore manual stop");
                return;
            }
            StopRecord();
        }
    }

    void MediaRecorderPlugin::OnNewClientConnected(const std::string& visitor_device_id,
                                                   const std::string& stream_id,
                                                   const std::string& conn_type) {
        PxPluginInterface::OnNewClientConnected(visitor_device_id, stream_id, conn_type);
        if (!auto_enabled_ || !IsPluginEnabled()) {
            return;
        }
        // 连接/断开事件必须能配对: RTC local 路径两者都填真实访客 stream id,
        // ws 路径填客户端 device id(见 rtc_data_channel/rtc_server/ws_server)
        auto key = visitor_device_id.empty() ? stream_id : visitor_device_id;
        bool first = false;
        {
            std::lock_guard<std::mutex> lk(clients_mtx_);
            first = client_devices_.empty();
            client_devices_.insert(key);
        }
        if (first) {
            LOGI("MediaRecord: auto start (client connected, key={})", key);
            StartRecord();
        }
    }

    void MediaRecorderPlugin::OnClientDisconnected(const std::string& visitor_device_id,
                                                   const std::string& stream_id) {
        if (!auto_enabled_ || !IsPluginEnabled()) {
            return;
        }
        auto key = visitor_device_id.empty() ? stream_id : visitor_device_id;
        bool last = false;
        {
            std::lock_guard<std::mutex> lk(clients_mtx_);
            client_devices_.erase(key);
            last = client_devices_.empty();
        }
        if (last) {
            LOGI("MediaRecord: auto stop (no clients, key={})", key);
            StopRecord();
        }
    }

    void MediaRecorderPlugin::OnEncodedVideoFrame(const std::string& mon_name,
                                                  const PxPluginEncodedVideoType& video_type,
                                                  const std::shared_ptr<Data>& data,
                                                  uint64_t frame_index,
                                                  int frame_width,
                                                  int frame_height,
                                                  bool key) {
        if (!IsPluginEnabled() || !recording_.load() || !data) {
            return;
        }
        if (video_type != PxPluginEncodedVideoType::kH264 &&
            video_type != PxPluginEncodedVideoType::kH265) {
            return; // 只录 h264/h265
        }
        Entry e;
        e.is_video = true;
        e.data = data;
        e.mon_name = mon_name;
        e.video_type = video_type;
        e.frame_index = frame_index;
        e.width = frame_width;
        e.height = frame_height;
        e.key = key;
        Enqueue(std::move(e));
    }

    void MediaRecorderPlugin::OnEncodedAudioFrame(const std::shared_ptr<Data>& data,
                                                  int samples, int channels, int bits, int frame_size) {
        if (!IsPluginEnabled() || !recording_.load() || !data) {
            return;
        }
        Entry e;
        e.is_video = false;
        e.data = data;
        Enqueue(std::move(e));
    }

    void MediaRecorderPlugin::StartRecord() {
        bool expected = false;
        if (!recording_.compare_exchange_strong(expected, true)) {
            return;
        }
        stopping_ = false;
        // 保证文件开头有关键帧/参数集
        InsertIdr();
        LOGI("MediaRecord: recording started");
    }

    void MediaRecorderPlugin::StopRecord() {
        bool expected = true;
        if (!recording_.compare_exchange_strong(expected, false)) {
            return;
        }
        stopping_ = true;
        PostWorkTask([this]() { Finalize(); });
    }

    void MediaRecorderPlugin::Enqueue(Entry&& e) {
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            if (queue_.size() >= kMaxQueuedEntries) {
                ++dropped_frames_;
                return;
            }
            queue_.push_back(std::move(e));
        }
        if (!drain_scheduled_.exchange(true)) {
            PostWorkTask([this]() { Drain(); });
        }
    }

    void MediaRecorderPlugin::ProcessBatch(std::deque<Entry>& batch) {
        for (auto& e : batch) {
            if (!e.data || e.data->Size() <= 0) {
                continue;
            }
            if (e.is_video) {
                auto& writer = writers_[e.mon_name];
                if (!writer) {
                    RecordWriterConfig cfg;
                    cfg.dir = record_dir_;
                    cfg.monitor_name = e.mon_name;
                    cfg.max_segment_bytes = max_segment_bytes_;
                    cfg.max_file_count = max_file_count_;
                    // 滚动到新段时请求关键帧
                    cfg.on_request_keyframe = [this]() { InsertIdr(); };
                    writer = RecordWriter::Make(cfg);
                    LOGI("MediaRecord: writer created for monitor '{}'", e.mon_name);
                }
                auto codec = (e.video_type == PxPluginEncodedVideoType::kH265)
                                 ? RecordVideoCodec::kH265
                                 : RecordVideoCodec::kH264;
                writer->OnEncodedVideo((const uint8_t*)e.data->CStr(), (size_t)e.data->Size(), codec,
                                       e.width, e.height, e.key);
            }
            else {
                // 音频喂给所有显示器 writer(与客户端行为一致)
                for (auto& [mon, writer] : writers_) {
                    writer->OnEncodedAudio((const uint8_t*)e.data->CStr(), (size_t)e.data->Size());
                }
            }
        }
    }

    void MediaRecorderPlugin::Drain() {
        if (stopping_.load() || destroyed_.load()) {
            drain_scheduled_ = false;
            return;
        }
        std::deque<Entry> batch;
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            batch.swap(queue_);
        }
        ProcessBatch(batch);
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            drain_scheduled_ = false;
            if (!queue_.empty() && !stopping_.load()) {
                if (!drain_scheduled_.exchange(true)) {
                    PostWorkTask([this]() { Drain(); });
                }
            }
        }
    }

    void MediaRecorderPlugin::Finalize() {
        // 排空剩余帧(Stop 前已入队的都要写)
        while (true) {
            std::deque<Entry> batch;
            {
                std::lock_guard<std::mutex> lk(queue_mtx_);
                batch.swap(queue_);
            }
            if (batch.empty()) {
                break;
            }
            ProcessBatch(batch);
        }
        for (auto& [mon, writer] : writers_) {
            writer->Stop();
        }
        writers_.clear();
        drain_scheduled_ = false;
        stopping_ = false;
        LOGI("MediaRecord: recording stopped");
    }

}
