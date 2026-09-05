//
// Created by RGAA on 2024/1/17.
//

#include "steam_manager.h"
#include "px_common/log.h"
#include "px_common/http_client.h"
#include "px_common/thread.h"
#include "px_common/vdf_parser.hpp"
#include "px_common/string_util.h"
#include "px_common/file_util.h"
#include "px_common/folder_util.h"

#include <array>
#include <map>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <Windows.h>
#include <filesystem>

#include "steam_entities.h"
#include "steam_api.h"

namespace fs = std::filesystem;

namespace px {
namespace {

constexpr std::size_t kRegistryKeyBufferSize = 256;

struct RegistryKeyCloser final {
    void operator()(HKEY key) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): HKEY is an opaque Win32 handle.
        ::RegCloseKey(key);
    }
};

using RegistryKey = std::unique_ptr<std::remove_pointer_t<HKEY>, RegistryKeyCloser>;

RegistryKey OpenRegistryKey(HKEY root_key, std::wstring_view sub_key) {  // NOLINT(gammaray-raw-pointer-boundary): Win32 registry API boundary.
    HKEY opened_key{};  // NOLINT(gammaray-raw-pointer-boundary): RegOpenKeyExW transfers the key through HKEY*.
    if (::RegOpenKeyExW(root_key, sub_key.data(), 0, KEY_READ, &opened_key) != ERROR_SUCCESS) {
        return {};
    }
    return RegistryKey{opened_key};
}

}  // namespace

std::shared_ptr<SteamManager> SteamManager::Make() {
    return std::make_shared<SteamManager>();
}

void SteamManager::QueryInstalledApps() {
    const auto key = OpenRegistryKey(HKEY_CURRENT_USER, steam_app_base_path_);
    if (!key) {
        return;
    }

    std::array<TCHAR, kRegistryKeyBufferSize> sub_key_name{};
    DWORD sub_key_count{};
    FILETIME last_write_time{};
    const auto query_result = ::RegQueryInfoKey(key.get(), nullptr, nullptr, nullptr, &sub_key_count, nullptr, nullptr, nullptr, nullptr, nullptr,
                                                nullptr, &last_write_time);
    if (query_result != ERROR_SUCCESS) {
        LOGE("RegQueryInfoKey failed.");
        return;
    }

    for (DWORD index{}; index < sub_key_count; ++index) {
        DWORD name_size = static_cast<DWORD>(sub_key_name.size());
        if (::RegEnumKeyEx(key.get(), index, sub_key_name.data(), &name_size, nullptr, nullptr, nullptr, &last_write_time) == ERROR_SUCCESS) {
            auto reg_app_info = RegAppInfo::Make();
            reg_app_info->app_id_ = std::atoi(StringUtil::ToUTF8(std::wstring(sub_key_name.data())).c_str());

            const auto inner_sub_key = steam_app_base_path_ + L"\\" + std::wstring(sub_key_name.data());
            const auto inner_key = OpenRegistryKey(HKEY_CURRENT_USER, inner_sub_key);
            if (inner_key) {
                {
                    wchar_t name_value[MAX_PATH] = L"Installed";
                    DWORD data{};
                    DWORD data_size = sizeof(DWORD);
                    if (RegGetValueW(inner_key.get(), nullptr, name_value, RRF_RT_REG_DWORD, nullptr, &data, &data_size) == ERROR_SUCCESS) {
                        reg_app_info->installed_ = data;
                    }
                }

                {
                    wchar_t name_value[MAX_PATH] = L"Running";
                    DWORD data{};
                    DWORD data_size = sizeof(DWORD);
                    if (RegGetValueW(inner_key.get(), nullptr, name_value, RRF_RT_REG_DWORD, nullptr, &data, &data_size) == ERROR_SUCCESS) {
                        reg_app_info->running_ = data;
                    }
                }

                if (reg_app_info->installed_) {
                    reg_apps_info_.push_back(reg_app_info);
                }
            }
        }
    }
}

bool SteamManager::ScanInstalledGames(bool recursive_exe) {
    reg_apps_info_.clear();
    games_.clear();
    installed_folders_.clear();
    installed_steam_path_ = ScanInstalledSteamPath();
    steam_app_base_path_ = L"SOFTWARE\\Valve\\Steam\\Apps";

    QueryInstalledApps();

    ParseLibraryFolders();
    ParseConfigForEachGame(recursive_exe);
    ScanHeaderImageInAppCache();

    return true;
}

