//
// 共享录制核心 RecordWriter 的确定性单测。
// - 视频: avcodec libx264 内存内生成 H264 Annex-B(首帧带 SPS/PPS)
// - 音频: 伪 Opus 包(muxer 不校验 payload)
// - 时间: 虚拟时钟注入, 快且可重复
// - 断言: 输出 MP4 用 avformat 重新打开校验(流/时长/同步/avcC/首帧关键帧/可解码)
//

#include <gtest/gtest.h>

#include "record_writer.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
}

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace px;

namespace {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

std::string MakeTempDir() {
    static std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    auto dir = fs::temp_directory_path() /
        ("record_writer_test_" + std::to_string(rng()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir.string();
}

std::vector<fs::path> ListMp4(const std::string& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return out;
    }
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) {
            continue;
        }
        auto name = e.path().filename().string();
        if (name.rfind("rec_", 0) == 0 && name.size() > 4 &&
            name.compare(name.size() - 4, 4, ".mp4") == 0) {
            out.push_back(e.path());
        }
    }
    return out;
}

struct FileInfo {
    int video_index = -1;
    int audio_index = -1;
    int64_t duration_ms = 0;       // 容器时长
    int64_t video_duration_ms = 0; // 视频轨时长
    int64_t audio_duration_ms = 0; // 音频轨时长
    int width = 0;
    int height = 0;
    bool avcc_present = false;
};

bool OpenAndInspect(const fs::path& path, AVFormatContext** out_fc, FileInfo* out_info) {
    AVFormatContext* fc = nullptr;
    if (avformat_open_input(&fc, path.string().c_str(), nullptr, nullptr) < 0) {
        return false;
    }
    if (avformat_find_stream_info(fc, nullptr) < 0) {
        avformat_close_input(&fc);
        return false;
    }
    FileInfo info;
    for (unsigned i = 0; i < fc->nb_streams; ++i) {
        auto* st = fc->streams[i];
        auto* par = st->codecpar;
        auto track_ms = [&]() -> int64_t {
            if (st->duration == AV_NOPTS_VALUE || st->duration <= 0) {
                return 0;
            }
            return st->duration * 1000 * st->time_base.num / st->time_base.den;
        };
        if (par->codec_type == AVMEDIA_TYPE_VIDEO && par->codec_id == AV_CODEC_ID_H264) {
            info.video_index = (int)i;
            info.width = par->width;
            info.height = par->height;
            info.avcc_present = par->extradata_size > 0;
            info.video_duration_ms = track_ms();
        }
        else if (par->codec_type == AVMEDIA_TYPE_AUDIO && par->codec_id == AV_CODEC_ID_OPUS) {
            info.audio_index = (int)i;
            info.audio_duration_ms = track_ms();
        }
    }
    info.duration_ms = fc->duration > 0 ? fc->duration / 1000 : 0;
    *out_fc = fc;
    *out_info = info;
    return true;
}

