//
// 共享录制核心实现。
//
// 关键设计(基于当前链接的 ffmpeg 8.1.1 movenc 源码确认, 见 movenc.c:6844-6861 / :6932-6945 / :881-884)：
// - MP4 muxer 从"第一个写入的视频包"里提取 SPS/PPS(H264) / VPS+SPS+PPS(H265)
//   生成 avcC/hvcC box，因此每个分段的第一个包必须携带参数集；
//   本实现缓存参数集并在开段时前置补齐(缺哪个补哪个)，不依赖编码器 repeat-headers。
// - 后续 Annex-B 包 muxer 自动转换为 length-prefixed 写入 mdat。
// - Opus 轨必须带 >=19 字节 OpusHead extradata，否则 movenc 报 "invalid extradata size"。
// - pts：视频 time_base 1/90000，pts = 会话毫秒 * 90；音频 time_base 1/48000，
//   pts = 会话毫秒 * 48。同一墙钟驱动两轨 => 天然同步，丢帧互不影响。
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

struct Nal {
    const uint8_t* data;
    size_t size;
};

// Annex-B 拆分（支持 3/4 字节起始码）
std::vector<Nal> SplitNals(const uint8_t* data, size_t size) {
    std::vector<Nal> nals;
    if (!data || size == 0) {
        return nals;
    }
    auto start_code_len = [&](size_t p) -> size_t {
        if (p + 4 <= size && data[p] == 0 && data[p + 1] == 0 &&
            data[p + 2] == 0 && data[p + 3] == 1) {
            return 4;
        }
        if (p + 3 <= size && data[p] == 0 && data[p + 1] == 0 && data[p + 2] == 1) {
            return 3;
        }
        return 0;
    };

    size_t i = 0;
    // 跳过开头的起始码
    while (i < size && start_code_len(i) == 0) {
        ++i;
    }
    if (i >= size) {
        return nals;
    }
    i += start_code_len(i);
    size_t start = i;
    while (i < size) {
        size_t len = start_code_len(i);
        if (len > 0) {
            if (i > start) {
                nals.push_back({data + start, i - start});
            }
            i += len;
            start = i;
        } else {
            ++i;
        }
    }
    if (size > start) {
        nals.push_back({data + start, size - start});
    }
    return nals;
}

int H264NalType(const Nal& nal) {
    return nal.size > 0 ? (nal.data[0] & 0x1F) : -1;
}

