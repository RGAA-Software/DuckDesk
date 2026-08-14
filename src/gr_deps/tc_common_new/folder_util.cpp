//
// Created by RGAA on 2024/3/29.
//

#include "folder_util.h"
#include <filesystem>
#include <iostream>
#include <format>
#include "log.h"
#include "file_util.h"
#include "string_util.h"

#ifdef WIN32
#include <windows.h>
#include <shlobj.h>
#include <Shlwapi.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace tc
{

    void FolderUtil::VisitFiles(const U8Path& path, std::function<void(VisitResult&&)>&& cbk, const std::string& filter_suffix) {
        if (fs::is_directory(path)) {
            for (const auto& entry : fs::directory_iterator(path)) {
                auto entry_path = entry.path();
                if (entry.is_regular_file()) {
                    if (!filter_suffix.empty()) {
                        auto u8path = StringUtil::ToUTF8(entry_path.wstring());
                        auto suffix = FileUtil::GetFileSuffix(u8path);
                        StringUtil::ToLower(suffix);
                        if (filter_suffix != suffix) {
                            continue;
                        }
                    }
                    VisitResult result{
                        .name_ = entry_path.filename().wstring(),
                        .path_ = entry_path.wstring(),
                    };
                    cbk(std::move(result));
                }
            }
        }
    }

    void FolderUtil::VisitFolders(const U8Path& path, std::function<void(VisitResult&&)>&& cbk, const std::string& filter_suffix) {
        if (fs::is_directory(path)) {
            for (const auto& entry : fs::directory_iterator(path)) {
                auto entry_path = entry.path();
                if (entry.is_directory()) {
                    if (!filter_suffix.empty()) {
                        auto u8path = StringUtil::ToUTF8(entry_path.wstring());
                        auto suffix = FileUtil::GetFileSuffix(u8path);
                        StringUtil::ToLower(suffix);
                        if (filter_suffix != suffix) {
                            continue;
                        }
                    }
                    VisitResult result{
                            .name_ = entry_path.filename().wstring(),
                            .path_ = entry_path.wstring(),
                    };
                    cbk(std::move(result));
                }
            }
        }
    }

    void FolderUtil::VisitAll(const U8Path& path, std::function<void(VisitResult&&)>&& cbk, const std::string& filter_suffix) {
        if (fs::is_directory(path)) {
            for (const auto& entry : fs::directory_iterator(path)) {
                auto entry_path = entry.path();
                if (!filter_suffix.empty()) {
                    auto u8path = StringUtil::ToUTF8(entry_path.wstring());
                    auto suffix = FileUtil::GetFileSuffix(u8path);
                    StringUtil::ToLower(suffix);
                    if (filter_suffix != suffix) {
                        continue;
                    }
                }
                VisitResult result{
                        .name_ = entry_path.filename().wstring(),
                        .path_ = entry_path.wstring(),
                };
                cbk(std::move(result));
            }
        }
    }

    void FolderUtil::VisitRecursiveFiles(const U8Path& path, int depth, int max_depth, const std::function<void(VisitResult&&)>& cbk, const std::string& filter_suffix) {
        // 如果达到最大深度，停止递归
        if (depth > max_depth) return;

        // 检查路径是否存在以及是否是目录
        if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
            // 使用迭代器遍历当前目录
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                // 获取目录项的路径
                const auto& p = entry.path();
                // 输出目录项路径
                //std::cout << std::string(depth * 2, ' ') << "|-- " << p.filename() << '\n';
                if (fs::is_regular_file(p)) {
                    if (!filter_suffix.empty()) {
                        auto u8path = StringUtil::ToUTF8(p.wstring());
                        auto suffix = StringUtil::ToLowerCpy(FileUtil::GetFileSuffix(u8path));
                        if (suffix == filter_suffix) {
                            cbk(VisitResult {
                                .name_ = p.filename().wstring(),
                                .path_ = p.wstring(),
                            });
                        }
                    } else {
                        cbk(VisitResult {
                            .name_ = p.filename().wstring(),
                            .path_ = p.wstring(),
                        });
                    }
                }
                // 如果是目录，递归遍历
                if (fs::is_directory(p)) {
                    VisitRecursiveFiles(p, depth + 1, max_depth, cbk, filter_suffix);
                }
            }
        }
    }

