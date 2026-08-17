#pragma once

#include <memory>
#include <string>

#include "px_message.pb.h"

namespace px {

class MediaRecordPluginClient;
class RecordWriter;

// 客户端侧录屏适配层:把收到的编码流(VideoFrame/AudioFrame)转交给共享录制核心
// RecordWriter(px_media_record_new), 由核心负责 remux MP4 / 1GB 分段 / 滚动清理。
class MediaRecorder {
public:
    static std::shared_ptr<MediaRecorder> Make(MediaRecordPluginClient* plugin);

    explicit MediaRecorder(MediaRecordPluginClient* plugin);
    ~MediaRecorder();

    void EndRecord();

    void RecvVideoFrame(const VideoFrame& frame);

    void RecvAudioFrame(const AudioFrame& frame);

    void SetIndex(int idx);
private:
    void EnsureWriter();

    MediaRecordPluginClient* plugin_ = nullptr;
    //录屏器索引(对应显示器)
    int index_ = 0;
    std::shared_ptr<RecordWriter> writer_;
};

}