// 第一个视频包必须是关键帧(段首可独立解码)
bool FirstVideoPacketIsKey(AVFormatContext* fc, int video_index) {
    AVPacket* pkt = av_packet_alloc();
    bool ok = false;
    while (av_read_frame(fc, pkt) >= 0) {
        if (pkt->stream_index == video_index) {
            ok = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
            av_packet_unref(pkt);
            break;
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return ok;
}

// 从段首开始解码, 能解出 >= min_frames 帧才算可播放
bool DecodesFromStart(AVFormatContext* fc, int video_index, int min_frames) {
    const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!dec) {
        return false;
    }
    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    if (!ctx) {
        return false;
    }
    // 必须把流的 codecpar(含 avcC extradata)传给解码器
    if (avcodec_parameters_to_context(ctx, fc->streams[video_index]->codecpar) < 0) {
        avcodec_free_context(&ctx);
        return false;
    }
    if (avcodec_open2(ctx, dec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return false;
    }
    av_seek_frame(fc, -1, 0, AVSEEK_FLAG_BACKWARD);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int decoded = 0;
    while (av_read_frame(fc, pkt) >= 0) {
        if (pkt->stream_index != video_index) {
            av_packet_unref(pkt);
            continue;
        }
        if (avcodec_send_packet(ctx, pkt) == 0) {
            while (avcodec_receive_frame(ctx, frame) == 0) {
                ++decoded;
                if (decoded >= min_frames) {
                    break;
                }
            }
        }
        av_packet_unref(pkt);
        if (decoded >= min_frames) {
            break;
        }
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    return decoded >= min_frames;
}

// ---------------------------------------------------------------------------
// H264 generator (libx264, Annex-B, 首帧含 SPS/PPS)
// ---------------------------------------------------------------------------

struct H264Gen {
    AVCodecContext* ctx = nullptr;
    int fps = 30;
};

bool OpenH264Encoder(H264Gen& g, int width, int height, int fps, int gop) {
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        return false;
    }
    g.fps = fps;
    g.ctx = avcodec_alloc_context3(codec);
    g.ctx->width = width;
    g.ctx->height = height;
    g.ctx->time_base = {1, fps};
    g.ctx->framerate = {fps, 1};
    g.ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    g.ctx->gop_size = gop;
    g.ctx->max_b_frames = 0;
    g.ctx->thread_count = 1;
    av_opt_set(g.ctx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(g.ctx->priv_data, "tune", "zerolatency", 0);
    return avcodec_open2(g.ctx, codec, nullptr) >= 0;
}

void CloseH264Encoder(H264Gen& g) {
    if (g.ctx) {
        avcodec_free_context(&g.ctx);
        g.ctx = nullptr;
    }
}

// 返回一帧编码结果(可能为空:编码器延迟)。force_key 强制 I 帧。
std::vector<uint8_t> EncodeOneFrame(H264Gen& g, int64_t pts, bool force_key) {
    AVFrame* frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = g.ctx->width;
    frame->height = g.ctx->height;
    av_frame_get_buffer(frame, 32);
    for (int y = 0; y < g.ctx->height; ++y) {
        for (int x = 0; x < g.ctx->width; ++x) {
            frame->data[0][y * frame->linesize[0] + x] =
                (uint8_t)((x * 2 + y + pts * 3) & 0xFF);
        }
    }
    for (int y = 0; y < g.ctx->height / 2; ++y) {
        for (int x = 0; x < g.ctx->width / 2; ++x) {
            frame->data[1][y * frame->linesize[1] + x] = 128;
            frame->data[2][y * frame->linesize[2] + x] = 128;
        }
    }
    frame->pts = pts;
    if (force_key) {
        frame->pict_type = AV_PICTURE_TYPE_I;
    }
    int ret = avcodec_send_frame(g.ctx, frame);
    av_frame_free(&frame);
    if (ret < 0) {
        return {};
    }
    std::vector<uint8_t> out;
    AVPacket* pkt = av_packet_alloc();
    while (avcodec_receive_packet(g.ctx, pkt) == 0) {
        out.insert(out.end(), pkt->data, pkt->data + pkt->size);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return out;
}

std::vector<uint8_t> FakeOpusPacket(size_t size, uint8_t seed) {
    std::vector<uint8_t> p(size);
    for (size_t i = 0; i < size; ++i) {
        p[i] = (uint8_t)(seed + i * 7);
    }
    return p;
}

struct VirtualClock {
    int64_t ms = 100000; // 从非 0 起点, 验证时间基换算
    int64_t operator()() { return ms; }
};

// 按真实时间交错喂 N 秒视频(30fps, GOP=30) + 音频(20ms/包), 共用同一虚拟墙钟
void FeedAV(RecordWriter& w, H264Gen& gen, VirtualClock& clk,
            int seconds, int fps, int gop, bool with_audio) {
    const int64_t base = clk.ms;
    const int total_ms = seconds * 1000;
    const int frame_dur = 1000 / fps;
    int video_next_ms = 0;
    int audio_next_ms = 0;
    int audio_idx = 0;
    while (video_next_ms < total_ms || (with_audio && audio_next_ms < total_ms)) {
        if (video_next_ms <= audio_next_ms && video_next_ms < total_ms) {
            clk.ms = base + video_next_ms;
            int frame_no = video_next_ms / frame_dur;
            bool key = (frame_no % gop == 0);
            auto pkt = EncodeOneFrame(gen, frame_no, key);
            if (!pkt.empty()) {
                w.OnEncodedVideo(std::span<const uint8_t>(pkt), RecordVideoCodec::kH264,
                                 gen.ctx->width, gen.ctx->height, key);
            }
            video_next_ms += frame_dur;
        }
        else {
            clk.ms = base + audio_next_ms;
            auto a = FakeOpusPacket(40, (uint8_t)audio_idx++);
            w.OnEncodedAudio(std::span<const uint8_t>(a));
            audio_next_ms += 20;
        }
    }
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

TEST(RecordWriter, BasicAVSync) {
    auto dir = MakeTempDir();
    VirtualClock clk;
    RecordWriterConfig cfg;
    cfg.dir = dir;
    cfg.monitor_name = "mon1";
    cfg.clock_ms = [&]() { return clk.ms; };
    auto w = RecordWriter::Make(cfg);

    H264Gen gen;
    ASSERT_TRUE(OpenH264Encoder(gen, 640, 360, 30, 30));
    FeedAV(*w, gen, clk, 10, 30, 30, true);
    w->Stop();
    CloseH264Encoder(gen);

    auto files = ListMp4(dir);
    ASSERT_EQ(files.size(), 1u) << "should produce exactly one file";

    AVFormatContext* fc = nullptr;
    FileInfo info;
    ASSERT_TRUE(OpenAndInspect(files[0], &fc, &info));
    ASSERT_GE(info.video_index, 0);
    ASSERT_GE(info.audio_index, 0);
    EXPECT_EQ(info.width, 640);
    EXPECT_EQ(info.height, 360);
    EXPECT_TRUE(info.avcc_present) << "mp4 must carry avcC";

    // 时长: 名义 ~10s, 允许编码器延迟容差; 两轨同墙钟 => 互差 < 300ms(同步回归)
    EXPECT_GT(info.video_duration_ms, 9000);
    EXPECT_LT(info.video_duration_ms, 11000);
    EXPECT_GT(info.audio_duration_ms, 8000);
    EXPECT_NEAR(info.video_duration_ms, info.audio_duration_ms, 300)
        << "audio/video tracks must stay in sync";

    // 首帧关键帧 + 可从头解码
    EXPECT_TRUE(FirstVideoPacketIsKey(fc, info.video_index));
    EXPECT_TRUE(DecodesFromStart(fc, info.video_index, 5));
    avformat_close_input(&fc);
    fs::remove_all(dir);
}

TEST(RecordWriter, RollingAndCleanup) {
    auto dir = MakeTempDir();
    VirtualClock clk;
    RecordWriterConfig cfg;
    cfg.dir = dir;
    cfg.monitor_name = "mon0";
    cfg.max_segment_bytes = 150 * 1024; // 150KB/段, 30s 素材必然滚动多次
    cfg.max_file_count = 3;
    cfg.clock_ms = [&]() { return clk.ms; };
    int key_requests = 0;
    cfg.on_request_keyframe = [&]() { ++key_requests; };
    auto w = RecordWriter::Make(cfg);

    H264Gen gen;
    ASSERT_TRUE(OpenH264Encoder(gen, 640, 360, 30, 30));
    FeedAV(*w, gen, clk, 30, 30, 30, true);
    w->Stop();
    CloseH264Encoder(gen);

    auto files = ListMp4(dir);
    ASSERT_LE(files.size(), 3u) << "cleanup must cap file count at max_file_count";
    ASSERT_GE(files.size(), 2u) << "30s @150KB segments must roll at least once";
    EXPECT_GT(key_requests, 0) << "rollover must request keyframes";

    int64_t video_total_ms = 0;
    int64_t audio_total_ms = 0;
    for (auto& f : files) {
        AVFormatContext* fc = nullptr;
        FileInfo info;
        ASSERT_TRUE(OpenAndInspect(f, &fc, &info));
        EXPECT_GE(info.video_index, 0);
        EXPECT_TRUE(info.avcc_present);
        EXPECT_TRUE(FirstVideoPacketIsKey(fc, info.video_index));
        EXPECT_TRUE(DecodesFromStart(fc, info.video_index, 3));
        video_total_ms += info.video_duration_ms;
        audio_total_ms += info.audio_duration_ms;
        avformat_close_input(&fc);
    }
    // 各段音频时长之和 ≈ 各段视频时长之和(音频连续, 误差 < 1.5s)
    EXPECT_NEAR((double)video_total_ms, (double)audio_total_ms, 1500.0)
        << "audio must stay continuous across segments";
    // 幸存段(3 个上限)至少覆盖 ~2 个完整段; 旧段被删是滚动清理的预期行为
    EXPECT_GT(video_total_ms, 9000) << "surviving segments must cover at least ~2 full segments";
}

TEST(RecordWriter, RecordingSidecarMarker) {
    auto dir = MakeTempDir();
    VirtualClock clk;
    RecordWriterConfig cfg;
    cfg.dir = dir;
    cfg.clock_ms = [&]() { return clk.ms; };

    H264Gen gen;
    ASSERT_TRUE(OpenH264Encoder(gen, 320, 240, 30, 30));

    {
        auto w = RecordWriter::Make(cfg);
        FeedAV(*w, gen, clk, 2, 30, 30, true);

        // 录制中: mp4 存在且 sidecar 标记存在(查看功能据此过滤不可播文件)
        auto files = ListMp4(dir);
        ASSERT_EQ(files.size(), 1u);
        const auto marker = files[0].string() + ".recording";
        EXPECT_TRUE(fs::exists(marker)) << "in-progress file must have .recording sidecar";

        w->Stop();
        // 关闭后 moov 落盘: sidecar 必须被删除
        EXPECT_FALSE(fs::exists(marker)) << "finished file must not keep .recording sidecar";
        EXPECT_TRUE(fs::exists(files[0]));
    }

    // 崩溃残留: 手工造孤儿标记,新 writer 构造时应清理(此时本 writer 无打开文件)
    auto files = ListMp4(dir);
    ASSERT_EQ(files.size(), 1u);
    const auto orphan = files[0].string() + ".recording";
    {
        std::ofstream(orphan, std::ios::trunc).close();
        ASSERT_TRUE(fs::exists(orphan));
        auto w2 = RecordWriter::Make(cfg);
        EXPECT_FALSE(fs::exists(orphan)) << "stale orphan marker must be cleaned on start";
        w2->Stop();
    }

    CloseH264Encoder(gen);
    fs::remove_all(dir);
}

TEST(RecordWriter, EarlyStop) {
    auto dir = MakeTempDir();
    VirtualClock clk;
    RecordWriterConfig cfg;
    cfg.dir = dir;
    cfg.clock_ms = [&]() { return clk.ms; };
    auto w = RecordWriter::Make(cfg);

    H264Gen gen;
    ASSERT_TRUE(OpenH264Encoder(gen, 320, 240, 30, 30));
    FeedAV(*w, gen, clk, 2, 30, 30, true);
    w->Stop();
    CloseH264Encoder(gen);

    auto files = ListMp4(dir);
    ASSERT_EQ(files.size(), 1u);
    AVFormatContext* fc = nullptr;
    FileInfo info;
    ASSERT_TRUE(OpenAndInspect(files[0], &fc, &info));
    EXPECT_GE(info.duration_ms, 1500);
    EXPECT_TRUE(FirstVideoPacketIsKey(fc, info.video_index));
    avformat_close_input(&fc);
    fs::remove_all(dir);
}

TEST(RecordWriter, NamingAndSanitize) {
    auto dir = MakeTempDir();
    VirtualClock clk;
    RecordWriterConfig cfg;
    cfg.dir = dir;
    cfg.monitor_name = "\\\\.\\DISPLAY1"; // 设备路径前缀应被剥掉 -> DISPLAY1
    cfg.clock_ms = [&]() { return clk.ms; };
    auto w = RecordWriter::Make(cfg);

    H264Gen gen;
    ASSERT_TRUE(OpenH264Encoder(gen, 320, 240, 30, 30));
    clk.ms += 33;
    auto pkt = EncodeOneFrame(gen, 0, true);
    ASSERT_FALSE(pkt.empty());
    w->OnEncodedVideo(std::span<const uint8_t>(pkt), RecordVideoCodec::kH264, 320, 240, true);
    w->Stop();
    CloseH264Encoder(gen);

    auto files = ListMp4(dir);
    ASSERT_EQ(files.size(), 1u);
    auto name = files[0].filename().string();
    // 结构: rec_{monitor}_{YYYYMMDD}_{HH.MM.SS}.mp4 (无 device id / 毫秒)
    EXPECT_EQ(name.rfind("rec_DISPLAY1_", 0), 0u)
        << "unexpected name: " << name;
    EXPECT_EQ(name.compare(name.size() - 4, 4, ".mp4"), 0);

    // 时间戳必须是墙上时钟(人类可读): 17 位 "20260817_12.43.28", 年份不能是 1970
    auto ts = name.substr(name.size() - 4 - 17, 17);
    EXPECT_EQ(ts.size(), 17u) << "timestamp part malformed: " << ts;
    EXPECT_EQ(ts[8], '_');
    EXPECT_EQ(ts[11], '.');
    EXPECT_EQ(ts[14], '.');
    int year = std::atoi(ts.substr(0, 4).c_str());
    EXPECT_GE(year, 2024) << "timestamp must be wall clock, got: " << ts;

    fs::remove_all(dir);
}

TEST(RecordWriter, AudioOnlyNoFile) {
    auto dir = MakeTempDir();
    VirtualClock clk;
    RecordWriterConfig cfg;
    cfg.dir = dir;
    cfg.clock_ms = [&]() { return clk.ms; };
    auto w = RecordWriter::Make(cfg);

    for (int i = 0; i < 100; ++i) {
        clk.ms += 20;
        auto a = FakeOpusPacket(40, (uint8_t)i);
        w->OnEncodedAudio(std::span<const uint8_t>(a));
    }
    w->Stop();
    EXPECT_TRUE(ListMp4(dir).empty()) << "audio-only must not create a file";
    fs::remove_all(dir);
}

} // namespace
