//
// 共享录制核心：把已编码的音视频流(视频 H264/H265 Annex-B，音频 Opus)直接
// remux 成 MP4，不解码不重编码。
//
// - 只依赖 C++ std + FFmpeg，不依赖 Qt/protobuf/插件接口/平台。
// - 客户端(px_client)与 Render 端(px_render)共用。
// - 线程模型：所有接口必须由同一个线程串行调用（适配层负责把回调转到自己的工作线程）。
//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace px {

enum class RecordVideoCodec {
    kH264,
    kH265,
};

struct RecordWriterConfig {
    // 录像目录（不存在会自动创建）
    std::string dir;
    // 命名用显示器名；空 -> "mon0"
    std::string monitor_name;
    // 文件名前缀，命名与滚动清理均按此前缀匹配
    // 文件名结构: {file_prefix}{monitor}_{YYYYMMDD}_{HH.MM.SS}.mp4
    std::string file_prefix = "rec_";
    // 单文件字节上限，超出滚动到新文件
    int64_t max_segment_bytes = 1024LL * 1024 * 1024;
    // 目录内保留的最大文件数（超出删除最旧）；<=0 表示不清理
    int max_file_count = 24;
    // 时间源（毫秒）。默认系统墙钟；测试可注入虚拟时钟。
    // 音视频 pts 统一由该时钟驱动(ms*90 / ms*48)，保证两轨同步。
    std::function<int64_t()> clock_ms = nullptr;
    // 滚动到新段后回调，请求适配层插入关键帧（render：插件 InsertIdr；客户端可留空）
    std::function<void()> on_request_keyframe = nullptr;
};

class RecordWriter {
public:
    static std::shared_ptr<RecordWriter> Make(const RecordWriterConfig& cfg);
    ~RecordWriter();

    RecordWriter(const RecordWriter&) = delete;
    RecordWriter& operator=(const RecordWriter&) = delete;

    // 编码视频帧（Annex-B，含起始码；宽高以首次到达为准）
    void OnEncodedVideo(const uint8_t* data, size_t size,
                        RecordVideoCodec codec, int width, int height, bool key);
    // 编码音频包（Opus，约定 48kHz/立体声/20ms）
    void OnEncodedAudio(const uint8_t* data, size_t size);

    // 结束录制：写 trailer、关文件、执行滚动清理。已入队数据由适配层先排空再调用。
    void Stop();

    // 是否处于录制会话中（含等待关键帧阶段）
    bool IsRecording() const;

private:
    explicit RecordWriter(const RecordWriterConfig& cfg);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
