#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "architecture/modules/render_module.h"
#include "px_capture_new/capture_message.h"
#include "px_encoder_new/encoder_config.h"

namespace px {

class Image;

struct WorkingEncoderInfo final {
    std::string target_name_;
    std::int32_t fps_{0};
    std::string encoder_name_;
    std::vector<std::int32_t> encode_durations_;
};
using WorkingEncoderInfoPtr = std::shared_ptr<WorkingEncoderInfo>;

struct EncoderCapability final {
    bool support_h264_yuv444_{false};
    bool support_hevc_yuv444_{false};
};

enum class VideoEncoderErrorType {
    kOk,
    kNotFound,
    kEncodeFailed,
    kInvalidInput,
    kNotImplemented,
    kUnknown,
};

struct VideoEncoderError final {
    [[nodiscard]] static VideoEncoderError Ok();
    [[nodiscard]] static VideoEncoderError NotFound();
    [[nodiscard]] static VideoEncoderError EncodeFailed();
    [[nodiscard]] static VideoEncoderError InvalidInput();
    [[nodiscard]] static VideoEncoderError NotImplemented();
    [[nodiscard]] bool Success() const noexcept;
    [[nodiscard]] std::string GetReadableType() const;

    VideoEncoderErrorType type_{VideoEncoderErrorType::kUnknown};
    int inner_error_{0};
    std::string msg_;
};

class VideoEncoderModule : public RenderModule {
public:
    [[nodiscard]] RenderModuleKind Kind() const final {
        return RenderModuleKind::kEncoder;
    }

    bool Start(const RenderModuleConfiguration& configuration) override;
    bool Destroy() override;
    void RequestKeyFrame() override;
    virtual void RequestKeyFrame(const std::string& monitor_name);
    virtual bool InvalidateReferenceFrame(
        const std::string& monitor_name,
        std::uint64_t invalid_frame_index);
    void Tick1Second() override;

    [[nodiscard]] virtual bool CanEncodeTexture() const;
    [[nodiscard]] virtual bool HasEncoderForMonitor(
        const std::string& monitor_name) const = 0;
    virtual bool Initialize(const EncoderConfig& configuration,
                            const std::string& monitor_name);
    virtual VideoEncoderError Encode(
        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
        std::uint64_t frame_index,
        const CaptureVideoFrame& capture_frame);
    virtual VideoEncoderError Encode(
        const std::shared_ptr<Image>& image,
        std::uint64_t frame_index,
        const CaptureVideoFrame& capture_frame);
    virtual void Remove(const std::string& monitor_name);
    virtual void RemoveAll();
    [[nodiscard]] virtual std::map<std::string, WorkingEncoderInfoPtr>
    WorkingCaptures() const = 0;
    [[nodiscard]] std::optional<EncoderConfig> Configuration(
        const std::string& monitor_name) const;
    void SetClientSideMediaRecording(bool recording) noexcept;
    virtual void Reconfigure(const std::string& monitor_name,
                             std::uint32_t bits_per_second,
                             std::uint32_t frames_per_second);
    [[nodiscard]] virtual std::optional<EncoderCapability> Capability(
        const std::string& monitor_name) const;

protected:
    int refresh_rate_{60};
    std::uint32_t out_width_{0};
    std::uint32_t out_height_{0};
    int gop_size_{60};
    int bitrate_{10'000'000};
    bool insert_idr_{false};
    std::map<std::string, EncoderConfig> encoder_configurations_;

private:
    std::atomic_bool client_side_media_recording_{false};
};

}  // namespace px
