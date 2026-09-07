#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace px {

    class UpgradeRequestState final {
    public:
        struct Request final {
            std::uint64_t generation = 0;
            std::shared_ptr<std::atomic_bool> cancellation;
        };

        [[nodiscard]] Request BeginCheck() {
            return Begin(check_);
        }

        [[nodiscard]] Request BeginDownload() {
            return Begin(download_);
        }

        [[nodiscard]] bool IsCheckCurrent(std::uint64_t generation) const {
            return IsCurrent(check_, generation);
        }

        [[nodiscard]] bool IsDownloadCurrent(std::uint64_t generation) const {
            return IsCurrent(download_, generation);
        }

        [[nodiscard]] bool CompleteCheck(std::uint64_t generation) {
            return Complete(check_, generation);
        }

        [[nodiscard]] bool CompleteDownload(std::uint64_t generation) {
            return Complete(download_, generation);
        }

        void Stop() {
            std::lock_guard lock(mutex_);
            stopped_ = true;
            Stop(check_);
            Stop(download_);
        }

    private:
        struct Slot final {
            std::uint64_t generation = 0;
            bool active = false;
            std::shared_ptr<std::atomic_bool> cancellation;
        };

        [[nodiscard]] Request Begin(Slot& slot) {
            std::lock_guard lock(mutex_);
            if (stopped_) {
                return {};
            }
            Cancel(slot);
            slot.active = true;
            ++slot.generation;
            slot.cancellation = std::make_shared<std::atomic_bool>(false);
            return {slot.generation, slot.cancellation};
        }

        [[nodiscard]] bool IsCurrent(
            const Slot& slot, std::uint64_t generation) const {
            std::lock_guard lock(mutex_);
            return !stopped_ && slot.active && generation != 0
                && slot.generation == generation;
        }

        [[nodiscard]] bool Complete(Slot& slot, std::uint64_t generation) {
            std::lock_guard lock(mutex_);
            if (stopped_ || !slot.active || generation == 0
                || slot.generation != generation) {
                return false;
            }
            slot.active = false;
            slot.cancellation.reset();
            return true;
        }

        static void Cancel(Slot& slot) {
            if (slot.cancellation) {
                slot.cancellation->store(true, std::memory_order_release);
            }
            slot.active = false;
            slot.cancellation.reset();
        }

        static void Stop(Slot& slot) {
            Cancel(slot);
            ++slot.generation;
        }

        mutable std::mutex mutex_;
        Slot check_;
        Slot download_;
        bool stopped_ = false;
    };

}
