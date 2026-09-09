#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include <asio2/base/error.hpp>

#include "data.h"

namespace px {

using ReliableWriteCallback = std::function<void(bool)>;

// Owns completion through queue cancellation as well as a normal write. Never calls application code twice.
class ReliableWriteCompletion final {
  public:
    explicit ReliableWriteCompletion(ReliableWriteCallback callback) : callback_(std::move(callback)) {}
    ~ReliableWriteCompletion() {
        Complete(false);
    }

    void Complete(const bool success) noexcept {
        if (completed_.exchange(true)) {
            return;
        }
        const auto callback = std::move(callback_);
        if (callback) {
            try {
                callback(success);
            } catch (...) {
                // Application completion must not unwind an asio2 executor or a cancellation destructor.
            }
        }
    }

  private:
    ReliableWriteCallback callback_{};
    std::atomic_bool completed_{false};
};

// The caller enforces the per-stream byte budget and authorization. is_current rejects old adapter generations.
// No raw view survives without its Data owner; discarded queue handlers report cancellation via RAII.
template <typename Socket>
void PostReliableWebSocketWrite(const std::shared_ptr<Socket>& socket, std::shared_ptr<Data> data, ReliableWriteCallback callback,
                                std::function<bool()> is_current) {
    const auto completion = std::make_shared<ReliableWriteCompletion>(std::move(callback));
    if (!socket || !data || data->Size() == 0 || !is_current) {
        return;
    }
    try {
        socket->post([weak = std::weak_ptr<Socket>(socket), data = std::move(data), completion, is_current = std::move(is_current)] {
            const auto current = weak.lock();
            if (!current || !current->is_started() || !is_current()) {
                completion->Complete(false);
                return;
            }
            try {
                current->ws_stream().binary(true);
                current->async_send(data->Bytes().data(), data->Size(), [data, completion, is_current](const std::size_t written) {
                    const bool success = written == data->Size() && !asio2::get_last_error();
                    completion->Complete(success && is_current());
                });
            } catch (...) {
                completion->Complete(false);
            }
        });
    } catch (...) {
        completion->Complete(false);
    }
}

} // namespace px
