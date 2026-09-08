//
// 共享录制核心实现。
//
// 关键设计(基于当前链接的 ffmpeg 8.1.1 movenc 源码确认, 见 movenc.c:6844-6861 / :6932-6945 / :881-884)：
// - MP4 muxer 从"第一个写入的视频包"里提取 SPS/PPS(H264) / VPS+SPS+PPS(H265)
//   生成 avcC/hvcC box，因此每个分段的第一个包必须携带参数集；
//   本实现缓存参数集并在开段时前置补齐(缺哪个补哪个)，不依赖编码器 repeat-headers。
// - 后续 Annex-B 包 muxer 自动转换为 length-prefixed 写入 mdat。
// - Opus 轨必须带 >=19 字节 OpusHead extradata，否则 movenc 报 "invalid extradata size"。
// - pts：写头后读取 muxer 最终 time_base，并把同一分段起点后的单调毫秒换算到两轨；
//   等关键帧期间早于分段起点的音频丢弃，避免新分段出现只有音频的负向前导。
// - 分段：写满 max_segment_bytes -> trailer 关文件 -> 回调请求关键帧 ->
//   丢弃非关键视频帧(音频入队缓冲) -> 下一个关键帧开新段 -> 回填缓冲音频。
//

#include "record_writer.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/channel_layout.h"
#include "libavutil/mathematics.h"
#include "libavutil/time.h"
}

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace px {

namespace {

int64_t DefaultClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 墙上时钟毫秒(Unix epoch)。仅用于文件名时间戳——单调时钟(DefaultClockMs)
// 从系统启动起算, 格式化出来是 1970 年的假日期。
int64_t WallClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// OpusHead(19 字节)：Magic/版本/声道数/预跳过(312)/采样率(48000)/增益/mapping family。
// 缺它 MP4 里的 Opus 轨无法播放（movenc 报 invalid extradata size）。
const uint8_t kOpusHead[19] = {
    'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', // Magic signature
    0x01,                                   // Version
    0x02,                                   // Channel count
    0x38, 0x01,                             // Pre-skip (312 LE)
    0x80, 0xBB, 0x00, 0x00,                 // Sample rate (48000 LE)
    0x00, 0x00,                             // Output gain
    0x00,                                   // Channel mapping family
};

// 缓冲上限：约 10 秒音频(50 包/秒 * 20ms)，超出丢最旧
constexpr size_t kMaxAudioBufferPackets = 512;
constexpr size_t kMaxOpusPacketBytes = 1275;

bool IsValidOpusFrameSamples(const int frame_samples) {
    return frame_samples == 120 || frame_samples == 240 || frame_samples == 480 || frame_samples == 960 || frame_samples == 1920 ||
           frame_samples == 2880;
}

struct Nal final {
    std::span<const uint8_t> bytes;
};

struct AvPacketDeleter final {
    void operator()(AVPacket* packet) const noexcept { // NOLINT(gammaray-raw-pointer-boundary)
        av_packet_free(&packet);
    }
};

struct AvFormatContextDeleter final {
    void operator()(AVFormatContext* context) const noexcept { // NOLINT(gammaray-raw-pointer-boundary)
        if (!context) {
            return;
        }
        if (context->pb) {
            avio_closep(&context->pb);
        }
        avformat_free_context(context);
    }
};

using AvPacketHandle = std::unique_ptr<AVPacket, AvPacketDeleter>;
using AvFormatContextHandle = std::unique_ptr<AVFormatContext, AvFormatContextDeleter>;

// Annex-B 拆分（支持 3/4 字节起始码）
std::vector<Nal> SplitNals(const std::span<const uint8_t> data) {
    std::vector<Nal> nals;
    if (data.empty()) {
        return nals;
    }
    auto start_code_len = [&](size_t p) -> size_t {
        if (p + 4 <= data.size() && data[p] == 0 && data[p + 1] == 0 &&
            data[p + 2] == 0 && data[p + 3] == 1) {
            return 4;
        }
        if (p + 3 <= data.size() && data[p] == 0 && data[p + 1] == 0 && data[p + 2] == 1) {
            return 3;
        }
        return 0;
    };

    size_t i = 0;
    // 跳过开头的起始码
    while (i < data.size() && start_code_len(i) == 0) {
        ++i;
    }
    if (i >= data.size()) {
        return nals;
    }
    i += start_code_len(i);
    size_t start = i;
    while (i < data.size()) {
        size_t len = start_code_len(i);
        if (len > 0) {
            if (i > start) {
                nals.push_back({data.subspan(start, i - start)});
            }
            i += len;
            start = i;
        } else {
            ++i;
        }
    }
    if (data.size() > start) {
        nals.push_back({data.subspan(start)});
    }
    return nals;
}

int H264NalType(const Nal& nal) {
    return nal.bytes.empty() ? -1 : (nal.bytes.front() & 0x1F);
}

int H265NalType(const Nal& nal) {
    return nal.bytes.empty() ? -1 : ((nal.bytes.front() >> 1) & 0x3F);
}

std::string SanitizeFileNamePart(std::string s) {
    // 去掉设备路径前缀 \\.\ (显示器名如 \\.\DISPLAY1 -> DISPLAY1)
    if (s.size() >= 4 && s[0] == '\\' && s[1] == '\\' && s[2] == '.' && s[3] == '\\') {
        s = s.substr(4);
    }
    for (auto& c : s) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c == ' ' ||
            c == '\t' || c == '\n' || c == '\r') {
            c = '_';
        }
    }
    if (s.empty() || s == "." || s == "..") {
        s = "default";
    }
    return s;
}

