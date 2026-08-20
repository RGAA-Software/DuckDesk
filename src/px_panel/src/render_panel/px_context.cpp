//
// Created by RGAA on 2024/1/17.
//

#include "px_context.h"
#include "px_exe_names.h"

#include "px_common_new/task_runtime.h"
#include "px_common_new/shared_preference.h"
#include "px_common_new/uuid.h"
#include "px_steam_manager_new/steam_manager.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"
#include <nlohmann/json.hpp>
#include "px_settings.h"
#include "render_panel/database/db_game_operator.h"
#include "px_resources.h"
#include "px_render_controller.h"
#include "px_run_game_manager.h"
#include "px_app_messages.h"
#include "px_common_new/hardware.h"
#include "px_common_new/md5.h"
#include "service/service_manager.h"
#include "px_settings.h"
#include "px_application.h"
#include "database/stream_db_operator.h"
#include "px_cms_client/cms_device_api.h"
#include "devices/running_stream_manager.h"
#include "px_qt_widget/notify/notifymanager.h"
#include "px_dialog.h"
#include "px_label.h"
#include "px_workspace.h"
#include "database/px_database.h"
#include "px_account_sdk/acc_sdk.h"
#include "px_relay_client/relay_api.h"
#include "relay_message.pb.h"
#include "app_config.h"
#include "cms/px_cms_manager.h"
#include "cms/px_event_manager.h"
#include <QApplication>

using namespace nlohmann;

namespace px
{

    PxContext::PxContext(QWidget* main_window) : QObject(nullptr) {
        main_window_ = main_window;
    }

