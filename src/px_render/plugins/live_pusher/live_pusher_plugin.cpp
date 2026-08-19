#include "live_pusher_plugin.h"

#include "px_render/plugins/plugin_ids.h"
#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "libswresample/swresample.h"

// Implemented by libavformat. The FLV enhanced-RTMP muxer needs hvcC
// extradata before it can write an HEVC sequence-start packet.
int ff_isom_write_hvcc(AVIOContext* pb, const uint8_t* data, int size,
                       int ps_array_completeness);
}

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace px {
namespace {

constexpr size_t kMaxQueue = 320;

struct Nal {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

std::vector<Nal> SplitAnnexB(const uint8_t* data, size_t size) {
    std::vector<Nal> out;
    auto sc_len = [data, size](size_t pos) -> size_t {
        if (pos + 4 <= size && data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 0 && data[pos + 3] == 1) return 4;
        if (pos + 3 <= size && data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 1) return 3;
        return 0;
    };
    size_t pos = 0;
    while (pos < size && !sc_len(pos)) ++pos;
    if (pos == size) return out;
    pos += sc_len(pos);
    size_t start = pos;
    while (pos < size) {
        const auto len = sc_len(pos);
        if (!len) {
            ++pos;
            continue;
        }
        if (pos > start) out.push_back({data + start, pos - start});
        pos += len;
        start = pos;
    }
    if (start < size) out.push_back({data + start, size - start});
    return out;
}

void AppendNal(std::vector<uint8_t>& out, const std::vector<uint8_t>& nal) {
    static constexpr uint8_t kStartCode[] = {0, 0, 0, 1};
    if (nal.empty()) return;
    out.insert(out.end(), std::begin(kStartCode), std::end(kStartCode));
    out.insert(out.end(), nal.begin(), nal.end());
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool SetHevcExtradata(AVCodecParameters* codecpar,
                      const std::vector<uint8_t>& vps,
                      const std::vector<uint8_t>& sps,
                      const std::vector<uint8_t>& pps) {
    std::vector<uint8_t> annexb;
    AppendNal(annexb, vps);
    AppendNal(annexb, sps);
    AppendNal(annexb, pps);
    if (annexb.empty()) return false;

    AVIOContext* dyn = nullptr;
    if (avio_open_dyn_buf(&dyn) < 0 || !dyn) return false;
    const int write_ret = ff_isom_write_hvcc(dyn, annexb.data(), (int)annexb.size(), 1);
    uint8_t* hvcc = nullptr;
    const int hvcc_size = avio_close_dyn_buf(dyn, &hvcc);
    if (write_ret < 0 || !hvcc || hvcc_size <= 0) {
        av_free(hvcc);
        LOGE("LivePusher failed to construct hvcC extradata: write={}, size={}", write_ret, hvcc_size);
        return false;
    }

    auto* padded = static_cast<uint8_t*>(av_malloc(hvcc_size + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!padded) {
        av_free(hvcc);
        return false;
    }
    std::memcpy(padded, hvcc, hvcc_size);
    std::memset(padded + hvcc_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    av_free(hvcc);
    codecpar->extradata = padded;
    codecpar->extradata_size = hvcc_size;
    LOGI("LivePusher HEVC hvcC extradata prepared: {} bytes", hvcc_size);
    return true;
}

} // namespace

LivePusherPlugin::LivePusherPlugin() {
    plugin_enabled_ = true;
}

LivePusherPlugin::~LivePusherPlugin() {
    Shutdown();
}

std::string LivePusherPlugin::GetPluginId() { return kLivePusherPluginId; }
std::string LivePusherPlugin::GetPluginName() { return "Live Pusher"; }
std::string LivePusherPlugin::GetVersionName() { return "0.1.0"; }
uint32_t LivePusherPlugin::GetVersionCode() { return 10; }
std::string LivePusherPlugin::GetPluginDescription() { return "Passive RTMP live pusher"; }

bool LivePusherPlugin::OnCreate(const PxPluginParam& param) {
    PxStreamPlugin::OnCreate(param);
    enabled_ = GetConfigBoolParam("push_enabled");
    rtmp_url_ = GetConfigParam<std::string>("push_rtmp_url");
    stream_id_ = GetConfigParam<std::string>("live_stream_id");
    primary_monitor_ = GetConfigParam<std::string>("push_primary_monitor");
    audio_bitrate_ = (int)GetConfigIntParam("push_audio_bitrate");
    if (audio_bitrate_ <= 0) audio_bitrate_ = 96000;
    if (!enabled_) {
        LOGI("LivePusher disabled by [push].enabled");
        return true;
    }
    if (rtmp_url_.empty() || stream_id_.empty()) {
        LOGE("LivePusher disabled: rtmp_url or live_stream_id is empty");
        enabled_ = false;
        return true;
    }
    worker_ = std::thread([this] { WorkerLoop(); });
    LOGI("LivePusher enabled: stream={}, selected_monitor={}", stream_id_, primary_monitor_.empty() ? "<first-active>" : primary_monitor_);
    return true;
}

bool LivePusherPlugin::OnStop() {
    Shutdown();
    return PxStreamPlugin::OnStop();
}

bool LivePusherPlugin::OnDestroy() {
    Shutdown();
    return PxStreamPlugin::OnDestroy();
}

void LivePusherPlugin::On1Second() {
    if (!enabled_) return;
    const auto now = NowMs();
    if (dropped_ && now - last_drop_log_ms_ >= 10000) {
        last_drop_log_ms_ = now;
        LOGW("LivePusher dropped {} queued media entries", std::exchange(dropped_, 0));
    }
}

bool LivePusherPlugin::IsSelectedMonitor(const std::string& mon_name) {
    if (primary_monitor_.empty()) {
        primary_monitor_ = mon_name;
        LOGI("LivePusher selected first active monitor as primary: {}", primary_monitor_);
    }
    return primary_monitor_ == mon_name;
}

void LivePusherPlugin::OnEncodedVideoFrame(const std::string& mon_name,
                                            const PxPluginEncodedVideoType& video_type,
                                            const std::shared_ptr<Data>& data,
                                            uint64_t,
                                            int frame_width,
                                            int frame_height,
                                            bool key) {
    if (!enabled_ || !data || data->Size() <= 0 || !IsSelectedMonitor(mon_name)) return;
    if (video_type != PxPluginEncodedVideoType::kH264 && video_type != PxPluginEncodedVideoType::kH265) return;
    Enqueue({EntryKind::Video, data, video_type, frame_width, frame_height, key, 0, 0, 0, NowMs()});
}

void LivePusherPlugin::OnRawAudioData(const std::shared_ptr<Data>& data, int samples, int channels, int bits) {
    if (!enabled_ || !data || data->Size() <= 0) return;
    Enqueue({EntryKind::Audio, data, PxPluginEncodedVideoType::kH264, 0, 0, false, samples, channels, bits, NowMs()});
}

void LivePusherPlugin::Enqueue(Entry&& entry) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) return;
        if (queue_.size() >= kMaxQueue) {
            // Preserve keyframes where possible; never block the shared stream callback.
            if (entry.kind == EntryKind::Video && entry.key) {
                auto it = std::find_if(queue_.begin(), queue_.end(), [](const Entry& e) { return e.kind == EntryKind::Video && !e.key; });
                if (it != queue_.end()) queue_.erase(it); else { ++dropped_; return; }
            } else {
                ++dropped_;
                return;
            }
        }
        queue_.push_back(std::move(entry));
    }
    cv_.notify_one();
}

void LivePusherPlugin::WorkerLoop() {
    while (true) {
        Entry entry;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) break;
            entry = std::move(queue_.front());
            queue_.pop_front();
        }
        if (entry.kind == EntryKind::Video) ProcessVideo(entry);
        else ProcessAudio(entry);
    }
    CloseOutput();
    ResetAudioState();
}

