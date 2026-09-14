#pragma once

#include <chrono>
#include <optional>

namespace px {

// Owned by one monitor's decode workflow and used only on its video queue.
// Recovery P frames cannot initialize a new decoder without an IDR/configuration.
class DecoderStartupGate final {
  public:
    enum class Decision { kDecode, kWait, kRequestKeyFrame };

    Decision Observe(bool key_frame, bool complete_configuration, std::chrono::steady_clock::time_point now) {
        if (key_frame && complete_configuration) {
            synchronized_ = true;
            last_request_.reset();
            return Decision::kDecode;
        }
        if (synchronized_) {
            return Decision::kDecode;
        }
        if (!last_request_ || now - *last_request_ >= std::chrono::seconds{1}) {
            last_request_ = now;
            return Decision::kRequestKeyFrame;
        }
        return Decision::kWait;
    }

    void RequireKeyFrame() {
        synchronized_ = false;
        last_request_.reset();
    }

    [[nodiscard]] bool IsSynchronized() const {
        return synchronized_;
    }

  private:
    bool synchronized_{};
    std::optional<std::chrono::steady_clock::time_point> last_request_{};
};

} // namespace px
