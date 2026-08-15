//
// Created by RGAA on 19/11/2024.
//

#include "px_video_encoder_plugin.h"
#include "px_common_new/log.h"

namespace tc
{

    GrVideoEncoderPlugin::GrVideoEncoderPlugin() : GrPluginInterface() {
        plugin_type_ = GrPluginType::kEncoder;
    }

    GrVideoEncoderPlugin::~GrVideoEncoderPlugin() {

    }

    bool GrVideoEncoderPlugin::OnCreate(const tc::GrPluginParam &param) {
        GrPluginInterface::OnCreate(param);
        return true;
    }

    bool GrVideoEncoderPlugin::OnDestroy() {
        return GrPluginInterface::OnDestroy();
    }

    bool GrVideoEncoderPlugin::CanEncodeTexture() {
        return false;
    }

    bool GrVideoEncoderPlugin::Init(const EncoderConfig& config, const std::string& monitor_name) {
        LOGI("GrVideoEncoderPlugin Init, {}x{}", config.encode_width, config.encode_height);
        encoder_configs_[monitor_name] = config;
        out_width_ = config.encode_width;
        out_height_ = config.encode_height;
        refresh_rate_ = config.fps;
        return true;
    }

    VideoEncoderError GrVideoEncoderPlugin::Encode(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex2d, uint64_t frame_index, const std::any& extra) {
        return VideoEncoderError::NotImplemented();
    }

    VideoEncoderError GrVideoEncoderPlugin::Encode(const std::shared_ptr<Image>& i420_image, uint64_t frame_index, const std::any& extra) {
        return VideoEncoderError::NotImplemented();
    }

    void GrVideoEncoderPlugin::InsertIdr() {
        insert_idr_ = true;
    }

    void GrVideoEncoderPlugin::InsertIdr(const std::string& mon_name) {
        // 默认实现:无视屏名,全量补 IDR(保持未 override 插件的旧行为)
        InsertIdr();
    }

    void GrVideoEncoderPlugin::On1Second() {
        if (client_side_media_recording_) {
            InsertIdr();
        }
    }

    void GrVideoEncoderPlugin::SetClientSideMediaRecording(bool recording) {
        client_side_media_recording_ = recording;
    }

    std::optional<EncoderConfig> GrVideoEncoderPlugin::GetEncoderConfig(const std::string& monitor_name) {

        if (encoder_configs_.find(monitor_name) != encoder_configs_.end()) {
            return encoder_configs_[monitor_name];
        }
        return std::nullopt;
    }

    void GrVideoEncoderPlugin::Exit(const std::string& monitor_name) {

    }

    void GrVideoEncoderPlugin::ExitAll() {

    }

}
