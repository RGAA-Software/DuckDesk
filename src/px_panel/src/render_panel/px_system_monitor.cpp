//
// Created by RGAA on 2024/3/26.
//

#include "px_system_monitor.h"
#include "px_exe_names.h"
#include <qdir.h>
#include <qfileinfo.h>
#include <QApplication>
#include "px_context.h"
#include "px_application.h"
#include "px_app_messages.h"
#include "px_common_new/thread.h"
#include "px_common_new/log.h"
#include "px_controller/vigem_driver_manager.h"
#include "px_controller/vigem/sdk/ViGEm/Client.h"
#include "px_common_new/process_util.h"
#include "px_common_new/string_util.h"
#include "px_common_new/md5.h"
#include "px_common_new/win32/process_helper.h"
#include "px_render_controller.h"
#include "px_run_game_manager.h"
#include "render_panel/network/ws_panel_server.h"
#include "px_settings.h"
#include "service/service_manager.h"
#include "px_console_client/console_device_api.h"
#include "px_console_client/console_device.h"
#include "px_common_new/http_base_op.h"
#include "px_common_new/cpu_frequency.h"
#include "px_profile_client/profile_api.h"
#include "px_qt_widget/px_dialog.h"
#include "px_qt_widget/translator/px_translator.h"
#include "companion/panel_companion.h"
#include "skin/interface/skin_interface.h"

#pragma comment(lib, "version.lib")
#pragma comment(lib, "kernel32.lib")

namespace px
{
    namespace {
        bool HasServiceBinaries() {
            const auto base_path = QCoreApplication::applicationDirPath();
            return QFileInfo::exists(base_path + "/" + px::kPxRenderExeName) &&
                   QFileInfo::exists(base_path + "/" + px::kPxServiceExeName);
        }

        std::string ExtractServiceExecutablePath(const std::string& value) {
            auto qvalue = QString::fromStdString(value).trimmed();
            if (qvalue.isEmpty()) {
                return "";
            }

            if (qvalue.startsWith("\"")) {
                int end_quote = qvalue.indexOf("\"", 1);
                if (end_quote > 1) {
                    return qvalue.mid(1, end_quote - 1).toStdString();
                }
            }

            auto lower = qvalue.toLower();
            int exe_index = lower.indexOf(".exe");
            if (exe_index >= 0) {
                return qvalue.left(exe_index + 4).trimmed().toStdString();
            }

            return qvalue.toStdString();
        }
    }

    std::shared_ptr<PxSystemMonitor> PxSystemMonitor::Make(const std::shared_ptr<PxApplication>& app) {
        return std::make_shared<PxSystemMonitor>(app);
    }

    PxSystemMonitor::PxSystemMonitor(const std::shared_ptr<PxApplication>& app) {
        this->app_ = app;
        this->context_ = app->GetContext();
        this->service_manager_ = context_->GetServiceManager();
        this->settings_ = PxSettings::Instance();
    }

    PxSystemMonitor::~PxSystemMonitor() {
        Exit();
    }

    void PxSystemMonitor::Start() {
        if (exit_ || monitor_thread_) {
            return;
        }
        const bool has_service_binaries = HasServiceBinaries();
        if (has_service_binaries) {
            CheckServiceAlive();
            // install service
            this->service_manager_->Install();
        } else {
            LOGI("{} or {} not found in app dir, skip service management.", px::kPxRenderExeName, px::kPxServiceExeName);
        }

        vigem_driver_manager_ = VigemDriverManager::Make();
        RegisterMessageListener();

        // Default: do not auto-install ViGEm driver at startup.
        // The driver will only be installed when the user explicitly triggers MsgInstallViGEm.
        // if (!CheckViGEmDriver()) {
        //     InstallViGem(true);
        // }

        const auto weak_self = weak_from_this();
        monitor_thread_ = Thread::MakeOnceTask([weak_self, has_service_binaries]() {
            for (;;) {
                const auto self = weak_self.lock();
                if (!self || self->exit_) {
                    break;
                }
                // check system servers
                if (self->settings_->HasConsoleServerConfig()) {
                    self->context_->PostTask([weak_self]() {
                        const auto self = weak_self.lock();
                        if (!self || self->exit_) {
                            return;
                        }
                        // this->CheckOnlineServers();
                        self->CheckThisDeviceInfo();
                    });
                }

                // check vigem
                self->context_->PostTask([weak_self]() {
                    const auto self = weak_self.lock();
                    if (!self || self->exit_) {
                        return;
                    }
                    bool vigem_installed = CheckViGEmDriver();
                    if (vigem_installed) {
                        if (!self->TryConnectViGEmDriver()) {
                            self->NotifyViGEnState(false);
                        }
                        else {
                            self->NotifyViGEnState(true);
                        }
                    }
                    else {
                        self->NotifyViGEnState(false);
                    }
                });

                // check running game
                const auto skin = self->app_->GetSkin();
                if (skin && skin->IsGameEnabled()) {
                    self->context_->PostTask([weak_self]() {
                        const auto self = weak_self.lock();
                        if (!self || self->exit_) {
                            return;
                        }
                        auto rgm = self->context_->GetRunGameManager();
                        rgm->CheckRunningGame();
                        auto msg = rgm->GetRunningGamesAsProto();
                        auto ws_server = self->app_->GetWsPanelServer();
                        if (ws_server) {
                            ws_server->PostPanelMessage(msg);
                        }

                        auto game_ids = rgm->GetRunningGameIds();
                        self->context_->SendAppMessage(MsgRunningGameIds{
                            .game_ids_ = game_ids,
                        });
                    });
                }

                // check service status
                if (has_service_binaries) {
                    self->context_->PostTask([weak_self]() {
                        const auto self = weak_self.lock();
                        if (!self || self->exit_) {
                            return;
                        }
                        auto status = self->service_manager_->QueryStatus();
                        self->context_->SendAppMessage(MsgServiceAlive {
                            .alive_ = (status == ServiceStatus::kRunning),
                        });
                        //LOGI("Service Status: {}", (int)status);
                    });
                }

                std::unique_lock lock(self->exit_mutex_);
                self->exit_cv_.wait_for(lock, std::chrono::seconds(5), [self]() {
                    return self->exit_.load();
                });
            }
        }, "sys_monitor", false);
    }

