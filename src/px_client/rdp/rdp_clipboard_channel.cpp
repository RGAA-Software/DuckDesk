#include "rdp_clipboard_channel.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace px::rdp {
namespace {
constexpr std::size_t kMaximumClipboardBytes{16U * 1024U * 1024U};
constexpr std::string_view kHtmlFormatName{"HTML Format"};
constexpr std::string_view kPngFormatName{"PNG"};
constexpr std::string_view kFileDescriptorFormatName{"FileGroupDescriptorW"};
constexpr std::string_view kFileContentsFormatName{"FileContents"};

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

std::string_view FormatName(const CLIPRDR_FORMAT& format) { return format.formatName ? std::string_view{format.formatName} : std::string_view{}; }

}  // namespace

ClipboardChannel::ClipboardChannel(std::shared_ptr<CliprdrClientContext> channel, Publish publish)
    : channel_{std::move(channel)}, publish_{std::move(publish)} {}

bool ClipboardChannel::Ready() {
    CLIPRDR_GENERAL_CAPABILITY_SET general{};
    general.capabilitySetType = CB_CAPSTYPE_GENERAL;
    general.capabilitySetLength = CB_CAPSTYPE_GENERAL_LEN;
    general.version = CB_CAPS_VERSION_2;
    general.generalFlags = CB_USE_LONG_FORMAT_NAMES | CB_STREAM_FILECLIP_ENABLED | CB_FILECLIP_NO_FILE_PATHS;
    CLIPRDR_CAPABILITIES capabilities{};
    capabilities.common.msgType = CB_CLIP_CAPS;
    capabilities.cCapabilitiesSets = 1;
    capabilities.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET*>(&general);  // Transient FreeRDP serialization boundary.
    ready_ = channel_->ClientCapabilities(channel_.get(), &capabilities) == CHANNEL_RC_OK;
    return ready_ && Advertise();
}

bool ClipboardChannel::Capabilities(const CLIPRDR_CAPABILITIES& capabilities) {
    return capabilities.cCapabilitiesSets > 0 && capabilities.capabilitySets;
}

bool ClipboardChannel::Formats(const CLIPRDR_FORMAT_LIST& formats) {
    if (formats.numFormats > 128U || (formats.numFormats > 0U && !formats.formats)) return false;

    requests_.clear();
    pending_.reset();
    pendingFileRequest_.reset();
    remoteOutput_.reset();
    remoteFiles_.clear();
    remoteStaging_.reset();
    remoteFileIndex_ = 0;
    remoteFileOffset_ = 0;
    remote_ = {};
    for (const auto& format : std::span<const CLIPRDR_FORMAT>{formats.formats, formats.numFormats}) {
        if (format.formatId == CF_UNICODETEXT) {
            requests_.push_back({format.formatId, FormatKind::kText});
        } else if (format.formatId == CF_DIB) {
            requests_.push_back({format.formatId, FormatKind::kDib});
        } else if (format.formatId == CF_DIBV5) {
            requests_.push_back({format.formatId, FormatKind::kDibV5});
        } else if (FormatName(format) == kHtmlFormatName) {
            requests_.push_back({format.formatId, FormatKind::kHtml});
        } else if (FormatName(format) == kPngFormatName) {
            requests_.push_back({format.formatId, FormatKind::kPng});
        } else if (FormatName(format) == kFileDescriptorFormatName) {
            requests_.push_back({format.formatId, FormatKind::kFiles});
        }
    }

    CLIPRDR_FORMAT_LIST_RESPONSE response{};
    response.common.msgType = CB_FORMAT_LIST_RESPONSE;
    response.common.msgFlags = CB_RESPONSE_OK;
    if (channel_->ClientFormatListResponse(channel_.get(), &response) != CHANNEL_RC_OK) return false;
    if (requests_.empty()) {
        publish_({});
        return true;
    }
    return RequestNext();
}

