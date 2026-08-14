//
// Created by RGAA on 2024/1/26.
//

#include "sdk_mediacodec_video_decoder.h"

#ifdef ANDROID

#include "tc_common_new/log.h"
#include "tc_common_new/time_util.h"
#include "tc_client_sdk_new/gl/raw_image.h"
#include "sdk_stream_helper.h"
#include "sdk_statistics.h"

#include <fstream>

namespace tc
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

    MediacodecVideoDecoder::~MediacodecVideoDecoder() {

    }

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
                csd0 = pps_buf.substr(0,pps_size);
                csd1 = sps_buf.substr(0,sps_size);

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

        media_codec_ = AMediaCodec_createDecoderByType(decoder_name.c_str());
        media_format_ = AMediaFormat_new();
        AMediaFormat_setString(media_format_, "mime", decoder_name.c_str());
        AMediaFormat_setInt32(media_format_, AMEDIAFORMAT_KEY_WIDTH, width);
        AMediaFormat_setInt32(media_format_, AMEDIAFORMAT_KEY_HEIGHT, height);
        AMediaFormat_setInt32(media_format_, AMEDIAFORMAT_KEY_FRAME_RATE, 60);

        if (!csd0.empty()) {
            // AMediaFormat_setBuffer(media_format_, "csd-0", csd0.c_str(), csd0.size());
        }
        if (!csd1.empty()) {
            // AMediaFormat_setBuffer(media_format_, "csd-1", csd1.c_str(), csd1.size());
        }

        ANativeWindow* target = use_oes_ ? (ANativeWindow*)(surface) : nullptr;
        LOGI("decoder name: {}, target: {}", decoder_name, (void*)target);
        media_status_t status = AMediaCodec_configure(media_codec_, media_format_,
                                                      target,
                                                      nullptr,
                                                      0);//解码，flags 给0，编码给AMEDIACODEC_CONFIGURE_FLAG_ENCODE
        if (status != AMEDIA_OK) {
            LOGE("error config {}", (int) status);
            AMediaCodec_delete(media_codec_);
            AMediaFormat_delete(media_format_);
            media_codec_ = nullptr;
            media_format_ = nullptr;
            return -1;
        }

        //启动
        status = AMediaCodec_start(media_codec_);
        if (status != AMEDIA_OK) {
            LOGE("error start: {}", (int) status);
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
        ssize_t buf_idx = AMediaCodec_dequeueInputBuffer(media_codec_, 2000);
        if (buf_idx >= 0) {
            size_t buf_size = 0;
            uint8_t* buf = AMediaCodec_getInputBuffer(media_codec_, buf_idx, &buf_size);
            if (buf_size <= 0) {
                LOGE("getInputBuffer failed.");
                return TRError(-1);
            }
            memcpy(buf, in_data, in_size);
            uint64_t presentationTimeUs = getTimeUsec();
            AMediaCodec_queueInputBuffer(media_codec_, buf_idx, 0, in_size, presentationTimeUs, 0);
        }

        AMediaCodecBufferInfo info;
        do {
            buf_idx = AMediaCodec_dequeueOutputBuffer(media_codec_, &info, 2000);
            if (buf_idx >= 0) {
                size_t out_buf_size = 0;
                uint8_t* buf = nullptr;
                int real_frame_size = 0;
                auto format = AMediaCodec_getOutputFormat(media_codec_);
                // to do 格式变化的时候 android 这里也要注意下
                int width, height;
                AMediaFormat_getInt32(format, "width", &width);
                AMediaFormat_getInt32(format, "height", &height);
                int32_t color_format;
                AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT,&color_format);
//                real_frame_size = info.size;
//                buf = AMediaCodec_getOutputBuffer(media_codec_, buf_idx, &out_buf_size);
//
//                // test/beg
//                // static int i = 0;
//                // if (i < 5) {
//                //     std::string name = fmt::format("/data/data/com.tc.client/cache/aa_{}.yuv", i++);
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
                AMediaCodec_releaseOutputBuffer(media_codec_, buf_idx, true);
                auto end = TimeUtil::GetCurrentTimestamp();
                SdkStatistics::Instance()->AppendDecodeDuration(monitor_name_, end-beg);
                return image;
            } else if (buf_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                int width, height;
                auto format = AMediaCodec_getOutputFormat(media_codec_);
                AMediaFormat_getInt32(format, "width", &width);
                AMediaFormat_getInt32(format, "height", &height);
                int32_t color_format;
                AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT,&color_format);
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
            AMediaCodec_stop(media_codec_);
            AMediaCodec_delete(media_codec_);
        }

        LOGI("will delete media format");
        if (media_format_) {
            AMediaFormat_delete(media_format_);
        }
    }

    bool MediacodecVideoDecoder::Ready() {
        return inited_;
    }

    bool MediacodecVideoDecoder::NeedReConstruct(int codec_type, int width, int height, int img_format) {
        return codec_type != this->codec_type_ || width != this->frame_width_ || height != this->frame_height_;
    }

}

#endif