#ifdef WIN32
    void FolderUtil::VisitAllByQt(const U8Path& path, std::function<void(VisitResult&&)>&& cbk, const std::string& filter_suffix) {
        if (!fs::is_directory(path)) {
            return;
        }
        for (const auto& entry : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto p = entry.path();
            if (!filter_suffix.empty()) {
                auto u8path = StringUtil::ToUTF8(p.wstring());
                auto suffix = FileUtil::GetFileSuffix(u8path);
                StringUtil::ToLower(suffix);
                if (filter_suffix != suffix) {
                    continue;
                }
            }
            cbk(VisitResult{
                .name_ = p.filename().wstring(),
                .path_ = p.wstring(),
            });
        }
    }

    std::wstring FolderUtil::GetCurrentFilePath() {
        wchar_t path[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return {path};
    }

    std::wstring FolderUtil::GetCurrentFolderPath() {
        constexpr int maxPath = 4096;
        wchar_t szFullPath[maxPath] = { 0 };
        ::GetModuleFileNameW(nullptr, szFullPath, maxPath);
        ::PathRemoveFileSpecW(szFullPath);
        return {szFullPath};
    }

    void FolderUtil::CreateDir(const U8Path& path) {
        try {
            if (!fs::exists(path)) {
                if (!fs::create_directories(path)) {
                    LOGE("Create folder failed: {}", StringUtil::ToUTF8(path.wstring()));
                }
            }
        } catch (const fs::filesystem_error& ex) {
            LOGE("Create folder failed: {}, {}", StringUtil::ToUTF8(path.wstring()), ex.what());
        }
    }

    void FolderUtil::OpenDir(const U8Path& path) {
        std::wstring wpath = path.wstring();
        std::wstring args = std::format(L"\"{}\"", wpath);
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }

#endif

    std::wstring FolderUtil::GetProgramDataPath(const std::string& app) {
#ifdef WIN32
        PWSTR publicPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Public, 0, nullptr, &publicPath))) {
            std::filesystem::path app_path = std::filesystem::path(publicPath) / app;
            CoTaskMemFree(publicPath);
            try {
                if (!fs::exists(app_path)) {
                    if (!fs::create_directories(app_path)) {
                        LOGE("Create folder failed: {}", StringUtil::ToUTF8(app_path.wstring()));
                    }
                }
            } catch (const fs::filesystem_error& ex) {
                LOGE("Create folder failed: {}, {}", StringUtil::ToUTF8(app_path.wstring()), ex.what());
            }
            return app_path.wstring();
        }
        return L"";
#else
        return L"";
#endif
    }

    bool FolderUtil::DeleteDir(const U8Path& path) {
        try {
            std::error_code ec;
            auto removed = fs::remove_all(path, ec);
            (void)removed;
            if (ec) {
                LOGE("DeleteDir failed: {}, {}", StringUtil::ToUTF8(path.wstring()), ec.message());
                return false;
            }
            return true;
        } catch (const std::exception& ex) {
            LOGE("DeleteDir failed: {}, {}", StringUtil::ToUTF8(path.wstring()), ex.what());
            return false;
        }
    }

    bool FolderUtil::CopyDir(const U8Path& source, const U8Path& destination, const std::vector<std::string>& ignore_suffix, bool overwrite) {
        return CopyDir(source, destination, [&](const std::string& path, const std::string& filename) -> bool {
            auto suffix = FileUtil::GetFileSuffix(filename);
            suffix = StringUtil::ToLowerCpy(suffix);
            bool need_ignore_it = false;
            for (const auto& sf : ignore_suffix) {
                auto cmp = sf;
                if (!cmp.empty() && cmp.front() == '.') {
                    cmp = cmp.substr(1);
                }
                if (suffix.find(cmp) != std::string::npos) {
                    need_ignore_it = true;
                    break;
                }
            }
            return need_ignore_it;
        }, overwrite);
    }

    bool FolderUtil::CopyDir(const U8Path& source,
                             const U8Path& destination,
                             std::function<bool(const std::string& path, const std::string& filename)>&& ignore_predicate,
                             bool overwrite) {
        try {
            if (!fs::exists(source) || !fs::is_directory(source)) {
                LOGE("Source directory does not exist or is not a directory: {}", StringUtil::ToUTF8(source.wstring()));
                return false;
            }

            if (!fs::exists(destination)) {
                fs::create_directories(destination);
                LOGI("Created destination directory: {}", StringUtil::ToUTF8(destination.wstring()));
            }

            for (const auto& entry : fs::recursive_directory_iterator(
                    source,
                    fs::directory_options::skip_permission_denied)) {

                try {
                    auto relative_path = fs::relative(entry.path(), source);
                    auto target_path = destination.path() / relative_path;

                    if (fs::is_directory(entry.status())) {
                        fs::create_directories(target_path);
                    }
                    else if (fs::is_regular_file(entry.status())) {
                        auto path = entry.path().string();
                        auto filename = entry.path().filename().string();
                        auto need_ignore_it = ignore_predicate(path, filename);
                        if (need_ignore_it) {
                            LOGI("CopyDir, ignoring the file: {}", filename);
                            continue;
                        }

                        if (fs::exists(target_path)) {
                            if (overwrite) {
                                fs::remove(target_path);
                            }
                            else {
                                LOGI("Skipped (already exists): {}", relative_path.string());
                                continue;
                            }
                        }

                        fs::copy_file(entry.path(), target_path, fs::copy_options::overwrite_existing);
                    }
                }
                catch (const fs::filesystem_error& ex) {
                    auto filename = entry.path().filename().string();
                    LOGE("Error processing :{} -> {}", filename, ex.what());
                    continue;
                }
            }

            LOGI("Directory copy completed: {} -> {}", StringUtil::ToUTF8(source.wstring()), StringUtil::ToUTF8(destination.wstring()));
            return true;

        }
        catch (const fs::filesystem_error& ex) {
            LOGE("Filesystem error: {}", ex.what());
            return false;
        }
        catch (const std::exception& ex) {
            LOGE("Error: {} ", ex.what());
            return false;
        }
    }

}
