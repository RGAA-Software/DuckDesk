#include <gtest/gtest.h>
#include "../../app/win/owned_game_process.h"
#include <TlHelp32.h>
#include <chrono>
#include <thread>
#include <array>

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

TEST(GameOwnedProcess, SuspendedRootIsAdmittedBeforeAnyChildCanExecute) {
    const auto owner = px::OwnedGameProcess::LaunchSuspended(kFixture, L"--spawn-child", false);
    ASSERT_TRUE(owner);
    EXPECT_TRUE(owner->Acquire(owner->RootPid(), kFixture, false));
    EXPECT_EQ(FindChild(owner->RootPid()), 0u);
    EXPECT_TRUE(owner->Resume());
    EXPECT_FALSE(owner->Resume());
}

TEST(GameOwnedProcess, BootstrapFailureDestroysSuspendedProcessAndStopPreventsResume) {
    auto owner = px::OwnedGameProcess::LaunchSuspended(kFixture, L"--spawn-child", false);
    ASSERT_TRUE(owner);
    const auto guard = owner->Acquire(owner->RootPid(), kFixture, false);
    ASSERT_TRUE(guard);
    owner->Stop();
    EXPECT_FALSE(owner->Resume());
    owner.reset();
    EXPECT_EQ(WaitForSingleObject(guard->get(), 3000), WAIT_OBJECT_0);
}

TEST(GameOwnedProcess, RejectsMalformedEnvironmentBeforeCreatingProcess) {
    EXPECT_FALSE(px::OwnedGameProcess::LaunchSuspended(kFixture, L"", false, {{L"BAD=NAME", L"value"}}));
    EXPECT_FALSE(px::OwnedGameProcess::LaunchSuspended(kFixture, L"", false, {{L"", L"value"}}));
}

TEST(GameOwnedProcess, StandardUserPolicyDoesNotLaunchAnElevatedGame) {
    const auto owner = px::OwnedGameProcess::LaunchSuspended(
        kFixture, L"--check-environment", false, {{L"PIXELS_LAUNCH_ENV_FIXTURE", L"child-only Unicode 中文"}}, px::GameTokenPolicy::kStandardUser);
    ASSERT_TRUE(owner);
    const auto process = owner->Acquire(owner->RootPid(), kFixture, false);
    ASSERT_TRUE(process);
    HANDLE result{}; // NOLINT(gammaray-raw-pointer-boundary) Win32 token output immediately wrapped.
    ASSERT_TRUE(OpenProcessToken(process->get(), TOKEN_QUERY, &result));
    const px::UniqueWinHandle token{result};
    TOKEN_ELEVATION elevation{};
    DWORD returned{};
    ASSERT_TRUE(GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &returned));
    EXPECT_EQ(elevation.TokenIsElevated, 0u);
    std::array<std::byte, SECURITY_MAX_SID_SIZE + sizeof(TOKEN_MANDATORY_LABEL)> integrity{};
    ASSERT_TRUE(GetTokenInformation(token.get(), TokenIntegrityLevel, integrity.data(), static_cast<DWORD>(integrity.size()), &returned));
    const auto level = *GetSidSubAuthority(reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(integrity.data())->Label.Sid, 0);
    EXPECT_LT(level, SECURITY_MANDATORY_HIGH_RID);
    EXPECT_TRUE(owner->Resume());
    ASSERT_EQ(WaitForSingleObject(process->get(), 3000), WAIT_OBJECT_0);
    DWORD exit_code{};
    ASSERT_TRUE(GetExitCodeProcess(process->get(), &exit_code));
    EXPECT_EQ(exit_code, 0u);
}

TEST(GameOwnedProcess, EnvironmentOverrideIsChildOnlyAndPreservesUnicode) {
    std::array<wchar_t, 128> before{};
    const auto before_size = GetEnvironmentVariableW(L"PIXELS_LAUNCH_ENV_FIXTURE", before.data(), static_cast<DWORD>(before.size()));
    const auto owner =
        px::OwnedGameProcess::LaunchSuspended(kFixture, L"--check-environment", false, {{L"PIXELS_LAUNCH_ENV_FIXTURE", L"child-only Unicode 中文"}});
    ASSERT_TRUE(owner);
    const auto process = owner->Acquire(owner->RootPid(), kFixture, false);
    ASSERT_TRUE(process);
    ASSERT_TRUE(owner->Resume());
    ASSERT_EQ(WaitForSingleObject(process->get(), 3000), WAIT_OBJECT_0);
    DWORD result{};
    ASSERT_TRUE(GetExitCodeProcess(process->get(), &result));
    EXPECT_EQ(result, 0u);
    std::array<wchar_t, 128> after{};
    EXPECT_EQ(GetEnvironmentVariableW(L"PIXELS_LAUNCH_ENV_FIXTURE", after.data(), static_cast<DWORD>(after.size())), before_size);
    EXPECT_EQ(before, after);
}

TEST(GameOwnedProcess, MediumCallerCanLaunchWithoutServicePrivileges) {
    std::vector<wchar_t> path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    ASSERT_GT(length, 0u);
    ASSERT_LT(length, path.size());
    const std::filesystem::path executable{std::wstring(path.data(), length)};
    const auto owner = px::OwnedGameProcess::LaunchSuspended(
        executable, L"--gtest_filter=GameOwnedProcess.StandardUserPolicyDoesNotLaunchAnElevatedGame", false, {}, px::GameTokenPolicy::kStandardUser);
    ASSERT_TRUE(owner);
    const auto process = owner->Acquire(owner->RootPid(), executable, false);
    ASSERT_TRUE(process);
    ASSERT_TRUE(owner->Resume());
    ASSERT_EQ(WaitForSingleObject(process->get(), 5000), WAIT_OBJECT_0);
    DWORD result{};
    ASSERT_TRUE(GetExitCodeProcess(process->get(), &result));
    EXPECT_EQ(result, 0u);
}
