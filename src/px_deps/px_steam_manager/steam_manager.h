//
// Created by RGAA on 2024/1/17.
//

#ifndef TC_SERVER_STEAM_STEAM_MANAGER_H
#define TC_SERVER_STEAM_STEAM_MANAGER_H

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace px
{
    class SteamApp;
    class InstalledFolder;
    class RegAppInfo;

    class SteamManager {
    public:

        static std::shared_ptr<SteamManager> Make();

        SteamManager() = default;
        ~SteamManager() = default;

        bool ScanInstalledGames(bool recursive_exe);
        std::vector<std::shared_ptr<SteamApp>> GetInstalledGames();
        void DumpGamesInfo();
        void UpdateAppDetails();
        std::string GetSteamInstalledPath();
        std::string GetSteamImageCachePath();
        std::string GetSteamExePath();
        std::string ScanInstalledSteamPath();
        void RescanRecursively();
        // steam://xx/xxx
        static bool IsSteamPath(const std::string& path);
        // parse steam id from steam://xxxx/xxx
        static std::string ParseSteamIdFromPath(const std::string& game_path);

    private:
        void QueryInstalledApps();
        void ParseLibraryFolders();
        void ParseConfigForEachGame(bool recursive_exe);
        void ScanHeaderImageInAppCache();
        std::string EstimateEngine(const std::shared_ptr<SteamApp>& app, bool recursive_exe);
        // 过滤不要的exe
        bool ExeFilter(const std::string& lowercase_exe_name);
        // 过滤不要的游戏名字
        bool NameFilter(const std::string& lowercase_name);

    private:
        std::string installed_steam_path_;
        std::wstring steam_app_base_path_;
        std::vector<std::shared_ptr<RegAppInfo>> reg_apps_info_;
        std::vector<std::shared_ptr<SteamApp>> games_;
        std::vector<std::shared_ptr<InstalledFolder>> installed_folders_;

    };

}


#endif