    bool PxContext::Init(const std::shared_ptr<PxApplication>& app) {
        app_ = app;
        settings_ = PxSettings::Instance();
        sp_ = SharedPreference::Instance();
        db_ready_ = false;
        db_error_.clear();

        database_ = std::make_shared<PxDatabase>(shared_from_this());
        if (database_->Init()) {
            db_ready_ = true;
        } else {
            db_error_ = database_->GetLastError();
            LOGE("Database init failed, error: {}", db_error_);
        }

        stream_db_mgr_ = database_->GetStreamDBOperator();
        if (!stream_db_mgr_) {
            if (!db_error_.empty()) {
                db_error_ += "; ";
            }
            db_error_ += "StreamDBOperator init failed";
            LOGE("StreamDBOperator init failed");
        }

        db_game_manager_ = database_->GetDBGameOperator();
        if (!db_game_manager_) {
            if (!db_error_.empty()) {
                db_error_ += "; ";
            }
            db_error_ += "DBGameOperator init failed";
            LOGE("DBGameOperator init failed");
        }

        db_ready_ = db_ready_ && stream_db_mgr_ != nullptr && db_game_manager_ != nullptr;

        if (!db_ready_ && db_error_.empty()) {
            db_error_ = "Database is not ready";
        }

        auto hardware = Hardware::Instance();
        auto beg = TimeUtil::GetCurrentTimestamp();
        hardware->Detect(false, true, false);
        hardware->Dump();
        auto end = TimeUtil::GetCurrentTimestamp();
        LOGI("Detect hardware info used: {}ms", end-beg);

        srv_manager_ = std::make_shared<PxRenderController>(app);

        task_rt_ = std::make_shared<TaskRuntime>(8);

        steam_mgr_ = SteamManager::Make();
        steam_mgr_->ScanInstalledSteamPath();

        msg_notifier_ = app_->GetMessageNotifier();

        // ips
        auto ips = IPUtil::ScanIPs();

        LOGI("Scan IP size: {}", ips.size());
        for (auto& item : ips) {
            LOGI("IP: {} -> {}", item.ip_addr_, item.nt_type_ == IPNetworkType::kWired ? "WIRED" : "WIRELESS");
        }

        res_manager_ = std::make_shared<PxResources>(shared_from_this());
        res_manager_->ExtractIconsIfNeeded();

        run_game_manager_ = std::make_shared<PxRunGameManager>(shared_from_this());
        cms_manager_ = std::make_shared<PxCmsManager>(shared_from_this());
        event_manager_ = std::make_shared<PxEventManager>(shared_from_this());
        service_manager_ = ServiceManager::Make();
        std::string base_path = qApp->applicationDirPath().toStdString();
        std::string bin_path = std::format("\"{}/{}\" {}", base_path, px::kPxServiceExeName, settings_->sys_service_port_);
        LOGI("Service path: {}", bin_path);
        service_manager_->Init("px_service", bin_path, "px_service", "** px_service **");
        //service_manager_->Install();

        running_stream_mgr_ = std::make_shared<RunningStreamManager>(shared_from_this());
        running_stream_mgr_->InitMessageListeners();

        notify_mgr_ = std::make_shared<NotifyManager>(main_window_);
        auto weak_self = weak_from_this();
        connect(notify_mgr_.get(), &NotifyManager::notifyDetail, this, [weak_self](const NotifyItem& data) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->PostTask([weak_self, data]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_) {
                        self->SendAppMessage(MsgNotificationClicked {
                            .data_ = data,
                        });
                    }
                });
            }
        });

        StartTimers();
        return true;
    }

    void PxContext::Exit() {
        exiting_ = true;
        if (srv_manager_) {
            srv_manager_->Exit();
        }
    }

    std::shared_ptr<SteamManager> PxContext::GetSteamManager() {
        return steam_mgr_;
    }

    void PxContext::PostTask(std::function<void()>&& task) {
        if (!task_rt_) {
            LOGE("PostTask ignored because task runtime is not ready");
            return;
        }
        task_rt_->Post(SimpleThreadTask::Make(std::move(task)));
    }

    void PxContext::PostTask(std::function<std::any()>&& exec_task, std::function<void(std::any)>&& cbk_task) {
        if (!task_rt_) {
            LOGE("PostTask(exec/callback) ignored because task runtime is not ready");
            return;
        }
        task_rt_->Post(
            ReturnThreadTask<ExecFunc, CallbackFunc>::Make(std::move(exec_task), std::move(cbk_task))
        );
    }

    void PxContext::PostUITask(std::function<void()>&& task) {
        auto weak_self = weak_from_this();
        QMetaObject::invokeMethod(this, [weak_self, task = std::move(task)]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            task();
        });
    }

    void PxContext::PostUIDelayTask(std::function<void()>&& task, int ms) {
        auto weak_self = weak_from_this();
        this->PostUITask([weak_self, ms, t = std::move(task)]() {
            QTimer::singleShot(ms, [weak_self, t]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                t();
            });
        });
    }

    void PxContext::PostDelayTask(std::function<void()>&& task, int ms) {
        auto weak_self = weak_from_this();
        this->PostUIDelayTask([weak_self, task = std::move(task)]() mutable {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            self->PostTask(std::move(task));
        }, ms);
    }

    void PxContext::PostDBTask(std::function<void()>&& task) {
        if (!task_rt_) {
            LOGE("PostDBTask ignored because task runtime is not ready");
            return;
        }
        task_rt_->GetLastThread()->Post(SimpleThreadTask::Make(std::move(task)));
    }

    void PxContext::PostDBTask(std::function<std::any()>&& exec_task, std::function<void(std::any)>&& cbk_task) {
        if (!task_rt_) {
            LOGE("PostDBTask(exec/callback) ignored because task runtime is not ready");
            return;
        }
        task_rt_->GetLastThread()->Post(
                ReturnThreadTask<ExecFunc, CallbackFunc>::Make(std::move(exec_task), std::move(cbk_task))
        );
    }

    int PxContext::GetIndexByUniqueId() {
        return std::atoi(settings_->GetDeviceId().c_str())%30+1;
    }

    std::vector<EthernetInfo> PxContext::GetIps() {
        return IPUtil::ScanIPs();
    }

    std::string PxContext::GetFirstAvailableIp() {
        const auto ips = IPUtil::ScanIPs();
        for (const auto& ip : ips) {
            return ip.ip_addr_;
        }
        return "";
    }

    std::string PxContext::GetDeviceIdOrIpAddress() {
        auto ips = this->GetIps();
        std::string ip_address;
        if (!ips.empty()) {
            ip_address = ips[0].ip_addr_;
        }
        auto device_id = settings_->GetDeviceId();
        //LOGI("** This Device ID: {}, ip: {}", device_id, ip_address);
        return !device_id.empty() ? device_id : ip_address;
    }

    std::string PxContext::MakeDesktopLinkMessage(const std::vector<EthernetInfo>& info) {
        json obj;
        // device
        // device_id
        obj["did"] = settings_->GetDeviceId();
        obj["dn"] = settings_->GetDeviceName();
        // random passwor
        obj["rpwd"] = settings_->GetDeviceRandomPwd();
        obj["iidx"] = this->GetIndexByUniqueId();
        // ips
        auto ip_array = json::array();
        std::vector<EthernetInfo> ips = info;
        if (info.empty()) {
            ips = this->GetIps();
        }

        for (auto& item : ips) {
            json ip_obj;
            ip_obj["ip"] = item.ip_addr_;
            //ip_obj["type"] = "";//item.nt_type_ == IPNetworkType::kWired ? "WIRED" : "WIRELESS";
            ip_array.push_back(ip_obj);
        }
        obj["ips"] = ip_array;

        // panel_srv_port
        obj["ppt"] = settings_->GetPanelServerPort();
        // render_srv_port
        obj["rdpt"] = settings_->GetRenderServerPort();
        // relay_host
        obj["rlst"] = settings_->GetRelayServerHost();
        // relay_port
        obj["rlpt"] = settings_->GetRelayServerPort();
        // relay_appkey
        obj["rlak"] = grApp->GetAppkey();
        return obj.dump();
    }

    std::shared_ptr<DBGameOperator> PxContext::GetDBGameManager() {
        return db_game_manager_;
    }

    std::shared_ptr<MessageNotifier> PxContext::GetMessageNotifier() {
        return msg_notifier_;
    }

    std::shared_ptr<MessageListener> PxContext::ObtainMessageListener() {
        if (!msg_notifier_) {
            LOGE("ObtainMessageListener failed because notifier is not ready");
            return nullptr;
        }
        return msg_notifier_->CreateListener();
    }

    std::shared_ptr<PxRenderController> PxContext::GetRenderController() {
        return srv_manager_;
    }

    void PxContext::StartTimers() {
        timer_ = std::make_shared<asio2::timer>();
        auto weak_self = weak_from_this();
        timer_->start_timer(100, 100, [weak_self]() {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->SendAppMessage(MsgGrTimer100{});
            }
        });

        timer_->start_timer(1, 1000, [weak_self]() {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->SendAppMessage(MsgGrTimer1S{});
            }
        });

        timer_->start_timer(2, 2000, [weak_self]() {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->SendAppMessage(MsgGrTimer2S{});
            }
        });

        timer_->start_timer(5, 5000, [weak_self]() {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->SendAppMessage(MsgGrTimer5S{});
            }
        });

        timer_->start_timer(6, 10 * 3600 * 1000, [weak_self]() {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->SendAppMessage(MsgGrTimer10H{});
            }
        });
    }

    std::shared_ptr<PxRunGameManager> PxContext::GetRunGameManager() {
        return run_game_manager_;
    }

    std::string PxContext::GetCurrentExeFolder() {
        return QCoreApplication::applicationDirPath().toStdString();
    }

    std::shared_ptr<ServiceManager> PxContext::GetServiceManager() {
        return service_manager_;
    }

    std::shared_ptr<PxApplication> PxContext::GetApplication() {
        return app_;
    }

    std::shared_ptr<PxCmsManager> PxContext::GetCmsManager() {
        return cms_manager_;
    }

    std::shared_ptr<PxEventManager> PxContext::GetEventManager() {
        return event_manager_;
    }

    std::shared_ptr<StreamDBOperator> PxContext::GetStreamDBManager() {
        return stream_db_mgr_;
    }

    std::shared_ptr<RunningStreamManager> PxContext::GetRunningStreamManager() {
        return running_stream_mgr_;
    }

    std::shared_ptr<NotifyManager> PxContext::GetNotifyManager() {
        return notify_mgr_;
    }

    void PxContext::NotifyAppMessage(const QString& title, const QString& msg, std::function<void()>&& cbk) {
        auto weak_self = weak_from_this();
        QMetaObject::invokeMethod(this, [weak_self, title, msg, cbk = std::move(cbk)]() {
            if (auto self = weak_self.lock(); self && !self->exiting_ && self->notify_mgr_) {
                self->notify_mgr_->notify(NotifyItem {
                    .type_ = NotifyItemType::kNormal,
                    .title_ = title,
                    .body_ = msg,
                    .cbk_ = cbk,
                });
            }
        });
    }

    void PxContext::NotifyAppErrMessage(const QString& title, const QString& msg, std::function<void()>&& cbk) {
        auto weak_self = weak_from_this();
        QMetaObject::invokeMethod(this, [weak_self, title, msg, cbk = std::move(cbk)]() {
            if (auto self = weak_self.lock(); self && !self->exiting_ && self->notify_mgr_) {
                self->notify_mgr_->notify(NotifyItem {
                    .type_ = NotifyItemType::kError,
                    .title_ = title,
                    .body_ = msg,
                    .cbk_ = cbk,
                });
            }
        });
    }

    std::shared_ptr<PxDatabase> PxContext::GetDatabase() {
        return database_;
    }

    bool PxContext::IsPreferenceReady() const {
        return sp_ && sp_->IsReady();
    }

    std::shared_ptr<px_relay::RelayDeviceInfo> PxContext::GetRelayServerSideDeviceInfo(const std::string& relay_host,
                                                                                    int relay_port,
                                                                                    const std::string& relay_appkey,
                                                                                    const std::string& device_id,
                                                                                    bool show_dialog) {
        if (!settings_->HasRelayServerConfig()) {
            return nullptr;
        }

        auto srv_remote_device_id = "server_" + device_id;
        auto relay_result = px_relay::RelayApi::GetRelayDeviceInfo(relay_host, relay_port, srv_remote_device_id, relay_appkey);
        if (!relay_result) {
            LOGE("Get device info in [Relay Server] for: {} failed: {}, code: {}", srv_remote_device_id, px_relay::RelayError2String(relay_result.error()), relay_result.error());
            if (show_dialog) {
                TcDialog dialog(tcTr("id_error"), tcTr("id_cant_get_remote_device_info"), grWorkspace.get());
                dialog.exec();
            }
            return nullptr;
        }
        auto relay_device_info = relay_result.value();
        //LOGI("Remote device in [Relay Server] info: id: {}, relay host: {}, port: {}",
        //     srv_remote_device_id, relay_device_info->relay_server_ip(), relay_device_info->relay_server_port());
        return relay_device_info;

    }

    void PxContext::SpPutString(const std::string& key, const std::string& value) {
        if (!sp_ || !sp_->IsReady()) {
            LOGE("SpPutString ignored because SharedPreference is not ready, key: {}", key);
            return;
        }
        sp_->Put(key, value);
    }

    std::string PxContext::SpGetString(const std::string& key, const std::string& def_val) {
        if (!sp_ || !sp_->IsReady()) {
            return def_val;
        }
        return sp_->Get(key, def_val);
    }

    void PxContext::SpPutInteger(const std::string& key, int value) {
        if (!sp_ || !sp_->IsReady()) {
            LOGE("SpPutInteger ignored because SharedPreference is not ready, key: {}", key);
            return;
        }
        sp_->PutInt(key, value);
    }

    int PxContext::SpGetInteger(const std::string& key, int def_val) {
        if (!sp_ || !sp_->IsReady()) {
            return def_val;
        }
        return sp_->GetInt(key, def_val);
    }

}
