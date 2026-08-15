//
// Created by RGAA on 2024-02-06.
//

#include "file_util.h"
#include "string_util.h"
#include "log.h"
#include <filesystem>
#include <format>

#ifdef WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace tc
{

    std::string FileUtil::GetFileNameFromPath(const std::string& path) {
        std::string target_path = path;
        StringUtil::Replace(target_path, R"(\\)", "/");
        StringUtil::Replace(target_path, R"(\)", "/");
        if (!target_path.empty() && target_path.back() == '/') {
            target_path.pop_back();
        }
        auto idx = target_path.rfind('/');
        if (idx == std::string::npos) {
            return target_path;
        }
        return target_path.substr(idx + 1);
    }

    std::string FileUtil::GetFileNameFromPathNoSuffix(const std::string& path) {
        auto filename = GetFileNameFromPath(path);
        auto idx = filename.rfind('.');
        return filename.substr(0, idx);
    }

    std::string FileUtil::GetFileSuffix(const std::string& path) {
        auto idx = path.rfind('.');
        return path.substr(idx+1, path.size());
    }

    std::string FileUtil::GetFileFolder(const std::string& path) {
        std::string target_path = path;
        StringUtil::Replace(target_path, R"(\\)", "/");
        StringUtil::Replace(target_path, R"(\)", "/");
        auto idx = target_path.rfind('/');
        return target_path.substr(0, idx);
    }

    std::string FileUtil::GetFolderNameFormAbsFolderPath(const std::string& path) {
        std::string target_path = path;
        StringUtil::Replace(target_path, R"(\\)", "/");
        StringUtil::Replace(target_path, R"(\)", "/");
        if (!target_path.empty() && target_path.back() == '/') {
            target_path.pop_back();
        }
        auto idx = target_path.rfind('/');
        if (idx == std::string::npos) {
            return target_path;
        }
        return target_path.substr(idx + 1);
    }

    bool FileUtil::CopyFileExt(const U8Path& from, const U8Path& to, bool force_replace) {
        try {
            if (std::filesystem::exists(to)) {
                if (force_replace) {
                    std::filesystem::remove(to);
                } else {
                    return true;
                }
            }
            std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
            return true;
        } catch (const std::filesystem::filesystem_error& ex) {
            LOGE("CopyFileExt failed: {} -> {}, {}",
                 StringUtil::ToUTF8(from.wstring()),
                 StringUtil::ToUTF8(to.wstring()),
                 ex.what());
            return false;
        }
    }

    void FileUtil::SelectFileInExplorer(const U8Path& p) {
#ifdef WIN32
        std::wstring wpath = p.wstring();
        for (auto& ch : wpath) {
            if (ch == L'/') ch = L'\\';
        }
        std::wstring args = std::format(L"/select,\"{}\"", wpath);
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
#endif
    }

    bool FileUtil::ReName(const U8Path& old_path, const U8Path& new_path) {
        try {
            if (!std::filesystem::exists(old_path)) {
                return false;
            }
            std::filesystem::rename(old_path, new_path);
            return true;
        } catch (const std::filesystem::filesystem_error& ex) {
            LOGE("ReName failed: {} -> {}, {}",
                 StringUtil::ToUTF8(old_path.wstring()),
                 StringUtil::ToUTF8(new_path.wstring()),
                 ex.what());
            return false;
        }
    }

}
