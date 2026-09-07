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
            last_request_.reset();
            return Decision::kDecode;
        }
        if (!last_request_ || now - *last_request_ >= std::chrono::seconds{1}) {
            last_request_ = now;
            return Decision::kRequestKeyFrame;
        }
        return Decision::kWait;
    }

  private:
    std::optional<std::chrono::steady_clock::time_point> last_request_{};
};

} // namespace px
