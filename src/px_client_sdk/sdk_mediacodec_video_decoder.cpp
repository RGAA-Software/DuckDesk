//
// Created by RGAA on 2024/1/26.
//

#include "sdk_mediacodec_video_decoder.h"

#ifdef ANDROID

#include "px_common/log.h"
#include "px_common/time_util.h"
#include "px_client_sdk/gl/raw_image.h"
#include "sdk_stream_helper.h"
#include "sdk_statistics.h"

#include <fstream>
#include <utility>

namespace px
{

    const int32_t kCOLOR_FormatSurface = 0x7f000789;
    const int32_t kCOLOR_FormatYUV420SemiPlanar = 0x00000015;

    static int64_t getTimeNsec() {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        return (int64_t) now.tv_sec*1000*1000*1000 + now.tv_nsec;
    }
    static int64_t getTimeSec() {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        return (int64_t)now.tv_sec;
    }
    static int64_t getTimeMsec(){ //毫秒
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        return now.tv_sec*1000 +(int64_t)now.tv_nsec/(1000*1000);
    }
    static int64_t getTimeUsec(){ //us
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        return now.tv_sec*1000*1000 +(int64_t)now.tv_nsec/(1000);
    }

    MediacodecVideoDecoder::MediacodecVideoDecoder(const std::shared_ptr<ThunderSdk>& sdk, std::shared_ptr<AndroidVideoOutput> output)
        : VideoDecoder(sdk), output_(std::move(output)) {}

    MediacodecVideoDecoder::~MediacodecVideoDecoder() { Release(); }

    int MediacodecVideoDecoder::Init(const std::string& mon_name, VideoType codec_type, int width, int height, const std::string& frame,
                                     EImageFormat img_format, bool ignore_hw) {
        std::lock_guard<std::mutex> guard(decode_mtx_);
        if (VideoDecoder::Init(mon_name, codec_type, width, height, frame, img_format, ignore_hw) != 0)
            return -1;
        if (!output_ || inited_ || width <= 0 || height <= 0) return -1;
        window_ = output_->Snapshot();
        if (!window_) return -1;
        monitor_name_ = mon_name;
        auto decoder_name = [&]() -> std::string {
            if (codec_type == VideoType::kNetHevc) {
                return "video/hevc";
            } else {
                return "video/avc";
            }
        }();

        use_oes_ = true;
        std::string csd0;
        std::string csd1;
        if (use_oes_) {
            if (codec_type == VideoType::kNetH264) {
                auto parameter_sets = StreamHelper::ExtractH264ParameterSets(frame);
                csd0 = std::move(parameter_sets.sps);
                csd1 = std::move(parameter_sets.pps);
                if (csd0.empty() || csd1.empty()) {
                    LOGW("H.264 access unit has no complete SPS/PPS; initialize for in-band codec configuration");
                }
                sdk_stat_->video_format_ = "H264";
            } else {
                csd0 = StreamHelper::ExtractH265ParameterSets(frame);
                if (csd0.empty()) {
                    LOGW("H.265 access unit has no VPS/SPS/PPS; initialize for in-band codec configuration");
                }

                sdk_stat_->video_format_ = "HEVC";
            }

            LOGI("MediaCodec parameter sets: csd-0={} bytes, csd-1={} bytes", csd0.size(), csd1.size());
        }

        media_codec_.reset(AMediaCodec_createDecoderByType(decoder_name.c_str()));
        media_format_.reset(AMediaFormat_new());
        if (!media_codec_ || !media_format_) {
            LOGE("Failed to create MediaCodec decoder resources");
            media_codec_.reset();
            media_format_.reset();
            return -1;
        }
        AMediaFormat_setString(media_format_.get(), "mime", decoder_name.c_str());
        AMediaFormat_setInt32(media_format_.get(), AMEDIAFORMAT_KEY_WIDTH, width);
        AMediaFormat_setInt32(media_format_.get(), AMEDIAFORMAT_KEY_HEIGHT, height);
        AMediaFormat_setInt32(media_format_.get(), AMEDIAFORMAT_KEY_FRAME_RATE, 60);
        const auto max_input_size = width * height;
        AMediaFormat_setInt32(media_format_.get(), AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, max_input_size);

        if (!csd0.empty()) {
            AMediaFormat_setBuffer(media_format_.get(), "csd-0", csd0.data(), csd0.size());
        }
        if (!csd1.empty()) {
            AMediaFormat_setBuffer(media_format_.get(), "csd-1", csd1.data(), csd1.size());
        }

        LOGI("decoder name: {}, surface output enabled", decoder_name);
        media_status_t status = AMediaCodec_configure(media_codec_.get(), media_format_.get(),
                                                      window_.get(),
                                                      nullptr,
                                                      0);//解码，flags 给0，编码给AMEDIACODEC_CONFIGURE_FLAG_ENCODE
        if (status != AMEDIA_OK) {
            LOGE("error config {}", (int) status);
            media_codec_.reset();
            media_format_.reset();
            return -1;
        }

        //启动
        status = AMediaCodec_start(media_codec_.get());
        if (status != AMEDIA_OK) {
            LOGE("error start: {}", (int) status);
            media_codec_.reset();
            media_format_.reset();
            return -1;
        }

        this->codec_type_ = codec_type;
        img_format_ = img_format;
        this->frame_width_ = width;
        this->frame_height_ = height;
        sdk_stat_->video_decoder_.Update("MediaCodec hardware");
        inited_ = true;

        return AMEDIA_OK;
    }

