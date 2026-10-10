#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace px::render {

// Encoder-worker confined. Cached replays and live captures share one identity
// sequence for receipts and reference recovery, even if the capture index resets.
class EncoderFrameSequence final {
public:
    [[nodiscard]] std::optional<std::uint64_t> Assign(std::uint64_t capture_index) {
        if (last_index_ == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
        last_index_ = last_index_ ? std::max(capture_index, *last_index_ + 1) : capture_index;
        return last_index_;
    }

private:
    std::optional<std::uint64_t> last_index_{};
};

}  // namespace px::render