void LivePusherPlugin::ResetVideoState() {
    CloseOutput();
    ResetAudioState();
    have_key_ = false;
    video_width_ = video_height_ = 0;
    session_start_ms_ = 0;
    last_video_dts_ = -1;
    vps_.clear(); sps_.clear(); pps_.clear(); pending_key_.clear();
    pending_key_ts_ = 0;
}

void LivePusherPlugin::ProcessVideo(const Entry& entry) {
    if (!codec_known_ || codec_ != entry.video_type) {
        if (codec_known_) LOGI("LivePusher video codec switch: {} -> {}", codec_ == PxPluginEncodedVideoType::kH264 ? "h264" : "h265", entry.video_type == PxPluginEncodedVideoType::kH264 ? "h264" : "h265");
        codec_ = entry.video_type;
        codec_known_ = true;
        ResetVideoState();
        InsertIdr();
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(entry.data->DataAddr());
    const auto size = (size_t)entry.data->Size();
    for (const auto& nal : SplitAnnexB(bytes, size)) {
        if (nal.size == 0) continue;
        const int type = codec_ == PxPluginEncodedVideoType::kH264 ? (nal.data[0] & 0x1f) : ((nal.data[0] >> 1) & 0x3f);
        if (codec_ == PxPluginEncodedVideoType::kH264) {
            if (type == 7) sps_.assign(nal.data, nal.data + nal.size);
            else if (type == 8) pps_.assign(nal.data, nal.data + nal.size);
        } else {
            if (type == 32) vps_.assign(nal.data, nal.data + nal.size);
            else if (type == 33) sps_.assign(nal.data, nal.data + nal.size);
            else if (type == 34) pps_.assign(nal.data, nal.data + nal.size);
        }
    }

    const bool params_ready = codec_ == PxPluginEncodedVideoType::kH264 ? (!sps_.empty() && !pps_.empty()) : (!vps_.empty() && !sps_.empty() && !pps_.empty());
    if (!fmt_) {
        if (!entry.key || !params_ready) return;
        video_width_ = entry.width;
        video_height_ = entry.height;
        pending_key_.clear();
        if (codec_ == PxPluginEncodedVideoType::kH265) AppendNal(pending_key_, vps_);
        AppendNal(pending_key_, sps_);
        AppendNal(pending_key_, pps_);
        pending_key_.insert(pending_key_.end(), bytes, bytes + size);
        have_key_ = true;
        pending_key_ts_ = entry.timestamp_ms;
        if (!OpenOutput()) return;
        return;
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) return;
    pkt->stream_index = video_stream_->index;
    pkt->data = const_cast<uint8_t*>(bytes);
    pkt->size = (int)size;
    auto dts = (entry.timestamp_ms - session_start_ms_) * 90;
    if (dts <= last_video_dts_) dts = last_video_dts_ + 1;
    last_video_dts_ = dts;
    pkt->pts = pkt->dts = dts;
    if (entry.key) pkt->flags |= AV_PKT_FLAG_KEY;
    const auto ret = av_interleaved_write_frame(fmt_, pkt);
    av_packet_free(&pkt);
    if (ret < 0) {
        LOGW("LivePusher video write failed: {}", ret);
        ResetVideoState();
        InsertIdr();
    }
}

bool LivePusherPlugin::InitAudio(int sample_rate, int channels, int bits) {
    if (aac_) return true;
    if (bits != 16 || sample_rate <= 0 || channels <= 0) {
        LOGE("LivePusher unsupported PCM format: {} Hz, {} ch, {} bits", sample_rate, channels, bits);
        return false;
    }
    const auto* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) return false;
    aac_ = avcodec_alloc_context3(codec);
    if (!aac_) return false;
    aac_->sample_rate = 48000;
    aac_->bit_rate = audio_bitrate_;
    aac_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(&aac_->ch_layout, 2);
    aac_->time_base = AVRational{1, aac_->sample_rate};
    if (avcodec_open2(aac_, codec, nullptr) < 0) {
        avcodec_free_context(&aac_);
        return false;
    }
    AVChannelLayout in_layout{};
    av_channel_layout_default(&in_layout, channels);
    if (swr_alloc_set_opts2(&swr_, &aac_->ch_layout, aac_->sample_fmt, aac_->sample_rate,
                            &in_layout, AV_SAMPLE_FMT_S16, sample_rate, 0, nullptr) < 0 || !swr_ || swr_init(swr_) < 0) {
        av_channel_layout_uninit(&in_layout);
        swr_free(&swr_);
        avcodec_free_context(&aac_);
        return false;
    }
    av_channel_layout_uninit(&in_layout);
    audio_fifo_ = av_audio_fifo_alloc(aac_->sample_fmt, aac_->ch_layout.nb_channels, aac_->frame_size * 4);
    if (!audio_fifo_) {
        swr_free(&swr_);
        avcodec_free_context(&aac_);
        return false;
    }
    next_audio_pts_ = 0;
    LOGI("LivePusher AAC initialized: {} Hz {} ch {} bps", aac_->sample_rate, aac_->ch_layout.nb_channels, audio_bitrate_);
    return true;
}