    void PxSystemMonitor::Exit() {
        if (exit_.exchange(true)) {
            return;
        }
        if (msg_listener_) {
            msg_listener_->UnListenAll();
            msg_listener_.reset();
        }
        if (state_msg_listener_) {
            state_msg_listener_->UnListenAll();
            state_msg_listener_.reset();
        }
        exit_cv_.notify_all();
        if (monitor_thread_) {
            monitor_thread_->Exit();
            monitor_thread_.reset();
        }
        vigem_driver_manager_.reset();
        service_manager_.reset();
        context_.reset();
        app_.reset();
    }

    bool PxSystemMonitor::CheckViGEmDriver() {
        DWORD major = 0, minor = 0;
        std::wstring path = L"C:\\Windows\\System32\\drivers\\ViGEmBus.sys";

        if (GetFileVersion(path, major, minor)) {
            //LOGI("ViGEmBus: {}.{}", major, minor);
            if (major > 1 || (major == 1 && minor >= 17)) {
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    bool PxSystemMonitor::GetFileVersion(const std::wstring& filePath, DWORD& major, DWORD& minor){
        DWORD dummy;
        DWORD size = GetFileVersionInfoSizeW(filePath.c_str(), &dummy);
        if (size == 0) {
            return false;
        }

        std::vector<BYTE> data(size);
        if (!GetFileVersionInfoW(filePath.c_str(), 0, size, data.data())) {
            return false;
        }

        VS_FIXEDFILEINFO* fileInfo = nullptr;
        UINT fileInfoSize;
        if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fileInfo), &fileInfoSize)) {
            return false;
        }

        major = (fileInfo->dwFileVersionMS >> 16) & 0xffff;
        minor = (fileInfo->dwFileVersionLS >> 16) & 0xffff;
        return true;
    }

    bool PxSystemMonitor::TryConnectViGEmDriver() {
        // driver seems already exists, try to connect
        if (!connect_vigem_success_) {
            connect_vigem_success_ = vigem_driver_manager_->TryConnect();
        }

        if (!connect_vigem_success_) {
            //LOGI("connect failed.");
            return false;
        } else {
            //LOGI("already tested, connect to vigem success.");
            return true;
        }
    }

    void PxSystemMonitor::InstallViGem(bool silent) {
        auto exe_folder_path = PxContext::GetCurrentExeFolder();
        std::string cmd;
        if (silent) {
            cmd = std::format("{}/px_joystick.exe /S", exe_folder_path);
        } else {
            cmd = std::format("{}/px_joystick.exe", exe_folder_path);
        }

        if (!ProcessUtil::StartProcessInWorkDir(exe_folder_path, cmd, {})) {
            LOGE("Install ViGEm device failed.");
        }
    }

    void PxSystemMonitor::NotifyViGEnState(bool ok) {
        static bool first_emit_state = true;
        const auto weak_self = weak_from_this();
        auto task = [weak_self, ok]() {
            const auto self = weak_self.lock();
            if (!self || self->exit_) {
                return;
            }
            self->context_->SendAppMessage(MsgViGEmState {
                .ok_ = ok,
            });
        };

        if (first_emit_state) {
            first_emit_state = false;
            context_->PostUIDelayTask([=]() {
                task();
            }, 250);
        } else {
            task();
        }
    }