bool ClipboardChannel::DataRequest(const CLIPRDR_FORMAT_DATA_REQUEST& request) {
    const auto encodedText = request.requestedFormatId == CF_UNICODETEXT ? Utf16FromUtf8(local_.text) : std::vector<wchar_t>{};
    std::span<const std::uint8_t> bytes{};
    if (!encodedText.empty()) {
        bytes = {reinterpret_cast<const std::uint8_t*>(encodedText.data()), encodedText.size() * sizeof(wchar_t)};
    } else if (request.requestedFormatId == CF_DIB) {
        bytes = local_.dib;
    } else if (request.requestedFormatId == CF_DIBV5) {
        bytes = local_.dibV5;
    } else if (request.requestedFormatId == RegisterClipboardFormatW(L"HTML Format")) {
        bytes = local_.html;
    } else if (request.requestedFormatId == RegisterClipboardFormatW(L"PNG")) {
        bytes = local_.png;
    } else if (request.requestedFormatId == RegisterClipboardFormatW(L"FileGroupDescriptorW")) {
        bytes = localFileDescriptors_;
    }

    CLIPRDR_FORMAT_DATA_RESPONSE response{};
    response.common.msgType = CB_FORMAT_DATA_RESPONSE;
    response.common.msgFlags = bytes.empty() ? CB_RESPONSE_FAIL : CB_RESPONSE_OK;
    response.common.dataLen = static_cast<UINT32>(bytes.size());
    response.requestedFormatData = bytes.data();
    return channel_->ClientFormatDataResponse(channel_.get(), &response) == CHANNEL_RC_OK;
}

bool ClipboardChannel::DataResponse(const CLIPRDR_FORMAT_DATA_RESPONSE& response) {
    if (!pending_) return false;
    const auto completed = *pending_;
    pending_.reset();

    if (response.common.msgFlags == CB_RESPONSE_OK && response.common.dataLen > 0 && response.common.dataLen <= kMaximumClipboardBytes &&
        response.requestedFormatData) {
        if (completed.kind == FormatKind::kText) {
            if (response.common.dataLen % sizeof(wchar_t) != 0) return false;
            remote_.text = Utf8FromUtf16(
                std::span<const wchar_t>{reinterpret_cast<const wchar_t*>(response.requestedFormatData), response.common.dataLen / sizeof(wchar_t)});
        } else if (completed.kind == FormatKind::kFiles) {
            remoteFiles_ = ParseRemoteClipboardFiles({response.requestedFormatData, response.common.dataLen});
            if (remoteFiles_.empty()) return false;
        } else {
            const auto begin = response.requestedFormatData;
            const auto end = response.requestedFormatData + response.common.dataLen;
            switch (completed.kind) {
                case FormatKind::kHtml:
                    remote_.html.assign(begin, end);
                    break;
                case FormatKind::kDib:
                    remote_.dib.assign(begin, end);
                    break;
                case FormatKind::kDibV5:
                    remote_.dibV5.assign(begin, end);
                    break;
                case FormatKind::kPng:
                    remote_.png.assign(begin, end);
                    break;
                case FormatKind::kText:
                case FormatKind::kFiles:
                    break;
            }
        }
    } else if (response.common.msgFlags != CB_RESPONSE_FAIL) {
        return false;
    }

    if (!requests_.empty()) return RequestNext();
    return remoteFiles_.empty() ? PublishRemote() : StartRemoteFiles();
}

bool ClipboardChannel::FileRequest(const CLIPRDR_FILE_CONTENTS_REQUEST& request) {
    std::vector<std::uint8_t> bytes{};
    bool success{request.listIndex < localFiles_.size()};
    if (success && request.dwFlags == FILECONTENTS_SIZE) {
        const auto size = localFiles_[request.listIndex].size;
        bytes.resize(sizeof(size));
        for (std::size_t index{}; index < bytes.size(); ++index) bytes[index] = static_cast<std::uint8_t>(size >> (index * 8U));
    } else if (success && request.dwFlags == FILECONTENTS_RANGE) {
        const auto offset = (static_cast<std::uint64_t>(request.nPositionHigh) << 32U) | request.nPositionLow;
        const auto requested = std::min<std::size_t>(request.cbRequested, kClipboardFileChunkBytes);
        success = ReadClipboardFileRange(localFiles_[request.listIndex], offset, requested, bytes);
    } else {
        success = false;
    }

    CLIPRDR_FILE_CONTENTS_RESPONSE response{};
    response.common.msgType = CB_FILECONTENTS_RESPONSE;
    response.common.msgFlags = success ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
    response.streamId = request.streamId;
    response.cbRequested = static_cast<UINT32>(bytes.size());
    response.requestedData = bytes.data();
    return channel_->ClientFileContentsResponse(channel_.get(), &response) == CHANNEL_RC_OK;
}

