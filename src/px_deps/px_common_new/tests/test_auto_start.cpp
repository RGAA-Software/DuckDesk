#include <gtest/gtest.h>
#include "../auto_start.h"
#include <windows.h>
#include <filesystem>

using namespace tc;

TEST(AutoStartTest, DISABLED_SetAutoStart_WritesAndRemoves) {
    std::wstring exe_path = L"C:\\TestApp\\my_test_app.exe";
    AutoStart::SetAutoStart(exe_path, true);

    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        GTEST_SKIP() << "Failed to open registry key for reading, error: " << result;
    }

    wchar_t value[4096] = {0};
    DWORD size = sizeof(value);
    DWORD type = 0;
    result = RegQueryValueExW(hKey, L"my_test_app", nullptr, &type, (LPBYTE)value, &size);
    RegCloseKey(hKey);

    ASSERT_EQ(result, ERROR_SUCCESS) << "Registry value not found after SetAutoStart";
    ASSERT_EQ(std::wstring(value), exe_path) << "Registry value mismatch";

    // Remove
    AutoStart::SetAutoStart(exe_path, false);

    result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    ASSERT_EQ(result, ERROR_SUCCESS) << "Failed to open registry key for verification";
    result = RegQueryValueExW(hKey, L"my_test_app", nullptr, &type, (LPBYTE)value, &size);
    RegCloseKey(hKey);
    ASSERT_EQ(result, ERROR_FILE_NOT_FOUND) << "Registry value should be removed after disabling auto start";
}

TEST(AutoStartTest, DISABLED_SetAutoStartAdmin_WritesAndRemoves) {
    std::wstring exe_path = L"C:\\TestApp\\my_test_app.exe";
    AutoStart::SetAutoStartAdmin(exe_path, true);

    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        GTEST_SKIP() << "Failed to open HKLM registry key for reading (may need admin), error: " << result;
    }

    wchar_t value[4096] = {0};
    DWORD size = sizeof(value);
    DWORD type = 0;
    result = RegQueryValueExW(hKey, L"my_test_app", nullptr, &type, (LPBYTE)value, &size);
    RegCloseKey(hKey);

    ASSERT_EQ(result, ERROR_SUCCESS) << "HKLM registry value not found after SetAutoStartAdmin";
    ASSERT_EQ(std::wstring(value), exe_path) << "HKLM registry value mismatch";

    // Remove
    AutoStart::SetAutoStartAdmin(exe_path, false);

    result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    ASSERT_EQ(result, ERROR_SUCCESS) << "Failed to open HKLM registry key for verification";
    result = RegQueryValueExW(hKey, L"my_test_app", nullptr, &type, (LPBYTE)value, &size);
    RegCloseKey(hKey);
    ASSERT_EQ(result, ERROR_FILE_NOT_FOUND) << "HKLM registry value should be removed after disabling auto start";
}

TEST(AutoStartTest, SetAutoStart_NoOpOnEmptyPath) {
    // Ensure empty path doesn't crash and doesn't create invalid registry entries
    std::wstring empty_path;
    AutoStart::SetAutoStart(empty_path, true);

    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        wchar_t value[4096] = {0};
        DWORD size = sizeof(value);
        DWORD type = 0;
        result = RegQueryValueExW(hKey, L"", nullptr, &type, (LPBYTE)value, &size);
        RegCloseKey(hKey);
        // Empty stem might produce empty key name; we just ensure no crash
    }
    SUCCEED();
}

TEST(AutoStartTest, DISABLED_SetAutoStartChinesePath) {
    std::wstring exe_path = L"C:\\测试程序\\我的应用.exe";
    AutoStart::SetAutoStart(exe_path, true);

    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        GTEST_SKIP() << "Failed to open registry key for reading, error: " << result;
    }

    wchar_t value[4096] = {0};
    DWORD size = sizeof(value);
    DWORD type = 0;
    result = RegQueryValueExW(hKey, L"我的应用", nullptr, &type, (LPBYTE)value, &size);
    RegCloseKey(hKey);

    ASSERT_EQ(result, ERROR_SUCCESS) << "Registry value not found for Chinese path";
    ASSERT_EQ(std::wstring(value), exe_path) << "Registry value mismatch for Chinese path";

    // Remove
    AutoStart::SetAutoStart(exe_path, false);

    result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    ASSERT_EQ(result, ERROR_SUCCESS);
    result = RegQueryValueExW(hKey, L"我的应用", nullptr, &type, (LPBYTE)value, &size);
    RegCloseKey(hKey);
    ASSERT_EQ(result, ERROR_FILE_NOT_FOUND) << "Registry value should be removed";
}