void LivePusherPlugin::ResetAudioState() {
    av_audio_fifo_free(audio_fifo_);
    audio_fifo_ = nullptr;
    swr_free(&swr_);
    avcodec_free_context(&aac_);
    next_audio_pts_ = 0;
}

void LivePusherPlugin::ProcessAudio(const Entry& entry) {
    if (!InitAudio(entry.sample_rate, entry.channels, entry.bits)) return;
    if (!fmt_ && have_key_ && !OpenOutput()) return;
    const int in_samples = entry.data->Size() / (entry.channels * (entry.bits / 8));
    if (in_samples <= 0) return;
    const uint8_t* in[] = {reinterpret_cast<const uint8_t*>(entry.data->DataAddr())};
    const int out_capacity = swr_get_out_samples(swr_, in_samples);
    uint8_t** converted = nullptr;
    int linesize = 0;
    if (av_samples_alloc_array_and_samples(&converted, &linesize, aac_->ch_layout.nb_channels, out_capacity, aac_->sample_fmt, 0) < 0) return;
    const int out_samples = swr_convert(swr_, converted, out_capacity, in, in_samples);
    if (out_samples > 0 && av_audio_fifo_realloc(audio_fifo_, av_audio_fifo_size(audio_fifo_) + out_samples) >= 0) {
        av_audio_fifo_write(audio_fifo_, reinterpret_cast<void**>(converted), out_samples);
    }
    av_freep(&converted[0]);
    av_freep(&converted);

    while (fmt_ && av_audio_fifo_size(audio_fifo_) >= aac_->frame_size) {
        AVFrame* frame = av_frame_alloc();
        frame->nb_samples = aac_->frame_size;
        frame->format = aac_->sample_fmt;
        frame->sample_rate = aac_->sample_rate;
        av_channel_layout_copy(&frame->ch_layout, &aac_->ch_layout);
        frame->pts = next_audio_pts_;
        if (av_frame_get_buffer(frame, 0) < 0) { av_frame_free(&frame); return; }
        av_audio_fifo_read(audio_fifo_, reinterpret_cast<void**>(frame->data), frame->nb_samples);
        next_audio_pts_ += frame->nb_samples;
        if (avcodec_send_frame(aac_, frame) >= 0) {
            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(aac_, pkt) == 0) {
                pkt->stream_index = audio_stream_->index;
                av_packet_rescale_ts(pkt, aac_->time_base, audio_stream_->time_base);
                const auto ret = av_interleaved_write_frame(fmt_, pkt);
                av_packet_unref(pkt);
                if (ret < 0) { LOGW("LivePusher audio write failed: {}", ret); ResetVideoState(); InsertIdr(); break; }
            }
            av_packet_free(&pkt);
        }
        av_frame_free(&frame);
    }
}

