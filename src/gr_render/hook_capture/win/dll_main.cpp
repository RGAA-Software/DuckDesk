//
// Created by RGAA on 2023-12-20.
//

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "easyhook/easyhook.h"
#include "tc_common_new/log.h"
#include "hk_video/capture_texture.h"
#include "hk_video/hook_event.h"
#include "client_ipc_manager.h"
#include "client_manager.h"

//#ifdef CAPTURETEX_EXPORTS
#define CAPTURETEX_API __declspec(dllexport)
//#else
//#define CAPTURETEX_API __declspec(dllimport)
//#endif

CaptureTex g_capture_tex;
HookEvent* g_hook_event = HookEvent::Instance();
ClientIpcManager* ipc_manager = ClientIpcManager::Instance();
ClientManager* client_manager = ClientManager::Instance();

using namespace tc;

extern "C" CAPTURETEX_API void __stdcall NativeInjectionEntryPoint(REMOTE_ENTRY_INFO * remote_info) {
    client_manager->CopyUserData(remote_info->UserData, (int)remote_info->UserDataSize);
    auto params = client_manager->GetInjectParams();

    auto log_path = std::string(params->host_exe_folder) + "/tc_capture_inject.log";
    Logger::InitLog(log_path, true);
    LOGI("----------------------------------------------------");
    LOGI("Inject host  : {}", params->host_exe_folder);
    LOGI("Inject listening port  : {}", params->listening_port);
    LOGI("Inject client to host buffer size: {}", params->shm_client_to_host_buffer_size);
    LOGI("Inject send video by shm: {}", params->send_video_frame_by_shm);

    ipc_manager->Init(params->listening_port, params->shm_client_to_host_buffer_size);
    ipc_manager->WaitForMessage();
    ipc_manager->MockSend();

//    for (int i = 0; i < 100; i++) {
//        LOGI("----------------------------------------------------");
//    }
//    auto log_suffix_path = "rgaa_injector_" + std::to_string(remote_info->HostPID) + ".log";
//    std::string log_file = log_suffix_path;
//
//    if (has_user_data) {
//        log_file = std::string(inject_data.host_exe_folder) + "/logs/" + log_suffix_path;
//    }
//
//    tc::Logger::Init(log_file, true);
//    auto conn = tc::InterCommClient::Instance();
//    conn->Init(remote_info->HostPID);
//
//    LOG_INFO("Injected DLL : %d", remote_info->HostPID);
//    LOG_INFO("UserData : %s", log_file.c_str());
//    LOG_INFO("Enable Gpu router ? : %d", inject_data.enable_gpu_router);
//    LOG_INFO("Enable Audio capture ? : %d", inject_data.enable_hook_audio);
//
//    g_hook_event->Init();
//
//    if (inject_data.enable_hook_audio) {
//        g_hook_core_audio->Init();
//    }
//
//    if (inject_data.enable_gpu_router) {
//        g_hook_gpu->Init(inject_data.gpu_router_target_lid);
//    }
//
    if (!g_capture_tex.Run()) {
        LOGE("g_capture_tex run  failed!");
        return;
    }
//
//    g_hook_event->ReceiveIPCMessages();

    RhWakeUpProcess();
}
