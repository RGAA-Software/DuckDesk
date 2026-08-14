//
// Created by RGAA on 2024-02-06.
//

#ifndef TC_APPLICATION_STEAM_ENTITIES_H
#define TC_APPLICATION_STEAM_ENTITIES_H

#include <sstream>
#include <memory>
#include <vector>
#include <any>

namespace tc
{

    class SteamApp {
    public:
        static std::shared_ptr<SteamApp> Make() {
            return std::make_shared<SteamApp>();
        }

    public:
        // steam app的id
        int app_id_{};
        // app name
        std::string name_;
        // installed dir
        std::string installed_dir_;
        // 目录下所有的exe，ue的在内部文件夹中
        std::vector<std::string> exes_;
        std::vector<std::string> exe_names_;
        // 查注册表能知道是否已经运行了
        bool is_running_{};
        // 查注册表能知道是否已经安装了
        bool is_installed_{};
        // 拼接的steam 快捷方式
        std::string steam_url_{};
        // cover url
        std::string cover_url_;
        // cover's name
        std::string cover_name_;
        //
        std::string engine_type_;

        std::any cover_pixmap_;

    public:

        [[nodiscard]] std::string Dump() const {
            std::stringstream ss;
            ss << "name: " << name_ << std::endl;
            ss << "app id: " << app_id_ << std::endl;
            ss << "installed dir: " << installed_dir_ << std::endl;
            ss << "exe size: " << exes_.size() << std::endl;
            ss << "cover url: " << cover_url_ << std::endl;
            ss << "exes:" << std::endl;
            for (auto& exe : exes_) {
                ss << "  - " << exe << std::endl;
            }
            ss << "exe names: " << std::endl;
            for (auto& n : exe_names_) {
                ss << "  * " << n << std::endl;
            }
            return ss.str();
        }
    };
    using SteamAppPtr = std::shared_ptr<SteamApp>;

    // 安装的应用使用信息
    class RegAppInfo{
    public:
        static std::shared_ptr<RegAppInfo> Make() {
            return std::make_shared<RegAppInfo>();
        }
    public:
        int app_id_;
        bool installed_;
        bool running_;
        // 应该是app占用的空间大小
        int app_size_;
    };
    using RegAppInfoPtr = std::shared_ptr<RegAppInfo>;

    class InstalledFolder {
    public:
        static std::shared_ptr<InstalledFolder> Make() {
            return std::make_shared<InstalledFolder>();
        }
    public:
        std::string path_;
        std::vector<RegAppInfoPtr> app_id_value_;
    };
    using InstalledFolderPtr = std::shared_ptr<InstalledFolder>;

}

#endif //TC_APPLICATION_STEAM_ENTITIES_H