// 人类可读时间戳: YYYYMMDD_HH.MM.SS (例: 20260817_12.43.28)
std::string FormatTimestamp(int64_t ms) {
    std::time_t secs = (std::time_t)(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char buf[64] = {0};
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H.%M.%S", &tm);
    return std::string(buf);
}

} // namespace

struct RecordWriter::Impl {
    explicit Impl(const RecordWriterConfig& cfg)
        : cfg_(cfg),
          clock_ms_(cfg.clock_ms ? cfg.clock_ms : std::function<int64_t()>(DefaultClockMs)),
          session_start_ms_(clock_ms_()),
          recording_(true) {}

    ~Impl() {
        Stop();
    }

    // ---- 状态 ----
    RecordWriterConfig cfg_;
    std::function<int64_t()> clock_ms_;
    int64_t session_start_ms_ = 0;
    bool recording_ = false;
    bool writing_ = false;
    int64_t segment_no_ = 0;
    std::string error_{};
    uint64_t completed_segments_{};

    void Fail(std::string reason) {
        if (error_.empty()) error_ = std::move(reason);
        recording_ = false;
    }

    // ---- 媒体信息 ----
    RecordVideoCodec codec_ = RecordVideoCodec::kH264;
    int width_ = 0;
    int height_ = 0;

    // ---- 参数集缓存(不含起始码) ----
    std::vector<uint8_t> vps_, sps_, pps_;

    // ---- 等关键帧期间的音频缓冲 ----
    struct BufferedAudio final {
        std::vector<uint8_t> payload;
        int64_t elapsed_ms{0};
        int frame_samples{0};
    };
    std::vector<BufferedAudio> audio_buffer_;

    // ---- ffmpeg ----
    AvFormatContextHandle fmt_;
    int video_stream_index_{-1};
    int audio_stream_index_{-1};
    AVRational video_time_base_{1, 90000};
    AVRational audio_time_base_{1, 48000};
    int64_t segment_start_elapsed_ms_{0};
    int64_t written_bytes_ = 0;
    std::string current_path_; // 当前分段路径(sidecar 标记用)

    // ---- 录制中 sidecar 标记(xxx.mp4.recording) ----
    // 文件打开(写完 header)后创建,关闭(moov 落盘)后删除;
    // 录像查看功能据此过滤不可播的进行中文件,不用 mtime 启发式
    void CreateRecordingMarker() {
        if (current_path_.empty()) return;
        auto marker = std::ofstream(current_path_ + ".recording", std::ios::trunc);
        marker.close();
        if (!marker) Fail("recording_marker_create_failed");
    }

