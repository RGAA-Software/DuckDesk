#include "media_recorder.h"
#include "px_media_record_new/record_writer.h"
#include "px_common_new/log.h"
#include "px_common_new/folder_util.h"

#include <filesystem>
#include <QDir>
#include <QApplication>

namespace px {

static std::string ResolveRecordDir(const std::string& configured_path) {
    auto record_path = configured_path;
    if (record_path.empty()) {
        // 默认: C:\Users\Public\Pixels\px_client_records (与数据根同约定)
        record_path = (std::filesystem::path(FolderUtil::GetProgramDataPath()) / "px_client_records").string();
    }

    QDir qdir{QString::fromStdString(record_path)};
    if (!qdir.exists()) {
        qdir.mkpath(".");
    }
    if (!qdir.exists()) {
        record_path = QCoreApplication::applicationDirPath().toStdString();
    }
    return record_path;
}

std::shared_ptr<MediaRecorder> MediaRecorder::Make(std::string record_path) {
    return std::make_shared<MediaRecorder>(std::move(record_path));
}

MediaRecorder::MediaRecorder(std::string record_path)
    : configured_record_path_(std::move(record_path)) {

}

MediaRecorder::~MediaRecorder() {
    (void)EndRecord();
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
    writer_->OnEncodedVideo(
                            std::span<const uint8_t>(
                                reinterpret_cast<const uint8_t*>(d.data()), d.size()),
                            codec,
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
    writer_->OnEncodedAudio(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(d.data()), d.size()));
}

std::optional<std::string> MediaRecorder::EndRecord() {
    // 只在实际录制过(创建过 writer)的 recorder 上弹通知,
    // 否则多显示器槽位里没参与录制的实例也会弹一遍 toast
    bool recorded = (writer_ != nullptr);
    if (writer_) {
        writer_->Stop();
        writer_ = nullptr;
    }

    if (!recorded) {
        return std::nullopt;
    }
    return ResolveRecordDir(configured_record_path_);
}

void MediaRecorder::EnsureWriter() {
    if (writer_) {
        return;
    }
    RecordWriterConfig cfg;
    cfg.dir = ResolveRecordDir(configured_record_path_);
    cfg.monitor_name = "mon" + std::to_string(index_);
    // 滚动开新段时无需主动请求关键帧: 客户端录制期间服务端每秒插 IDR,
    // 等待 <=1s 即可收到下一个关键帧。
    writer_ = RecordWriter::Make(cfg);
    LOGI("MediaRecord: writer created, dir={}, index={}",
         cfg.dir, index_);
}

}
