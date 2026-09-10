#include <gtest/gtest.h>
#include "../../app/win/owned_game_process.h"
#include <TlHelp32.h>
#include <chrono>
#include <thread>

namespace {
const std::filesystem::path kFixture{GAME_OWNERSHIP_FIXTURE};

DWORD FindChild(DWORD root) {
    const px::UniqueWinHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE) {
        return 0;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.get(), &entry)) {
        do {
            if (entry.th32ParentProcessID == root) {
                return entry.th32ProcessID;
            }
        } while (Process32NextW(snapshot.get(), &entry));
    }
    return 0;
}
} // namespace

TEST(GameOwnedProcess, RequiresOwnershipAndPathEvenForAnIdenticalExecutable) {
    const auto owned = px::OwnedGameProcess::Launch(kFixture, L"", false);
    const auto other = px::OwnedGameProcess::Launch(kFixture, L"", false);
    ASSERT_TRUE(owned);
    ASSERT_TRUE(other);
    EXPECT_TRUE(owned->Acquire(owned->RootPid(), kFixture, false));
    EXPECT_FALSE(owned->Acquire(other->RootPid(), kFixture, false));
    EXPECT_FALSE(owned->Acquire(other->RootPid(), kFixture, true));
    EXPECT_FALSE(owned->Acquire(owned->RootPid(), kFixture.parent_path() / "different.exe", true));
    EXPECT_FALSE(owned->Acquire(GetCurrentProcessId(), kFixture, true));
    owned->Stop();
    EXPECT_TRUE(other->Acquire(other->RootPid(), kFixture, false));
}

TEST(GameOwnedProcess, LaunchesWithQuotedSpacedPathAndMixedSeparators) {
    ASSERT_NE(kFixture.native().find(L' '), std::wstring::npos);
    const auto quoted = L"\"" + kFixture.generic_wstring() + L"\"";
    const auto owned = px::OwnedGameProcess::Launch(quoted, LR"(--name "a b" --empty "")", false);
    ASSERT_TRUE(owned);
    EXPECT_TRUE(owned->Acquire(owned->RootPid(), quoted, false));
    EXPECT_TRUE(owned->Acquire(owned->RootPid(), kFixture, false));
}

TEST(GameOwnedProcess, AllowsOnlyOwnedDescendantsWhenViewModeIsEnabled) {
    const auto owned = px::OwnedGameProcess::Launch(kFixture, L"--spawn-child", false);
    ASSERT_TRUE(owned);
    DWORD child{};
    for (int attempt{}; attempt < 30 && !child; ++attempt) {
        child = FindChild(owned->RootPid());
        if (!child) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    ASSERT_NE(child, 0u);
    EXPECT_FALSE(owned->Acquire(child, kFixture, false));
    const auto guard = owned->Acquire(child, kFixture, true);
    ASSERT_TRUE(guard);
    EXPECT_FALSE(owned->Acquire(child, kFixture.parent_path() / "different.exe", true));
    owned->Stop();
    EXPECT_EQ(WaitForSingleObject(guard->get(), 3000), WAIT_OBJECT_0);
    EXPECT_FALSE(owned->Acquire(child, kFixture, true));
}

TEST(GameOwnedProcess, DestructionCancelsQueuedWorkAndRepeatedStopIsSafe) {
    for (int cycle{}; cycle < 3; ++cycle) {
        auto owned = px::OwnedGameProcess::Launch(kFixture, L"", false);
        ASSERT_TRUE(owned);
        const auto pid = owned->RootPid();
        const auto guard = owned->Acquire(pid, kFixture, false);
        ASSERT_TRUE(guard);
        const auto queued = [weak = std::weak_ptr{owned}, pid]() {
            const auto owner = weak.lock();
            if (!owner) {
                return false;
            }
            owner->Stop();
            owner->Stop();
            return static_cast<bool>(owner->Acquire(pid, kFixture, false));
        };
        EXPECT_FALSE(queued());
        owned.reset();
        EXPECT_FALSE(queued());
        EXPECT_EQ(WaitForSingleObject(guard->get(), 3000), WAIT_OBJECT_0);
    }
}

TEST(GameOwnedProcess, RejectsInvalidLaunchWithoutCreatingAnOwner) {
    EXPECT_FALSE(px::OwnedGameProcess::Launch(L"game.exe", L"", false));
    EXPECT_FALSE(px::OwnedGameProcess::Launch(L"steam://run/123", L"", false));
    EXPECT_FALSE(px::OwnedGameProcess::Launch(kFixture.parent_path() / "missing.exe", L"", false));
}
