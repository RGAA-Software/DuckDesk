#include "rdp_text_clipboard_channel.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <span>
#include <vector>

namespace px::rdp {
namespace {
constexpr std::size_t kMaximumClipboardBytes{16U * 1024U * 1024U};

std::vector<wchar_t> Utf16FromUtf8(const std::string& text) {
    if (text.empty() || text.size() > kMaximumClipboardBytes) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0 || static_cast<std::size_t>(count) >= kMaximumClipboardBytes / sizeof(wchar_t)) return {};
    std::vector<wchar_t> result(static_cast<std::size_t>(count) + 1U);
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count) != count) return {};
    return result;
}

std::string Utf8FromUtf16(const std::span<const wchar_t> text) {
    const auto terminator = std::ranges::find(text, L'\0');
    const auto count = static_cast<int>(std::distance(text.begin(), terminator));
    if (count <= 0) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), count, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0 || static_cast<std::size_t>(bytes) > kMaximumClipboardBytes) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), count, result.data(), bytes, nullptr, nullptr) == bytes ? result
                                                                                                                               : std::string{};
}
} // namespace

TextClipboardChannel::TextClipboardChannel(std::shared_ptr<CliprdrClientContext> channel, Publish publish)
    : channel_{std::move(channel)}, publish_{std::move(publish)} {}

bool TextClipboardChannel::Ready() {
    CLIPRDR_GENERAL_CAPABILITY_SET general{};
    general.capabilitySetType = CB_CAPSTYPE_GENERAL;
    general.capabilitySetLength = CB_CAPSTYPE_GENERAL_LEN;
    general.version = CB_CAPS_VERSION_2;
    CLIPRDR_CAPABILITIES capabilities{};
    capabilities.common.msgType = CB_CLIP_CAPS;
    capabilities.cCapabilitiesSets = 1;
    capabilities.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET*>(&general); // Transient FreeRDP serialization boundary.
    ready_ = channel_->ClientCapabilities(channel_.get(), &capabilities) == CHANNEL_RC_OK;
    return ready_ && Advertise();
}

bool TextClipboardChannel::Capabilities(const CLIPRDR_CAPABILITIES& capabilities) {
    return capabilities.cCapabilitiesSets > 0 && capabilities.capabilitySets;
}

bool TextClipboardChannel::Formats(const CLIPRDR_FORMAT_LIST& formats) {
    if (formats.numFormats > 128U || (formats.numFormats > 0U && !formats.formats)) return false;
    bool hasText{};
    for (const auto& format : std::span<const CLIPRDR_FORMAT>{formats.formats, formats.numFormats}) {
        hasText = hasText || format.formatId == CF_UNICODETEXT;
    }
    CLIPRDR_FORMAT_LIST_RESPONSE response{};
    response.common.msgType = CB_FORMAT_LIST_RESPONSE;
    response.common.msgFlags = CB_RESPONSE_OK;
    if (channel_->ClientFormatListResponse(channel_.get(), &response) != CHANNEL_RC_OK) return false;
    if (!hasText) return true;
    CLIPRDR_FORMAT_DATA_REQUEST request{};
    request.common.msgType = CB_FORMAT_DATA_REQUEST;
    request.requestedFormatId = CF_UNICODETEXT;
    pending_ = channel_->ClientFormatDataRequest(channel_.get(), &request) == CHANNEL_RC_OK;
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    return pending_;
}

bool TextClipboardChannel::DataRequest(const CLIPRDR_FORMAT_DATA_REQUEST& request) {
    const auto encoded = request.requestedFormatId == CF_UNICODETEXT ? Utf16FromUtf8(local_) : std::vector<wchar_t>{};
    CLIPRDR_FORMAT_DATA_RESPONSE response{};
    response.common.msgType = CB_FORMAT_DATA_RESPONSE;
    response.common.msgFlags = encoded.empty() ? CB_RESPONSE_FAIL : CB_RESPONSE_OK;
    response.common.dataLen = static_cast<UINT32>(encoded.size() * sizeof(wchar_t));
    response.requestedFormatData = reinterpret_cast<const BYTE*>(encoded.data()); // Transient FreeRDP serialization boundary.
    return channel_->ClientFormatDataResponse(channel_.get(), &response) == CHANNEL_RC_OK;
}

bool TextClipboardChannel::DataResponse(const CLIPRDR_FORMAT_DATA_RESPONSE& response) {
    if (!pending_) return false;
    pending_ = false;
    if (response.common.msgFlags != CB_RESPONSE_OK || response.common.dataLen < sizeof(wchar_t) ||
        response.common.dataLen > kMaximumClipboardBytes || response.common.dataLen % sizeof(wchar_t) != 0 || !response.requestedFormatData) {
        return response.common.msgFlags != CB_RESPONSE_OK;
    }
    const auto text = Utf8FromUtf16(std::span<const wchar_t>{reinterpret_cast<const wchar_t*>(response.requestedFormatData),
                                                            response.common.dataLen / sizeof(wchar_t)});
    if (!text.empty()) publish_(text);
    return true;
}

bool TextClipboardChannel::SetLocal(std::string text) {
    if (text.size() > kMaximumClipboardBytes || text.find('\0') != std::string::npos) return false;
    local_ = std::move(text);
    return !ready_ || Advertise();
}

bool TextClipboardChannel::Tick() const {
    return !pending_ || std::chrono::steady_clock::now() < deadline_;
}

bool TextClipboardChannel::Advertise() {
    std::array<CLIPRDR_FORMAT, 1> formats{{{CF_UNICODETEXT, nullptr}}};
    CLIPRDR_FORMAT_LIST list{};
    list.common.msgType = CB_FORMAT_LIST;
    list.numFormats = local_.empty() ? 0U : 1U;
    list.formats = formats.data();
    return channel_->ClientFormatList(channel_.get(), &list) == CHANNEL_RC_OK;
}

} // namespace px::rdp