    Result<std::shared_ptr<RawImage>, int> MediacodecVideoDecoder::Decode(std::span<const std::uint8_t> encoded) {
        std::lock_guard guard(decode_mtx_);
        if (!media_codec_ || encoded.empty()) return TRError(-1);
        const auto started_at = TimeUtil::GetCurrentTimestamp();
        const auto input_index = AMediaCodec_dequeueInputBuffer(media_codec_.get(), 2000);
        if (input_index >= 0) {
            std::size_t capacity{};
            // The first boundary check avoids constructing a nonempty span from null.
            if (!AMediaCodec_getInputBuffer(media_codec_.get(), input_index, &capacity)) return TRError(-1);
            const std::span<std::uint8_t> buffer{AMediaCodec_getInputBuffer(media_codec_.get(), input_index, &capacity), capacity};
            if (encoded.size() > buffer.size()) return TRError(-1);
            std::memcpy(buffer.data(), encoded.data(), encoded.size());
            if (AMediaCodec_queueInputBuffer(media_codec_.get(), input_index, 0, encoded.size(), getTimeUsec(), 0) != AMEDIA_OK) {
                return TRError(-1);
            }
        }
        for (int attempt{}; attempt < 4; ++attempt) {
            AMediaCodecBufferInfo info{};
            const auto output_index = AMediaCodec_dequeueOutputBuffer(media_codec_.get(), &info, 2000);
            if (output_index >= 0) {
                struct OutputLease final {
                    std::reference_wrapper<AMediaCodec> codec;
                    std::size_t index{};
                    bool present{};
                    ~OutputLease() { AMediaCodec_releaseOutputBuffer(&codec.get(), index, present); }
                };
                OutputLease output{*media_codec_, static_cast<std::size_t>(output_index), false};
                const std::unique_ptr<AMediaFormat, decltype(&AMediaFormat_delete)> format{
                    AMediaCodec_getOutputFormat(media_codec_.get()), &AMediaFormat_delete};
                std::int32_t width{};
                std::int32_t height{};
                if (!format || !AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_WIDTH, &width) ||
                    !AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_HEIGHT, &height)) return TRError(-1);
                auto image = RawImage::MakePresented(width, height);
                if (!image) return TRError(-1);
                output.present = true;
                sdk_stat_->AppendDecodeDuration(monitor_name_, TimeUtil::GetCurrentTimestamp() - started_at);
                return image;
            }
            if (output_index != AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED && output_index != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) break;
        }
        return TRError(0);
    }

    void MediacodecVideoDecoder::Release() {
        std::lock_guard<std::mutex> guard(decode_mtx_);
        VideoDecoder::Release();
        LOGI("will stop media codec");
        if (media_codec_) {
            AMediaCodec_stop(media_codec_.get());
            media_codec_.reset();
        }

        LOGI("will delete media format");
        media_format_.reset();
        window_.reset();
        inited_ = false;
    }

    bool MediacodecVideoDecoder::RefreshOutput() {
        std::lock_guard<std::mutex> guard(decode_mtx_);
        const auto replacement = output_ ? output_->Snapshot() : std::shared_ptr<ANativeWindow>{};
        if (!media_codec_ || !replacement) return false;
        const auto status = AMediaCodec_setOutputSurface(media_codec_.get(), replacement.get());
        if (status != AMEDIA_OK) {
            LOGE("MediaCodec output surface update failed: {}", static_cast<int>(status));
            return false;
        }
        LOGI("MediaCodec output surface updated");
        window_ = replacement;
        return true;
    }

    bool MediacodecVideoDecoder::Ready() {
        return inited_;
    }

    bool MediacodecVideoDecoder::NeedReConstruct(VideoType codec_type, int width, int height, EImageFormat img_format) {
        return VideoDecoder::NeedReConstruct(codec_type, width, height, img_format);
    }
}

#endif
