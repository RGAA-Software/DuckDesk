#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>

#include "px_common_new/weak_callback.h"

namespace {

struct CallbackOwner {
    std::atomic_int call_count{0};
};

TEST(WeakCallback, QueuedCallbackDoesNotRetainOwnerTenRounds) {
    for (int round = 1; round <= 10; ++round) {
        auto owner = std::make_shared<CallbackOwner>();
        const std::weak_ptr<CallbackOwner> weak_owner = owner;
        auto callback = px::MakeWeakVoidCallback(
            weak_owner,
            [](const std::shared_ptr<CallbackOwner>& locked, int) {
                locked->call_count.fetch_add(1, std::memory_order_relaxed);
            });
        owner.reset();

        callback(round);
        EXPECT_TRUE(weak_owner.expired()) << "round=" << round;
    }
}

TEST(WeakCallback, ConcurrentQueuedCallbackSkipsDestroyedOwnerTenRounds) {
    for (int round = 1; round <= 10; ++round) {
        auto owner = std::make_shared<CallbackOwner>();
        const std::weak_ptr<CallbackOwner> weak_owner = owner;
        auto callback = px::MakeWeakVoidCallback(
            weak_owner,
            [](const std::shared_ptr<CallbackOwner>& locked) {
                locked->call_count.fetch_add(1, std::memory_order_relaxed);
            });
        auto release = std::make_shared<std::atomic_bool>(false);
        std::jthread worker([callback = std::move(callback), release]() {
            while (!release->load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            callback();
        });

        owner.reset();
        release->store(true, std::memory_order_release);
        worker.join();
        EXPECT_TRUE(weak_owner.expired()) << "round=" << round;
    }
}

TEST(WeakCallback, CallbackMayReleaseLastExternalOwnerTenRounds) {
    for (int round = 1; round <= 10; ++round) {
        auto owner_holder = std::make_shared<std::shared_ptr<CallbackOwner>>(
            std::make_shared<CallbackOwner>());
        const std::weak_ptr<CallbackOwner> weak_owner = *owner_holder;
        auto callback = px::MakeWeakVoidCallback(
            weak_owner,
            [owner_holder](const std::shared_ptr<CallbackOwner>& locked) {
                owner_holder->reset();
                locked->call_count.fetch_add(1, std::memory_order_relaxed);
            });

        callback();
        EXPECT_TRUE(weak_owner.expired()) << "round=" << round;
    }
}

}  // namespace