bool ClipboardChannel::FileResponse(const CLIPRDR_FILE_CONTENTS_RESPONSE& response) {
    if (!pendingFileRequest_ || response.streamId != pendingStreamId_) return false;
    const auto requestKind = *pendingFileRequest_;
    pendingFileRequest_.reset();
    if (response.common.msgFlags != CB_RESPONSE_OK || response.cbRequested > pendingFileBytes_ ||
        (response.cbRequested > 0 && !response.requestedData)) {
        return false;
    }
    if (requestKind == RemoteFileRequestKind::kSize) {
        if (response.cbRequested != sizeof(std::uint64_t)) return false;
        remoteFileSize_ = 0;
        for (std::size_t index{}; index < sizeof(std::uint64_t); ++index) {
            remoteFileSize_ |= static_cast<std::uint64_t>(response.requestedData[index]) << (index * 8U);
        }
        if (remoteFileSize_ != remoteFiles_[remoteFileIndex_].advertisedSize) return false;
        const auto outputPath = remoteStaging_->Root() / remoteFiles_[remoteFileIndex_].relative;
        std::error_code error{};
        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error) return false;
        remoteOutput_ = std::make_unique<std::ofstream>(outputPath, std::ios::binary | std::ios::trunc);
        if (!*remoteOutput_) return false;
        remoteFileOffset_ = 0;
        if (remoteFileSize_ == 0) return AdvanceRemoteFile();
        return RequestRemoteFile(RemoteFileRequestKind::kRange);
    }
    if (!remoteOutput_ || response.cbRequested == 0 || response.cbRequested != pendingFileBytes_) return false;
    remoteOutput_->write(reinterpret_cast<const char*>(response.requestedData),  // NOLINT(pixels-raw-pointer-boundary): sync stream view.
                         static_cast<std::streamsize>(response.cbRequested));
    if (!*remoteOutput_) return false;
    remoteFileOffset_ += response.cbRequested;
    if (remoteFileOffset_ < remoteFileSize_) return RequestRemoteFile(RemoteFileRequestKind::kRange);
    return remoteFileOffset_ == remoteFileSize_ && AdvanceRemoteFile();
}

bool ClipboardChannel::SetLocal(ClipboardContent content) {
    if (content.text.size() > kMaximumClipboardBytes || content.text.find('\0') != std::string::npos ||
        content.html.size() > kMaximumClipboardBytes || content.dib.size() > kMaximumClipboardBytes ||
        content.dibV5.size() > kMaximumClipboardBytes || content.png.size() > kMaximumClipboardBytes) {
        return false;
    }
    localFiles_ = InventoryLocalClipboardFiles(content.files);
    localFileDescriptors_ = SerializeLocalClipboardFiles(localFiles_);
    if (!content.files.empty() && localFileDescriptors_.empty()) content.files.clear();
    local_ = std::move(content);
    return !ready_ || Advertise();
}

bool ClipboardChannel::Tick() const { return (!pending_ && !pendingFileRequest_) || std::chrono::steady_clock::now() < deadline_; }

