#pragma once

#include <wrl/client.h>
#include <d3d11.h>
#include "tc_common_new/image.h"

namespace tc
{

    constexpr uint32_t kDefaultVideoQuality = 23;

    enum class EHardwareEncoder
    {
        kNone = 0,
        kAmf,
        kNvEnc,
        kQsv
    };

    enum class EVideoCodecType
    {
        kH264 = 0,
        kHEVC = 1,
    };

    enum class ERateControlMode {
        kRateControlModeCbr = 0,
        kRateControlModeVbr = 1,
        kRateControlModeConstQp = 2,
    };

    enum class ENvdiaEncMultiPass {
        kMultiPassDisabled,                /**< Single Pass */
        kTwoPassQuarterResolution,        /**< Two Pass encoding is enabled where first Pass is quarter resolution */
        kTwoPassFullResolution,           /**< Two Pass encoding is enabled where first Pass is full resolution */
    };

    class EncoderFeature {
    public:
        // 显卡设备uid
        int64_t adapter_uid_ = -1;
        // 采集画面的索引，通常用于多桌面模式
        int capture_index_ = -1;
    };

    struct EncoderConfig
    {
        int width = -1;
        int height = -1;
        bool frame_resize = false;
        int encode_width = -1;
        int encode_height = -1;
        int sample_desc_count = -1;
    #ifdef WIN32
        int texture_format = -1;
    #endif
        EVideoCodecType codec_type;
        int64_t bitrate = -1;
        /** for ffmpeg encoder. */
        EHardwareEncoder Hardware = EHardwareEncoder::kNone;
        int fps = -1;

        //关键帧间隔 备注：在使用英伟达显卡编码中，如果设置-1，则使用 NVENC_INFINITE_GOPLENGTH
        int gop_size = -1;
        ERateControlMode rate_control_mode;

        // nvidia qp set, -1 表示不启用， 如果启用了 const_qp，就不要设置maxqp与minqp了，const_qp 要求编码模式不能为vbr
        int const_qp = -1;
        int max_qp = 36;
        int min_qp = 22;

        // nvidia quality preset (1-7),随着从 P1 到 P7 的转变，性能下降而质量提高
        uint8_t quality_preset = 1;

        // nvidia set, 可以在视频序列中定期插入额外的帧内编码帧，以减少编码引起的累积错误和失真
        bool supports_intra_refresh = true;

        // 英伟达 多次编码设置
        /*
        0, disable Single Pass
        1, Two Pass encoding is enabled where first Pass is quarter resolution
        2, Two Pass encoding is enabled where first Pass is full resolution
        */
        ENvdiaEncMultiPass multi_pass;

        // nvidia vbr mode set enable Adaptive Quantization  启用自适应量化
        bool enable_adaptive_quantization = true;

        // nvidia set, -1 表示不启用
        // 在 NVIDIA Video Codec (NVENC) SDK 中，targetQuality 是用于可变比特率（VBR）模式的参数设置之一。
        // Target CQ (Constant Quality) level for VBR mode (range 0-51 with 0-automatic)
        // targetQuality 值越大编码质量越好
        int target_quality = -1;

        int64_t adapter_uid_ = -1;

        // 是否启用全彩模式: 如果启用全彩模式, 则编码输出的帧可以解码为yuv444, 否则为yuv420
        bool enable_full_color_mode_ = false;
    };
}