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
namespace {

std::int32_t MouseButtonFlag(const std::uint8_t button, const bool down) {
    switch (button) {
    case 1:
        return down ? px::ButtonFlag::kLeftMouseButtonDown : px::ButtonFlag::kLeftMouseButtonUp;
    case 2:
        return down ? px::ButtonFlag::kMiddleMouseButtonDown : px::ButtonFlag::kMiddleMouseButtonUp;
    case 3:
        return down ? px::ButtonFlag::kRightMouseButtonDown : px::ButtonFlag::kRightMouseButtonUp;
    default:
        return px::ButtonFlag::kNone;
    }
}

} // namespace

bool ClientSession::SendMouseMove(const float xRatio, const float yRatio) {
    if (config_.rdp) {
        std::shared_ptr<px::rdp::RdpSession> session{};
        int width{};
        int height{};
        {
            const std::scoped_lock lock{mutex_};
            session = rdpSession_;
            width = rdpWidth_;
            height = rdpHeight_;
        }
        if (!session || width <= 0 || height <= 0) return false;
        session->Mouse(PTR_FLAGS_MOVE, static_cast<int>(std::clamp(xRatio, 0.0F, 1.0F) * (width - 1)),
                       static_cast<int>(std::clamp(yRatio, 0.0F, 1.0F) * (height - 1)), false);
        return true;
    }
    std::string monitor{};
    {
        const std::scoped_lock lock{mutex_};
        cursorX_ = std::clamp(xRatio, 0.0F, 1.0F);
        cursorY_ = std::clamp(yRatio, 0.0F, 1.0F);
        monitor = monitorName_;
    }
    return !config_.viewOnly && !monitor.empty() &&
           SendMedia(px::ProtoMessageMaker::MakeMouseEvent(px::ButtonFlag::kMouseMove, monitor, cursorX_, cursorY_, 0, false, false,
                                                            "client_" + config_.localDeviceId + "_" + px::MD5::Hex(config_.remoteDeviceId),
                                                            config_.streamId));
}

bool ClientSession::SendMouseButton(const std::uint8_t button, const bool down, const float xRatio, const float yRatio) {
    if (config_.rdp) {
        std::shared_ptr<px::rdp::RdpSession> session{};
        int width{};
        int height{};
        {
            const std::scoped_lock lock{mutex_};
            session = rdpSession_;
            width = rdpWidth_;
            height = rdpHeight_;
        }
        const std::uint16_t flag = button == 1 ? PTR_FLAGS_BUTTON1 : button == 2 ? PTR_FLAGS_BUTTON3 : button == 3 ? PTR_FLAGS_BUTTON2 : 0;
        if (!session || flag == 0 || width <= 0 || height <= 0) return false;
        session->Mouse(static_cast<std::uint16_t>(flag | (down ? PTR_FLAGS_DOWN : 0)),
                       static_cast<int>(std::clamp(xRatio, 0.0F, 1.0F) * (width - 1)),
                       static_cast<int>(std::clamp(yRatio, 0.0F, 1.0F) * (height - 1)), false);
        return true;
    }
    const auto flag = MouseButtonFlag(button, down);
    if (flag == px::ButtonFlag::kNone || !SendMouseMove(xRatio, yRatio)) {
        return false;
    }
    std::string monitor{};
    {
        const std::scoped_lock lock{mutex_};
        monitor = monitorName_;
    }
    return SendMedia(px::ProtoMessageMaker::MakeMouseEvent(flag, monitor, cursorX_, cursorY_, 0, down, !down,
                                                            "client_" + config_.localDeviceId + "_" + px::MD5::Hex(config_.remoteDeviceId),
                                                            config_.streamId));
}

bool ClientSession::SendMouseWheel(const float horizontal, const float vertical) {
    if (config_.rdp) {
        std::shared_ptr<px::rdp::RdpSession> session{};
        {
            const std::scoped_lock lock{mutex_};
            session = rdpSession_;
        }
        if (!session) return false;
        const auto encode = [](const float delta) {
            const auto magnitude = static_cast<std::uint16_t>(std::min(255.0F, std::abs(delta) * 120.0F));
            return static_cast<std::uint16_t>(magnitude | (delta < 0.0F ? PTR_FLAGS_WHEEL_NEGATIVE : 0));
        };
        if (vertical != 0.0F) session->Mouse(static_cast<std::uint16_t>(PTR_FLAGS_WHEEL | encode(vertical)), 0, 0, false);
        if (horizontal != 0.0F) session->Mouse(static_cast<std::uint16_t>(PTR_FLAGS_HWHEEL | encode(horizontal)), 0, 0, false);
        return vertical != 0.0F || horizontal != 0.0F;
    }
    std::string monitor{};
    {
        const std::scoped_lock lock{mutex_};
        monitor = monitorName_;
    }
    const std::string device{"client_" + config_.localDeviceId + "_" + px::MD5::Hex(config_.remoteDeviceId)};
    bool sent{};
    if (vertical != 0.0F) {
        sent = SendMedia(px::ProtoMessageMaker::MakeMouseEvent(px::ButtonFlag::kMouseEventWheel, monitor, cursorX_, cursorY_,
                                                               static_cast<int>(vertical * 120.0F), false, false, device, config_.streamId));
    }
    if (horizontal != 0.0F) {
        sent = SendMedia(px::ProtoMessageMaker::MakeMouseEvent(px::ButtonFlag::kMouseEventHWheel, monitor, cursorX_, cursorY_,
                                                               static_cast<int>(horizontal * 120.0F), false, false, device,
                                                               config_.streamId)) || sent;
    }
    return sent;
}