bool ClipboardChannel::Advertise() {
    std::array<char, 12> htmlName{"HTML Format"};
    std::array<char, 4> pngName{"PNG"};
    std::array<char, 21> fileDescriptorName{"FileGroupDescriptorW"};
    std::array<char, 13> fileContentsName{"FileContents"};
    std::array<CLIPRDR_FORMAT, 7> formats{};
    std::size_t count{};
    if (!local_.text.empty()) formats[count++] = {CF_UNICODETEXT, nullptr};
    if (!local_.html.empty()) formats[count++] = {RegisterClipboardFormatW(L"HTML Format"), htmlName.data()};
    if (!local_.dib.empty()) formats[count++] = {CF_DIB, nullptr};
    if (!local_.dibV5.empty()) formats[count++] = {CF_DIBV5, nullptr};
    if (!local_.png.empty()) formats[count++] = {RegisterClipboardFormatW(L"PNG"), pngName.data()};
    if (!localFileDescriptors_.empty()) {
        formats[count++] = {RegisterClipboardFormatW(L"FileGroupDescriptorW"), fileDescriptorName.data()};
        formats[count++] = {RegisterClipboardFormatW(L"FileContents"), fileContentsName.data()};
    }
    CLIPRDR_FORMAT_LIST list{};
    list.common.msgType = CB_FORMAT_LIST;
    list.numFormats = static_cast<UINT32>(count);
    list.formats = formats.data();
    return channel_->ClientFormatList(channel_.get(), &list) == CHANNEL_RC_OK;
}

bool ClipboardChannel::StartRemoteFiles() {
    remoteStaging_ = ClipboardStagingDirectory::Create();
    if (!remoteStaging_) return false;
    for (const auto& file : remoteFiles_) {
        if (!file.directory) continue;
        std::error_code error{};
        std::filesystem::create_directories(remoteStaging_->Root() / file.relative, error);
        if (error) return false;
    }
    remoteFileIndex_ = 0;
    return AdvanceRemoteFile();
}

bool ClipboardChannel::AdvanceRemoteFile() {
    if (remoteOutput_) {
        remoteOutput_->close();
        if (!*remoteOutput_) return false;
        remoteOutput_.reset();
        ++remoteFileIndex_;
    }
    while (remoteFileIndex_ < remoteFiles_.size() && remoteFiles_[remoteFileIndex_].directory) ++remoteFileIndex_;
    if (remoteFileIndex_ == remoteFiles_.size()) {
        remote_.files = ClipboardTopLevelPaths(remoteStaging_->Root(), remoteFiles_);
        remote_.staging = std::move(remoteStaging_);
        remoteFiles_.clear();
        return PublishRemote();
    }
    remoteFileOffset_ = 0;
    remoteFileSize_ = 0;
    return RequestRemoteFile(RemoteFileRequestKind::kSize);
}

bool ClipboardChannel::RequestRemoteFile(const RemoteFileRequestKind kind) {
    if (pendingFileRequest_ || remoteFileIndex_ >= remoteFiles_.size()) return false;
    CLIPRDR_FILE_CONTENTS_REQUEST request{};
    request.common.msgType = CB_FILECONTENTS_REQUEST;
    request.streamId = nextStreamId_++;
    request.listIndex = static_cast<UINT32>(remoteFileIndex_);
    request.dwFlags = kind == RemoteFileRequestKind::kSize ? FILECONTENTS_SIZE : FILECONTENTS_RANGE;
    request.nPositionLow = static_cast<UINT32>(remoteFileOffset_);
    request.nPositionHigh = static_cast<UINT32>(remoteFileOffset_ >> 32U);
    request.cbRequested = kind == RemoteFileRequestKind::kSize
                              ? static_cast<UINT32>(sizeof(std::uint64_t))
                              : static_cast<UINT32>(std::min<std::uint64_t>(kClipboardFileChunkBytes, remoteFileSize_ - remoteFileOffset_));
    pendingFileRequest_ = kind;
    pendingStreamId_ = request.streamId;
    pendingFileBytes_ = request.cbRequested;
    if (channel_->ClientFileContentsRequest(channel_.get(), &request) != CHANNEL_RC_OK) {
        pendingFileRequest_.reset();
        return false;
    }
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    return true;
}

bool ClipboardChannel::PublishRemote() {
    publish_(std::move(remote_));
    remote_ = {};
    return true;
}

bool ClipboardChannel::RequestNext() {
    if (requests_.empty() || pending_) return false;
    pending_ = requests_.front();
    requests_.pop_front();
    CLIPRDR_FORMAT_DATA_REQUEST request{};
    request.common.msgType = CB_FORMAT_DATA_REQUEST;
    request.requestedFormatId = pending_->id;
    if (channel_->ClientFormatDataRequest(channel_.get(), &request) != CHANNEL_RC_OK) {
        pending_.reset();
        return false;
    }
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    return true;
}

}  // namespace px::rdp