std::string LivePusherPlugin::BuildUrl() const {
    auto out = rtmp_url_;
    constexpr std::string_view needle = "{live_stream_id}";
    if (const auto pos = out.find(needle); pos != std::string::npos) out.replace(pos, needle.size(), stream_id_);
    return out;
}

bool LivePusherPlugin::OpenOutput() {
    if (!have_key_ || !aac_ || pending_key_.empty()) return false;
    const auto url = BuildUrl();
    if (url.empty()) return false;
    if (avformat_alloc_output_context2(&fmt_, nullptr, "flv", url.c_str()) < 0 || !fmt_) return false;
    video_stream_ = avformat_new_stream(fmt_, nullptr);
    audio_stream_ = avformat_new_stream(fmt_, nullptr);
    if (!video_stream_ || !audio_stream_) { CloseOutput(); return false; }
    video_stream_->time_base = AVRational{1, 90000};
    auto* vc = video_stream_->codecpar;
    vc->codec_type = AVMEDIA_TYPE_VIDEO;
    vc->codec_id = codec_ == PxPluginEncodedVideoType::kH265 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
    vc->width = video_width_;
    vc->height = video_height_;
    if (vc->codec_id == AV_CODEC_ID_HEVC && !SetHevcExtradata(vc, vps_, sps_, pps_)) {
        LOGE("LivePusher cannot publish HEVC without a valid hvcC configuration record");
        CloseOutput();
        return false;
    }
    if (vc->codec_id == AV_CODEC_ID_HEVC &&
        avformat_query_codec(fmt_->oformat, AV_CODEC_ID_HEVC, FF_COMPLIANCE_NORMAL) <= 0) {
        LOGE("LivePusher FFmpeg FLV muxer lacks enhanced RTMP/HEVC support");
        CloseOutput();
        return false;
    }
    audio_stream_->time_base = aac_->time_base;
    if (avcodec_parameters_from_context(audio_stream_->codecpar, aac_) < 0) { CloseOutput(); return false; }
    if (!(fmt_->oformat->flags & AVFMT_NOFILE) && avio_open(&fmt_->pb, url.c_str(), AVIO_FLAG_WRITE) < 0) { CloseOutput(); return false; }
    if (avformat_write_header(fmt_, nullptr) < 0) { CloseOutput(); return false; }
    session_start_ms_ = pending_key_ts_;
    last_video_dts_ = 0;
    LOGI("LivePusher publishing {} ({})", url, codec_ == PxPluginEncodedVideoType::kH265 ? "h265+aac" : "h264+aac");

    AVPacket* pkt = av_packet_alloc();
    pkt->stream_index = video_stream_->index;
    pkt->data = pending_key_.data();
    pkt->size = (int)pending_key_.size();
    pkt->flags = AV_PKT_FLAG_KEY;
    pkt->pts = pkt->dts = 0;
    const auto ret = av_interleaved_write_frame(fmt_, pkt);
    av_packet_free(&pkt);
    pending_key_.clear();
    if (ret < 0) { CloseOutput(); return false; }
    return true;
}

void LivePusherPlugin::CloseOutput() {
    if (!fmt_) return;
    av_write_trailer(fmt_);
    if (!(fmt_->oformat->flags & AVFMT_NOFILE) && fmt_->pb) avio_closep(&fmt_->pb);
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    video_stream_ = nullptr;
    audio_stream_ = nullptr;
}

void LivePusherPlugin::Shutdown() {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

} // namespace px