std::string SteamManager::ScanInstalledSteamPath() {
    constexpr std::wstring_view sub_key = L"SOFTWARE\\Valve\\Steam";
    constexpr std::wstring_view value_name = L"SteamPath";
    DWORD buffer_size = MAX_PATH * sizeof(wchar_t);
    wchar_t buffer[MAX_PATH]{};

    const auto key = OpenRegistryKey(HKEY_CURRENT_USER, sub_key);
    if (key) {
        if (RegGetValueW(key.get(), nullptr, value_name.data(), RRF_RT_REG_SZ, nullptr, buffer, &buffer_size) == ERROR_SUCCESS) {
            installed_steam_path_ = StringUtil::ToUTF8(buffer);
            return installed_steam_path_;
        }
    }
    return "";
}

std::vector<SteamAppPtr> SteamManager::GetInstalledGames() {
    return games_;
}

void SteamManager::DumpGamesInfo() {
    LOGI("Total games: {}", games_.size());
    for (auto& game : games_) {
        LOGI("Game: {}", game->Dump());
    }
}

void SteamManager::ParseLibraryFolders() {
    auto library_folders_path = installed_steam_path_ + "/steamapps/libraryfolders.vdf";
    std::ifstream library_folders(library_folders_path);

    if (!std::filesystem::exists(PathFromUTF8(library_folders_path))) {
        LOGE("Library folder not exist: {}", library_folders_path);
        return;
    }

    bool ok{};
    tyti::vdf::basic_object<char> objs;
    try {
        objs = tyti::vdf::read(library_folders, &ok);
    } catch (std::exception& e) {
        LOGE("Parse failed: {}, error: {}", library_folders_path, e.what());
        return;
    }
    if (!ok) {
        LOGE("Parse library folders failed.");
        return;
    }

    for (auto& c : objs.childs) {
        // LOGI("key: {}", c.first);
        auto s = c.second;
        // path
        if (s->attribs.find("path") == s->attribs.end()) {
            continue;
        }
        std::string path = s->attribs["path"];

        //
        StringUtil::Replace(path, "\\", "/");

        auto installed_folder = InstalledFolder::Make();
        installed_folder->path_ = path;

        // 读取这个地方不准，把注册表里的都拿出来，如果安装在不同磁盘，每个磁盘都把所有的app文件夹都检测一次
        // 最后只保留检测到的就可以
        installed_folder->app_id_value_.insert(installed_folder->app_id_value_.begin(), reg_apps_info_.begin(), reg_apps_info_.end());
        installed_folders_.push_back(installed_folder);
    }
}

void SteamManager::ParseConfigForEachGame(bool recursive_exe) {
    for (const auto& folder : installed_folders_) {
        if (folder->path_.empty() || folder->app_id_value_.empty()) {
            continue;
        }

        auto steam_apps_path = folder->path_ + "/steamapps";
        for (const auto& app : folder->app_id_value_) {
            auto app_cfg = steam_apps_path + "/appmanifest_" + std::to_string(app->app_id_) + ".acf";
            auto u8_cfg_path = PathFromUTF8(app_cfg);
            if (!std::filesystem::exists(u8_cfg_path)) {
                // LOGE("Path not exist: {}", app_cfg);
                continue;
            }

            std::ifstream app_cfg_path(app_cfg);
            bool ok{};
            tyti::vdf::basic_object<char> objs;
            try {
                objs = tyti::vdf::read(app_cfg_path, &ok);
            } catch (std::exception& e) {
                LOGE("Read file failed: {}, error: {}", app_cfg, e.what());
                continue;
            }
            if (!ok) {
                LOGE("Parse library folders failed.");
                return;
            }

            if (objs.attribs.find("installdir") == objs.attribs.end()) {
                continue;
            }
            if (objs.attribs.find("name") == objs.attribs.end()) {
                continue;
            }

            std::string name = objs.attribs["name"];
            StringUtil::ToLower(name);
            if (NameFilter(name)) {
                continue;
            }

            auto installed_dir_name = objs.attribs["installdir"];
            auto installed_dir = steam_apps_path;
            installed_dir.append("/common/").append(installed_dir_name);

            auto steam_app = SteamApp::Make();
            steam_app->app_id_ = app->app_id_;
            steam_app->name_ = objs.attribs["name"];
            steam_app->installed_dir_ = installed_dir;
            steam_app->steam_url_ = std::format("steam://rungameid/{}", app->app_id_);

            EstimateEngine(steam_app, recursive_exe);
            // LOGI("game: {}", steam_app.Dump());
            games_.push_back(steam_app);
        }
    }
    // LOGI("---Parse completed....");
}

