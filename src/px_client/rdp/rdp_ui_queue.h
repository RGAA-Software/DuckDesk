#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace px::rdp {

// The UI root must Close before destroying the Qt receiver. Close serializes
// worker posts and cancels not-yet-dispatched actions, including callback shutdown.
class UiQueue final {
    struct Budget final {
        std::atomic_size_t bytes{0};
        std::atomic_size_t actions{0};
        std::atomic_bool accepting{true};
    };
    struct Permit final {
        std::shared_ptr<Budget> budget{};
        std::size_t bytes{0};
        explicit Permit(std::shared_ptr<Budget> value) : budget(std::move(value)) {}
        ~Permit() {
            if (bytes != 0) {
                budget->bytes.fetch_sub(bytes);
                budget->actions.fetch_sub(1);
            }
        }
    };

  public:
    explicit UiQueue(QObject& receiver) : receiver_(&receiver) {} // NOLINT(gammaray-raw-pointer-boundary): Qt observation only, never ownership.
    ~UiQueue() {
        Close();
    }
    bool Post(std::function<void()> action, std::size_t bytes = 1024) {
        if (!action || bytes > kMaximumBytes - 1024) {
            return false;
        }
        bytes += 1024;
        std::lock_guard lock(mutex_);
        if (!budget_->accepting.load() || !receiver_ || budget_->actions.load() >= 2048 || budget_->bytes.load() > kMaximumBytes - bytes) {
            return false;
        }
        try {
            const auto permit = std::make_shared<Permit>(budget_);
            budget_->bytes.fetch_add(bytes);
            budget_->actions.fetch_add(1);
            permit->bytes = bytes;
            return QMetaObject::invokeMethod(
                receiver_.data(),
                [permit, action = std::move(action)] {
                    if (permit->budget->accepting.load()) {
                        action();
                    }
                },
                Qt::QueuedConnection);
        } catch (...) {
            return false;
        }
    }
    void Close() noexcept {
        std::lock_guard lock(mutex_);
        budget_->accepting.store(false);
    }

  private:
    static constexpr std::size_t kMaximumBytes{80 * 1024 * 1024};
    QPointer<QObject> receiver_{};
    std::mutex mutex_{};
    std::shared_ptr<Budget> budget_{std::make_shared<Budget>()};
};

} // namespace px::rdp
