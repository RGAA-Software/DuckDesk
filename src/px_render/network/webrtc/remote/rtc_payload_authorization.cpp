#include "rtc_messages.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace px {
namespace {

bool ReadVarint(const std::string& payload, std::size_t& offset, std::uint64_t& value) {
    value = 0;
    for (unsigned shift = 0; shift < 64 && offset < payload.size(); shift += 7) {
        const auto byte = static_cast<std::uint8_t>(payload[offset++]);
        value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            return true;
        }
    }
    return false;
}

bool SkipProtoField(const std::string& payload, std::size_t& offset, const std::uint32_t wire_type) {
    std::uint64_t length = 0;
    switch (wire_type) {
    case 0:
        return ReadVarint(payload, offset, length);
    case 1:
        if (payload.size() - offset < 8) {
            return false;
        }
        offset += 8;
        return true;
    case 2:
        if (!ReadVarint(payload, offset, length) || length > payload.size() - offset) {
            return false;
        }
        offset += static_cast<std::size_t>(length);
        return true;
    case 5:
        if (payload.size() - offset < 4) {
            return false;
        }
        offset += 4;
        return true;
    default:
        return false;
    }
}

bool ReadMessageType(const std::string& payload, std::uint64_t& message_type) {
    std::size_t offset = 0;
    bool has_hello_payload = false;
    while (offset < payload.size()) {
        std::uint64_t tag = 0;
        if (!ReadVarint(payload, offset, tag) || tag == 0) {
            return false;
        }
        const auto field = static_cast<std::uint32_t>(tag >> 3);
        const auto wire_type = static_cast<std::uint32_t>(tag & 7);
        if (field == 10) {
            return wire_type == 0 && ReadVarint(payload, offset, message_type);
        }
        // kHello is enum value zero, so protobuf omits field 10. A valid
        // hello envelope is instead identified by its length-delimited field 40.
        if (field == 40 && wire_type == 2) {
            has_hello_payload = true;
        }
        if (!SkipProtoField(payload, offset, wire_type)) {
            return false;
        }
    }
    if (!has_hello_payload) {
        return false;
    }
    message_type = 0;
    return true;
}

bool HasPermission(const std::vector<std::string>& permissions, const std::string_view permission) {
    return std::ranges::any_of(permissions, [permission](const std::string& candidate) { return candidate == permission; });
}

} // namespace

bool IsRtcPayloadAuthorized(const std::string& payload, const std::vector<std::string>& permissions) {
    std::uint64_t type = 0;
    if (!ReadMessageType(payload, type)) {
        return false;
    }
    switch (type) {
    case 50:  // kKeyEvent
    case 60:  // kMouseEvent
    case 80:  // kGamepadState
    case 170: // kSwitchMonitor
    case 190: // kSwitchWorkMode
    case 200: // kChangeMonitorResolution
    case 230: // kInsertKeyFrame
    case 328: // kLockDevice
    case 329: // kStopRender
    case 330: // kReqCtrlAltDelete
    case 340: // kUpdateDesktop
    case 341: // kHardUpdateDesktop
    case 460: // kSwitchFullColorMode
    case 470: // kStartMediaRecordClientSide
    case 471: // kStopMediaRecordClientSide
    case 480: // kModifyFps
    case 570: // kVirtualDisplayRequest
    case 580: // kTextInput
        return HasPermission(permissions, "input");
    case 160: // kClipboardInfo
    case 161: // kClipboardInfoResp
    case 349: // kClipboardReqAtBegin
    case 350: // kClipboardReqBuffer
    case 351: // kClipboardReqAtEnd
    case 360: // kClipboardRespBuffer
        return HasPermission(permissions, "clipboard");
    case 270: // kFileAction
    case 280: // kFileResponse
        return HasPermission(permissions, "file");
    case 590: // kVoiceCallRequest
    case 591: // kVoiceCallResponse
    case 592: // kVoiceAudioConfig
    case 593: // kVoiceAudioFrame
        return HasPermission(permissions, "audio");
    default:
        return HasPermission(permissions, "view");
    }
}

} // namespace px
