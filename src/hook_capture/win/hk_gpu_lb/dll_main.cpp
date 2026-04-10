//
// Created by RGAA on 2024-02-26.
//

#include <tc_common_new/string_util.h>

#include "easyhook/easyhook.h"
#include "tc_common_new/log.h"
#include "client_manager.h"

#define CAPTURETEX_API __declspec(dllexport)

using namespace tc;

ClientManager* client_manager = ClientManager::Instance();
extern "C" CAPTURETEX_API void __stdcall NativeInjectionEntryPoint(REMOTE_ENTRY_INFO * remote_info) {
    client_manager->CopyUserData(remote_info->UserData, (int)remote_info->UserDataSize);
    auto params = client_manager->GetInjectParams();

    auto log_path = std::string(params->host_exe_folder) + "/tc_graphics_lb.log";
    Logger::InitLog(StringUtil::ToWString(log_path), true);
    LOGI("Init graphics lb...");

    RhWakeUpProcess();
}
