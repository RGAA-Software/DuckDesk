#pragma once

#include "rdp_clipboard_files.h"
#include <freerdp/client/cliprdr.h>
#include <chrono>
#include <deque>
#include <functional>
#include <map>

namespace px::rdp {

// All methods execute on the single FreeRDP protocol worker. The aliasing
// channel handle shares the root context lifetime; Detach precedes its teardown.
class ClipboardChannel final {
  public:
    using Publish = std::function<void(std::shared_ptr<const ClipboardData>)>;
    ClipboardChannel(std::shared_ptr<CliprdrClientContext> channel, Publish publish);
    bool SetLocal(std::shared_ptr<const ClipboardData> data);
    bool Ready();
    bool Capabilities(const CLIPRDR_CAPABILITIES& capabilities);
    bool Formats(const CLIPRDR_FORMAT_LIST& list);
    bool DataRequest(const CLIPRDR_FORMAT_DATA_REQUEST& request);
    bool DataResponse(const CLIPRDR_FORMAT_DATA_RESPONSE& response);
    bool FileRequest(const CLIPRDR_FILE_CONTENTS_REQUEST& request);
    bool FileResponse(const CLIPRDR_FILE_CONTENTS_RESPONSE& response);
    bool Lock(std::uint32_t id);
    bool Unlock(std::uint32_t id);
    bool Tick();

  private:
    enum class Kind { kText, kHtml, kDib, kFiles };
    struct Request final {
        Kind kind{Kind::kText};
        std::uint32_t id{0};
        std::uint64_t generation{0};
    };
    bool Advertise();
    bool Next();
    bool NextFile();
    bool ReleaseRemoteLock();
    void CancelReceive();
    std::shared_ptr<CliprdrClientContext> channel_{};
    Publish publish_{};
    std::shared_ptr<const ClipboardData> local_{};
    std::shared_ptr<ClipboardFiles> offered_files_{};
    std::map<std::uint32_t, std::shared_ptr<ClipboardFiles>> locks_{};
    std::shared_ptr<ClipboardData> remote_{};
    std::deque<Request> requests_{};
    std::optional<Request> pending_{};
    std::chrono::steady_clock::time_point deadline_{};
    std::chrono::steady_clock::time_point transfer_deadline_{};
    std::uint64_t generation_{0};
    std::uint32_t html_id_{0};
    std::uint32_t files_id_{0};
    std::uint32_t contents_id_{0};
    std::uint32_t stream_id_{0};
    std::uint32_t pending_stream_{0};
    std::uint32_t requested_bytes_{0};
    std::size_t file_index_{0};
    std::uint64_t file_offset_{0};
    bool ready_{false};
    bool file_capability_{false};
    bool size_verified_{false};
    bool lock_capability_{false};
    bool channel_failed_{false};
    std::uint32_t next_lock_{0};
    std::uint32_t remote_lock_{0};
};
} // namespace px::rdp
