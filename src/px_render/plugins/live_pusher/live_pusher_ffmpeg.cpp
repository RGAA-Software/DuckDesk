#include "live_pusher_ffmpeg.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "px_common_new/data.h"
#include "px_common_new/log.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/samplefmt.h"
#include "libswresample/swresample.h"

int ff_isom_write_hvcc(AVIOContext* output, const uint8_t* data, int size,  // NOLINT(gammaray-raw-pointer-boundary)
                       int complete, void* log_context);  // NOLINT(gammaray-raw-pointer-boundary)
}

namespace px {
namespace {

struct FormatContextDeleter final {
    void operator()(AVFormatContext* context) const {  // NOLINT(gammaray-raw-pointer-boundary)
        avformat_free_context(context);
    }
};

struct CodecContextDeleter final {
    void operator()(AVCodecContext* context) const {  // NOLINT(gammaray-raw-pointer-boundary)
        avcodec_free_context(&context);
    }
};

struct AudioFifoDeleter final {
    void operator()(AVAudioFifo* fifo) const {  // NOLINT(gammaray-raw-pointer-boundary)
        av_audio_fifo_free(fifo);
    }
};

struct ResamplerDeleter final {
    void operator()(SwrContext* context) const {  // NOLINT(gammaray-raw-pointer-boundary)
        swr_free(&context);
    }
};

struct PacketDeleter final {
    void operator()(AVPacket* packet) const {  // NOLINT(gammaray-raw-pointer-boundary)
        av_packet_free(&packet);
    }
};

struct FrameDeleter final {
    void operator()(AVFrame* frame) const {  // NOLINT(gammaray-raw-pointer-boundary)
        av_frame_free(&frame);
    }
};

struct AvBufferDeleter final {
    void operator()(uint8_t* buffer) const {  // NOLINT(gammaray-raw-pointer-boundary)
        av_free(buffer);
    }
};

using FormatContextHandle = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextHandle = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using AudioFifoHandle = std::unique_ptr<AVAudioFifo, AudioFifoDeleter>;
using ResamplerHandle = std::unique_ptr<SwrContext, ResamplerDeleter>;
using PacketHandle = std::unique_ptr<AVPacket, PacketDeleter>;
using FrameHandle = std::unique_ptr<AVFrame, FrameDeleter>;
using AvBufferHandle = std::unique_ptr<uint8_t, AvBufferDeleter>;

thread_local std::chrono::steady_clock::time_point io_deadline{};

class IoDeadlineScope final {
public:
    IoDeadlineScope()
        : previous_(io_deadline) {
        io_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }
    ~IoDeadlineScope() { io_deadline = previous_; }
    IoDeadlineScope(const IoDeadlineScope&) = delete;
    IoDeadlineScope& operator=(const IoDeadlineScope&) = delete;

private:
    std::chrono::steady_clock::time_point previous_;
};

int InterruptExpired(void*) {  // NOLINT(gammaray-raw-pointer-boundary)
    return io_deadline != std::chrono::steady_clock::time_point{} &&
           std::chrono::steady_clock::now() >= io_deadline;
}

class ChannelLayoutScope final {
public:
    explicit ChannelLayoutScope(int channels) {
        av_channel_layout_default(&layout_, channels);
    }
    ~ChannelLayoutScope() { av_channel_layout_uninit(&layout_); }
    ChannelLayoutScope(const ChannelLayoutScope&) = delete;
    ChannelLayoutScope& operator=(const ChannelLayoutScope&) = delete;
    AVChannelLayout& Get() { return layout_; }

private:
    AVChannelLayout layout_{};
};

std::vector<std::span<const uint8_t>> SplitAnnexB(
    std::span<const uint8_t> bytes) {
    std::vector<std::span<const uint8_t>> output;
    const auto start_code_length = [&bytes](size_t position) -> size_t {
        if (position + 4 <= bytes.size() && bytes[position] == 0 &&
            bytes[position + 1] == 0 && bytes[position + 2] == 0 &&
            bytes[position + 3] == 1) {
            return 4;
        }
        if (position + 3 <= bytes.size() && bytes[position] == 0 &&
            bytes[position + 1] == 0 && bytes[position + 2] == 1) {
            return 3;
        }
        return 0;
    };

    size_t position = 0;
    while (position < bytes.size() && start_code_length(position) == 0) {
        ++position;
    }
    if (position == bytes.size()) {
        return output;
    }
    position += start_code_length(position);
    size_t start = position;
    while (position < bytes.size()) {
        const auto length = start_code_length(position);
        if (length == 0) {
            ++position;
            continue;
        }
        if (position > start) {
            output.emplace_back(bytes.subspan(start, position - start));
        }
        position += length;
        start = position;
    }
    if (start < bytes.size()) {
        output.emplace_back(bytes.subspan(start));
    }
    return output;
}

std::vector<uint8_t> AnnexBToAvcc(std::span<const uint8_t> bytes) {
    const auto nals = SplitAnnexB(bytes);
    if (nals.empty()) {
        return {};
    }
    std::vector<uint8_t> output;
    output.reserve(bytes.size());
    for (const auto nal : nals) {
        if (nal.empty() || nal.size() > UINT32_MAX) {
            return {};
        }
        const auto size = static_cast<uint32_t>(nal.size());
        output.push_back(static_cast<uint8_t>(size >> 24));
        output.push_back(static_cast<uint8_t>(size >> 16));
        output.push_back(static_cast<uint8_t>(size >> 8));
        output.push_back(static_cast<uint8_t>(size));
        output.insert(output.end(), nal.begin(), nal.end());
    }
    return output;
}

void AppendNal(std::vector<uint8_t>& output, const std::vector<uint8_t>& nal) {
    static constexpr uint8_t kStartCode[] = {0, 0, 0, 1};
    if (!nal.empty()) {
        output.insert(output.end(), std::begin(kStartCode), std::end(kStartCode));
        output.insert(output.end(), nal.begin(), nal.end());
    }
}

bool SetAvcExtradata(
    AVCodecParameters& parameters,
    const std::vector<uint8_t>& sps,
    const std::vector<uint8_t>& pps) {
    if (sps.size() < 4 || pps.empty() ||
        sps.size() > UINT16_MAX || pps.size() > UINT16_MAX) {
        return false;
    }
    std::vector<uint8_t> avcc;
    avcc.reserve(11 + sps.size() + pps.size());
    avcc.push_back(1);
    avcc.push_back(sps[1]);
    avcc.push_back(sps[2]);
    avcc.push_back(sps[3]);
    avcc.push_back(0xff);
    avcc.push_back(0xe1);
    avcc.push_back(static_cast<uint8_t>(sps.size() >> 8));
    avcc.push_back(static_cast<uint8_t>(sps.size()));
    avcc.insert(avcc.end(), sps.begin(), sps.end());
    avcc.push_back(1);
    avcc.push_back(static_cast<uint8_t>(pps.size() >> 8));
    avcc.push_back(static_cast<uint8_t>(pps.size()));
    avcc.insert(avcc.end(), pps.begin(), pps.end());

    parameters.extradata = static_cast<uint8_t*>(
        av_malloc(avcc.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!parameters.extradata) {
        return false;
    }
    std::memcpy(parameters.extradata, avcc.data(), avcc.size());
    std::memset(parameters.extradata + avcc.size(), 0,
                AV_INPUT_BUFFER_PADDING_SIZE);
    parameters.extradata_size = static_cast<int>(avcc.size());
    LOGI("LivePusher H.264 avcC extradata prepared: {} bytes", avcc.size());
    return true;
}

bool SetHevcExtradata(
    AVCodecParameters& parameters,
    const std::vector<uint8_t>& vps,
    const std::vector<uint8_t>& sps,
    const std::vector<uint8_t>& pps) {
    std::vector<uint8_t> annexb;
    AppendNal(annexb, vps);
    AppendNal(annexb, sps);
    AppendNal(annexb, pps);
    if (annexb.empty()) {
        return false;
    }

    AVIOContext* dynamic_context = nullptr;  // NOLINT(gammaray-raw-pointer-boundary)
    if (avio_open_dyn_buf(&dynamic_context) < 0 || !dynamic_context) {
        return false;
    }
    const auto write_result = ff_isom_write_hvcc(
        dynamic_context, annexb.data(), static_cast<int>(annexb.size()), 1, nullptr);
    uint8_t* output_buffer = nullptr;  // NOLINT(gammaray-raw-pointer-boundary)
    const auto output_size = avio_close_dyn_buf(dynamic_context, &output_buffer);
    AvBufferHandle hvcc(output_buffer);
    if (write_result < 0 || !hvcc || output_size <= 0) {
        LOGE("LivePusher failed to construct hvcC extradata: write={}, size={}",
             write_result, output_size);
        return false;
    }

    parameters.extradata = static_cast<uint8_t*>(
        av_malloc(output_size + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!parameters.extradata) {
        return false;
    }
    std::memcpy(parameters.extradata, hvcc.get(), output_size);
    std::memset(parameters.extradata + output_size, 0,
                AV_INPUT_BUFFER_PADDING_SIZE);
    parameters.extradata_size = output_size;
    LOGI("LivePusher HEVC hvcC extradata prepared: {} bytes", output_size);
    return true;
}

class FfmpegLivePushProcessor final : public LivePushProcessor {
public:
    FfmpegLivePushProcessor(
        LivePusherRuntime::Config config,
        LivePusherRuntime::KeyframeRequester request_keyframe)
        : config_(std::move(config)),
          request_keyframe_(std::move(request_keyframe)) {}

    ~FfmpegLivePushProcessor() override { Close(); }

    void ProcessVideo(
        const std::shared_ptr<Data>& data,
        PxPluginEncodedVideoType video_type,
        int width,
        int height,
        bool key,
        int64_t timestamp_ms) override {
        if (!data || data->Size() <= 0) {
            return;
        }
        if (!codec_known_ || codec_ != video_type) {
            if (codec_known_) {
                LOGI("LivePusher video codec switch: {} -> {}",
                     codec_ == PxPluginEncodedVideoType::kH264 ? "h264" : "h265",
                     video_type == PxPluginEncodedVideoType::kH264 ? "h264" : "h265");
            }
            codec_ = video_type;
            codec_known_ = true;
            ResetVideoState();
            RequestKeyframe();
        }

        const auto bytes = std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(data->CStr()),
            static_cast<size_t>(data->Size()));
        for (const auto nal : SplitAnnexB(bytes)) {
            if (nal.empty()) {
                continue;
            }
            const int type = codec_ == PxPluginEncodedVideoType::kH264
                ? (nal.front() & 0x1f)
                : ((nal.front() >> 1) & 0x3f);
            if (codec_ == PxPluginEncodedVideoType::kH264) {
                if (type == 7) {
                    sps_.assign(nal.begin(), nal.end());
                } else if (type == 8) {
                    pps_.assign(nal.begin(), nal.end());
                }
            } else if (type == 32) {
                vps_.assign(nal.begin(), nal.end());
            } else if (type == 33) {
                sps_.assign(nal.begin(), nal.end());
            } else if (type == 34) {
                pps_.assign(nal.begin(), nal.end());
            }
        }

        const bool parameters_ready = codec_ == PxPluginEncodedVideoType::kH264
            ? (!sps_.empty() && !pps_.empty())
            : (!vps_.empty() && !sps_.empty() && !pps_.empty());
        const auto payload = AnnexBToAvcc(bytes);
        if (payload.empty()) {
            LOGW("LivePusher ignored video frame without Annex-B NAL units");
            return;
        }
        if (!format_) {
            if (!key || !parameters_ready) {
                return;
            }
            video_width_ = width;
            video_height_ = height;
            pending_key_ = payload;
            pending_key_timestamp_ms_ = timestamp_ms;
            have_key_ = true;
            if (!OpenOutput()) {
                return;
            }
            last_keyframe_request_ms_ = timestamp_ms;
            RequestKeyframe();
            return;
        }

        if (timestamp_ms - last_keyframe_request_ms_ >= 1000) {
            last_keyframe_request_ms_ = timestamp_ms;
            RequestKeyframe();
        }
        auto packet = PacketHandle(av_packet_alloc());
        if (!packet) {
            return;
        }
        packet->stream_index = video_stream_index_;
        packet->data = const_cast<uint8_t*>(payload.data());
        packet->size = static_cast<int>(payload.size());
        const auto& video_stream = *format_->streams[video_stream_index_];
        auto dts = av_rescale_q(timestamp_ms - session_start_ms_,
                                AVRational{1, 1000}, video_stream.time_base);
        if (dts <= last_video_dts_) {
            dts = last_video_dts_ + 1;
        }
        last_video_dts_ = dts;
        packet->pts = packet->dts = dts;
        if (key) {
            packet->flags |= AV_PKT_FLAG_KEY;
        }
        int result = 0;
        {
            IoDeadlineScope deadline;
            result = av_interleaved_write_frame(format_.get(), packet.get());
        }
        if (result < 0) {
            LOGW("LivePusher video write failed: {}", result);
            ResetVideoState();
            RequestKeyframe();
        }
    }

    void ProcessAudio(
        const std::shared_ptr<Data>& data,
        int sample_rate,
        int channels,
        int bits,
        int64_t timestamp_ms) override {
        (void)timestamp_ms;
        if (!data || data->Size() <= 0 ||
            !InitAudio(sample_rate, channels, bits)) {
            return;
        }
        if (!format_ && have_key_ && !OpenOutput()) {
            return;
        }
        const int input_samples = static_cast<int>(
            data->Size() / (channels * (bits / 8)));
        if (input_samples <= 0) {
            return;
        }
        const int output_capacity = swr_get_out_samples(resampler_.get(), input_samples);
        auto converted = FrameHandle(av_frame_alloc());
        if (!converted) {
            return;
        }
        converted->nb_samples = output_capacity;
        converted->format = aac_->sample_fmt;
        converted->sample_rate = aac_->sample_rate;
        if (av_channel_layout_copy(&converted->ch_layout, &aac_->ch_layout) < 0 ||
            av_frame_get_buffer(converted.get(), 0) < 0) {
            return;
        }
        const uint8_t* input_planes[] = {  // NOLINT(gammaray-raw-pointer-boundary)
            reinterpret_cast<const uint8_t*>(data->CStr())};
        const int output_samples = swr_convert(
            resampler_.get(), converted->data, output_capacity,
            input_planes, input_samples);
        if (output_samples > 0 &&
            av_audio_fifo_realloc(audio_fifo_.get(),
                av_audio_fifo_size(audio_fifo_.get()) + output_samples) >= 0) {
            av_audio_fifo_write(audio_fifo_.get(),
                                reinterpret_cast<void**>(converted->data),
                                output_samples);
        }

        while (format_ &&
               av_audio_fifo_size(audio_fifo_.get()) >= aac_->frame_size) {
            auto frame = FrameHandle(av_frame_alloc());
            if (!frame) {
                return;
            }
            frame->nb_samples = aac_->frame_size;
            frame->format = aac_->sample_fmt;
            frame->sample_rate = aac_->sample_rate;
            if (av_channel_layout_copy(&frame->ch_layout, &aac_->ch_layout) < 0) {
                return;
            }
            frame->pts = next_audio_pts_;
            if (av_frame_get_buffer(frame.get(), 0) < 0) {
                return;
            }
            av_audio_fifo_read(audio_fifo_.get(),
                               reinterpret_cast<void**>(frame->data),
                               frame->nb_samples);
            next_audio_pts_ += frame->nb_samples;
            if (avcodec_send_frame(aac_.get(), frame.get()) < 0) {
                continue;
            }
            auto packet = PacketHandle(av_packet_alloc());
            if (!packet) {
                return;
            }
            while (avcodec_receive_packet(aac_.get(), packet.get()) == 0) {
                packet->stream_index = audio_stream_index_;
                av_packet_rescale_ts(
                    packet.get(), aac_->time_base,
                    format_->streams[audio_stream_index_]->time_base);
                int result = 0;
                {
                    IoDeadlineScope deadline;
                    result = av_interleaved_write_frame(
                        format_.get(), packet.get());
                }
                av_packet_unref(packet.get());
                if (result < 0) {
                    LOGW("LivePusher audio write failed: {}", result);
                    ResetVideoState();
                    RequestKeyframe();
                    return;
                }
            }
        }
    }

    void Close() override {
        CloseOutput();
        ResetAudioState();
        pending_key_.clear();
        have_key_ = false;
    }

    bool IsPublishing() const override { return format_ && header_written_; }

private:
    bool InitAudio(int sample_rate, int channels, int bits) {
        if (aac_) {
            return true;
        }
        if (bits != 16 || sample_rate <= 0 || channels <= 0) {
            LOGE("LivePusher unsupported PCM format: {} Hz, {} ch, {} bits",
                 sample_rate, channels, bits);
            return false;
        }
        const AVCodec* encoder =  // NOLINT(gammaray-raw-pointer-boundary)
            avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!encoder) {
            return false;
        }
        aac_.reset(avcodec_alloc_context3(encoder));
        if (!aac_) {
            return false;
        }
        aac_->sample_rate = 48000;
        aac_->bit_rate = config_.audio_bitrate;
        aac_->sample_fmt = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_default(&aac_->ch_layout, 2);
        aac_->time_base = AVRational{1, aac_->sample_rate};
        if (avcodec_open2(aac_.get(), encoder, nullptr) < 0) {
            ResetAudioState();
            return false;
        }

        ChannelLayoutScope input_layout(channels);
        SwrContext* allocated_resampler = nullptr;  // NOLINT(gammaray-raw-pointer-boundary)
        if (swr_alloc_set_opts2(
                &allocated_resampler, &aac_->ch_layout,
                aac_->sample_fmt, aac_->sample_rate,
                &input_layout.Get(), AV_SAMPLE_FMT_S16, sample_rate,
                0, nullptr) < 0 || !allocated_resampler) {
            ResetAudioState();
            return false;
        }
        resampler_.reset(allocated_resampler);
        if (swr_init(resampler_.get()) < 0) {
            ResetAudioState();
            return false;
        }
        audio_fifo_.reset(av_audio_fifo_alloc(
            aac_->sample_fmt, aac_->ch_layout.nb_channels,
            aac_->frame_size * 4));
        if (!audio_fifo_) {
            ResetAudioState();
            return false;
        }
        next_audio_pts_ = 0;
        LOGI("LivePusher AAC initialized: {} Hz {} ch {} bps",
             aac_->sample_rate, aac_->ch_layout.nb_channels,
             config_.audio_bitrate);
        return true;
    }

    bool OpenOutput() {
        if (!have_key_ || !aac_ || pending_key_.empty() ||
            config_.publish_url.empty()) {
            return false;
        }
        header_written_ = false;
        AVFormatContext* allocated_format = nullptr;  // NOLINT(gammaray-raw-pointer-boundary)
        if (avformat_alloc_output_context2(
                &allocated_format, nullptr, "flv",
                config_.publish_url.c_str()) < 0 || !allocated_format) {
            return false;
        }
        format_.reset(allocated_format);
        format_->interrupt_callback.callback = InterruptExpired;
        format_->interrupt_callback.opaque = nullptr;
        format_->max_interleave_delta = 100000;
        format_->flags |= AVFMT_FLAG_FLUSH_PACKETS;

        video_stream_index_ = static_cast<int>(format_->nb_streams);
        if (!avformat_new_stream(format_.get(), nullptr)) {
            CloseOutput();
            return false;
        }
        audio_stream_index_ = static_cast<int>(format_->nb_streams);
        if (!avformat_new_stream(format_.get(), nullptr)) {
            CloseOutput();
            return false;
        }
        auto& video_stream = *format_->streams[video_stream_index_];
        auto& audio_stream = *format_->streams[audio_stream_index_];
        video_stream.time_base = AVRational{1, 90000};
        auto& video_parameters = *video_stream.codecpar;
        video_parameters.codec_type = AVMEDIA_TYPE_VIDEO;
        video_parameters.codec_id = codec_ == PxPluginEncodedVideoType::kH265
            ? AV_CODEC_ID_HEVC
            : AV_CODEC_ID_H264;
        video_parameters.width = video_width_;
        video_parameters.height = video_height_;
        if (video_parameters.codec_id == AV_CODEC_ID_H264 &&
            !SetAvcExtradata(video_parameters, sps_, pps_)) {
            LOGE("LivePusher cannot publish H.264 without valid avcC");
            CloseOutput();
            return false;
        }
        if (video_parameters.codec_id == AV_CODEC_ID_HEVC &&
            !SetHevcExtradata(video_parameters, vps_, sps_, pps_)) {
            LOGE("LivePusher cannot publish HEVC without valid hvcC");
            CloseOutput();
            return false;
        }
        if (video_parameters.codec_id == AV_CODEC_ID_HEVC &&
            avformat_query_codec(format_->oformat, AV_CODEC_ID_HEVC,
                                 FF_COMPLIANCE_NORMAL) <= 0) {
            LOGE("LivePusher FFmpeg FLV muxer lacks enhanced RTMP/HEVC support");
            CloseOutput();
            return false;
        }
        audio_stream.time_base = aac_->time_base;
        if (avcodec_parameters_from_context(audio_stream.codecpar, aac_.get()) < 0) {
            CloseOutput();
            return false;
        }
        if (!(format_->oformat->flags & AVFMT_NOFILE)) {
            int open_result = 0;
            {
                IoDeadlineScope deadline;
                open_result = avio_open2(
                    &format_->pb, config_.publish_url.c_str(), AVIO_FLAG_WRITE,
                    &format_->interrupt_callback, nullptr);
            }
            if (open_result < 0) {
                CloseOutput();
                return false;
            }
        }
        int header_result = 0;
        {
            IoDeadlineScope deadline;
            header_result = avformat_write_header(format_.get(), nullptr);
        }
        if (header_result < 0) {
            CloseOutput();
            return false;
        }
        header_written_ = true;
        session_start_ms_ = pending_key_timestamp_ms_;
        last_video_dts_ = 0;
        LOGI("LivePusher publishing {} ({})", config_.publish_url,
             codec_ == PxPluginEncodedVideoType::kH265 ? "h265+aac" : "h264+aac");

        auto packet = PacketHandle(av_packet_alloc());
        if (!packet) {
            CloseOutput();
            return false;
        }
        packet->stream_index = video_stream_index_;
        packet->data = pending_key_.data();
        packet->size = static_cast<int>(pending_key_.size());
        packet->flags = AV_PKT_FLAG_KEY;
        packet->pts = packet->dts = 0;
        int result = 0;
        {
            IoDeadlineScope deadline;
            result = av_interleaved_write_frame(format_.get(), packet.get());
        }
        pending_key_.clear();
        if (result < 0) {
            CloseOutput();
            return false;
        }
        return true;
    }

    void CloseOutput() {
        if (!format_) {
            return;
        }
        if (header_written_) {
            IoDeadlineScope deadline;
            av_write_trailer(format_.get());
        }
        if (!(format_->oformat->flags & AVFMT_NOFILE) && format_->pb) {
            avio_closep(&format_->pb);
        }
        format_.reset();
        video_stream_index_ = -1;
        audio_stream_index_ = -1;
        header_written_ = false;
    }

    void ResetAudioState() {
        audio_fifo_.reset();
        resampler_.reset();
        aac_.reset();
        next_audio_pts_ = 0;
    }

    void ResetVideoState() {
        CloseOutput();
        ResetAudioState();
        have_key_ = false;
        video_width_ = 0;
        video_height_ = 0;
        session_start_ms_ = 0;
        last_video_dts_ = -1;
        last_keyframe_request_ms_ = 0;
        vps_.clear();
        sps_.clear();
        pps_.clear();
        pending_key_.clear();
        pending_key_timestamp_ms_ = 0;
    }

    void RequestKeyframe() const {
        if (request_keyframe_) {
            request_keyframe_();
        }
    }

    const LivePusherRuntime::Config config_;
    const LivePusherRuntime::KeyframeRequester request_keyframe_;
    FormatContextHandle format_;
    CodecContextHandle aac_;
    AudioFifoHandle audio_fifo_;
    ResamplerHandle resampler_;
    int video_stream_index_ = -1;
    int audio_stream_index_ = -1;
    bool header_written_ = false;
    PxPluginEncodedVideoType codec_ = PxPluginEncodedVideoType::kH264;
    bool codec_known_ = false;
    bool have_key_ = false;
    int video_width_ = 0;
    int video_height_ = 0;
    int64_t session_start_ms_ = 0;
    int64_t last_video_dts_ = -1;
    int64_t last_keyframe_request_ms_ = 0;
    int64_t pending_key_timestamp_ms_ = 0;
    int64_t next_audio_pts_ = 0;
    std::vector<uint8_t> vps_;
    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
    std::vector<uint8_t> pending_key_;
};

}  // namespace

std::shared_ptr<LivePushProcessor> MakeFfmpegLivePushProcessor(
    const LivePusherRuntime::Config& config,
    const LivePusherRuntime::KeyframeRequester& request_keyframe) {
    return std::make_shared<FfmpegLivePushProcessor>(config, request_keyframe);
}

}  // namespace px