    void PxSystemMonitor::RegisterMessageListener() {
        msg_listener_ = context_->ObtainMessageListener(MessageExecutionLane::kControl);
        state_msg_listener_ = context_->ObtainMessageListener(MessageExecutionLane::kState);
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<MsgInstallViGEm>([weak_self](const MsgInstallViGEm&) {
            const auto self = weak_self.lock();
            if (!self || self->exit_) {
                return;
            }
            self->context_->PostTask([weak_self]() {
                const auto self = weak_self.lock();
                if (!self || self->exit_) {
                    return;
                }
                px::PxSystemMonitor::InstallViGem(true);
            });
        });

        msg_listener_->Listen<MsgConnectedToService>([weak_self](const MsgConnectedToService&) {
            const auto self = weak_self.lock();
            if (self && !self->exit_) {
                self->StartServer();
            }
        });

        state_msg_listener_->Listen<MsgGrTimer2S>([weak_self](const MsgGrTimer2S&) {
            const auto self = weak_self.lock();
            if (!self || self->exit_) {
                return;
            }
            self->context_->PostTask([weak_self]() {
                const auto self = weak_self.lock();
                if (!self || self->exit_) {
                    return;
                }
                // cpu frequency
                auto freq = CpuFrequency::GetCurrentCpuSpeed();
                {
                    std::lock_guard<std::mutex> guard(self->cpu_frequency_mtx_);
                    self->current_cpu_frequency_.push_back(freq);
                    if (self->current_cpu_frequency_.size() >= 180) {
                        self->current_cpu_frequency_.pop_front();
                    }
                }
                if (auto companion = self->app_->GetCompanion(); companion) {
                    companion->UpdateCurrentCpuFrequency((float)freq);
                }
            });
        });
    }

    Response<bool, bool> PxSystemMonitor::CheckRenderAlive() {
        auto resp = Response<bool, bool>::Make(false, false);
        auto processes = ProcessHelper::GetProcessList(false);
        if (processes.empty()) {
            return resp;
        }
        resp.ok_ = true;

        LOGI("-------------------------------------------------------------------");
        for (auto& p : processes) {
            LOGI("p.exe_name: {}", p->exe_full_path_);
            if (p->exe_full_path_.find(kPxRenderName) != std::string::npos) {
                resp.value_ = true;
                //LOGI("Yes, find it.");
                break;
            }
        }
        return resp;
    }

