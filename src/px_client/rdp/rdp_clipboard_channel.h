#pragma once

#include <freerdp/client/cliprdr.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>

#include "rdp_clipboard_content.h"
#include "rdp_clipboard_files.h"

namespace px::rdp {

class ClipboardChannel final {
public:
    using Publish = std::function<void(ClipboardContent)>;

    ClipboardChannel(std::shared_ptr<CliprdrClientContext> channel, Publish publish);
    bool Ready();
    bool Capabilities(const CLIPRDR_CAPABILITIES& capabilities);
    bool Formats(const CLIPRDR_FORMAT_LIST& formats);
    bool DataRequest(const CLIPRDR_FORMAT_DATA_REQUEST& request);
    bool DataResponse(const CLIPRDR_FORMAT_DATA_RESPONSE& response);
    bool FileRequest(const CLIPRDR_FILE_CONTENTS_REQUEST& request);
    bool FileResponse(const CLIPRDR_FILE_CONTENTS_RESPONSE& response);
    bool SetLocal(ClipboardContent content);
    bool Tick() const;

private:
    enum class FormatKind : std::uint8_t { kText, kHtml, kDib, kDibV5, kPng, kFiles };
    enum class RemoteFileRequestKind : std::uint8_t { kSize, kRange };
    struct FormatRequest final {
        std::uint32_t id{};
        FormatKind kind{FormatKind::kText};
    };

    bool Advertise();
    bool RequestNext();
    bool StartRemoteFiles();
    bool AdvanceRemoteFile();
    bool RequestRemoteFile(RemoteFileRequestKind kind);
    bool PublishRemote();

    std::shared_ptr<CliprdrClientContext> channel_{};
    Publish publish_{};
    ClipboardContent local_{};
    ClipboardContent remote_{};
    std::vector<LocalClipboardFile> localFiles_{};
    std::vector<std::uint8_t> localFileDescriptors_{};
    std::vector<RemoteClipboardFile> remoteFiles_{};
    std::shared_ptr<ClipboardStagingDirectory> remoteStaging_{};
    std::unique_ptr<std::ofstream> remoteOutput_{};
    std::deque<FormatRequest> requests_{};
    std::optional<FormatRequest> pending_{};
    std::optional<RemoteFileRequestKind> pendingFileRequest_{};
    std::size_t remoteFileIndex_{};
    std::uint64_t remoteFileOffset_{};
    std::uint64_t remoteFileSize_{};
    std::uint32_t nextStreamId_{1U};
    std::uint32_t pendingStreamId_{};
    std::size_t pendingFileBytes_{};
    std::chrono::steady_clock::time_point deadline_{};
    bool ready_{};
};

}  // namespace px::rdp
