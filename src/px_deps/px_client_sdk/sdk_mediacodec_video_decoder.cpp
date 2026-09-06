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

    MediacodecVideoDecoder::MediacodecVideoDecoder(const std::shared_ptr<ThunderSdk>& sdk) : VideoDecoder(sdk) {

    }

    MediacodecVideoDecoder::~MediacodecVideoDecoder() { Release(); }

    int MediacodecVideoDecoder::Init(const std::string& mon_name, int codec_type, int width, int height, const std::string& frame, void* surface, int img_format, bool ignore_hw) {
        std::lock_guard<std::mutex> guard(decode_mtx_);
        monitor_name_ = mon_name;
        auto decoder_name = [&]() -> std::string {
            if (codec_type == 1) {
                return "video/hevc";
            }
            else {
                return "video/avc";
            }
        }();

        use_oes_ = surface != nullptr;
        std::string csd0;
        std::string csd1;
        if (use_oes_) {
            auto in_frame_data = frame.data();
            auto in_frame_size = frame.size();
            size_t sps_size, pps_size;
            std::string sps_buf;
            std::string pps_buf;
            sps_buf.resize(in_frame_size + 20);
            pps_buf.resize(in_frame_size + 20);

            if(codec_type == 0) {
                if (0 != StreamHelper::ConvertH264SPSPPS((const uint8_t *) in_frame_data, (size_t) in_frame_size,
                                               (uint8_t *) sps_buf.data(), &sps_size,
                                               (uint8_t *) pps_buf.data(), &pps_size)) {
                    LOGE("{}: convert_sps_pps: failed\n", __func__);
                    return -1;
                }
                csd0 = sps_buf.substr(0, sps_size);
                csd1 = pps_buf.substr(0, pps_size);

                sdk_stat_->video_format_ = "H264";
            }
            else
            {
                csd0 = StreamHelper::ConvertVPSH265SPSPPS((const uint8_t *) in_frame_data, (size_t) in_frame_size);
                if (csd0.empty()) {
                    LOGE("{} :convert_sps_pps: failed\n", __func__);
                    return -1;
                }

                sdk_stat_->video_format_ = "HEVC";
            }

            LOGI("csd0: {} size: {}, csd1: {}, size: {}", csd0.c_str(), csd0.size(), csd1.c_str(), csd1.size());
            if (csd0.size() > 100 || csd1.size() > 100) {
                LOGI("Ignore the error csd...");
                return -1;
            }
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

        ANativeWindow* target = use_oes_ ? (ANativeWindow*)(surface) : nullptr;
        LOGI("decoder name: {}, target: {}", decoder_name, (void*)target);
        media_status_t status = AMediaCodec_configure(media_codec_.get(), media_format_.get(),
                                                      target,
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
        this->frame_width_ = width;
        this->frame_height_ = height;
        inited_ = true;

        return AMEDIA_OK;
    }

    Result<std::shared_ptr<RawImage>, int> MediacodecVideoDecoder::Decode(const uint8_t *in_data, int in_size) {
        auto beg = TimeUtil::GetCurrentTimestamp();
        if (!media_codec_ || !in_data || in_size <= 0) {
            LOGE("param valid...");
            return TRError(-1);
        }
        ssize_t buf_idx = AMediaCodec_dequeueInputBuffer(media_codec_.get(), 2000);
        if (buf_idx >= 0) {
            size_t buf_size = 0;
            auto* buf = AMediaCodec_getInputBuffer(media_codec_.get(), buf_idx, &buf_size); // NOLINT(gammaray-raw-pointer-boundary)
            if (!buf || static_cast<size_t>(in_size) > buf_size) {
                LOGE("getInputBuffer failed or encoded frame exceeds buffer: frame={}, buffer={}", in_size, buf_size);
                return TRError(-1);
            }
            memcpy(buf, in_data, in_size);
            uint64_t presentationTimeUs = getTimeUsec();
            const auto queue_status = AMediaCodec_queueInputBuffer(media_codec_.get(), buf_idx, 0, in_size, presentationTimeUs, 0);
            if (queue_status != AMEDIA_OK) {
                LOGE("queueInputBuffer failed: {}", static_cast<int>(queue_status));
                return TRError(-1);
            }
        }

        AMediaCodecBufferInfo info;
        do {
            buf_idx = AMediaCodec_dequeueOutputBuffer(media_codec_.get(), &info, 2000);
            if (buf_idx >= 0) {
                size_t out_buf_size = 0;
                uint8_t* buf = nullptr;
                int real_frame_size = 0;
                std::unique_ptr<AMediaFormat, decltype(&AMediaFormat_delete)> format(AMediaCodec_getOutputFormat(media_codec_.get()),
                                                                                    &AMediaFormat_delete);
                if (!format) {
                    LOGE("getOutputFormat failed");
                    return TRError(-1);
                }
                // to do 格式变化的时候 android 这里也要注意下
                int width, height;
                AMediaFormat_getInt32(format.get(), "width", &width);
                AMediaFormat_getInt32(format.get(), "height", &height);
                int32_t color_format;
                AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_COLOR_FORMAT,&color_format);
//                real_frame_size = info.size;
//                buf = AMediaCodec_getOutputBuffer(media_codec_, buf_idx, &out_buf_size);
//
//                // test/beg
//                // static int i = 0;
//                // if (i < 5) {
//                //     std::string name = fmt::format("/data/data/com.px.client/cache/aa_{}.yuv", i++);
//                //     std::ofstream file(name, std::ios::binary);
//                //     file.write((char *) buf, real_frame_size);
//                //     file.close();
//                // }
//                // test/end
//
//                LOGI("out:[{}]X[{}], format: {}, real_frame_size:{}, buf_size: {} ", width, height, color_format, real_frame_size, out_buf_size); //21 == nv21
//                if (buf && cbk && real_frame_size > 0 && !use_oes_) {
//                    auto image = RawImage::Make((char *) buf, real_frame_size, width, height, -1, RawImageFormat::kNV12);
//                    cbk(image);
//                }
//                else {
//                    cbk(nullptr);
//                }

                // only callback frame info
                auto image = RawImage::Make(nullptr, 0, width, height, -1, RawImageFormat::kRawImageNV12);
                AMediaCodec_releaseOutputBuffer(media_codec_.get(), buf_idx, true);
                auto end = TimeUtil::GetCurrentTimestamp();
                SdkStatistics::Instance()->AppendDecodeDuration(monitor_name_, end-beg);
                return image;
            } else if (buf_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                int width, height;
                std::unique_ptr<AMediaFormat, decltype(&AMediaFormat_delete)> format(AMediaCodec_getOutputFormat(media_codec_.get()),
                                                                                    &AMediaFormat_delete);
                if (!format) {
                    return TRError(-1);
                }
                AMediaFormat_getInt32(format.get(), "width", &width);
                AMediaFormat_getInt32(format.get(), "height", &height);
                int32_t color_format;
                AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_COLOR_FORMAT,&color_format);
            } else if (buf_idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {

            } else {

            }
        } while (buf_idx > 0);

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
        inited_ = false;
    }

    bool MediacodecVideoDecoder::Ready() {
        return inited_;
    }

    bool MediacodecVideoDecoder::NeedReConstruct(int codec_type, int width, int height, int img_format) {
        return codec_type != this->codec_type_ || width != this->frame_width_ || height != this->frame_height_;
    }

}

#endif