    void RemoveRecordingMarker() {
        if (current_path_.empty()) return;
        std::error_code ec;
        std::filesystem::remove(current_path_ + ".recording", ec);
        current_path_.clear();
    }

    // Other runs may still be writing. A stale marker also does not prove its MP4 was finalized.

    // 墙钟 ms*90 / ms*48 量化后可能出现同毫秒多帧 => dts 重复。
    // 对 muxer/播放器做严格单调递增保护(同段内)。
    int64_t last_video_dts_ = -1;
    int64_t last_audio_dts_ = -1;
    int64_t last_audio_duration_ = 0;

    int64_t ElapsedMs() const {
        auto ms = clock_ms_() - session_start_ms_;
        return ms > 0 ? ms : 0;
    }

    bool ParamsReady() const {
        if (codec_ == RecordVideoCodec::kH265) {
            return !vps_.empty() && !sps_.empty() && !pps_.empty();
        }
        return !sps_.empty() && !pps_.empty();
    }

    void OnEncodedVideo(std::span<const uint8_t> data,
                        RecordVideoCodec codec, int width, int height, bool key) {
        if (!recording_ || data.empty()) {
            return;
        }
        codec_ = codec;
        width_ = width;
        height_ = height;

        // 1. 收集参数集（任何时候出现都更新缓存）
        //    注意: PPS 最短只有 4 字节(1 字节 NAL 头 + 3 字节), 过滤条件不能用 size>4
        for (const auto& nal : SplitNals(data)) {
            if (codec_ == RecordVideoCodec::kH264) {
                int t = H264NalType(nal);
                if (t == 7 && nal.bytes.size() > 1) {
                    sps_.assign(nal.bytes.begin(), nal.bytes.end());
                } else if (t == 8 && nal.bytes.size() > 1) {
                    pps_.assign(nal.bytes.begin(), nal.bytes.end());
                }
            } else {
                int t = H265NalType(nal);
                if (t == 32 && nal.bytes.size() > 1) {
                    vps_.assign(nal.bytes.begin(), nal.bytes.end());
                } else if (t == 33 && nal.bytes.size() > 1) {
                    sps_.assign(nal.bytes.begin(), nal.bytes.end());
                } else if (t == 34 && nal.bytes.size() > 1) {
                    pps_.assign(nal.bytes.begin(), nal.bytes.end());
                }
            }
        }

        // 2. 未开段：等"参数集齐 + 关键帧"才开
        if (!writing_) {
            if (!(key && ParamsReady())) {
                return;
            }
            if (!OpenFile()) {
                return;
            }
            WriteVideo(data, /*prepend_params=*/true);
            return;
        }

        // 3. 已开段：写满则滚动
        if (written_bytes_ >= cfg_.max_segment_bytes) {
            CloseFile();
            if (!error_.empty()) return;
            if (cfg_.on_request_keyframe) {
                cfg_.on_request_keyframe();
            }
            if (!key) {
                return; // 丢弃非关键帧，等下一个关键帧开新段
            }
            if (!OpenFile()) {
                return;
            }
            WriteVideo(data, /*prepend_params=*/true);
            return;
        }

        WriteVideo(data, /*prepend_params=*/false);
    }

    void OnEncodedAudio(std::span<const uint8_t> data, const int frame_samples) {
        if (!recording_ || data.empty() || data.size() > kMaxOpusPacketBytes || !IsValidOpusFrameSamples(frame_samples)) {
            return;
        }
        if (!writing_) {
            // 等关键帧期间缓冲，开段后回填（上限保护）
            if (audio_buffer_.size() >= kMaxAudioBufferPackets) {
                audio_buffer_.erase(audio_buffer_.begin());
            }
            audio_buffer_.push_back(BufferedAudio{
                .payload = std::vector<uint8_t>(data.begin(), data.end()),
                .elapsed_ms = ElapsedMs(),
                .frame_samples = frame_samples,
            });
            return;
        }
        WriteAudio(data, ElapsedMs(), frame_samples);
    }

    void Stop() {
        recording_ = false;
        audio_buffer_.clear();
        if (writing_) {
            CloseFile();
        }
    }

