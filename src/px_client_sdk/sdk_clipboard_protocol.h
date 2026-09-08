#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "px_message.pb.h"

namespace px {

// Leave room for protobuf/TLV headers in the reliable file channel's 256 KiB message budget.
inline constexpr std::int64_t kClipboardReadChunkBytes = 128LL * 1024LL;

inline bool IsClipboardTransferNameValid(std::string_view name) {
    return !name.empty() && name.size() <= 4096U && name.find('\0') == std::string_view::npos;
}

inline bool IsClipboardFileDescriptorValid(std::string_view display_name, std::string_view transfer_name, std::int64_t size) {
    return IsClipboardTransferNameValid(display_name) && IsClipboardTransferNameValid(transfer_name) && size >= 0;
}

struct ClipboardReadRequest final {
    std::string transfer_name{};
    std::int64_t index{};
    std::int64_t offset{};
    std::int64_t size{};

    [[nodiscard]] bool IsValid() const {
        return IsClipboardTransferNameValid(transfer_name) && index >= 0 && offset >= 0 && size > 0 && size <= kClipboardReadChunkBytes &&
               offset <= std::numeric_limits<std::int64_t>::max() - size;
    }

    [[nodiscard]] bool Matches(const ClipboardRespBuffer& response) const {
        return IsValid() && response.full_name() == transfer_name && response.req_index() == index && response.req_start() == offset &&
               response.req_size() == size && response.read_size() >= 0 && response.read_size() <= size &&
               response.buffer().size() == static_cast<std::size_t>(response.read_size());
    }

    // A file that grows after publication must never expose bytes outside the published offer.
    [[nodiscard]] std::optional<std::int64_t> ReadSize(std::int64_t published_size) const {
        if (!IsValid() || published_size < 0 || offset > published_size) {
            return std::nullopt;
        }
        return std::min(size, published_size - offset);
    }

    static ClipboardReadRequest From(const ClipboardReqBuffer& request) {
        return {request.full_name(), request.req_index(), request.req_start(), request.req_size()};
    }
};

// The host serializes access. Reserve the identity before invoking a possibly synchronous sender.
// Cancel invalidates the pending read, but never reuses its sequence number (including failed sends).
class ClipboardPendingRead final {
  public:
    [[nodiscard]] std::optional<ClipboardReadRequest> Begin(std::string name, std::int64_t offset, std::int64_t size) {
        Cancel();
        if (next_index_ == std::numeric_limits<std::int64_t>::max()) {
            return {};
        }
        ClipboardReadRequest request{std::move(name), next_index_++, offset, size};
        if (!request.IsValid()) {
            return {};
        }
        pending_ = request;
        return pending_;
    }

    [[nodiscard]] bool Accepts(const ClipboardRespBuffer& response) const {
        return pending_ && pending_->Matches(response);
    }

    void Cancel() {
        pending_.reset();
    }

  private:
    std::int64_t next_index_{};
    std::optional<ClipboardReadRequest> pending_{};
};

// A host queue may retain an update after a later local/remote clipboard change or Stop.
class ClipboardUpdateEpoch final {
  public:
    [[nodiscard]] std::uint64_t Advance() {
        return epoch_.fetch_add(1) + 1;
    }
    [[nodiscard]] bool IsCurrent(std::uint64_t epoch) const {
        return epoch != 0 && epoch_.load() == epoch;
    }

  private:
    std::atomic_uint64_t epoch_{};
};

} // namespace px
