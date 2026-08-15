//
// Created by RGAA on 2024/3/17.
//

#include "app_shared_info_reader.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>

#include "px_capture_new/capture_message.h"
#include "px_common_new/folder_util.h"
#include "px_common_new/log.h"
#include "px_common_new/string_util.h"

namespace px
{

    std::shared_ptr<AppSharedInfoReader> AppSharedInfoReader::Make(const std::string& name) {
        return std::make_shared<AppSharedInfoReader>(name);
    }

    AppSharedInfoReader::AppSharedInfoReader(const std::string& name) {
        name_ = name;
    }

    std::shared_ptr<AppSharedMessage> AppSharedInfoReader::ReadData() {
        // name_ is "application_shm_{pid}" or "application_{pid}" — extract pid.
        uint32_t pid = 0;
        auto pos = name_.rfind('_');
        if (pos != std::string::npos) {
            try {
                pid = static_cast<uint32_t>(std::stoul(name_.substr(pos + 1)));
            } catch (...) {
                pid = 0;
            }
        }
        if (pid == 0) {
            pid = GetCurrentProcessId();
        }

        auto path = std::filesystem::path(FolderUtil::GetProgramDataPath())
                    / L"hook_boot"
                    / std::format(L"application_{}.bin", pid);

        if (!std::filesystem::exists(path)) {
            LOGE("Hook boot config not found: {}", StringUtil::ToUTF8(path.wstring()));
            return nullptr;
        }

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            LOGE("Open hook boot config failed: {}", StringUtil::ToUTF8(path.wstring()));
            return nullptr;
        }

        auto msg = std::make_shared<AppSharedMessage>();
        ifs.read(reinterpret_cast<char*>(msg.get()), static_cast<std::streamsize>(sizeof(AppSharedMessage)));
        if (!ifs) {
            LOGE("Read hook boot config incomplete: {}", StringUtil::ToUTF8(path.wstring()));
            return nullptr;
        }
        if (msg->self_size_ != 0 && msg->self_size_ != sizeof(AppSharedMessage)) {
            LOGW("Hook boot self_size mismatch: file={}, expect={}", msg->self_size_, sizeof(AppSharedMessage));
        }
        if (msg->ipc_port_ == 0) {
            LOGE("Hook boot config has ipc_port=0");
            return nullptr;
        }
        LOGI("Loaded hook boot config from file (WS IPC, not SHM): port={}, present={:x}",
             msg->ipc_port_, msg->dxgi_present);
        return msg;
    }

    void AppSharedInfoReader::Exit() {
    }

}
