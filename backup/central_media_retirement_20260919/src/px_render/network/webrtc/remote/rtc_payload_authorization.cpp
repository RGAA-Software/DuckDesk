#include "rtc_messages.h"
#include "message_type_ids.h"

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
    message_type = wire::kHello;
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
    case wire::kApplicationTextCapabilities:
    case wire::kApplicationTextState:
    case wire::kApplicationTextSubmit:
    case wire::kApplicationTextResult:
    case wire::kApplicationTextBarrier:
    case wire::kApplicationTextBarrierResult:
        return HasPermission(permissions, "input");
    case wire::kKeyEvent:
    case wire::kMouseEvent:
    case wire::kGamepadState:
    case wire::kSwitchMonitor:
    case wire::kSwitchWorkMode:
    case wire::kChangeMonitorResolution:
    case wire::kInsertKeyFrame:
    case wire::kLockDevice:
    case wire::kStopRender:
    case wire::kReqCtrlAltDelete:
    case wire::kUpdateDesktop:
    case wire::kHardUpdateDesktop:
    case wire::kSwitchFullColorMode:
    case wire::kStartMediaRecordClientSide:
    case wire::kStopMediaRecordClientSide:
    case wire::kModifyFps:
    case wire::kVirtualDisplayRequest:
    case wire::kTextInput:
        return HasPermission(permissions, "input");
    case wire::kClipboardInfo:
    case wire::kClipboardInfoResp:
    case wire::kClipboardReqAtBegin:
    case wire::kClipboardReqBuffer:
    case wire::kClipboardReqAtEnd:
    case wire::kClipboardRespBuffer:
        return HasPermission(permissions, "clipboard");
    case wire::kFileAction:
    case wire::kFileResponse:
        return HasPermission(permissions, "file");
    case wire::kVoiceCallRequest:
    case wire::kVoiceCallResponse:
    case wire::kVoiceAudioConfig:
    case wire::kVoiceAudioFrame:
        return HasPermission(permissions, "audio");
    default:
        return HasPermission(permissions, "view");
    }
}

} // namespace px
