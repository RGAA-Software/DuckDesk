//
// Test helper executable for ProcessUtil tests.
// Used instead of cmd.exe to avoid codepage / quoting issues.
//
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

static std::string ToUTF8(const std::wstring& wstr) {
    if (wstr.empty() || wstr.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return {};
    const auto input_size = static_cast<int>(wstr.size());
    const int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), input_size, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, wstr.data(), input_size, result.data(), size, nullptr, nullptr) != size)
        return {};
    return result;
}

int main() {
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);

    int argc = 0;
    const std::unique_ptr<wchar_t*, decltype(&LocalFree)> argv{CommandLineToArgvW(GetCommandLineW(), &argc), &LocalFree};
    if (!argv || argc < 2) {
        return 1;
    }

    std::string cmd = ToUTF8(argv.get()[1]);
    int ret = 0;

    if (cmd == "echo") {
        for (int i = 2; i < argc; ++i) {
            if (i > 2)
                std::cout.write(" ", 1);
            auto utf8 = ToUTF8(argv.get()[i]);
            std::cout.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        }
        std::cout.write("\r\n", 2);
    } else if (cmd == "echo-stderr") {
        for (int i = 2; i < argc; ++i) {
            if (i > 2)
                std::cerr.write(" ", 1);
            auto utf8 = ToUTF8(argv.get()[i]);
            std::cerr.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        }
        std::cerr.write("\r\n", 2);
    } else if (cmd == "echo-lines") {
        for (int i = 2; i < argc; ++i) {
            auto utf8 = ToUTF8(argv.get()[i]);
            std::cout.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
            std::cout.write("\r\n", 2);
        }
    } else if (cmd == "create-file") {
        if (argc < 4) {
            ret = 1;
        } else {
            auto path = std::filesystem::path(argv.get()[2]);
            auto content = ToUTF8(argv.get()[3]);
            std::ofstream ofs(path, std::ios::binary);
            if (!ofs) {
                ret = 1;
            } else {
                ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
            }
        }
    } else if (cmd == "read-file") {
        if (argc < 3) {
            ret = 1;
        } else {
            auto path = std::filesystem::path(argv.get()[2]);
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs) {
                ret = 1;
            } else {
                std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                std::cout.write(content.data(), static_cast<std::streamsize>(content.size()));
                std::cout.write("\r\n", 2);
            }
        }
    } else if (cmd == "exit-code") {
        if (argc < 3) {
            ret = 1;
        } else {
            ret = std::stoi(ToUTF8(argv.get()[2]));
        }
    } else if (cmd == "sleep") {
        if (argc < 3) {
            ret = 1;
        } else {
            auto ms = std::stoi(ToUTF8(argv.get()[2]));
            Sleep(static_cast<DWORD>(ms));
        }
    } else if (cmd == "cwd") {
        auto cwd = std::filesystem::current_path().wstring();
        auto utf8 = ToUTF8(cwd);
        std::cout.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        std::cout.write("\r\n", 2);
    } else {
        ret = 1;
    }

    return ret;
}
