#include "media_recorder.h"
#include "media_record_plugin.h"
#include "px_media_record_new/record_writer.h"
#include "px_common_new/log.h"
#include "px_common_new/folder_util.h"
#include "px_client/plugin_interface/ct_plugin_context.h"
#include "px_client/plugin_interface/ct_plugin_events.h"

#include <filesystem>
#include <QDir>
#include <QApplication>

namespace px {

static std::string ResolveRecordDir(MediaRecordPluginClient* plugin) {
    std::string record_path;
    if (plugin) {
        record_path = plugin->GetScreenRecordingPath();
    }
    if (record_path.empty()) {
        // 默认: C:\Users\Public\Pixels\px_client_records (与数据根同约定)
        record_path = (std::filesystem::path(FolderUtil::GetProgramDataPath()) / "px_client_records").string();
    }

    QDir qdir{QString::fromStdString(record_path)};
    if (!qdir.exists()) {
        qdir.mkpath(".");
    }
    if (!qdir.exists()) {
        record_path = qApp->applicationDirPath().toStdString();
    }
    return record_path;
}

std::shared_ptr<MediaRecorder> MediaRecorder::Make(MediaRecordPluginClient* plugin) {
    return std::make_shared<MediaRecorder>(plugin);
}

MediaRecorder::MediaRecorder(MediaRecordPluginClient* plugin) : plugin_(plugin) {

}

MediaRecorder::~MediaRecorder() {
    EndRecord();
}

void MediaRecorder::SetIndex(int idx) {
    index_ = idx;
}

void MediaRecorder::RecvVideoFrame(const VideoFrame& frame) {
    EnsureWriter();
    if (!writer_) {
        return;
    }
    const auto& d = frame.data();
    if (d.empty()) {
        return;
    }
    auto codec = (frame.type() == px::VideoType::kNetHevc) ? RecordVideoCodec::kH265 : RecordVideoCodec::kH264;
    writer_->OnEncodedVideo((const uint8_t*)d.data(), d.size(), codec,
                            frame.frame_width(), frame.frame_height(), frame.key());
}

void MediaRecorder::RecvAudioFrame(const AudioFrame& frame) {
    EnsureWriter();
    if (!writer_) {
        return;
    }
    const auto& d = frame.data();
    if (d.empty()) {
        return;
    }
    writer_->OnEncodedAudio((const uint8_t*)d.data(), d.size());
}

void MediaRecorder::EndRecord() {
    // 只在实际录制过(创建过 writer)的 recorder 上弹通知,
    // 否则多显示器槽位里没参与录制的实例也会弹一遍 toast
    bool recorded = (writer_ != nullptr);
    if (writer_) {
        writer_->Stop();
        writer_ = nullptr;
    }

    if (plugin_ && recorded) {
        auto record_dir = ResolveRecordDir(plugin_);
        if (!record_dir.empty()) {
            auto event = std::make_shared<ClientPluginNotifyMsgEvent>();
            event->title_ = "Screen recording success";
            event->message_ = record_dir;
            event->clicked_cbk_ = [=, this]() {
                LOGI("====> Screen recording ended: {}", event->message_);
                auto folder_path = event->message_;
                plugin_->GetPluginContext()->PostUITask([=]() {
                    FolderUtil::OpenDir(PathFromUTF8(folder_path));
                });
            };
            plugin_->CallbackEvent(event);
        }
    }
}

void MediaRecorder::EnsureWriter() {
    if (writer_ || !plugin_) {
        return;
    }
    RecordWriterConfig cfg;
    cfg.dir = ResolveRecordDir(plugin_);
    cfg.monitor_name = "mon" + std::to_string(index_);
    // 滚动开新段时无需主动请求关键帧: 客户端录制期间服务端每秒插 IDR,
    // 等待 <=1s 即可收到下一个关键帧。
    writer_ = RecordWriter::Make(cfg);
    LOGI("MediaRecord: writer created, dir={}, index={}",
         cfg.dir, index_);
}

}
