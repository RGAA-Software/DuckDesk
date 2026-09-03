#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "plugins/net_ws/direct_session_grant_store.h"

namespace px {
namespace {

DirectSessionGrantBinding Binding() {
    return {
        .device_id_ = "device-a",
        .stream_id_ = "stream-a",
        .client_nonce_ = "nonce-a",
        .remote_address_ = "192.0.2.10",
    };
}

TEST(DirectSessionGrantStore, RedeemConsumesTheOriginalGrant) {
    DirectSessionGrantStore store;
    const auto token = store.Issue(Binding(), 1'000);

    EXPECT_TRUE(store.Redeem(token, Binding(), 1'001));
    EXPECT_FALSE(store.Redeem(token, Binding(), 1'002));
}

TEST(DirectSessionGrantStore, PreparedIpDirectStreamIsItsOwnOneTimeConnectionKey) {
    DirectSessionGrantStore store;
    auto binding = Binding();
    binding.device_id_.clear();
    binding.stream_id_.clear();

    const auto stream_id = store.IssueStreamBinding(binding, 1'000);
    binding.stream_id_ = stream_id;

    EXPECT_TRUE(stream_id.starts_with("ip-direct:"));
    EXPECT_EQ(stream_id.size(), std::string("ip-direct:").size() + 32u);
    EXPECT_EQ(stream_id.find_first_not_of(
        "0123456789abcdef", std::string("ip-direct:").size()), std::string::npos);
    EXPECT_TRUE(store.Redeem(stream_id, binding, 1'001));
    EXPECT_FALSE(store.Redeem(stream_id, binding, 1'002));
}

TEST(DirectSessionGrantStore, RejectsAChangedPeerBindingWithoutConsumingGrant) {
    DirectSessionGrantStore store;
    const auto token = store.Issue(Binding(), 1'000);
    auto wrong_peer = Binding();
    wrong_peer.remote_address_ = "192.0.2.11";

    EXPECT_FALSE(store.Redeem(token, wrong_peer, 1'001));
    EXPECT_TRUE(store.Redeem(token, Binding(), 1'002));
}

TEST(DirectSessionGrantStore, RejectsEveryChangedSessionIdentityField) {
    const auto original = Binding();
    std::vector<DirectSessionGrantBinding> mismatches;
    auto wrong_device = original;
    wrong_device.device_id_ = "device-b";
    mismatches.push_back(wrong_device);
    auto wrong_stream = original;
    wrong_stream.stream_id_ = "stream-b";
    mismatches.push_back(wrong_stream);
    auto wrong_nonce = original;
    wrong_nonce.client_nonce_ = "nonce-b";
    mismatches.push_back(wrong_nonce);

    for (const auto& mismatch : mismatches) {
        DirectSessionGrantStore store;
        const auto token = store.Issue(original, 1'000);
        EXPECT_FALSE(store.Redeem(token, mismatch, 1'001));
        EXPECT_TRUE(store.Redeem(token, original, 1'002));
    }
}

TEST(DirectSessionGrantStore, RejectsExpiredGrant) {
    DirectSessionGrantStore store;
    const auto token = store.Issue(Binding(), 1'000);

    EXPECT_FALSE(store.Redeem(
        token, Binding(), 1'000 + DirectSessionGrantStore::kLifetimeMilliseconds));
}

TEST(DirectSessionGrantStore, ConcurrentReplayHasExactlyOneWinner) {
    const auto store = std::make_shared<DirectSessionGrantStore>();
    const auto binding = Binding();
    const auto token = store->Issue(binding, 1'000);
    const auto successes = std::make_shared<std::atomic_int>(0);
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int index = 0; index < 8; ++index) {
        workers.emplace_back([store, binding, token, successes] {
            if (store->Redeem(token, binding, 1'001)) {
                successes->fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    EXPECT_EQ(successes->load(std::memory_order_relaxed), 1);
}

} // namespace
} // namespace px
