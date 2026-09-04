#include "video_encoder_module.h"

#include "px_common_new/log.h"

namespace px {

namespace {
VideoEncoderError MakeError(const VideoEncoderErrorType type) {
    VideoEncoderError error;
    error.type_ = type;
    return error;
}
}  // namespace

VideoEncoderError VideoEncoderError::Ok() { return MakeError(VideoEncoderErrorType::kOk); }
VideoEncoderError VideoEncoderError::NotFound() { return MakeError(VideoEncoderErrorType::kNotFound); }
VideoEncoderError VideoEncoderError::EncodeFailed() { return MakeError(VideoEncoderErrorType::kEncodeFailed); }
VideoEncoderError VideoEncoderError::InvalidInput() { return MakeError(VideoEncoderErrorType::kInvalidInput); }
VideoEncoderError VideoEncoderError::NotImplemented() { return MakeError(VideoEncoderErrorType::kNotImplemented); }

bool VideoEncoderError::Success() const noexcept {
    return type_ == VideoEncoderErrorType::kOk;
}

std::string VideoEncoderError::GetReadableType() const {
    switch (type_) {
        case VideoEncoderErrorType::kOk: return "Ok";
        case VideoEncoderErrorType::kNotFound: return "Not found encoder";
        case VideoEncoderErrorType::kEncodeFailed: return "Encode failed";
        case VideoEncoderErrorType::kInvalidInput: return "Invalid input";
        case VideoEncoderErrorType::kNotImplemented: return "Not implemented";
        case VideoEncoderErrorType::kUnknown: return "Unknown error";
    }
    return "Unknown error";
}

bool VideoEncoderModule::Start(const RenderModuleConfiguration& configuration) {
    return RenderModule::Start(configuration);
}

bool VideoEncoderModule::Destroy() { return RenderModule::Destroy(); }

void VideoEncoderModule::RequestKeyFrame() { insert_idr_ = true; }

void VideoEncoderModule::RequestKeyFrame(const std::string&) { RequestKeyFrame(); }

bool VideoEncoderModule::InvalidateReferenceFrame(const std::string&, std::uint64_t) {
    return false;
}

void VideoEncoderModule::Tick1Second() {
    if (client_side_media_recording_.load()) {
        RequestKeyFrame();
    }
}

bool VideoEncoderModule::CanEncodeTexture() const { return false; }

bool VideoEncoderModule::Initialize(
    const EncoderConfig& configuration, const std::string& monitor_name) {
    LOGI("event=encoder.initialize component={} width={} height={} fps={}",
         Id(), configuration.encode_width, configuration.encode_height,
         configuration.fps);
    encoder_configurations_[monitor_name] = configuration;
    out_width_ = configuration.encode_width;
    out_height_ = configuration.encode_height;
    refresh_rate_ = configuration.fps;
    return true;
}

VideoEncoderError VideoEncoderModule::Encode(
    const Microsoft::WRL::ComPtr<ID3D11Texture2D>&,
    std::uint64_t, const CaptureVideoFrame&) {
    return VideoEncoderError::NotImplemented();
}

VideoEncoderError VideoEncoderModule::Encode(
    const std::shared_ptr<Image>&, std::uint64_t, const CaptureVideoFrame&) {
    return VideoEncoderError::NotImplemented();
}

void VideoEncoderModule::Remove(const std::string&) {}
void VideoEncoderModule::RemoveAll() {}

std::optional<EncoderConfig> VideoEncoderModule::Configuration(
    const std::string& monitor_name) const {
    const auto found = encoder_configurations_.find(monitor_name);
    return found == encoder_configurations_.end()
        ? std::nullopt
        : std::optional<EncoderConfig>{found->second};
}

void VideoEncoderModule::SetClientSideMediaRecording(const bool recording) noexcept {
    client_side_media_recording_.store(recording);
}

void VideoEncoderModule::Reconfigure(
    const std::string&, std::uint32_t, std::uint32_t) {}

std::optional<EncoderCapability> VideoEncoderModule::Capability(
    const std::string&) const {
    return std::nullopt;
}

}  // namespace px
