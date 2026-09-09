#include "rdp_clipboard_channel.h"
#include "px_common/log.h"

#include <QtEndian>
#include <algorithm>
#include <array>
#include <cstring>

namespace px::rdp {
namespace {
constexpr std::chrono::seconds kResponseTimeout{10};
constexpr std::chrono::seconds kFileTransferTimeout{120};
} // namespace
ClipboardChannel::ClipboardChannel(std::shared_ptr<CliprdrClientContext> channel, Publish publish)
    : channel_(std::move(channel)), publish_(std::move(publish)), html_id_(RegisterClipboardFormatW(L"HTML Format")),
      files_id_(RegisterClipboardFormatW(L"FileGroupDescriptorW")), contents_id_(RegisterClipboardFormatW(L"FileContents")) {}

void ClipboardChannel::CancelReceive() {
    channel_failed_ = !ReleaseRemoteLock() || channel_failed_;
    ++generation_;
    requests_.clear();
    remote_.reset();
    pending_stream_ = 0; // File responses carry IDs; a late cancelled response can be discarded exactly.
    file_index_ = 0;
    file_offset_ = 0;
    size_verified_ = false;
    // Format responses have no request ID. Preserve pending_ and drain its one
    // stale reply before issuing a new format request, or fail on its deadline.
}
bool ClipboardChannel::SetLocal(std::shared_ptr<const ClipboardData> data) {
    LOGI("event=rdp.clipboard.local pending_file={}", pending_stream_ != 0);
    CancelReceive();
    local_ = std::move(data);
    offered_files_.reset();
    if (local_ && !local_->urls.isEmpty()) {
        offered_files_ = ClipboardFiles::Offer(local_->urls);
    }
    return !ready_ || Advertise();
}
bool ClipboardChannel::Ready() {
    if (!html_id_ || !files_id_ || !contents_id_) {
        return false;
    }
    CLIPRDR_GENERAL_CAPABILITY_SET general{};
    general.capabilitySetType = CB_CAPSTYPE_GENERAL;
    general.capabilitySetLength = CB_CAPSTYPE_GENERAL_LEN;
    general.version = CB_CAPS_VERSION_2;
    general.generalFlags = CB_USE_LONG_FORMAT_NAMES | CB_STREAM_FILECLIP_ENABLED | CB_FILECLIP_NO_FILE_PATHS | CB_CAN_LOCK_CLIPDATA;
    CLIPRDR_CAPABILITIES capabilities{};
    capabilities.common.msgType = CB_CLIP_CAPS;
    capabilities.cCapabilitiesSets = 1;
    capabilities.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET*>(&general); // Transient synchronous FreeRDP ABI.
    if (channel_->ClientCapabilities(channel_.get(), &capabilities) != CHANNEL_RC_OK) {
        return false;
    }
    ready_ = true;
    return Advertise();
}
bool ClipboardChannel::Capabilities(const CLIPRDR_CAPABILITIES& capabilities) {
    if (capabilities.cCapabilitiesSets != 1 || !capabilities.capabilitySets) {
        return false;
    }
    const auto& general = reinterpret_cast<const CLIPRDR_GENERAL_CAPABILITY_SET&>(*capabilities.capabilitySets);
    if (general.capabilitySetType != CB_CAPSTYPE_GENERAL || general.capabilitySetLength < CB_CAPSTYPE_GENERAL_LEN) {
        return false;
    }
    file_capability_ = (general.generalFlags & CB_STREAM_FILECLIP_ENABLED) != 0;
    lock_capability_ = (general.generalFlags & CB_CAN_LOCK_CLIPDATA) != 0;
    LOGI("event=rdp.clipboard.capabilities files={} flags={}", file_capability_, general.generalFlags);
    return true;
}
bool ClipboardChannel::Advertise() {
    // Names and ABI records are local values, valid only through the synchronous
    // serialization call; FreeRDP copies them into its outgoing packet.
    std::array<char, 12> html_name{"HTML Format"};
    std::array<char, 21> files_name{"FileGroupDescriptorW"};
    std::array<char, 13> contents_name{"FileContents"};
    std::array<CLIPRDR_FORMAT, 5> formats{};
    std::size_t count{};
    if (local_) {
        if (local_->text) {
            formats[count++] = {CF_UNICODETEXT, nullptr};
        }
        if (local_->html) {
            formats[count++] = {html_id_, html_name.data()};
        }
        if (!local_->image.isNull()) {
            formats[count++] = {CF_DIB, nullptr};
        }
        if (offered_files_ && file_capability_) {
            formats[count++] = {files_id_, files_name.data()};
            formats[count++] = {contents_id_, contents_name.data()};
        }
    }
    CLIPRDR_FORMAT_LIST list{};
    list.common.msgType = CB_FORMAT_LIST;
    list.numFormats = static_cast<UINT32>(count);
    list.formats = formats.data();
    return channel_->ClientFormatList(channel_.get(), &list) == CHANNEL_RC_OK;
}
bool ClipboardChannel::Formats(const CLIPRDR_FORMAT_LIST& list) {
    if (list.numFormats > 128 || (list.numFormats != 0 && !list.formats)) {
        return false;
    }
    CancelReceive();
    local_.reset();
    offered_files_.reset();
    remote_ = std::make_shared<ClipboardData>();
    std::array<bool, 4> seen{};
    for (const auto& format : std::span<const CLIPRDR_FORMAT>{list.formats, list.numFormats}) {
        std::optional<Kind> kind{};
        if (format.formatId == CF_UNICODETEXT) {
            kind = Kind::kText;
        } else if (format.formatId == CF_DIB) {
            kind = Kind::kDib;
        } else if (format.formatName) {
            // FreeRDP owns a terminated name; reject unexpectedly large names
            // without scanning arbitrary data beyond the supported wire bound.
            const auto length = strnlen_s(format.formatName, 256);
            if (length == 256) {
                return false;
            }
            const std::string_view name{format.formatName, length};
            if (name == "HTML Format") {
                kind = Kind::kHtml;
            } else if (name == "FileGroupDescriptorW" && file_capability_) {
                kind = Kind::kFiles;
            }
        }
        if (kind && !seen.at(static_cast<std::size_t>(*kind))) {
            seen.at(static_cast<std::size_t>(*kind)) = true;
            requests_.push_back({*kind, format.formatId, generation_});
        }
    }
    CLIPRDR_FORMAT_LIST_RESPONSE response{};
    LOGI("event=rdp.clipboard.formats text={} html={} image={} files={}", seen[0], seen[1], seen[2], seen[3]);
    response.common.msgType = CB_FORMAT_LIST_RESPONSE;
    response.common.msgFlags = CB_RESPONSE_OK;
    return channel_->ClientFormatListResponse(channel_.get(), &response) == CHANNEL_RC_OK && Next();
}
bool ClipboardChannel::Next() {
    if (channel_failed_) {
        return false;
    }
    if (pending_ || pending_stream_ != 0 || !remote_) {
        return true;
    }
    if (requests_.empty()) {
        if (!ReleaseRemoteLock()) {
            return false;
        }
        publish_(std::exchange(remote_, {}));
        return true;
    }
    pending_ = requests_.front();
    requests_.pop_front();
    deadline_ = std::chrono::steady_clock::now() + kResponseTimeout;
    CLIPRDR_FORMAT_DATA_REQUEST request{};
    request.common.msgType = CB_FORMAT_DATA_REQUEST;
    request.requestedFormatId = pending_->id;
    if (pending_->kind == Kind::kFiles && lock_capability_) {
        if (next_lock_ == UINT32_MAX || !ReleaseRemoteLock()) {
            return false;
        }
        remote_lock_ = ++next_lock_;
        CLIPRDR_LOCK_CLIPBOARD_DATA lock{};
        lock.common.msgType = CB_LOCK_CLIPDATA;
        lock.clipDataId = remote_lock_;
        if (channel_->ClientLockClipboardData(channel_.get(), &lock) != CHANNEL_RC_OK) {
            return false;
        }
    }
    return channel_->ClientFormatDataRequest(channel_.get(), &request) == CHANNEL_RC_OK;
}
bool ClipboardChannel::ReleaseRemoteLock() {
    if (remote_lock_ == 0) {
        return true;
    }
    CLIPRDR_UNLOCK_CLIPBOARD_DATA unlock{};
    unlock.common.msgType = CB_UNLOCK_CLIPDATA;
    unlock.clipDataId = std::exchange(remote_lock_, 0);
    return channel_->ClientUnlockClipboardData(channel_.get(), &unlock) == CHANNEL_RC_OK;
}
bool ClipboardChannel::DataRequest(const CLIPRDR_FORMAT_DATA_REQUEST& request) {
    QByteArray bytes{};
    if (local_) {
        if (request.requestedFormatId == CF_UNICODETEXT && local_->text) {
            bytes = EncodeClipboardText(*local_->text);
        } else if (request.requestedFormatId == html_id_ && local_->html) {
            bytes = EncodeClipboardHtml(*local_->html);
        } else if (request.requestedFormatId == CF_DIB) {
            bytes = EncodeClipboardDib(local_->image);
        } else if (request.requestedFormatId == files_id_ && offered_files_) {
            bytes = offered_files_->Descriptors();
        }
    }
    CLIPRDR_FORMAT_DATA_RESPONSE response{};
    response.common.msgType = CB_FORMAT_DATA_RESPONSE;
    response.common.msgFlags = bytes.isEmpty() ? CB_RESPONSE_FAIL : CB_RESPONSE_OK;
    response.common.dataLen = static_cast<UINT32>(bytes.size());
    response.requestedFormatData = reinterpret_cast<const BYTE*>(bytes.constData()); // Transient serialization ABI.
    return channel_->ClientFormatDataResponse(channel_.get(), &response) == CHANNEL_RC_OK;
}
bool ClipboardChannel::DataResponse(const CLIPRDR_FORMAT_DATA_RESPONSE& response) {
    if (!pending_) {
        return false;
    }
    const auto request = *std::exchange(pending_, {});
    LOGI("event=rdp.clipboard.data kind={} flags={} bytes={} stale={}", static_cast<int>(request.kind), response.common.msgFlags,
         response.common.dataLen, request.generation != generation_);
    if (request.generation != generation_ || !remote_) {
        return Next();
    }
    if (response.common.msgFlags != CB_RESPONSE_OK) {
        return Next();
    }
    if (response.common.dataLen > kClipboardImageLimit || (response.common.dataLen && !response.requestedFormatData)) {
        return false;
    }
    const auto bytes = std::span<const unsigned char>{response.requestedFormatData, response.common.dataLen};
    switch (request.kind) {
    case Kind::kText:
        remote_->text = DecodeClipboardText(bytes);
        break;
    case Kind::kHtml:
        remote_->html = DecodeClipboardHtml(bytes);
        LOGI("event=rdp.clipboard.html accepted={}", remote_->html.has_value());
        break;
    case Kind::kDib:
        remote_->image = DecodeClipboardDib(bytes);
        LOGI("event=rdp.clipboard.image accepted={}", !remote_->image.isNull());
        if (remote_->image.isNull() && bytes.size() >= 40) {
            LOGI("event=rdp.clipboard.dib header={} bits={} compression={}", qFromLittleEndian<quint32>(bytes.data()),
                 qFromLittleEndian<quint16>(bytes.data() + 14), qFromLittleEndian<quint32>(bytes.data() + 16));
        }
        break;
    case Kind::kFiles:
        remote_->files = ClipboardFiles::Receive(bytes);
        LOGI("event=rdp.clipboard.descriptors bytes={} accepted={}", bytes.size(), static_cast<bool>(remote_->files));
        if (remote_->files) {
            transfer_deadline_ = std::chrono::steady_clock::now() + kFileTransferTimeout;
            return NextFile();
        }
        break;
    }
    return Next();
}
bool ClipboardChannel::NextFile() {
    if (!remote_ || !remote_->files) {
        return false;
    }
    const auto& entries = remote_->files->Entries();
    while (file_index_ < entries.size() && (entries.at(file_index_).directory || (size_verified_ && file_offset_ == entries.at(file_index_).size))) {
        ++file_index_;
        file_offset_ = 0;
        size_verified_ = false;
    }
    if (file_index_ == entries.size()) {
        remote_->urls = remote_->files->Publish();
        LOGI("event=rdp.clipboard.files.complete entries={} published={}", entries.size(), !remote_->urls.isEmpty());
        if (remote_->urls.isEmpty()) {
            remote_->files.reset();
        }
        return Next();
    }
    if (stream_id_ == UINT32_MAX) {
        return false;
    } // Never reuse an ID while late responses may exist.
    pending_stream_ = ++stream_id_;
    requested_bytes_ =
        size_verified_ ? static_cast<std::uint32_t>(std::min<std::uint64_t>(64 * 1024, entries.at(file_index_).size - file_offset_)) : 8;
    CLIPRDR_FILE_CONTENTS_REQUEST request{};
    request.common.msgType = CB_FILECONTENTS_REQUEST;
    request.streamId = pending_stream_;
    request.listIndex = static_cast<UINT32>(file_index_);
    request.dwFlags = size_verified_ ? FILECONTENTS_RANGE : FILECONTENTS_SIZE;
    request.nPositionLow = static_cast<UINT32>(file_offset_);
    request.nPositionHigh = static_cast<UINT32>(file_offset_ >> 32);
    request.cbRequested = requested_bytes_;
    request.haveClipDataId = remote_lock_ != 0;
    request.clipDataId = remote_lock_;
    LOGI("event=rdp.clipboard.files.request stream={} index={} bytes={}", pending_stream_, file_index_, requested_bytes_);
    deadline_ = std::chrono::steady_clock::now() + kResponseTimeout;
    return channel_->ClientFileContentsRequest(channel_.get(), &request) == CHANNEL_RC_OK;
}
bool ClipboardChannel::FileResponse(const CLIPRDR_FILE_CONTENTS_RESPONSE& response) {
    LOGI("event=rdp.clipboard.files.received stream={} expected={} bytes={}", response.streamId, pending_stream_, response.cbRequested);
    if (response.streamId != pending_stream_ || pending_stream_ == 0) {
        return true;
    }
    pending_stream_ = 0;
    if (!remote_ || !remote_->files) {
        return false;
    }
    if (!size_verified_) {
        if (response.common.msgFlags != CB_RESPONSE_OK || response.cbRequested != 8 || !response.requestedData ||
            qFromLittleEndian<quint64>(response.requestedData) != remote_->files->Entries().at(file_index_).size) {
            remote_->files.reset();
            return Next();
        }
        size_verified_ = true;
        return NextFile();
    }
    if (response.common.msgFlags != CB_RESPONSE_OK || response.cbRequested == 0 || response.cbRequested > requested_bytes_ ||
        !response.requestedData || !remote_->files->Write(file_index_, file_offset_, {response.requestedData, response.cbRequested})) {
        LOGW("event=rdp.clipboard.files.response outcome=rejected flags={} bytes={}", response.common.msgFlags, response.cbRequested);
        remote_->files.reset(); // Cancellation removes only this private, incomplete staging tree.
        return Next();
    }
    file_offset_ += response.cbRequested;
    return NextFile();
}
bool ClipboardChannel::FileRequest(const CLIPRDR_FILE_CONTENTS_REQUEST& request) {
    auto files = offered_files_;
    if (request.haveClipDataId) {
        const auto found = locks_.find(request.clipDataId);
        files = found == locks_.end() ? nullptr : found->second;
    }
    QByteArray bytes{};
    bool ok{false};
    if (files && request.listIndex < files->Entries().size()) {
        const auto& entry = files->Entries().at(request.listIndex);
        const std::uint64_t offset = (static_cast<std::uint64_t>(request.nPositionHigh) << 32) | request.nPositionLow;
        if (!entry.directory && request.dwFlags == FILECONTENTS_SIZE && request.cbRequested == 8) {
            bytes.resize(8);
            qToLittleEndian<quint64>(entry.size, bytes.data());
            ok = true;
        } else if (!entry.directory && request.dwFlags == FILECONTENTS_RANGE && request.cbRequested <= kClipboardDataLimit && offset <= entry.size) {
            const auto length = std::min<std::uint32_t>(request.cbRequested, 64 * 1024);
            bytes = files->Read(request.listIndex, offset, length);
            ok = bytes.size() == static_cast<qsizetype>(std::min<std::uint64_t>(length, entry.size - offset));
        }
    }
    CLIPRDR_FILE_CONTENTS_RESPONSE response{};
    response.common.msgType = CB_FILECONTENTS_RESPONSE;
    response.common.msgFlags = ok ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
    response.streamId = request.streamId;
    response.cbRequested = static_cast<UINT32>(bytes.size());
    response.requestedData = reinterpret_cast<const BYTE*>(bytes.constData()); // Transient synchronous FreeRDP serialization ABI.
    return channel_->ClientFileContentsResponse(channel_.get(), &response) == CHANNEL_RC_OK;
}
bool ClipboardChannel::Lock(std::uint32_t id) {
    if (locks_.contains(id)) {
        return false;
    }
    if (locks_.size() >= 4) {
        return false;
    }
    locks_.emplace(id, offered_files_);
    return true;
}
bool ClipboardChannel::Unlock(std::uint32_t id) {
    locks_.erase(id);
    return true;
}
bool ClipboardChannel::Tick() {
    const auto now = std::chrono::steady_clock::now();
    if (pending_ && now >= deadline_) {
        return false;
    } // No request ID: reconnect is required to safely resynchronize.
    if (pending_stream_ && (now >= deadline_ || now >= transfer_deadline_)) {
        LOGW("event=rdp.clipboard.files.timeout stream={}", pending_stream_);
        pending_stream_ = 0;
        if (remote_) {
            remote_->files.reset();
        }
        return Next();
    }
    return true;
}
} // namespace px::rdp
