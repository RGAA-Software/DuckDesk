//
// Created by RGAA  on 2024/1/6.
//

#ifndef TC_APPLICATION_NVENC_VIDEO_ENCODER_H
#define TC_APPLICATION_NVENC_VIDEO_ENCODER_H

#include "nvencoder/12/NvEncoderD3D11.h"
#include "px_encoder_new/encoder_config.h"
#include <fstream>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <any>
#include <atomic>
#include <mutex>
#include "px_common_new/fps_stat.h"
#include "px_capture_new/capture_message.h"

using namespace Microsoft::WRL;

namespace px
{

    class NvencEncoderModule;

    class NVENCVideoEncoder {
    public:
        NVENCVideoEncoder(
            const std::shared_ptr<NvencEncoderModule>& owner,
            uint64_t adapter_uid);
        ~NVENCVideoEncoder();

        bool Initialize(const px::EncoderConfig& config);
        bool Encode(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex2d,
                    uint64_t frame_index,
                    const CaptureVideoFrame& capture_frame);
        void InsertIdr();
        bool InvalidateRefFrame(uint64_t invalid_frame_index);
        void Exit();
        int32_t GetEncodeFps();
        std::vector<int32_t> GetEncodeDurations();
        bool Config( uint32_t bps, uint32_t fps);

        bool SupportH264Yuv444();
        bool SupportHevcYuv444();

    private:
        bool Transmit(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex2d,
                      uint64_t frame_index,
                      const CaptureVideoFrame& capture_frame);
        void Shutdown();
        void FillEncodeConfig(NV_ENC_INITIALIZE_PARAMS& initialize_params, int refreshRate, int renderWidth, int renderHeight, uint64_t bitrate_bps);
        static NV_ENC_BUFFER_FORMAT DxgiFormatToNvEncFormat(DXGI_FORMAT dxgiFormat);
        bool CreateNvEncoder();
        bool ApplyPendingConfigLocked();

    private:
        std::shared_ptr<NvEncoder> nv_encoder_ = nullptr;
        EncoderConfig encoder_config_;
        bool insert_idr_ = false;
        std::weak_ptr<NvencEncoderModule> owner_;

        ComPtr<ID3D11Device> d3d11_device_;
        ComPtr<ID3D11DeviceContext> d3d11_device_context_;

        std::shared_ptr<FpsStat> fps_stat_ = nullptr;
        std::deque<int32_t> encode_durations_;

        NV_ENC_BUFFER_FORMAT e_buffer_format_;

        bool enable_yuv444_ = false;

        bool has_transmit_frames_ = false;
        uint64_t last_encoded_frame_index_ = 0;

        // ConfigEncoder 常在全局/插件线程触发,而 Encode 在 encoder_thread。
        // NVENC Reconfigure 与 EncodeFrame 并发会卡死(切屏新建第二路 encoder 时尤其易中招)。
        // Config 只写入 pending,真正 Reconfigure 串到 Encode 路径并加锁。
        std::mutex encode_mtx_;
        std::atomic_uint32_t pending_bps_{0};
        std::atomic_uint32_t pending_fps_{0};
        // WebRTC SetRates 在刷新/重连时每秒连打多次;全量 CreateDefaultEncoderParams+Reconfigure
        // 会触发驱动/断言崩(历史上 0x80000003)。节流 + 忽略微小变化。
        uint32_t applied_bps_{0};
        uint32_t applied_fps_{0};
        int64_t last_reconfigure_ms_{0};
    };

}
#endif //TC_APPLICATION_NVENC_VIDEO_ENCODER_H