    bool IsRecording() const {
        return recording_;
    }

private:
    void WriteVideo(const std::span<const uint8_t> data, bool prepend_params) {
        std::vector<uint8_t> combined;
        auto packet_data = data;
        if (prepend_params && ParamsReady()) {
            // 缺哪个参数集补哪个（避免 avcC/hvcC 里出现重复 SPS/PPS）
            bool has_vps = false, has_sps = false, has_pps = false;
            for (const auto& nal : SplitNals(data)) {
                if (codec_ == RecordVideoCodec::kH264) {
                    int t = H264NalType(nal);
                    if (t == 7) has_sps = true;
                    else if (t == 8) has_pps = true;
                } else {
                    int t = H265NalType(nal);
                    if (t == 32) has_vps = true;
                    else if (t == 33) has_sps = true;
                    else if (t == 34) has_pps = true;
                }
            }
            static const uint8_t kSc[4] = {0, 0, 0, 1};
            auto append = [&](const std::vector<uint8_t>& nal) {
                if (nal.empty()) return;
                combined.insert(combined.end(), kSc, kSc + 4);
                combined.insert(combined.end(), nal.begin(), nal.end());
            };
            if (codec_ == RecordVideoCodec::kH265 && !has_vps) append(vps_);
            if (!has_sps) append(sps_);
            if (!has_pps) append(pps_);
            if (!combined.empty()) {
                combined.insert(combined.end(), data.begin(), data.end());
                packet_data = combined;
            }
        }

        AvPacketHandle pkt{av_packet_alloc()};
        if (!pkt) {
            Fail("recording_packet_alloc_failed");
            return;
        }
        if (packet_data.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            av_new_packet(pkt.get(), static_cast<int>(packet_data.size())) < 0) {
            Fail("recording_packet_alloc_failed");
            return;
        }
        std::memcpy(pkt->data, packet_data.data(), packet_data.size());
        pkt->stream_index = video_stream_index_;
        const auto segment_elapsed_ms = std::max<int64_t>(0, ElapsedMs() - segment_start_elapsed_ms_);
        int64_t pts = av_rescale_q(segment_elapsed_ms, AVRational{1, 1000}, video_time_base_);
        if (pts <= last_video_dts_) {
            pts = last_video_dts_ + 1;
        }
        last_video_dts_ = pts;
        pkt->pts = pts;
        pkt->dts = pts;
        if (fmt_) {
            if (av_interleaved_write_frame(fmt_.get(), pkt.get()) < 0) Fail("recording_video_write_failed");
        }
        written_bytes_ += static_cast<int64_t>(packet_data.size());
    }

    void WriteAudio(const std::span<const uint8_t> data, const int64_t elapsed_ms, const int frame_samples) {
        if (elapsed_ms < segment_start_elapsed_ms_) {
            return;
        }
        AvPacketHandle pkt{av_packet_alloc()};
        if (!pkt) {
            Fail("recording_packet_alloc_failed");
            return;
        }
        if (av_new_packet(pkt.get(), static_cast<int>(data.size())) < 0) {
            Fail("recording_packet_alloc_failed");
            return;
        }
        std::memcpy(pkt->data, data.data(), data.size());
        pkt->stream_index = audio_stream_index_;
        const auto duration = av_rescale_q(frame_samples, AVRational{1, 48000}, audio_time_base_);
        const auto segment_elapsed_ms = elapsed_ms - segment_start_elapsed_ms_;
        const auto pts = last_audio_dts_ < 0
            ? av_rescale_q(segment_elapsed_ms, AVRational{1, 1000}, audio_time_base_)
            : last_audio_dts_ + last_audio_duration_;
        last_audio_dts_ = pts;
        last_audio_duration_ = duration;
        pkt->pts = pts;
        pkt->dts = pts;
        pkt->duration = duration;
        if (fmt_) {
            if (av_interleaved_write_frame(fmt_.get(), pkt.get()) < 0) Fail("recording_audio_write_failed");
        }
    }