void SteamManager::ScanHeaderImageInAppCache() {
    std::string library_cache_path = installed_steam_path_ + "/appcache/librarycache";
    if (!std::filesystem::exists(std::filesystem::path(StringUtil::ToWString(library_cache_path)))) {
        LOGE("Cache not exist:{}", library_cache_path);
        return;
    }

    std::vector<std::string> cached_file_names;
    for (const auto& entry : fs::directory_iterator(library_cache_path)) {
        const auto& path = entry.path();
        if (!entry.is_regular_file()) {
            continue;
        }
        cached_file_names.push_back(StringUtil::ToUTF8(path.wstring()));
    }

    for (auto& game : games_) {
        std::string file_prefix = std::format("{}_library_600", game->app_id_);
        // 2389120_library_header
        std::string file_prefix_2 = std::format("{}_library_header", game->app_id_).c_str();
        for (auto& file_name : cached_file_names) {
            if (file_name.find(file_prefix) != std::string::npos) {
                game->cover_url_ = file_name;
                game->cover_name_ = FileUtil::GetFileNameFromPath(file_name);
                break;
            }
            if (file_name.find(file_prefix_2) != std::string::npos) {
                game->cover_url_ = file_name;
                game->cover_name_ = FileUtil::GetFileNameFromPath(file_name);
                break;
            }
        }
    }
}

// TODO:BACKGROUND
void SteamManager::UpdateAppDetails() {
    for (auto& game : games_) {
        auto path = kApiAppDetails + "?appids=" + std::to_string(game->app_id_);
        auto client = HttpClient::MakeSSL(kApiBase, 443, path);
        auto resp = client->Request({});
        LOGI("resp : {} {}", resp.status, resp.body);
    }
}

std::string SteamManager::GetSteamInstalledPath() {
    return installed_steam_path_;
}

std::string SteamManager::GetSteamImageCachePath() {
    return installed_steam_path_ + "/appcache/librarycache";
}

std::string SteamManager::GetSteamExePath() {
    return installed_steam_path_ + "/steam.exe";
}

