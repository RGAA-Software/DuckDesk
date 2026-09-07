//
// Created by chess on 2024-01-28.
//

#ifndef TC_CLIENT_ANDROID_DECODER_RENDER_TYPE_H
#define TC_CLIENT_ANDROID_DECODER_RENDER_TYPE_H

enum class DecoderRenderType {
    kFFmpegI420,
    kMediaCodecNv21,
    kMediaCodecSurface,
};

#endif //TC_CLIENT_ANDROID_DECODER_RENDER_TYPE_H
