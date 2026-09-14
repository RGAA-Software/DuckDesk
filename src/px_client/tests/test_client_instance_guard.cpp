#include "client_instance_guard.h"

#include <Windows.h>
#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

namespace px::client::imgui {
namespace {

std::string UniqueRemote(const std::string_view suffix) {
    return "instance-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::string{suffix};
}

struct ActivationProbe final {
    std::mutex mutex{};
    std::condition_variable changed{};
    bool activated{};
};

} // namespace

TEST(ClientInstanceGuardTest, ActivatesTheExistingInstanceForTheSameRemoteAndMode) {
    const std::string remote{UniqueRemote("same")};
    auto first = ClientInstanceGuard::Acquire(remote, ClientInstanceMode::Desktop);
    ASSERT_TRUE(first.instance);
    EXPECT_FALSE(first.activatedExisting);

    const auto second = ClientInstanceGuard::Acquire(remote, ClientInstanceMode::Desktop);
    EXPECT_FALSE(second.instance);
    EXPECT_TRUE(second.activatedExisting);
    EXPECT_EQ(second.systemError, ERROR_SUCCESS);
    EXPECT_TRUE(first.instance->ConsumeActivationRequest());
    EXPECT_FALSE(first.instance->ConsumeActivationRequest());
}

TEST(ClientInstanceGuardTest, KeepsDesktopAndFileTransferAsSeparateSingletons) {
    const std::string remote{UniqueRemote("modes")};
    auto desktop = ClientInstanceGuard::Acquire(remote, ClientInstanceMode::Desktop);
    auto fileTransfer = ClientInstanceGuard::Acquire(remote, ClientInstanceMode::FileTransfer);
    auto otherRemote = ClientInstanceGuard::Acquire(UniqueRemote("other"), ClientInstanceMode::Desktop);

    EXPECT_TRUE(desktop.instance);
    EXPECT_TRUE(fileTransfer.instance);
    EXPECT_TRUE(otherRemote.instance);
    EXPECT_FALSE(desktop.activatedExisting);
    EXPECT_FALSE(fileTransfer.activatedExisting);
    EXPECT_FALSE(otherRemote.activatedExisting);
}

TEST(ClientInstanceGuardTest, ActivationMonitorReceivesRequestsWithoutFramePolling) {
    const std::string remote{UniqueRemote("monitor")};
    auto first = ClientInstanceGuard::Acquire(remote, ClientInstanceMode::Desktop);
    ASSERT_TRUE(first.instance);
    const auto probe = std::make_shared<ActivationProbe>();
    ASSERT_TRUE(first.instance->StartActivationMonitor([probe] {
        {
            const std::scoped_lock lock{probe->mutex};
            probe->activated = true;
        }
        probe->changed.notify_one();
    }));

    const auto second = ClientInstanceGuard::Acquire(remote, ClientInstanceMode::Desktop);
    ASSERT_TRUE(second.activatedExisting);
    std::unique_lock lock{probe->mutex};
    EXPECT_TRUE(probe->changed.wait_for(lock, std::chrono::seconds{1}, [probe] { return probe->activated; }));
}

TEST(ClientInstanceGuardTest, NormalizesDeviceIdSpacingAndAsciiCase) {
    const std::string suffix{std::to_string(GetCurrentProcessId())};
    auto first = ClientInstanceGuard::Acquire("MC 90 " + suffix, ClientInstanceMode::FileTransfer);
    ASSERT_TRUE(first.instance);

    const auto second = ClientInstanceGuard::Acquire("mc90" + suffix, ClientInstanceMode::FileTransfer);
    EXPECT_TRUE(second.activatedExisting);
    EXPECT_TRUE(first.instance->ConsumeActivationRequest());
}

} // namespace px::client::imgui
