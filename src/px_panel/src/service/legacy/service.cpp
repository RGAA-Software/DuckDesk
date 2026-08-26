//
// Created by RGAA on 21/10/2024.
//
//
// Deprecated:
// This C++ Windows service runtime is no longer built by CMake.
// The active service runtime lives in `rust_client/px_service`.

#include "service.h"
#include "service_context.h"
#include "px_common_new/log.h"
#include "service_msg_server.h"
#include "render_manager.h"
#include "px_common_new/win32/process_helper.h"

static HMODULE sasLibrary = nullptr;
typedef void(__stdcall* SendSAS_proto)(BOOL AsUser);
static SendSAS_proto _SendSAS = nullptr;

namespace px
{

    PxService::PxService(const std::shared_ptr<ServiceContext>& ctx) {
        context_ = ctx;
        render_manager_ = RenderManager::Make(ctx);
    }

    void PxService::Run(DWORD dwArgc, LPWSTR* lpszArgv, SERVICE_STATUS_HANDLE handle) {
        msg_server_ = std::make_shared<ServiceMsgServer>(context_, render_manager_);
        msg_server_->Init(shared_from_this());
        msg_server_->Start();

        DWORD Status = E_FAIL;
        status_handle_ = handle;

        ZeroMemory(&service_status_, sizeof(service_status_));
        service_status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        //service_status_.dwControlsAccepted = 0;
        //service_status_.dwCurrentState = SERVICE_START_PENDING;
        //service_status_.dwWin32ExitCode = 0;
        service_status_.dwServiceSpecificExitCode = 0;
        //service_status_.dwCheckPoint = 0;

        //if (SetServiceStatus(status_handle_, &service_status_) == FALSE) {
        //	RLogE("SetServiceStatus failed:{}", GetLastError());
        //	return;
        //}

        // It's important to set accepted control commands
        service_status_.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE | SERVICE_ACCEPT_POWEREVENT
                                             | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE;
        service_status_.dwCurrentState = SERVICE_RUNNING;
        service_status_.dwWin32ExitCode = 0;
        service_status_.dwCheckPoint = 0;
        if (SetServiceStatus(status_handle_, &service_status_) == FALSE) {
            LOGE("dl win Service: ServiceMain: SetServiceStatus failed: {}", GetLastError());
            return;
        }
        //Init();

        LOGI("service already init...");

        task_thread_ = std::thread(std::bind(&PxService::TaskThread, this));
        task_thread_.join();

        service_status_.dwControlsAccepted = 0;
        service_status_.dwCurrentState = SERVICE_STOPPED;
        service_status_.dwWin32ExitCode = 0;
        service_status_.dwCheckPoint = 3;

        if (SetServiceStatus(status_handle_, &service_status_) == FALSE) {
            LOGE("SetServiceStatus to Stop failed: {}", GetLastError());
        }
    }

    void PxService::OnCtrlStop() {
        //UnInit();
        this->SetStatus(SERVICE_STOP_PENDING);
        exit_ = true;
        cv_.notify_all();

        this->StopAll();
    }

    void PxService::OnCtrlContinue() {
        SetStatus(SERVICE_RUNNING);
    }

    void PxService::OnCtrlPause() {

    }

    void PxService::OnCtrlInterrogate() {

    }

    void PxService::OnConsoleConnect(int id) {
        context_->PostBgTask([=, this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            render_manager_->StopDesktopRender();
        });
    }

    void PxService::OnConsoleDisConnect(int id) {

    }

    //
    void PxService::OnSessionLogon(int id) {
        LOGI("OnSessionLogon : {}", id);
    }

    void PxService::OnSessionLogoff(int id) {
        LOGI("OnSessionLogoff : {}", id);
    }

    void PxService::OnSessionLock(int id) {
        LOGI("OnSessionLock : {}", id);
        //context_->PostBgTask([=, this]() {
        //    //std::this_thread::sleep_for(std::chrono::milliseconds(150));
        //    render_manager_->StopDesktopRender();
        //});
    }

    void PxService::OnSessionUnlock(int id) {
        LOGI("OnSessionUnLock : {}", id);
        //context_->PostBgTask([=, this]() {
        //    //std::this_thread::sleep_for(std::chrono::milliseconds(150));
        //    render_manager_->StopDesktopRender();
        //});
    }

    void PxService::SetStatus(DWORD dwState, DWORD dwErrCode, DWORD dwWait) {
        service_status_.dwCurrentState = dwState;
        service_status_.dwWin32ExitCode = dwErrCode;
        service_status_.dwWaitHint = dwWait;
        ::SetServiceStatus(status_handle_, &service_status_);
    }

    void PxService::StopAll() {
        render_manager_->Exit();
        // px_panel.exe
        auto processes = px::ProcessHelper::GetProcessList(false);
        for (auto& process : processes) {
            if (process->exe_full_path_.find(kPxRenderName) != std::string::npos
                || process->exe_full_path_.find(kPxClientName) != std::string::npos
                || process->exe_full_path_.find(kPxOsInfoName) != std::string::npos) {
                LOGI("Kill exe: {}", process->exe_full_path_);
                px::ProcessHelper::CloseProcess(process->pid_);
            }
        }
    }

    void PxService::TaskThread() {
        while (!exit_) {
            std::unique_lock<std::mutex> lk(cv_mtx_);
            cv_.wait(lk, [=, this]() -> bool {
                return !tasks_.empty() || exit_;
            });
            if (exit_) {
                LOGW("win service exit...");
                break;
            }

            auto task = tasks_.front();
            tasks_.pop();
            task();
        }
    }

    void PxService::PostTask(SrvTask&& task) {
        {
            std::lock_guard<std::mutex> lk(cv_mtx_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

    void PxService::SimulateCtrlAltDelete() {
        if (!sasLibrary) {
            sasLibrary = LoadLibraryW(L"sas.dll");
        }
        if (sasLibrary) {
            if (!_SendSAS) {
                _SendSAS = (SendSAS_proto)(void*)GetProcAddress(sasLibrary, "SendSAS");
            }
        }
        if (_SendSAS) {
            _SendSAS(FALSE);
        }
    }

    std::shared_ptr<RenderManager> PxService::GetRenderManager() {
        return render_manager_;
    }

}
