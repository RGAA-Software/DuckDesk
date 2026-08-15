#include <gtest/gtest.h>
#include "../win32/win_helper.h"
#include <windows.h>
#include <filesystem>

using namespace tc;

TEST(WinHelperTest, GetExeName_CurrentProcess) {
    DWORD pid = GetCurrentProcessId();
    auto resp = WinHelper::GetExeName(pid);
    ASSERT_TRUE(resp.ok_) << "GetExeName failed for current process";
    ASSERT_FALSE(resp.value_.empty()) << "Exe name should not be empty";

    wchar_t modulePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path expected(modulePath);
    EXPECT_EQ(resp.value_, expected.filename().string()) << "Exe name should match current process module filename";
}

TEST(WinHelperTest, GetModulePath_Valid) {
    auto resp = WinHelper::GetModulePath(nullptr);
    ASSERT_TRUE(resp.ok_) << "GetModulePath failed for current module";
    ASSERT_FALSE(resp.value_.empty()) << "Module path should not be empty";

    std::filesystem::path p(resp.value_);
    EXPECT_TRUE(std::filesystem::exists(p)) << "Module path should exist on disk: " << resp.value_;
}

TEST(WinHelperTest, IsX86Arch_CurrentProcess) {
    DWORD pid = GetCurrentProcessId();
    auto resp = WinHelper::IsX86Arch(pid);
    ASSERT_TRUE(resp.ok_) << "IsX86Arch failed for current process";

#ifdef _WIN64
    EXPECT_FALSE(resp.value_) << "Current process on Win64 should not be x86 arch";
#else
    EXPECT_TRUE(resp.value_) << "Current process on Win32 should be x86 arch";
#endif
}

TEST(WinHelperTest, GetErrorStr_ValidHR) {
    // ERROR_FILE_NOT_FOUND = 2
    auto resp = WinHelper::GetErrorStr(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    ASSERT_TRUE(resp.ok_) << "GetErrorStr should succeed";
    ASSERT_FALSE(resp.value_.empty()) << "Error string should not be empty";
}
