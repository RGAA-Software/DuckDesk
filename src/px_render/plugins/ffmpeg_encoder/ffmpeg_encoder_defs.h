//
// Created by RGAA on 19/11/2024.
//

#ifndef PX_FFMPEG_ENCODER_DEFS_H
#define PX_FFMPEG_ENCODER_DEFS_H

#include <string>
#include <string_view>

namespace px
{
    static const std::string kFFmpegPluginName = "Common Encoder";

    // These H.264 encoders otherwise turn an AV_PICTURE_TYPE_I request into a
    // non-IDR I-frame. A receiver joining an existing stream cannot decode that
    // frame without the previous reference chain, so forced-idr must be enabled.
    inline bool ShouldEnableForcedH264Idr(std::string_view codec_name) {
        return codec_name == "libx264" || codec_name == "h264_nvenc";
    }
}

#endif //PX_FFMPEG_ENCODER_DEFS_H
