//
// Created by RGAA on 2024/3/17.
//

#ifndef TC_APPLICATION_APP_SHARED_INFO_H
#define TC_APPLICATION_APP_SHARED_INFO_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "tc_capture_new/capture_message.h"

namespace tc
{

    class RdContext;

    // Writes hook bootstrap config for the injected DLL (ipc port + DXGI offsets).
    // Uses a small file under Public\GoDesk\hook_boot\ — NOT shared memory.
    // Ongoing frame IPC is plain WebSocket /ipc.
    class AppSharedInfo {
    public:
        static std::shared_ptr<AppSharedInfo> Make(const std::shared_ptr<RdContext>& ctx);
        static std::filesystem::path BootConfigPath(uint32_t pid);

        explicit AppSharedInfo(const std::shared_ptr<RdContext>& ctx);
        // Compatible entry: shm_name like "application_shm_{pid}" → file bootstrap.
        void WriteData(const std::string& shm_name, const std::string& data);
        bool WriteBootConfig(uint32_t pid, const std::string& data);
        void Exit();

    private:
        std::shared_ptr<RdContext> context_ = nullptr;
    };

}

#endif //TC_APPLICATION_APP_SHARED_INFO_H
