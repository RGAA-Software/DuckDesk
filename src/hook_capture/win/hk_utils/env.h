#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <string>

namespace tc
{

    static bool GetEnvironmentVariableA(const std::string &name, std::string *value) {
        DWORD length = ::GetEnvironmentVariableA(name.data(), nullptr, 0);
        // if length == 1, it's ""
        if (length > 0) {
            // length including terminating null character
            if (nullptr != value) {
                value->resize(length - 1);
                length = ::GetEnvironmentVariableA(name.data(), value->data(), length);
                if (length == 0) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    static bool GetEnvironmentVariableW(const std::wstring &name,  std::wstring *value) {
        DWORD length = ::GetEnvironmentVariableW(name.data(), nullptr, 0);
        // if length == 1, it's ""
        if (length > 0) {
            // length including terminating null character
            if (nullptr != value) {
                value->resize(length - 1);
                length = ::GetEnvironmentVariableW(name.data(), value->data(), length);
                if (length == 0) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    static std::string ExpandEnvironmentStringA(const std::string &original) {
        if (std::string::npos != original.find('%')) {
            // max = 32 * 1024
            const DWORD c_size = 32 * 1024;
            std::string expanded;
            expanded.resize(c_size);
            int length = (int) ExpandEnvironmentStringsA(original.data(),
                                                         expanded.data(), c_size);
            if (0 != length) {
                expanded.resize(length - 1);
                return expanded;
            }
        }

        return original;
    }

    static std::wstring ExpandEnvironmentStringW(const std::wstring &original) {
        if (std::wstring::npos != original.find(L'%')) {
            // max = 32 * 1024
            const DWORD c_size = 32 * 1024;
            std::wstring expanded;
            expanded.resize(c_size);
            int length = (int) ExpandEnvironmentStringsW(original.data(),
                                                         expanded.data(), c_size);
            if (0 != length) {
                expanded.resize(length - 1);
                return expanded;
            }
        }

        return original;
    }

    // no \ tail, unless it's root
    static std::string GetCurrentDirectoryA() {
        DWORD length = ::GetCurrentDirectoryA(0, nullptr);
        // if length == 1, it's ""
        if (length > 0) {
            std::string dir;
            dir.resize(length - 1);
            // length including terminating null character
            length = ::GetCurrentDirectoryA(length, dir.data());
            if (length > 0) {
                return dir;
            }
        }
        return "";
    }

    static std::wstring GetCurrentDirectoryW() {
        DWORD length = ::GetCurrentDirectoryW(0, nullptr);
        // if length == 1, it's ""
        if (length > 0) {
            std::wstring dir;
            dir.resize(length - 1);
            // length including terminating null character
            length = ::GetCurrentDirectoryW(length, dir.data());
            if (length > 0) {
                return dir;
            }
        }
        return L"";
    }

    static std::string GetTempDirectoryA() {
        std::string temp_dir;

        // The maximum possible return value is MAX_PATH+1 (261)
        for (DWORD buffer_size = MAX_PATH + 1;;) {
            temp_dir.resize(buffer_size);
            DWORD size = ::GetTempPathA(buffer_size, temp_dir.data());
            if (0 == size) {
                return "";
            }
            if (size < buffer_size) {
                temp_dir.resize(size);
                return temp_dir;
            }
            buffer_size = size;
        }
    }

    static std::wstring GetTempDirectoryW() {
        std::wstring temp_dir;

        // The maximum possible return value is MAX_PATH+1 (261)
        for (DWORD buffer_size = MAX_PATH + 1;;) {
            temp_dir.resize(buffer_size);
            DWORD size = ::GetTempPathW(buffer_size, temp_dir.data());
            if (0 == size) {
                return L"";
            }
            if (size < buffer_size) {
                temp_dir.resize(size);
                return temp_dir;
            }
            buffer_size = size;
        }
    }

}  // end of namespace tc