int H265NalType(const Nal& nal) {
    return nal.size > 0 ? ((nal.data[0] >> 1) & 0x3F) : -1;
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
          recording_(true) {
        CleanupStaleRecordingMarkers();
    }

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

    // ---- 媒体信息 ----
    RecordVideoCodec codec_ = RecordVideoCodec::kH264;
    int width_ = 0;
    int height_ = 0;

    // ---- 参数集缓存(不含起始码) ----
    std::vector<uint8_t> vps_, sps_, pps_;

    // ---- 等关键帧期间的音频缓冲 ----
    std::vector<std::vector<uint8_t>> audio_buffer_;

    // ---- ffmpeg ----
    AVFormatContext* fmt_ = nullptr;
    AVStream* vstream_ = nullptr;
    AVStream* astream_ = nullptr;
    int64_t written_bytes_ = 0;
    std::string current_path_; // 当前分段路径(sidecar 标记用)

    // ---- 录制中 sidecar 标记(xxx.mp4.recording) ----
    // 文件打开(写完 header)后创建,关闭(moov 落盘)后删除;
    // 录像查看功能据此过滤不可播的进行中文件,不用 mtime 启发式
    void CreateRecordingMarker() {
        if (current_path_.empty()) return;
        std::ofstream(current_path_ + ".recording", std::ios::trunc).close();
    }

    void RemoveRecordingMarker() {
        if (current_path_.empty()) return;
        std::error_code ec;
        std::filesystem::remove(current_path_ + ".recording", ec);
        current_path_.clear();
    }

    // 进程启动时本 writer 尚无打开文件,目录里残留的标记都是上次崩溃的孤儿
    void CleanupStaleRecordingMarkers() {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(cfg_.dir, ec)) return;
        for (auto& e : fs::directory_iterator(cfg_.dir, ec)) {
            if (e.path().extension() == ".recording") {
                fs::remove(e.path(), ec);
            }
        }
    }

    // 墙钟 ms*90 / ms*48 量化后可能出现同毫秒多帧 => dts 重复。
    // 对 muxer/播放器做严格单调递增保护(同段内)。
    int64_t last_video_dts_ = -1;
    int64_t last_audio_dts_ = -1;

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

    void OnEncodedVideo(const uint8_t* data, size_t size,
                        RecordVideoCodec codec, int width, int height, bool key) {
        if (!recording_ || !data || size == 0) {
            return;
        }
        codec_ = codec;
        width_ = width;
        height_ = height;

        // 1. 收集参数集（任何时候出现都更新缓存）
        //    注意: PPS 最短只有 4 字节(1 字节 NAL 头 + 3 字节), 过滤条件不能用 size>4
        for (const auto& nal : SplitNals(data, size)) {
            if (codec_ == RecordVideoCodec::kH264) {
                int t = H264NalType(nal);
                if (t == 7 && nal.size > 1) {
                    sps_.assign(nal.data, nal.data + nal.size);
                } else if (t == 8 && nal.size > 1) {
                    pps_.assign(nal.data, nal.data + nal.size);
                }
            } else {
                int t = H265NalType(nal);
                if (t == 32 && nal.size > 1) {
                    vps_.assign(nal.data, nal.data + nal.size);
                } else if (t == 33 && nal.size > 1) {
                    sps_.assign(nal.data, nal.data + nal.size);
                } else if (t == 34 && nal.size > 1) {
                    pps_.assign(nal.data, nal.data + nal.size);
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
            WriteVideo(data, size, /*prepend_params=*/true);
            return;
        }

        // 3. 已开段：写满则滚动
        if (written_bytes_ >= cfg_.max_segment_bytes) {
            CloseFile();
            if (cfg_.on_request_keyframe) {
                cfg_.on_request_keyframe();
            }
            if (!key) {
                return; // 丢弃非关键帧，等下一个关键帧开新段
            }
            if (!OpenFile()) {
                return;
            }
            WriteVideo(data, size, /*prepend_params=*/true);
            return;
        }

        WriteVideo(data, size, /*prepend_params=*/false);
    }

    void OnEncodedAudio(const uint8_t* data, size_t size) {
        if (!recording_ || !data || size == 0) {
            return;
        }
        if (!writing_) {
            // 等关键帧期间缓冲，开段后回填（上限保护）
            if (audio_buffer_.size() >= kMaxAudioBufferPackets) {
                audio_buffer_.erase(audio_buffer_.begin());
            }
            audio_buffer_.emplace_back(data, data + size);
            return;
        }
        WriteAudio(data, size);
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
    void WriteVideo(const uint8_t* data, size_t size, bool prepend_params) {
        std::vector<uint8_t> combined;
        const uint8_t* pdata = data;
        size_t psize = size;
        if (prepend_params && ParamsReady()) {
            // 缺哪个参数集补哪个（避免 avcC/hvcC 里出现重复 SPS/PPS）
            bool has_vps = false, has_sps = false, has_pps = false;
            for (const auto& nal : SplitNals(data, size)) {
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
                combined.insert(combined.end(), data, data + size);
                pdata = combined.data();
                psize = combined.size();
            }
        }

        AVPacket* pkt = av_packet_alloc();
        if (!pkt) {
            return;
        }
        pkt->stream_index = vstream_->index;
        pkt->data = (uint8_t*)pdata;
        pkt->size = (int)psize;
        int64_t pts = ElapsedMs() * 90; // 1/90000 时间基
        if (pts <= last_video_dts_) {
            pts = last_video_dts_ + 1; // 墙钟量化导致重复 dts, 钳制为严格递增
        }
        last_video_dts_ = pts;
        pkt->pts = pts;
        pkt->dts = pts;
        if (fmt_) {
            av_interleaved_write_frame(fmt_, pkt);
        }
        av_packet_free(&pkt);
        written_bytes_ += (int64_t)psize;
    }

    void WriteAudio(const uint8_t* data, size_t size) {
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) {
            return;
        }
        pkt->stream_index = astream_->index;
        pkt->data = (uint8_t*)data;
        pkt->size = (int)size;
        int64_t pts = ElapsedMs() * 48; // 1/48000 时间基
        if (pts <= last_audio_dts_) {
            pts = last_audio_dts_ + 1;
        }
        last_audio_dts_ = pts;
        pkt->pts = pts;
        pkt->dts = pts;
        if (fmt_) {
            av_interleaved_write_frame(fmt_, pkt);
        }
        av_packet_free(&pkt);
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
        int r = avformat_alloc_output_context2(&fmt_, nullptr, "mp4", path.c_str());
        if (r < 0) {
            std::fprintf(stderr, "[record_writer] alloc output ctx failed: %d\n", r);
            fmt_ = nullptr;
            return false;
        }

        vstream_ = avformat_new_stream(fmt_, nullptr);
        if (!vstream_) {
            CloseFile();
            return false;
        }
        vstream_->time_base = {1, 90000};
        auto* vc = vstream_->codecpar;
        vc->codec_type = AVMEDIA_TYPE_VIDEO;
        vc->codec_id = (codec_ == RecordVideoCodec::kH265) ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
        vc->width = width_;
        vc->height = height_;

        astream_ = avformat_new_stream(fmt_, nullptr);
        if (!astream_) {
            CloseFile();
            return false;
        }
        astream_->time_base = {1, 48000};
        auto* ac = astream_->codecpar;
        ac->codec_type = AVMEDIA_TYPE_AUDIO;
        ac->codec_id = AV_CODEC_ID_OPUS;
        ac->sample_rate = 48000;
        ac->format = AV_SAMPLE_FMT_S16;
        av_channel_layout_default(&ac->ch_layout, 2);
        ac->extradata = (uint8_t*)av_malloc(sizeof(kOpusHead) + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!ac->extradata) {
            CloseFile();
            return false;
        }
        std::memcpy(ac->extradata, kOpusHead, sizeof(kOpusHead));
        std::memset(ac->extradata + sizeof(kOpusHead), 0, AV_INPUT_BUFFER_PADDING_SIZE);
        ac->extradata_size = (int)sizeof(kOpusHead);

        r = avio_open(&fmt_->pb, path.c_str(), AVIO_FLAG_WRITE);
        if (r < 0) {
            std::fprintf(stderr, "[record_writer] avio_open failed: %d\n", r);
            CloseFile();
            return false;
        }
        r = avformat_write_header(fmt_, nullptr);
        if (r < 0) {
            std::fprintf(stderr, "[record_writer] write_header failed: %d\n", r);
            CloseFile();
            return false;
        }

        written_bytes_ = 0;
        ++segment_no_;
        writing_ = true;
        CreateRecordingMarker();
        std::fprintf(stderr, "[record_writer] segment %lld opened: %s\n",
                     (long long)segment_no_, path.c_str());

        // 回填等关键帧期间缓冲的音频
        for (auto& a : audio_buffer_) {
            WriteAudio(a.data(), a.size());
        }
        audio_buffer_.clear();
        return true;
    }

    void CloseFile() {
        if (fmt_) {
            av_write_trailer(fmt_);
            if (fmt_->pb) {
                avio_close(fmt_->pb);
                fmt_->pb = nullptr;
            }
            avformat_free_context(fmt_); // 会释放 streams 及 codecpar.extradata
            fmt_ = nullptr;
        }
        vstream_ = nullptr;
        astream_ = nullptr;
        written_bytes_ = 0;
        writing_ = false;
        RemoveRecordingMarker(); // moov 已落盘,文件可播
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

RecordWriter::RecordWriter(const RecordWriterConfig& cfg)
    : impl_(std::make_unique<Impl>(cfg)) {}

RecordWriter::~RecordWriter() = default;

std::shared_ptr<RecordWriter> RecordWriter::Make(const RecordWriterConfig& cfg) {
    return std::shared_ptr<RecordWriter>(new RecordWriter(cfg));
}

void RecordWriter::OnEncodedVideo(const uint8_t* data, size_t size,
                                  RecordVideoCodec codec, int width, int height, bool key) {
    impl_->OnEncodedVideo(data, size, codec, width, height, key);
}

void RecordWriter::OnEncodedAudio(const uint8_t* data, size_t size) {
    impl_->OnEncodedAudio(data, size);
}

void RecordWriter::Stop() {
    impl_->Stop();
}

bool RecordWriter::IsRecording() const {
    return impl_->IsRecording();
}

}