bool ClientSession::SendKey(const std::uint32_t virtualKey, const std::uint32_t scanCode, const bool down) {
    if (config_.rdp) {
        std::shared_ptr<px::rdp::RdpSession> session{};
        {
            const std::scoped_lock lock{mutex_};
            session = rdpSession_;
        }
        const auto resolvedScanCode = scanCode != 0U ? scanCode : MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX);
        if (!session || resolvedScanCode == 0U) return false;
        session->Key(resolvedScanCode, down);
        return true;
    }
    return !config_.viewOnly && virtualKey > 0 && virtualKey <= 0xFF &&
           SendMedia(px::ProtoMessageMaker::MakeKeyEvent(virtualKey, down,
                                                          "client_" + config_.localDeviceId + "_" + px::MD5::Hex(config_.remoteDeviceId),
                                                          config_.streamId, scanCode));
}

bool ClientSession::SendText(const std::string& text) {
    if (config_.rdp) {
        std::shared_ptr<px::rdp::RdpSession> session{};
        {
            const std::scoped_lock lock{mutex_};
            session = rdpSession_;
        }
        if (!session || text.empty()) return false;
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0) return false;
        std::vector<wchar_t> wide(static_cast<std::size_t>(length));
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), length) != length) {
            return false;
        }
        for (const wchar_t character : wide) {
            session->Unicode(static_cast<std::uint16_t>(character), true);
            session->Unicode(static_cast<std::uint16_t>(character), false);
        }
        return true;
    }
    return !config_.viewOnly && !text.empty() &&
           SendMedia(px::ProtoMessageMaker::MakeTextInput(text,
                                                           "client_" + config_.localDeviceId + "_" + px::MD5::Hex(config_.remoteDeviceId),
                                                           config_.streamId));
}

bool ClientSession::SendClipboardText(const std::string& text) {
    if (!config_.clipboard || text.empty() || text.size() > 1'048'576U) {
        return false;
    }
    if (config_.rdp) {
        std::shared_ptr<px::rdp::RdpSession> session{};
        {
            const std::scoped_lock lock{mutex_};
            session = rdpSession_;
        }
        if (!session) return false;
        session->PublishClipboard(text);
        return true;
    }
    auto message = std::make_shared<px::Message>();
    message->set_type(px::kClipboardInfo);
    message->set_device_id(config_.remoteDeviceId);
    message->set_stream_id(config_.streamId);
    message->mutable_clipboard_info()->set_type(px::kClipboardText);
    message->mutable_clipboard_info()->set_msg(text);
    return SendMedia(px::ProtoAsData(message));
}

std::optional<std::string> ClientSession::TakeRemoteClipboardText() {
    const std::scoped_lock lock{mutex_};
    auto result = std::move(remoteClipboardText_);
    remoteClipboardText_.reset();
    return result;
}

bool ClientSession::SendSecureAttention() {
    if (config_.rdp) {
        std::shared_ptr<px::rdp::RdpSession> session{};
        {
            const std::scoped_lock lock{mutex_};
            session = rdpSession_;
        }
        if (!session) return false;
        const auto control = MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC_EX);
        const auto alt = MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC_EX);
        const auto del = MapVirtualKeyW(VK_DELETE, MAPVK_VK_TO_VSC_EX);
        session->Key(control, true);
        session->Key(alt, true);
        session->Key(del, true);
        session->Key(del, false);
        session->Key(alt, false);
        session->Key(control, false);
        return true;
    }
    return !config_.viewOnly && SendMedia(px::ProtoMessageMaker::MakeCtrlAltDelete(
                                    "client_" + config_.localDeviceId + "_" + px::MD5::Hex(config_.remoteDeviceId), config_.streamId));
}
} // namespace px::client::imgui
