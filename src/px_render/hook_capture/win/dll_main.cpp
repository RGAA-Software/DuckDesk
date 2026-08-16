//
// Created by RGAA on 2023-12-20.
//
// Legacy EasyHook entry (ENABLE_HOOK_CAPTURE). Frame IPC is WebSocket /ipc —
// shared-memory ClientIpcManager has been removed. Prefer OBS inject (px_game_hook).

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "easyhook/easyhook.h"
#include "px_common_new/log.h"
#include "hk_video/capture_texture.h"
#include "hk_video/hook_event.h"
#include "client_manager.h"

#define CAPTURETEX_API __declspec(dllexport)

CaptureTex g_capture_tex;
HookEvent* g_hook_event = HookEvent::Instance();
ClientManager* client_manager = ClientManager::Instance();

using namespace px;

extern "C" CAPTURETEX_API void __stdcall NativeInjectionEntryPoint(REMOTE_ENTRY_INFO* remote_info) {
    client_manager->CopyUserData(remote_info->UserData, (int)remote_info->UserDataSize);
    auto params = client_manager->GetInjectParams();

    auto log_path = std::string(params->host_exe_folder) + "/px_capture_inject.log";
    Logger::InitLog(log_path, true);
    LOGI("----------------------------------------------------");
    LOGI("Inject host  : {}", params->host_exe_folder);
    LOGI("Inject listening port  : {}", params->listening_port);
    LOGW("EasyHook path: SHM frame IPC removed; use OBS px_game_hook + WS /ipc");

    if (!g_capture_tex.Run()) {
        LOGE("g_capture_tex run  failed!");
        return;
    }

    RhWakeUpProcess();
}
