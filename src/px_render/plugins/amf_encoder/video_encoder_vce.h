#pragma once

#include "amf/common/AMFFactory.h"
#include "amf/include/components/VideoEncoderVCE.h"
#include "amf/include/components/VideoEncoderHEVC.h"
#include "amf/include/components/VideoConverter.h"
#include "amf/common/Thread.h"
#include "px_encoder_new/encoder_config.h"
#include "px_encoder_new/video_encoder.h"
#include "px_common_new/fps_stat.h"
#include "px_capture_new/capture_message.h"
#include <thread>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>

typedef std::function<void (amf::AMFData *)> AMFTextureReceiver;

namespace px
{
    class AmfEncoderPlugin;

    class AMFTextureEncoder {
    public:
        AMFTextureEncoder(const amf::AMFContextPtr &amfContext, EncoderConfig config,
                          amf::AMF_SURFACE_FORMAT inputFormat, AMFTextureReceiver receiver);
        ~AMFTextureEncoder();
        bool Init();
        void Start();
        void Shutdown();
        void Submit(amf::AMFData *data);
        void Run();

    private:
        amf::AMFComponentPtr amf_encoder_ = nullptr;
        std::thread* thread_ = nullptr;
        AMFTextureReceiver receiver_;
        EVideoCodecType codec_;
        amf::AMFContextPtr amf_context_ = nullptr;
        EncoderConfig encoder_config_;
        amf::AMF_SURFACE_FORMAT input_format_;
    };

    class AMFTextureConverter {
    public:
        AMFTextureConverter(const amf::AMFContextPtr &amfContext, int width, int height,
                            amf::AMF_SURFACE_FORMAT inputFormat, amf::AMF_SURFACE_FORMAT outputFormat,
                            AMFTextureReceiver receiver);
        ~AMFTextureConverter();
        void Start();
        void Shutdown();
        void Submit(amf::AMFData *data);
        void Run();

    private:
        amf::AMFComponentPtr amf_converter_ = nullptr;
        std::thread* thread_ = nullptr;
        AMFTextureReceiver receiver_;
    };

    // Video encoder for AMD VCE.
    class VideoEncoderVCE {
    public:
        explicit VideoEncoderVCE(AmfEncoderPlugin* plugin, uint64_t adapter_uid);
        ~VideoEncoderVCE();

        bool Initialize(const px::EncoderConfig &config);
        bool Encode(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex2d,
                    uint64_t frame_index,
                    const CaptureVideoFrame& capture_frame);
        void InsertIdr();
        void Exit();
        void Shutdown();
        void Receive(amf::AMFData *data);
        int32_t GetEncodeFps();
        std::vector<int32_t> GetEncodeDurations();

    private:
        void ApplyFrameProperties(const amf::AMFSurfacePtr &surface, bool insertIDR);
        void SkipAUD(char** buffer, int* length);
        bool EncodeTexture(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex2d, int width, int height, int64_t frame_idx);

    private:
        amf::AMF_SURFACE_FORMAT convert_input_format_ = amf::AMF_SURFACE_BGRA;// AMF_SURFACE_RGBA;
        amf::AMF_SURFACE_FORMAT encoder_input_format_ = amf::AMF_SURFACE_BGRA;// amf::AMF_SURFACE_NV12;
        amf::AMFContextPtr amf_context_ = nullptr;
        std::shared_ptr<AMFTextureEncoder> encoder_ = nullptr;
        std::shared_ptr<AMFTextureConverter> converter_ = nullptr;
        EVideoCodecType codec_type_{};
        bool insert_idr_ = false;
        int gop_ = 180;
        EncoderConfig encoder_config_;
        px::AmfEncoderPlugin* plugin_ = nullptr;

        ComPtr<ID3D11Device> d3d11_device_;
        ComPtr<ID3D11DeviceContext> d3d11_device_context_;

        std::mutex capture_frames_mutex_;
        std::map<std::uint64_t, CaptureVideoFrame> capture_frames_;

        std::shared_ptr<FpsStat> fps_stat_ = nullptr;
        std::deque<int32_t> encode_durations_;
    };

}