std::string SteamManager::EstimateEngine(const std::shared_ptr<SteamApp>& app, bool recursive) {
    std::vector<std::string> lower_case_exe_names;
    std::vector<std::string> lower_case_folder_names;
    std::vector<VisitResult> file_results;
    std::vector<VisitResult> folder_results;

    // LOGI(" - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - recursive: {}", recursive);
    FolderUtil::VisitFiles(
        PathFromUTF8(app->installed_dir_),
        [&](VisitResult&& r) {
            auto lc_name = StringUtil::ToUTF8(r.name_);
            StringUtil::ToLower(lc_name);
            // 不被过滤的才会留下
            if (!ExeFilter(lc_name)) {
                lower_case_exe_names.push_back(lc_name);
                file_results.push_back(r);
            }
        },
        "exe");

    FolderUtil::VisitFolders(PathFromUTF8(app->installed_dir_), [&](VisitResult&& r) {
        auto lc_name = StringUtil::ToUTF8(r.name_);
        StringUtil::ToLower(lc_name);
        lower_case_folder_names.push_back(lc_name);
        folder_results.push_back(r);
    });

    bool is_unity = false;
    // 1. if has unitycrashhandler or not
    for (auto& n : lower_case_exe_names) {
        if (n.find("unity") != std::string::npos) {
            is_unity = true;
            LOGI("=====> UNITY 1");
        }
    }

    // 2. check folder name
    if (!is_unity) {
        for (auto& n : lower_case_exe_names) {
            auto filename = FileUtil::GetFileNameFromPathNoSuffix(n);
            // LOGI("filename: {}", filename);
            auto target_folder = filename + "_data";
            for (auto& f : lower_case_folder_names) {
                if (f == target_folder) {
                    is_unity = true;
                    // LOGI("=====> UNITY 2");
                }
            }
        }
    }

    if (is_unity) {
        // LOGI("Finally , this is a unity game.");
        for (auto& r : file_results) {
            app->exe_names_.push_back(StringUtil::ToUTF8(r.name_));
            app->exes_.push_back(StringUtil::ToUTF8(r.path_));
        }
        app->engine_type_ = "UNITY";
        return app->engine_type_;
    }

    // 3. match folder and exe name, see if ue or not
    bool has_engine_folder = false;
    bool has_same_name_folder = false;
    std::string target_lowercase_folder_name;
    for (auto& n : lower_case_exe_names) {
        for (auto& f : lower_case_folder_names) {
            if (f == "engine") {
                has_engine_folder = true;
            }
            auto filename = FileUtil::GetFileNameFromPathNoSuffix(n);
            if (filename == f) {
                has_same_name_folder = true;
                target_lowercase_folder_name = f;
            }
        }
    }

    bool is_ue = false;
    if (has_engine_folder && has_same_name_folder) {
        LOGI("It seems that this is a ue game, but we need another check.");
        for (auto& r : folder_results) {
            auto lowercase_folder_name = StringUtil::ToLowerCpy(StringUtil::ToUTF8(r.name_));
            if (lowercase_folder_name == target_lowercase_folder_name) {
                LOGI("find the game folder: {}", StringUtil::ToUTF8(r.path_));
                auto target_exe_folder = StringUtil::ToUTF8(r.path_ + L"/Binaries/Win64");
                FolderUtil::VisitFiles(
                    PathFromUTF8(target_exe_folder),
                    [&](VisitResult&& r) {
                        LOGI("Visit target folder: {}", StringUtil::ToUTF8(r.path_));
                        auto target_exe_name = StringUtil::ToUTF8(r.name_);
                        auto target_exe = StringUtil::ToUTF8(r.path_);
                        if (std::find(app->exe_names_.begin(), app->exe_names_.end(), target_exe_name) == app->exe_names_.end()) {
                            app->exe_names_.push_back(target_exe_name);
                        }
                        if (std::find(app->exes_.begin(), app->exes_.end(), target_exe) == app->exes_.end()) {
                            app->exes_.push_back(target_exe);
                        }
                        is_ue = true;
                    },
                    "exe");
            }
        }
    }
    if (is_ue) {
        app->engine_type_ = "UE";
        return app->engine_type_;
    }

    // LOGI("=====> file results: {}, app name: {}", file_results.size(), app->name_);
    //  we don't know the engine of the game, so to find exes and will close them when stream is closed
    if (recursive) {
        FolderUtil::VisitRecursiveFiles(
            std::filesystem::path(app->installed_dir_), 0, 3,
            [&](VisitResult&& r) {
                auto path = StringUtil::ToUTF8(r.path_);
                StringUtil::Replace(path, "\\", "/");
                // LOGI("// path: {}", path);
                bool already_exist = false;
                for (const auto& file_result : file_results) {
                    auto file_path = StringUtil::ToUTF8(file_result.path_);
                    StringUtil::Replace(file_path, "\\", "/");
                    if (file_path == path) {
                        // LOGI("Already exist: {}", file_path);
                        already_exist = true;
                    }
                }
                if (!already_exist) {
                    // LOGI("Not exist, add to file results");
                    file_results.push_back(r);
                }
            },
            "exe");
    }

    app->engine_type_ = "UNKNOWN";
    for (auto& r : file_results) {
        app->exe_names_.push_back(StringUtil::ToUTF8(r.name_));
        app->exes_.push_back(StringUtil::ToUTF8(r.path_));
    }

    return "UNKNOWN";
}

bool SteamManager::ExeFilter(const std::string& lowercase_exe_name) {
    static std::vector<std::string> names = {"crashhandler", "ffmpeg",   "ffprobe",     "qtwebengine", "vc_redist",
                                             "uninstall",    "crashpad", "crashdialog", "crashsender", "unitycrashhandler"};
    for (auto& n : names) {
        auto ln = StringUtil::ToLowerCpy(n);
        if (lowercase_exe_name.find(ln) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool SteamManager::NameFilter(const std::string& lowercase_name) {
    static std::vector<std::string> names = {"steamworks"};
    for (auto& n : names) {
        if (lowercase_name.find(n) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool SteamManager::IsSteamPath(const std::string& path) {
    std::string prefix = "steam://rungameid/";
    return path.starts_with(prefix);
}

std::string SteamManager::ParseSteamIdFromPath(const std::string& game_path) {
    std::string prefix = "steam://rungameid/";
    if (!game_path.starts_with(prefix)) {
        return "";
    }
    return game_path.substr(prefix.size(), game_path.size());
}

void SteamManager::RescanRecursively() {
    for (auto& game : games_) {
        EstimateEngine(game, true);
    }
}

} // namespace px
