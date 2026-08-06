//
// Created by RGAA on 2024/3/17.
//
// Boot config for the injected capture DLL. Intentionally NOT shared-memory:
// frame IPC uses plain WebSocket (/ipc). This file only carries port + DXGI
// offsets so the DLL knows where to connect.

#include "app_shared_info.h"

#include <filesystem>
#include <fstream>

#include "tc_common_new/folder_util.h"
#include "tc_common_new/log.h"
#include "tc_common_new/string_util.h"

namespace tc
{
    namespace {

        std::filesystem::path HookBootDir() {
            return std::filesystem::path(FolderUtil::GetProgramDataPath()) / L"hook_boot";
        }

    } // namespace

    std::filesystem::path AppSharedInfo::BootConfigPath(uint32_t pid) {
        return HookBootDir() / std::format(L"application_{}.bin", pid);
    }

    std::shared_ptr<AppSharedInfo> AppSharedInfo::Make(const std::shared_ptr<RdContext>& ctx) {
        return std::make_shared<AppSharedInfo>(ctx);
    }

    AppSharedInfo::AppSharedInfo(const std::shared_ptr<RdContext>& ctx) {
        context_ = ctx;
    }

    void AppSharedInfo::WriteData(const std::string& shm_name, const std::string& data) {
        // shm_name kept for call-site compatibility; expected form: application_shm_{pid}
        uint32_t pid = 0;
        auto pos = shm_name.rfind('_');
        if (pos != std::string::npos) {
            try {
                pid = static_cast<uint32_t>(std::stoul(shm_name.substr(pos + 1)));
            } catch (...) {
                pid = 0;
            }
        }
        if (pid == 0) {
            LOGE("Write hook boot config failed: cannot parse pid from {}", shm_name);
            return;
        }
        if (!WriteBootConfig(pid, data)) {
            LOGE("Write hook boot config failed for pid {}", pid);
        }
    }

    bool AppSharedInfo::WriteBootConfig(uint32_t pid, const std::string& data) {
        try {
            auto dir = HookBootDir();
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            auto path = BootConfigPath(pid);
            std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
            if (!ofs) {
                LOGE("Open hook boot file failed: {}", StringUtil::ToUTF8(path.wstring()));
                return false;
            }
            ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
            ofs.close();
            if (!ofs) {
                LOGE("Write hook boot file failed: {}", StringUtil::ToUTF8(path.wstring()));
                return false;
            }
            LOGI("Wrote hook boot config (WS IPC bootstrap, not SHM): {} ({} bytes)",
                 StringUtil::ToUTF8(path.wstring()), data.size());
            return true;
        } catch (const std::exception& ex) {
            LOGE("WriteBootConfig exception: {}", ex.what());
            return false;
        }
    }

    void AppSharedInfo::Exit() {
        // Leave boot files for the injected DLL; cleaned up on next write/overwrite.
    }

}
