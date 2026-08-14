//
// Created by RGAA  on 2024/1/6.
//

#ifndef TC_APPLICATION_NVENC_VIDEO_ENCODER_H
#define TC_APPLICATION_NVENC_VIDEO_ENCODER_H

#include "video_encoder.h"
#include "nvencoder/12/NvEncoderD3D11.h"
#include <fstream>

namespace tc
{

    class NVENCVideoEncoder : public VideoEncoder {
    public:
        NVENCVideoEncoder(const std::shared_ptr<MessageNotifier>& msg_notifier, const EncoderFeature& encoder_feature);
        ~NVENCVideoEncoder() override;

        bool Initialize(const tc::EncoderConfig& config) override;
        //void Encode(uint64_t handle, uint64_t frame_index) override;
        void Encode(ID3D11Texture2D* tex2d, uint64_t frame_index) override;
        void Exit() override;

    private:
        //void CopyRawTexture(ID3D11Texture2D* texture, DXGI_FORMAT format, int height);

    private:
        void Transmit(ID3D11Texture2D* pTexture);
        void Shutdown();
        void FillEncodeConfig(NV_ENC_INITIALIZE_PARAMS& initialize_params, int refreshRate, int renderWidth, int renderHeight, uint64_t bitrate_bps);
        static NV_ENC_BUFFER_FORMAT DxgiFormatToNvEncFormat(DXGI_FORMAT dxgiFormat);

        std::shared_ptr<NvEncoder> nv_encoder_ = nullptr;
    };

}
#endif //TC_APPLICATION_NVENC_VIDEO_ENCODER_H