    void PxSystemMonitor::CheckServiceAlive() {
        px::ServiceStatus serv_status = this->service_manager_->QueryStatus();
        if (px::ServiceStatus::kUnknownStatus == serv_status) {
            return;
        }

        auto serv_exe_path_res = this->service_manager_->GetServiceExecutablePath();

        if (!serv_exe_path_res.has_value()) {
            LOGE("cant not get serv_exe_path");
            return;
        }

        std::string serv_exe_path = ExtractServiceExecutablePath(serv_exe_path_res.value());

        QString exe_full_qpath = QString::fromStdString(serv_exe_path);
        QFileInfo file_info(exe_full_qpath);
        QDir parent_dir = file_info.dir();
        std::string serv_parent_path = parent_dir.absolutePath().toStdString();
        serv_parent_path = StringUtil::StandardizeWinPath(serv_parent_path);

        QDir cur_exe_dir = QCoreApplication::applicationDirPath();
        std::string cur_exe_parent_path = cur_exe_dir.absolutePath().toStdString();
        cur_exe_parent_path = StringUtil::StandardizeWinPath(cur_exe_parent_path);

        LOGI("*************************************");
        LOGI("serv_parent_path: {}", serv_parent_path);
        LOGI("cur_exe_parent_path: {}", cur_exe_parent_path);

        if (serv_parent_path != cur_exe_parent_path) {
            uint32_t cur_pid = QCoreApplication::applicationPid();
            QString msg = QString("{path: %1}").arg(QString::fromStdString(serv_parent_path));
            TcDialog dialog(tcTr("id_tips"), tcTr("id_run_other_service_instances") + " ? " + msg, nullptr);
            if (QDialog::Accepted != dialog.exec()) {
                px::ProcessHelper::CloseProcess(cur_pid);
                return;
            }
            this->service_manager_->Remove(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            auto processes = px::ProcessHelper::GetProcessList(false);
            for (auto& process : processes) {
                if (process->exe_full_path_.find(kPxClientName) != std::string::npos) {
                    LOGI("Kill exe: {}", process->exe_full_path_);
                    px::ProcessHelper::CloseProcess(process->pid_);
                    break;
                }
            }
            for (auto& process : processes) {
                if (process->exe_full_path_.find(kPxRenderName) != std::string::npos) {
                    LOGI("Kill exe: {}", process->exe_full_path_);
                    px::ProcessHelper::CloseProcess(process->pid_);
                    break;
                }
            }
            for (auto& process : processes) {
                if (process->exe_full_path_.find(kPxPanelName) != std::string::npos) {
                    LOGI("Kill exe: {}", process->exe_full_path_);
                    if (cur_pid != process->pid_) {
                        px::ProcessHelper::CloseProcess(process->pid_);
                    }
                }
            }
        }
    }

    void PxSystemMonitor::StartServer() {
        if (!HasServiceBinaries()) {
            LOGI("{} or {} not found in app dir, skip StartServer.", px::kPxRenderExeName, px::kPxServiceExeName);
            return;
        }
        auto srv_mgr = context_->GetRenderController();
        srv_mgr->StartServer();
    }

    void PxSystemMonitor::CheckThisDeviceInfo() {
        //LOGI("CheckThisDeviceInfo...");
        // profile server
        auto has_pr_server = HttpBaseOp::CanPingServer(settings_->IsConsoleSslEnabled(), settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort(), grApp->GetAppkey());
        if (!has_pr_server) {
            return;
        }

        // don't have device id, force to update
        if (settings_->GetDeviceId().empty() && has_pr_server) {
            context_->SendAppMessage(MsgForceRequestDeviceId{});
            return;
        }

        // has a device
        auto opt_device = px_console::ConsoleDeviceApi::QueryDevice(settings_->GetConsoleServerHost(),
                                                     settings_->GetConsoleServerPort(),
                                                     grApp->GetAppkey(),
                                                     settings_->GetDeviceId());
        if (!opt_device.has_value()) {
            if (auto err = opt_device.error(); err == px_console::ConsoleApiError::kDeviceNotFound) {
                LOGI("Don't have device in server, id: {}, will request a new one.", settings_->GetDeviceId());
                context_->SendAppMessage(MsgForceRequestDeviceId{});
            }
            return;
        }
        auto device = opt_device.value();
        if (!device) {
            LOGE("Query device for : {} failed.", settings_->GetDeviceId());
            return;
        }

        auto local_random_pwd_md5 = MD5::Hex(settings_->GetDeviceRandomPwd());
        if (device->random_pwd_md5_ != local_random_pwd_md5) {
            LOGW("Remote random-password verifier changed; refreshing it");
            auto opt_update_device = px_console::ConsoleDeviceApi::UpdateRandomPwd(settings_->GetConsoleServerHost(),
                                                                    settings_->GetConsoleServerPort(),
                                                                    grApp->GetAppkey(),
                                                                    settings_->GetDeviceId());
            if (opt_update_device.has_value()) {
                auto update_device =  opt_update_device.value();
                if (update_device && !update_device->gen_random_pwd_.empty()) {
                    settings_->SetDeviceRandomPwd(update_device->gen_random_pwd_);
                    context_->SendAppMessage(MsgRandomPasswordUpdated {
                        .device_id_ = settings_->GetDeviceId(),
                        .device_random_pwd_ = update_device->gen_random_pwd_,
                    });
                    context_->SendAppMessage(MsgSyncSettingsToRender{});
                }
            }
        }

        auto current_device_security_pwd = settings_->GetDeviceSecurityPwd();
        if (device->safety_pwd_md5_ != settings_->GetDeviceSecurityPwd() && !current_device_security_pwd.empty()) {
            LOGW("Remote safety-password verifier changed; refreshing it");
            // update safety password
            auto update_device = px_console::ConsoleDeviceApi::UpdateSafetyPwd(settings_->GetConsoleServerHost(),
                                                                settings_->GetConsoleServerPort(),
                                                                grApp->GetAppkey(),
                                                                settings_->GetDeviceId(),
                                                                current_device_security_pwd);
            if (!update_device) {
                LOGE("***UpdateSafetyPwd failed for device: {}", settings_->GetDeviceId());
            }
            else {
                LOGI("***UpdateSafetyPwd succeeded for device: {}", settings_->GetDeviceId());
            }
        }
    }

    std::vector<double> PxSystemMonitor::GetCurrentCpuFrequency() {
        std::lock_guard<std::mutex> guard(cpu_frequency_mtx_);
        std::vector<double> frequencies;
        for (const auto& v : current_cpu_frequency_) {
            frequencies.push_back(v);
        }
        return frequencies;
    }

}
