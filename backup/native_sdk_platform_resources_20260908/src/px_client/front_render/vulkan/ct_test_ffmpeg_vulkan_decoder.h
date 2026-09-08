#pragma once
#include <functional>
#include <optional>
#include <memory>
#include "px_client_sdk/av_frame_ref.h"
extern "C" {
	#include <libavcodec/codec.h>
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <libavutil/imgutils.h>
	#include <libavutil/opt.h>
	#include <libavutil/log.h>
	#include <libavutil/pixdesc.h>
	#include <libavutil/hwcontext.h>
	#include <libavutil/avassert.h>
}

namespace px {

    using TestPostAVFrameCallbackFuncType = std::function<void(AVFrame* frame)>;

    class TestFFmpegVulkanDecoder {
    public:
        static std::shared_ptr<TestFFmpegVulkanDecoder> Make();
        TestFFmpegVulkanDecoder();
        ~TestFFmpegVulkanDecoder() ;

        AVCodec* test_hevc_decoder_ = nullptr;
        AVCodecContext* test_hevc_video_decoder_ctx_ = nullptr;
        TestPostAVFrameCallbackFuncType test_hevc_post_av_frame_callback_func_ = nullptr;

        bool InitTestHevcDecoder();
        bool OpenTestHevcDecoder();
        AvFramePtr GetDecodeTestHevcYuv444Frame();
        void SetHwDeviceCtx(AVBufferRef* hw_device_ctx);
       
        static const uint8_t k_HEVCRExt8_444TestFrame[];
    };

}
