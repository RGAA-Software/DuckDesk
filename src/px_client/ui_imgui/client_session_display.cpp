#include "client_session.h"

#include "client_audio_output.h"
#include "ct_virtual_display_protocol.h"
#include "px_client_sdk/platform/windows/windows_decoder_factory.h"
#include "px_client_sdk/platform/windows/windows_video_resources.h"
#include "px_client_sdk/sdk_messages.h"
#include "px_client_sdk/sdk_params.h"
#include "px_client_sdk/sdk_connection_params.h"
#include "px_client_sdk/sdk_net_client.h"
#include "px_client_sdk/sdk_recording_session.h"
#include "px_client_sdk/sdk_statistics.h"
#include "px_client_sdk/sdk_voice_call.h"
#include "px_client_sdk/platform/voice_audio_endpoint_port.h"
#include "px_client_sdk/thunder_sdk.h"
#include "px_common/data.h"
#include "px_common/md5.h"
#include "px_common/message_notifier.h"
#include "px_common/time_util.h"
#include "px_common/url_helper.h"
#include "px_message/proto_converter.h"
#include "px_message/proto_message_maker.h"
#include "px_message.pb.h"
#include "px_ft_engine/ft_async_session.h"
#include "px_ft_engine/ft_engine.h"
#include "px_rdp/rdp_client_endpoint.h"
#include "px_rdp/rdp_stream_packet.h"
#include "rdp/rdp_session.h"

#include <SDL3/SDL.h>
#include <freerdp/input.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <utility>

namespace px::client::imgui {

bool ClientSession::SwitchMonitor(const std::string& name) {
    {
        const std::scoped_lock lock{mutex_};
        if (name.empty() || std::ranges::find(monitors_, name) == monitors_.end()) return false;
    }
    return SendMedia(px::ProtoMessageMaker::MakeChangeMonitor(
        0, name, "client_" + config_.localDeviceId + "_" + px::MD5::Hex(config_.remoteDeviceId), config_.streamId));
}

bool ClientSession::ChangeResolution(const int width, const int height) {
    if (width < 200 || height < 200 || width > 8192 || height > 8192) return false;
    if (config_.rdp) {
        std::shared_ptr<px::rdp::RdpSession> session{};
        {
            const std::scoped_lock lock{mutex_};
            session = rdpSession_;
        }
        if (!session) return false;
        session->Resize({width, height});
        return true;
    }
    std::string monitor{};
    {
        const std::scoped_lock lock{mutex_};
        monitor = monitorName_;
    }
    if (monitor.empty()) return false;
    auto message = std::make_shared<px::Message>();
    message->set_type(px::kChangeMonitorResolution);
    message->set_device_id("client_" + config_.localDeviceId + "_" + px::MD5::Hex(config_.remoteDeviceId));
    message->set_stream_id(config_.streamId);
    message->mutable_change_monitor_resolution()->set_monitor_name(monitor);
    message->mutable_change_monitor_resolution()->set_target_width(width);
    message->mutable_change_monitor_resolution()->set_target_height(height);
    return SendMedia(px::ProtoAsData(message));
}

bool ClientSession::SetFrameRate(const int frameRate) {
    if (frameRate < 15 || frameRate > 120) return false;
    auto message = std::make_shared<px::Message>();
    message->set_type(px::kModifyFps);
    message->set_device_id("client_" + config_.localDeviceId + "_" + px::MD5::Hex(config_.remoteDeviceId));
    message->set_stream_id(config_.streamId);
    message->mutable_modify_fps()->set_fps(frameRate);
    return SendMedia(px::ProtoAsData(message));
}

bool ClientSession::SetAudioEnabled(const bool enabled) {
    bool stop{};
    std::shared_ptr<px::rdp::RdpSession> rdpSession{};
    {
        const std::scoped_lock lock{mutex_};
        audioEnabled_ = enabled && config_.audio;
        stop = !audioEnabled_;
        rdpSession = rdpSession_;
    }
    if (config_.rdp) {
        if (rdpSession) rdpSession->SetAudioEnabled(audioEnabled_);
        return config_.audio && static_cast<bool>(rdpSession);
    }
    if (stop && audio_) audio_->Stop();
    return config_.audio;
}

bool ClientSession::CreateVirtualDisplay() {
    std::string requestId{};
    {
        const std::scoped_lock lock{mutex_};
        if (!virtualDisplayAvailable_ || !virtualDisplayRequestId_.empty() || virtualDisplayCount_ >= virtualDisplayMaximum_) return false;
        requestId = px::NextNativeVirtualDisplayRequestId(GetCurrentProcessId());
        virtualDisplayRequestId_ = requestId;
    }
    auto message = std::make_shared<px::Message>(px::MakeVirtualDisplayRequestMessage(
        "client_" + config_.localDeviceId + "_" + px::MD5::Hex(config_.remoteDeviceId), config_.streamId, requestId,
        px::kRemoteVirtualDisplayCreate, 1920, 1080, 60));
    if (SendMedia(px::ProtoAsData(message))) return true;
    const std::scoped_lock lock{mutex_};
    if (virtualDisplayRequestId_ == requestId) virtualDisplayRequestId_.clear();
    return false;
}

bool ClientSession::RemoveVirtualDisplay() {
    std::string requestId{};
    {
        const std::scoped_lock lock{mutex_};
        if (!virtualDisplayAvailable_ || !virtualDisplayRequestId_.empty() || virtualDisplayCount_ == 0U) return false;
        requestId = px::NextNativeVirtualDisplayRequestId(GetCurrentProcessId());
        virtualDisplayRequestId_ = requestId;
    }
    auto message = std::make_shared<px::Message>(px::MakeVirtualDisplayRequestMessage(
        "client_" + config_.localDeviceId + "_" + px::MD5::Hex(config_.remoteDeviceId), config_.streamId, requestId,
        px::kRemoteVirtualDisplayRemoveLast, 0, 0, 0));
    if (SendMedia(px::ProtoAsData(message))) return true;
    const std::scoped_lock lock{mutex_};
    if (virtualDisplayRequestId_ == requestId) virtualDisplayRequestId_.clear();
    return false;
}

std::optional<std::string> ClientSession::SaveScreenshot() const {
    std::shared_ptr<ClientVideoFrame> frame{};
    {
        const std::scoped_lock lock{mutex_};
        frame = latestFrame_;
    }
    if (!frame || frame->width <= 0 || frame->height <= 0 || frame->bgra.empty()) return std::nullopt;
    std::array<wchar_t, MAX_PATH> documents{};
    const auto length = GetEnvironmentVariableW(L"USERPROFILE", documents.data(), static_cast<DWORD>(documents.size()));
    if (length == 0 || length >= documents.size()) return std::nullopt;
    const auto directory = std::filesystem::path{documents.data()} / "Documents" / "Pixels" / "Screenshots";
    std::error_code error{};
    std::filesystem::create_directories(directory, error);
    if (error) return std::nullopt;
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto path = directory / std::format("Pixels-{}.bmp", timestamp);
    struct SurfaceDeleter final {
        void operator()(SDL_Surface* surface) const noexcept { // NOLINT(pixels-raw-pointer-boundary): SDL-owned surface ABI.
            SDL_DestroySurface(surface);
        }
    };
    const std::unique_ptr<SDL_Surface, SurfaceDeleter> surface{
        SDL_CreateSurfaceFrom(frame->width, frame->height, SDL_PIXELFORMAT_BGRA32, frame->bgra.data(), frame->width * 4)};
    if (!surface || !SDL_SaveBMP(surface.get(), path.string().c_str())) return std::nullopt;
    return path.string();
}
} // namespace px::client::imgui

