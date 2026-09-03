//
// Created by RGAA on 19/11/2024.
//

#include "px_video_encoder_plugin.h"
#include "px_common_new/log.h"

namespace px
{

    PxVideoEncoderPlugin::PxVideoEncoderPlugin() : PxPluginInterface() {
        plugin_type_ = PxPluginType::kEncoder;
    }

    PxVideoEncoderPlugin::~PxVideoEncoderPlugin() {

    }

    bool PxVideoEncoderPlugin::OnCreate(const px::PxPluginParam &param) {
        PxPluginInterface::OnCreate(param);
        return true;
    }

    bool PxVideoEncoderPlugin::OnDestroy() {
        return PxPluginInterface::OnDestroy();
    }

    bool PxVideoEncoderPlugin::CanEncodeTexture() {
        return false;
    }

    bool PxVideoEncoderPlugin::Init(const EncoderConfig& config, const std::string& monitor_name) {
        LOGI("PxVideoEncoderPlugin Init, {}x{}", config.encode_width, config.encode_height);
        encoder_configs_[monitor_name] = config;
        out_width_ = config.encode_width;
        out_height_ = config.encode_height;
        refresh_rate_ = config.fps;
        return true;
    }

    VideoEncoderError PxVideoEncoderPlugin::Encode(
        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex2d,
        uint64_t frame_index,
        const CaptureVideoFrame& capture_frame) {
        return VideoEncoderError::NotImplemented();
    }

    VideoEncoderError PxVideoEncoderPlugin::Encode(
        const std::shared_ptr<Image>& i420_image,
        uint64_t frame_index,
        const CaptureVideoFrame& capture_frame) {
        return VideoEncoderError::NotImplemented();
    }

    void PxVideoEncoderPlugin::InsertIdr() {
        insert_idr_ = true;
    }

    void PxVideoEncoderPlugin::InsertIdr(const std::string& mon_name) {
        // 默认实现:无视屏名,全量补 IDR(保持未 override 插件的旧行为)
        InsertIdr();
    }

    void PxVideoEncoderPlugin::On1Second() {
        if (client_side_media_recording_) {
            InsertIdr();
        }
    }

    void PxVideoEncoderPlugin::SetClientSideMediaRecording(bool recording) {
        client_side_media_recording_ = recording;
    }

    std::optional<EncoderConfig> PxVideoEncoderPlugin::GetEncoderConfig(const std::string& monitor_name) {

        if (encoder_configs_.find(monitor_name) != encoder_configs_.end()) {
            return encoder_configs_[monitor_name];
        }
        return std::nullopt;
    }

    void PxVideoEncoderPlugin::Exit(const std::string& monitor_name) {

    }

    void PxVideoEncoderPlugin::ExitAll() {

    }

}