    std::string MakeFilePath() const {
        namespace fs = std::filesystem;
        auto mon = SanitizeFileNamePart(cfg_.monitor_name.empty() ? "mon0" : cfg_.monitor_name);
        // 文件名时间戳必须用墙上时钟(人类可读); clock_ms_ 是单调时钟(pts 用)
        auto ts = FormatTimestamp(WallClockMs());
        std::error_code ec;
        fs::create_directories(fs::path(cfg_.dir), ec);
        auto base = cfg_.file_prefix + mon + "_" + ts;
        auto path = fs::path(cfg_.dir) / (base + ".mp4");
        // 同一秒内开新段(小段阈值/测试)会撞名: 追加序号 _1/_2/... 保证不覆盖旧文件
        for (int n = 1; fs::exists(path, ec); ++n) {
            path = fs::path(cfg_.dir) / (base + "_" + std::to_string(n) + ".mp4");
        }
        return path.string();
    }

    bool OpenFile() {
        auto path = MakeFilePath();
        current_path_ = path;
        AVFormatContext* format_context{}; // NOLINT(gammaray-raw-pointer-boundary)
        int r = avformat_alloc_output_context2(&format_context, nullptr, "mp4", path.c_str());
        fmt_.reset(format_context);
        if (r < 0 || !fmt_) {
            Fail("recording_format_alloc_failed");
            std::fprintf(stderr, "[record_writer] alloc output ctx failed: %d\n", r);
            return false;
        }

        const auto video_stream_boundary = avformat_new_stream(fmt_.get(), nullptr); // NOLINT(gammaray-raw-pointer-boundary)
        if (!video_stream_boundary) {
            Fail("recording_stream_alloc_failed");
            CloseFile();
            return false;
        }
        auto& video_stream = *video_stream_boundary;
        video_stream.time_base = {1, 90000};
        video_stream_index_ = video_stream.index;
        auto& video_parameters = *video_stream.codecpar;
        video_parameters.codec_type = AVMEDIA_TYPE_VIDEO;
        video_parameters.codec_id = (codec_ == RecordVideoCodec::kH265) ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
        video_parameters.width = width_;
        video_parameters.height = height_;

        const auto audio_stream_boundary = avformat_new_stream(fmt_.get(), nullptr); // NOLINT(gammaray-raw-pointer-boundary)
        if (!audio_stream_boundary) {
            Fail("recording_stream_alloc_failed");
            CloseFile();
            return false;
        }
        auto& audio_stream = *audio_stream_boundary;
        audio_stream.time_base = {1, 48000};
        audio_stream_index_ = audio_stream.index;
        auto& audio_parameters = *audio_stream.codecpar;
        audio_parameters.codec_type = AVMEDIA_TYPE_AUDIO;
        audio_parameters.codec_id = AV_CODEC_ID_OPUS;
        audio_parameters.sample_rate = 48000;
        audio_parameters.format = AV_SAMPLE_FMT_S16;
        av_channel_layout_default(&audio_parameters.ch_layout, 2);
        audio_parameters.extradata = static_cast<uint8_t*>(av_malloc(sizeof(kOpusHead) + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!audio_parameters.extradata) {
            Fail("recording_audio_parameters_alloc_failed");
            CloseFile();
            return false;
        }
        std::memcpy(audio_parameters.extradata, kOpusHead, sizeof(kOpusHead));
        std::memset(audio_parameters.extradata + sizeof(kOpusHead), 0, AV_INPUT_BUFFER_PADDING_SIZE);
        audio_parameters.extradata_size = static_cast<int>(sizeof(kOpusHead));

        r = avio_open(&fmt_->pb, path.c_str(), AVIO_FLAG_WRITE);
        if (r < 0) {
            Fail("recording_file_open_failed");
            std::fprintf(stderr, "[record_writer] avio_open failed: %d\n", r);
            CloseFile();
            return false;
        }
        CreateRecordingMarker();
        if (!error_.empty()) {
            CloseFile();
            return false;
        }
        r = avformat_write_header(fmt_.get(), nullptr);
        if (r < 0) {
            Fail("recording_header_write_failed");
            std::fprintf(stderr, "[record_writer] write_header failed: %d\n", r);
            CloseFile();
            return false;
        }
        video_time_base_ = video_stream.time_base;
        audio_time_base_ = audio_stream.time_base;
        segment_start_elapsed_ms_ = ElapsedMs();
        last_video_dts_ = -1;
        last_audio_dts_ = -1;
        last_audio_duration_ = 0;

        written_bytes_ = 0;
        ++segment_no_;
        writing_ = true;
        std::fprintf(stderr, "[record_writer] segment %lld opened: %s\n",
                     (long long)segment_no_, path.c_str());

        // 回填等关键帧期间缓冲的音频
        for (auto& a : audio_buffer_) {
            WriteAudio(a.payload, a.elapsed_ms, a.frame_samples);
        }
        audio_buffer_.clear();
        return true;
    }

    void CloseFile() {
        if (fmt_) {
            if (writing_ && av_write_trailer(fmt_.get()) < 0) Fail("recording_trailer_write_failed");
            if (fmt_->pb) {
                avio_flush(fmt_->pb);
                if (fmt_->pb->error < 0) Fail("recording_io_failed");
                if (avio_closep(&fmt_->pb) < 0) Fail("recording_file_close_failed");
            }
            fmt_.reset();
        }
        if (writing_ && error_.empty()) {
            ++completed_segments_;
            RemoveRecordingMarker();
        }
        video_stream_index_ = -1;
        audio_stream_index_ = -1;
        video_time_base_ = {1, 90000};
        audio_time_base_ = {1, 48000};
        segment_start_elapsed_ms_ = 0;
        last_audio_duration_ = 0;
        written_bytes_ = 0;
        writing_ = false;
        current_path_.clear(); // Failed segments keep their marker and cannot be reported as playable.
        CleanupOldFiles();
    }

    // 滚动清理：只删自己前缀的 .mp4，保留最新 max_file_count 个
    void CleanupOldFiles() {
        if (cfg_.max_file_count <= 0) {
            return;
        }
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(cfg_.dir, ec)) {
            return;
        }
        std::vector<fs::directory_entry> ours;
        for (auto& e : fs::directory_iterator(cfg_.dir, ec)) {
            if (!e.is_regular_file(ec)) {
                continue;
            }
            if (fs::exists(e.path().string() + ".recording", ec)) continue;
            auto name = e.path().filename().string();
            if (name.rfind(cfg_.file_prefix, 0) == 0 && name.size() > 4 &&
                name.compare(name.size() - 4, 4, ".mp4") == 0) {
                ours.push_back(e);
            }
        }
        if (ours.size() <= (size_t)cfg_.max_file_count) {
            return;
        }
        std::sort(ours.begin(), ours.end(), [&](const fs::directory_entry& a,
                                                const fs::directory_entry& b) {
            return a.last_write_time(ec) < b.last_write_time(ec);
        });
        size_t to_remove = ours.size() - (size_t)cfg_.max_file_count;
        for (size_t i = 0; i < to_remove; ++i) {
            std::error_code ec2;
            fs::remove(ours[i].path(), ec2);
            std::fprintf(stderr, "[record_writer] cleanup removed: %s\n",
                         ours[i].path().string().c_str());
        }
    }
};

RecordWriter::RecordWriter(ConstructionToken, const RecordWriterConfig& cfg)
    : impl_(std::make_unique<Impl>(cfg)) {}

RecordWriter::~RecordWriter() = default;

std::shared_ptr<RecordWriter> RecordWriter::Make(const RecordWriterConfig& cfg) {
    return std::make_shared<RecordWriter>(ConstructionToken{}, cfg);
}

void RecordWriter::OnEncodedVideo(std::span<const uint8_t> data,
                                  RecordVideoCodec codec, int width, int height, bool key) {
    impl_->OnEncodedVideo(data, codec, width, height, key);
}

void RecordWriter::OnEncodedAudio(std::span<const uint8_t> data, const int frame_samples) {
    impl_->OnEncodedAudio(data, frame_samples);
}

void RecordWriter::Stop() {
    impl_->Stop();
}

bool RecordWriter::IsRecording() const {
    return impl_->IsRecording();
}

const std::string& RecordWriter::Error() const {
    return impl_->error_;
}

uint64_t RecordWriter::CompletedSegments() const {
    return impl_->completed_segments_;
}

}
