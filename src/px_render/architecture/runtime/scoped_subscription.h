#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace px::render {

// Lifetime:
// - Shared by the component that owns a registration.
// - Destruction or Reset performs exactly one unregister operation.
// - The unregister callback captures only weak ownership.
//
// Threading:
// - Reset and IsActive may be called concurrently.
// - The internal mutex is released before unregister invokes external code.
class ScopedSubscription final {
public:
    explicit ScopedSubscription(std::function<void()> unregister)
        : unregister_(std::move(unregister)) {
        if (!unregister_) {
            throw std::invalid_argument("subscription requires unregister action");
        }
    }

    ~ScopedSubscription() {
        Reset();
    }

    ScopedSubscription(const ScopedSubscription&) = delete;
    ScopedSubscription& operator=(const ScopedSubscription&) = delete;
    ScopedSubscription(ScopedSubscription&&) = delete;
    ScopedSubscription& operator=(ScopedSubscription&&) = delete;

    void Reset() {
        std::function<void()> unregister;
        {
            const std::lock_guard lock(mutex_);
            unregister = std::move(unregister_);
        }
        if (unregister) {
            unregister();
        }
    }

    [[nodiscard]] bool IsActive() const {
        const std::lock_guard lock(mutex_);
        return static_cast<bool>(unregister_);
    }

private:
    mutable std::mutex mutex_;
    std::function<void()> unregister_;
};

// Lifetime:
// - Owned by the pipeline or module composition root.
// - Registry entries keep callbacks through weak_ptr only.
// - ScopedSubscription controls registration lifetime through RAII.
//
// Threading:
// - Subscribe, Dispatch, unregister, and Size may run concurrently.
// - User callbacks run without the registry mutex held.
template<typename Event>
class SubscriptionRegistry final
    : public std::enable_shared_from_this<SubscriptionRegistry<Event>> {
public:
    using Callback = std::function<void(const Event&)>;

    [[nodiscard]] static std::shared_ptr<SubscriptionRegistry> Create() {
        return std::make_shared<SubscriptionRegistry>();
    }

    [[nodiscard]] std::shared_ptr<ScopedSubscription> Subscribe(
        const std::shared_ptr<Callback>& callback) {
        if (!callback) {
            throw std::invalid_argument("subscription callback must be owned");
        }

        const auto entry = std::make_shared<Entry>(callback);
        std::uint64_t id = 0;
        {
            const std::lock_guard lock(mutex_);
            id = next_id_++;
            entry->id = id;
            entries_.emplace(id, entry);
        }

        const auto weak_self = this->weak_from_this();
        const std::weak_ptr<Entry> weak_entry = entry;
        return std::make_shared<ScopedSubscription>(
            [weak_self, weak_entry, id]() {
                if (const auto active_entry = weak_entry.lock()) {
                    active_entry->active.store(false, std::memory_order_release);
                }
                if (const auto self = weak_self.lock()) {
                    self->Unsubscribe(id);
                }
            });
    }

    void Dispatch(const Event& event) {
        std::vector<std::shared_ptr<Entry>> snapshot;
        {
            const std::lock_guard lock(mutex_);
            snapshot.reserve(entries_.size());
            for (const auto& [id, entry] : entries_) {
                static_cast<void>(id);
                snapshot.push_back(entry);
            }
        }

        std::vector<std::uint64_t> expired;
        for (const auto& entry : snapshot) {
            if (!entry->active.load(std::memory_order_acquire)) {
                continue;
            }
            const auto callback = entry->callback.lock();
            if (!callback) {
                entry->active.store(false, std::memory_order_release);
                expired.push_back(entry->id);
                continue;
            }
            if (entry->active.load(std::memory_order_acquire)) {
                (*callback)(event);
            }
        }
        RemoveExpired(expired);
    }

    [[nodiscard]] std::size_t Size() const {
        const std::lock_guard lock(mutex_);
        return entries_.size();
    }

private:
    struct Entry final {
        explicit Entry(const std::shared_ptr<Callback>& owned_callback)
            : callback(owned_callback) {}

        std::uint64_t id{0};
        std::weak_ptr<Callback> callback;
        std::atomic_bool active{true};
    };

    void Unsubscribe(const std::uint64_t id) {
        const std::lock_guard lock(mutex_);
        entries_.erase(id);
    }

    void RemoveExpired(const std::vector<std::uint64_t>& expired) {
        if (expired.empty()) {
            return;
        }
        const std::lock_guard lock(mutex_);
        for (const auto id : expired) {
            const auto found = entries_.find(id);
            if (found != entries_.end() &&
                !found->second->active.load(std::memory_order_acquire)) {
                entries_.erase(found);
            }
        }
    }

    mutable std::mutex mutex_;
    std::map<std::uint64_t, std::shared_ptr<Entry>> entries_;
    std::uint64_t next_id_{1};
};

}  // namespace px::render
