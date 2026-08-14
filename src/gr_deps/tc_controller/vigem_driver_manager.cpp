//
// Created by RGAA on 2024/3/23.
//

#include "vigem_driver_manager.h"

#include "tc_common_new/hardware.h"
#include "tc_common_new/log.h"
#include "tc_common_new/process_util.h"
#include "tc_common_new/string_util.h"
#include "vigem/sdk/ViGEm/Client.h"

namespace tc
{

    std::shared_ptr<VigemDriverManager> VigemDriverManager::Make() {
        return std::make_shared<VigemDriverManager>();
    }

    VigemDriverManager::VigemDriverManager() {
        hardware_ = std::make_shared<Hardware>();
    }

    bool VigemDriverManager::Detect() {
        auto result = hardware_->Detect(false, false, true);
        std::vector<SysDriver> drivers = hardware_->GetDrivers();
        LOGI("driver size: {}", drivers.size());
        if (drivers.empty()) {
            return false;
        }
        for (auto& driver : drivers) {
            if (driver.name_ == "ViGEmBus") {
                LOGI("find VigemBus, name: {}, display name: {}, state: {}", driver.name_, driver.display_name_, driver.state_);
                if (driver.state_ == "Running") {
                    is_vigem_driver_installed_ = true;
                }
            }
            if (driver.name_ == "vjoy") {
                LOGI("find vjoy, name: {}, display name: {}", driver.name_, driver.display_name_);
                if (driver.state_ == "Running") {
                    is_vjoy_driver_installed_ = true;
                }
            }
        }
        return true;
    }

    bool VigemDriverManager::TryConnect() {
        auto client = vigem_alloc();
        bool connect_result = false;
        if (!client) {
            LOGE("Try alloc vigem failed!!!");
        } else {
            const auto ret = vigem_connect(client);
            if (!VIGEM_SUCCESS(ret)) {
                //LOGE("Connect failed !!!!");
            } else {
                vigem_disconnect(client);
                connect_result = true;
            }
        }
        if (client) {
            vigem_free(client);
        }
        return connect_result;
    }

    void VigemDriverManager::InstallViGem() {
#if 0
        auto exe_folder_path = boost::filesystem::initial_path<boost::filesystem::path>().string();
        StringUtil::Replace(exe_folder_path, R"(\)", R"(/)");
        auto work_dir = std::format("{}/driver/vigem", exe_folder_path);
        // 1. remove
        LOGI("Vigem remove");
        auto cmd_remove = std::format(R"({}/driver/vigem/nefconw.exe --remove-device-node --hardware-id Nefarius\ViGEmBus\Gen1 --class-guid 4D36E97D-E325-11CE-BFC1-08002BE10318)", exe_folder_path);
        if (!ProcessUtil::StartProcessInWorkDir(work_dir, cmd_remove, {})) {
            LOGE("Remove device failed.");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        LOGI("Vigem remove old");
        cmd_remove = std::format(R"({}/driver/vigem/nefconw.exe --remove-device-node --hardware-id Root\ViGEmBus --class-guid 4D36E97D-E325-11CE-BFC1-08002BE10318)", exe_folder_path);
        if (!ProcessUtil::StartProcessInWorkDir(work_dir, cmd_remove, {})) {
            LOGE("Remove device failed.");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        LOGI("Vigem remove driver");
        cmd_remove = std::format(R"({}/driver/vigem/nefconw.exe --uninstall-driver --inf-path ViGEmBus.inf)", exe_folder_path);
        if (!ProcessUtil::StartProcessInWorkDir(work_dir, cmd_remove, {})) {
            LOGE("Remove device failed.");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 2. create
        LOGI("Vigem create device");
        auto cmd_create = std::format(R"({}/driver/vigem/nefconw.exe --create-device-node --hardware-id Nefarius\ViGEmBus\Gen1 --class-name System --class-guid 4D36E97D-E325-11CE-BFC1-08002BE10318)", exe_folder_path);
        if (!ProcessUtil::StartProcessInWorkDir(work_dir, cmd_create, {})) {
            LOGE("Create device node failed.");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 3. install
        LOGI("Vigem install");
        auto cmd_install = std::format(R"({}/driver/vigem/nefconw.exe --install-driver --inf-path ViGEmBus.inf)", exe_folder_path);
        if (!ProcessUtil::StartProcessInWorkDir(work_dir, cmd_install, {})) {
            LOGE("Install driver failed.");
        }
#endif
    }

}