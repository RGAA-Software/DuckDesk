//
// Created by RGAA on 2024/1/17.
//

#include "px_application.h"
#include <QTimer>
#include <QScreen>
#include <QApplication>
#include <QMessageBox>
#include "px_dialog.h"
#include "px_workspace.h"
#include "px_label.h"
#include "px_context.h"
#include "px_settings.h"
#include "px_statistics.h"
#include "px_app_messages.h"
#include "skin/skin_loader.h"
#include "px_common_new/log.h"
#include "px_system_monitor.h"
#include "px_connected_manager.h"
#include "px_common_new/thread.h"
#include "network/px_console_client.h"
#include "px_common_new/time_util.h"
#include "companion/panel_companion.h"
#include "companion/panel_companion_impl.h"
#include "console_scanner/console_scanner.h"
#include "ui/input_safety_pwd_dialog.h"
#include "ui/monitor_refresher.h"
#include <nlohmann/json.hpp>
#include "px_relay_client/relay_api.h"
#include "skin/interface/skin_interface.h"
#include "px_console_client/console_device_api.h"
#include "px_console_client/console_device.h"
#include "px_steam_manager_new/steam_manager.h"
#include "px_common_new/folder_util.h"
#include "px_common_new/http_client.h"
#include "px_common_new/win32/firewall_helper.h"
#include "px_common_new/shared_preference.h"
#include "px_common_new/message_notifier.h"
#include "px_console_client/console_stream.h"
#include "px_console_client/console_device_api.h"
#include "render_panel/px_render_msg_processor.h"
#include "render_panel/network/ws_panel_server.h"
#include "render_panel/network/udp_broadcaster.h"
#include "render_panel/network/px_service_client.h"
#include "render_panel/devices/stream_messages.h"
#include "render_panel/system/win/win_panel_message_loop.h"
#include "render_panel/clipboard/panel_clipboard_manager.h"
#include "render_panel/user/px_user_manager.h"
#include "render_panel/devices/px_device_manager.h"

#include <shellapi.h>

#include "px_common_new/const_auto.h"

using namespace nlohmann;


namespace px
{

    std::shared_ptr<PxApplication> grApp;

    std::shared_ptr<PxApplication> PxApplication::Make(QWidget* main_window, bool run_automatically, const std::string& skin_name) {
        struct PxApplicationEnabler final : PxApplication {
            PxApplicationEnabler(QWidget* window, bool auto_run, const std::string& skin) : PxApplication(window, auto_run, skin) {}
        };

        auto app = std::make_shared<PxApplicationEnabler>(main_window, run_automatically, skin_name);
        app->Init();
        return app;
    }

    PxApplication::PxApplication(QWidget* main_window, bool run_automatically, const std::string& skin_name) : QObject(main_window) {
        main_window_ = main_window;
        run_automatically_ = run_automatically;
        requested_skin_name_ = skin_name;
    }

    PxApplication::~PxApplication() {
        Exit();
    }

    void PxApplication::Init() {
        TimeDuration td("PxApplication::Init");
        // shared_from_this() below requires this object to be created by PxApplication::Make().
        grApp = shared_from_this();
        msg_notifier_ = std::make_shared<MessageNotifier>();

        auto begin_ctx_init_ts = TimeUtil::GetCurrentTimestamp();
        settings_ = PxSettings::Instance();
        settings_->Init(msg_notifier_);
        settings_->Load();
        settings_->Dump();

        // panel companion
        LoadPanelCompanion();
        if (companion_) {
            companion_->UpdateConsoleServerConfig(settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort(), settings_->IsConsoleSslEnabled());
        }

        skin_ = SkinLoader::LoadSkin(requested_skin_name_);
        if (!skin_) {
            LOGE("Load skin failed!!!");
        }

        //auto exeDir = QApplication::applicationDirPath().toStdString();
        //FolderUtil::CreateDir(std::format("{}/clients/windows", exeDir));
        //FolderUtil::CreateDir(std::format("{}/clients/android", exeDir));

        context_ = std::make_shared<PxContext>(main_window_);
        context_->Init(shared_from_this());
        if (!context_->IsDatabaseReady()) {
            const auto db_error = QString::fromStdString(context_->GetDatabaseError());
            QMessageBox::warning(nullptr, "Database degraded", "Local database init failed. Related features will run in degraded mode.\n\n" + db_error);
        }
        auto ctx_init_diff = TimeUtil::GetCurrentTimestamp() - begin_ctx_init_ts;
        LOGI("** Context init used: {}ms", ctx_init_diff);

        user_mgr_ = std::make_shared<PxUserManager>(context_);

        device_mgr_ = std::make_shared<PxDeviceManager>(context_);

        // firewall
        auto weak_self = weak_from_this();
        context_->PostTask([weak_self]() {
            auto self = weak_self.lock();
            if (!self || !self->context_) {
                return;
            }
            self->RegisterFirewall();
        });

        auto begin_conn_ts = TimeUtil::GetCurrentTimestamp();
        auto st = PxStatistics::Instance();
        st->SetContext(context_);
        st->RegisterEventListeners();

        px_connected_manager_ = std::make_shared<PxConnectedManager>(context_);
        clipboard_mgr_ = std::make_shared<ClipboardManager>(context_);
        rd_msg_processor_ = std::make_shared<PxRenderMsgProcessor>(context_);

        ws_panel_server_ = WsPanelServer::Make(shared_from_this());
        ws_panel_server_->Start();

        // Establish the Service control channel before system monitoring.
        // Hardware/WMI probing can stall during display-driver transitions;
        // delaying this connection also delays auth-info delivery, leaving a
        // restarted Service unable to reconnect to Console or redeem RTC
        // tickets even though Render itself is already running.
        service_client_ = std::make_shared<PxServiceClient>(shared_from_this());
        service_client_->Start();

        sys_monitor_ = PxSystemMonitor::Make(shared_from_this());
        sys_monitor_->Start();

        //udp_broadcaster_ = UdpBroadcaster::Make(context_);

        QCoreApplication::instance()->installNativeEventFilter(px_connected_manager_.get());

        // monitor refresher
        monitor_refresher_ = std::make_shared<MonitorRefresher>(context_, nullptr);
        monitor_refresher_->InitMessageListeners();

        auto conn_diff = TimeUtil::GetCurrentTimestamp() - begin_conn_ts;
        LOGI("** Connection used: {}ms", conn_diff);

        RefreshClientManagerSettings();
        RegisterMessageListener();
        StartWindowsMessagesLooping();
        console_scanner_ = std::make_shared<ConsoleScanner>(shared_from_this());
        console_scanner_->StartUdpReceiver(30501);

        // update device id
        if (cat comp = grApp->GetCompanion(); comp) {
            comp->UpdateDeviceId(settings_->GetDeviceId());
        }

        if (!run_automatically_) {
            context_->PostUIDelayTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || !self->context_) {
                    return;
                }
                self->UpdateServerSecurityPasswordIfNeeded();
                //CheckSecurityPassword();
            }, 500);
        }
    }

    void PxApplication::PrepareForShutdown() {
        if (shutdown_prepared_.exchange(true)) {
            return;
        }

        if (service_client_) {
            service_client_->Exit();
            service_client_ = nullptr;
        }
        if (sys_monitor_) {
            sys_monitor_->Exit();
            sys_monitor_ = nullptr;
        }
    }

    void PxApplication::Exit() {
        if (exiting_.exchange(true)) {
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
        PxStatistics::Instance()->Exit();
        PrepareForShutdown();
        if (win_msg_thread_ && win_msg_thread_->IsJoinable()) {
            win_msg_thread_->Join();
        }
        if (win_msg_loop_) {
            win_msg_loop_->Stop();
        }
        if (monitor_refresher_) {
            monitor_refresher_->Exit();
            monitor_refresher_ = nullptr;
        }
        if (console_client_) {
            console_client_->Stop();
            console_client_ = nullptr;
        }
        if (console_scanner_) {
            console_scanner_->Exit();
            console_scanner_ = nullptr;
        }
        if (ws_panel_server_) {
            ws_panel_server_->Exit();
            ws_panel_server_ = nullptr;
        }
        if (px_connected_manager_) {
            QCoreApplication::instance()->removeNativeEventFilter(px_connected_manager_.get());
            px_connected_manager_.reset();
        }
        clipboard_mgr_.reset();
        rd_msg_processor_.reset();
        user_mgr_.reset();
        device_mgr_.reset();
        win_msg_loop_.reset();
        win_msg_thread_.reset();
        if (context_) {
            context_->Exit();
        }
        if (msg_notifier_) {
            msg_notifier_->Stop(MessageBusStopMode::kCancel);
        }
        context_.reset();
        msg_notifier_.reset();
    }

    bool PxApplication::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
        if(eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG")
        {
            const auto pMsg = static_cast<MSG*>(message);
            if(pMsg->message == WM_COPYDATA) {

            }
            else if(pMsg->message == WM_DROPFILES) {

            }
            else if (pMsg->message == WM_DISPLAYCHANGE) {
                LOGI("WM_DISPLAYCHANGE, Monitor changed!");
                if (monitor_refresher_) {
                    LOGW("Will exit monitor refresher and recreate it.");
                    monitor_refresher_->Exit();
                    monitor_refresher_.reset();
                    auto weak_self = weak_from_this();
                    context_->PostUIDelayTask([weak_self]() {
                        auto self = weak_self.lock();
                        if (!self || !self->context_) {
                            return;
                        }
                        self->monitor_refresher_ = std::make_shared<MonitorRefresher>(self->context_, nullptr);
                        self->monitor_refresher_->InitMessageListeners();
                   }, 5000);
                }
            }
        }
        return false;
    }

    bool PxApplication::IsServiceConnected() const {
        return service_client_ && service_client_->IsAlive();
    }

    bool PxApplication::PostMessage2Service(const std::string& msg) {
        if (!IsServiceConnected()) {
            return false;
        }
        service_client_->PostNetMessage(msg);
        return true;
    }

    bool PxApplication::IsRendererConnected() {
        return ws_panel_server_ && ws_panel_server_->IsAlive();
    }

    bool PxApplication::PostMessage2Renderer(std::shared_ptr<Data> msg) {
        if (!IsRendererConnected()) {
            return false;
        }
        ws_panel_server_->PostRendererMessage(msg);
        return true;
    }

    void PxApplication::RefreshClientManagerSettings() {
        std::shared_ptr<Authorization> auth = nullptr;
        if (companion_) {
            auth = companion_->GetAuth();
        }
        // deprecated //
    }

    void PxApplication::RegisterMessageListener() {
        msg_listener_ = context_->ObtainMessageListener(MessageExecutionLane::kControl);
        state_msg_listener_ = context_->ObtainMessageListener(MessageExecutionLane::kState);
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<MsgSettingsChanged>([weak_self](const MsgSettingsChanged&) {
            const auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            LOGI("Settings changed...");
            self->RefreshClientManagerSettings();
            const bool force_update = self->settings_->GetDeviceId().empty();
            self->RequestNewClientId(force_update);
        });

        state_msg_listener_->Listen<MsgGrTimer100>([weak_self](const MsgGrTimer100&) {
            const auto self = weak_self.lock();
            if (self && !self->exiting_ && self->companion_) {
                self->companion_->OnTimer100ms();
            }
        });

        state_msg_listener_->Listen<MsgGrTimer1S>([weak_self](const MsgGrTimer1S&) {
            const auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            if (self->companion_) {
                self->companion_->OnTimer1S();
            }

            // console client
            self->StartConsoleClientIfNeeded();
        });

        // stop the console connection
        msg_listener_->Listen<MsgForceClearProgramData>([weak_self](const MsgForceClearProgramData&) {
            const auto self = weak_self.lock();
            if (self && !self->exiting_ && self->console_client_) {
                self->console_client_->Stop();
            }
        });

        state_msg_listener_->Listen<MsgGrTimer5S>([weak_self](const MsgGrTimer5S&) {
            const auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            if (self->settings_->GetDeviceId().empty()) {
                self->RequestNewClientId(true);
            }
            if (self->companion_) {
                self->companion_->OnTimer5S();
            }
        });

        msg_listener_->Listen<MsgForceRequestDeviceId>([weak_self](const MsgForceRequestDeviceId&) {
            const auto self = weak_self.lock();
            if (self && !self->exiting_) {
                self->RequestNewClientId(true);
            }
        });
    }

    bool PxApplication::RequestNewClientId(bool force_update, bool sync) {
        if (!force_update && !settings_->GetDeviceId().empty() && !settings_->GetDeviceRandomPwd().empty()) {
            return false;
        }

        auto weak_self = weak_from_this();
        auto task = [weak_self, force_update]() -> bool {
            auto self = weak_self.lock();
            if (!self || !self->context_) {
                return false;
            }
            if (!self->settings_->HasConsoleServerConfig()) {
                return false;
            }

            // make a default device name
            std::string def_device_name = "D-";
            auto ips = self->context_->GetIps();
            std::string device_suffix = "NULL";
            if (!ips.empty()) {
                auto ip = ips[0].ip_addr_;
                std::vector<std::string> ip_segments;
                StringUtil::Split(ip, ip_segments, ".");
                if (!ip_segments.empty()) {
                    device_suffix = ip_segments[ip_segments.size()-1];
                }
            }
            def_device_name += device_suffix;
            LOGI("Will request new device, device name: {}", def_device_name);

            auto opt_device = self->device_mgr_->RequestNewDevice(def_device_name, "");
            if (!opt_device.has_value()) {
                LOGE("Can't create new device, error: {}", (int)opt_device.error());
                return false;
            }
            auto device = opt_device.value();
            if (!device || device->device_id_.empty() || device->gen_random_pwd_.empty()) {
                LOGE("Can't create new device, device is nullptr.");
                return false;
            }

            self->settings_->SetDeviceId(device->device_id_);
            if (cat comp = grApp->GetCompanion(); comp) {
                comp->UpdateDeviceId(device->device_id_);
            }
            self->settings_->SetDeviceName(device->device_name_);
            self->settings_->SetDeviceRandomPwd(device->gen_random_pwd_);

            self->context_->SendAppMessage(MsgRequestedNewDevice {
                .device_id_ = device->device_id_,
                .device_random_pwd_ = device->gen_random_pwd_,
                .force_update_ = force_update,
            });
            self->context_->SendAppMessage(MsgSyncSettingsToRender{});

            return true;
        };

        if (sync) {
            return task();
        }
        else {
            context_->PostTask([task]() {
                task();
            });
            return true;
        }
    }

    void PxApplication::RegisterFirewall() {
        // register firewall
        auto begin_fm_ts = TimeUtil::GetCurrentTimestamp();
        auto app_path = qApp->applicationDirPath() + "/" + kPxPanelName.c_str();
        auto render_path = qApp->applicationDirPath() + "/" + kPxRenderName.c_str();
        auto client_inner_path = qApp->applicationDirPath() + "/" + kPxClientName.c_str();
        auto service_path = qApp->applicationDirPath() + "/" + kPxServiceName.c_str();
        auto fh = FirewallHelper::Instance();

        fh->RemoveProgramFromFirewall("PxPanelIn");
        fh->RemoveProgramFromFirewall("PxPanelOut");
        fh->RemoveProgramFromFirewall("PxRenderIn");
        fh->RemoveProgramFromFirewall("PxRenderOut");
        fh->RemoveProgramFromFirewall("PxClientIn");
        fh->RemoveProgramFromFirewall("PxClientOut");
        fh->RemoveProgramFromFirewall("PxServiceIn");
        fh->RemoveProgramFromFirewall("PxServiceOut");
        fh->RemoveProgramFromFirewall("PxRtcLocalUdpIn");

        fh->AddProgramToFirewall(RulesInfo("PxPanelIn", app_path.toStdString(), "", 1));
        fh->AddProgramToFirewall(RulesInfo("PxPanelOut", app_path.toStdString(), "", 2));
        fh->AddProgramToFirewall(RulesInfo("PxRenderIn", render_path.toStdString(), "", 1));
        fh->AddProgramToFirewall(RulesInfo("PxRenderOut", render_path.toStdString(), "", 2));
        fh->AddProgramToFirewall(RulesInfo("PxClientIn", client_inner_path.toStdString(), "", 1));
        fh->AddProgramToFirewall(RulesInfo("PxClientOut", client_inner_path.toStdString(), "", 2));
        fh->AddProgramToFirewall(RulesInfo("PxServiceIn", service_path.toStdString(), "", 1));
        fh->AddProgramToFirewall(RulesInfo("PxServiceOut", service_path.toStdString(), "", 2));
        // WebRTC local direct connection (net_rtc_local), UDP port range: 60430-60490
        fh->AddPortToFirewall("PxRtcLocalUdpIn", "60430-60490", 17 /*UDP*/, 1 /*in*/);
        auto fm_diff = TimeUtil::GetCurrentTimestamp()-begin_fm_ts;
        LOGI("** Firewall init used: {}ms", fm_diff);
        LOGI("app path: {}", app_path.toStdString());
        LOGI("render path: {}", render_path.toStdString());
        LOGI("client inner path: {}", client_inner_path.toStdString());
        LOGI("client inner path: {}", service_path.toStdString());
    }

    std::shared_ptr<MessageNotifier> PxApplication::GetMessageNotifier() {
        return msg_notifier_;
    }

    bool PxApplication::CheckLocalDeviceInfoWithPopup() {
        auto r = this->IsDeviceInfoOk();
        if (r) {
            return true;
        }

        auto err_msg = "Your device info invalid, ID is empty or password invalid";
        QString pre_msg = tcTr("id_local_device_info_error");
        TcDialog dialog(tcTr("id_error"), pre_msg + std::format(" {}", err_msg).c_str(), grWorkspace.get());
        dialog.exec();
        return false;
    }

    void PxApplication::CheckSecurityPassword() {
        if (settings_->GetDeviceSecurityPwd().empty()) {
            InputSafetyPwdDialog dialog(grApp, grWorkspace.get());
            dialog.exec();
        }
    }

    void PxApplication::UpdateServerSecurityPasswordIfNeeded() {
        auto weak_self = weak_from_this();
        context_->PostTask([weak_self]() {
            auto self = weak_self.lock();
            if (!self || !self->context_) {
                return;
            }
            if (self->settings_->GetDeviceSecurityPwd().empty()) {
                return;
            }
        });
    }

    void PxApplication::StartWindowsMessagesLooping() {
        auto weak_self = weak_from_this();
        win_msg_thread_ = Thread::MakeOnceTask([weak_self]() {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->win_msg_loop_ = std::make_shared<WinMessageLoop>(self);
            self->win_msg_loop_->Start();
        }, "", false);
    }

    bool PxApplication::PostMessage2RemoteRender(const std::shared_ptr<PxBaseStreamMessage>& msg) {
        if (!msg || !msg->stream_item_) {
            return false;
        }

        auto& item = msg->stream_item_;
        if (item->HasRelayInfo()) {
            auto srv_remote_device_id = "server_" + item->remote_device_id_;
            auto res = px_relay::RelayApi::NotifyEvent(item->relay_host_,
                                                    item->relay_port_,
                                                    context_->GetDeviceIdOrIpAddress(),
                                                    srv_remote_device_id,
                                                    msg->AsJson(),
                                                    this->GetAppkey());
            if (res.has_value()) {
                if (res.value() == px_relay::kRelayOk) {
                    return true;
                }
                else {
                    LOGE("NotifyEvent failed, res: {}", res.value());
                }
            }
            return false;
        }
        else {
            // host & port mode
            auto client = HttpClient::Make(item->stream_host_, item->stream_port_, "/panel/stream/message", 3000);
            auto res = client->Post({}, msg->AsJson());
            LOGI("res: {} {}", res.status, res.body);
            if (res.status == 200) {
                try {
                    auto obj = json::parse(res.body);
                    auto code = obj["code"].get<int>();
                    if (code == 200) {
                        return true;
                    }
                    else {
                        LOGE("NotifyEvent failed, error code: {}", code);
                    }
                } catch(std::exception& e) {
                    LOGE("NotifyEvent, parse json failed: {}, body: {}", e.what(), res.body);
                }
            }
            return false;
        }
    }

    void PxApplication::LoadPanelCompanion() {
        auto plugin = std::make_shared<PanelCompanionImpl>();
        if (!plugin->Init()) {
            LOGE("Can't init panel_companion");
            return;
        }
        LOGI("Load panel_companion success.");
        companion_ = plugin;
    }

    PanelCompanion* PxApplication::GetCompanion() {
        return companion_.get();
    }

    void PxApplication::JumpToOffSiteUpdate() {
        if (companion_) {
            companion_->JumpToGithub();
        }
    }

    bool PxApplication::HasOffSiteUpdate() {
        return companion_ && companion_->HasUpdateForOffSite();
    }

    std::string PxApplication::GetAppkey() {
        if (companion_ && companion_->GetAuth()) {
            return companion_->GetAuth()->appkey_;
        }
        return "";
    }

    void PxApplication::StartConsoleClientIfNeeded() {
        auto appkey = GetAppkey();
        auto console_host = settings_->GetConsoleServerHost();
        auto console_port = settings_->GetConsoleServerPort();
        auto device_id = settings_->GetDeviceId();
        if (appkey.empty() || console_host.empty() || console_port <= 0 || device_id.empty()) {
            return;
        }

        const bool host_changed = (console_host != using_console_host_);
        const bool port_changed = (console_port != using_console_port_);
        const bool ssl_changed = (settings_->IsConsoleSslEnabled() != using_console_ssl_);
        if (appkey != using_appkey_ || host_changed || port_changed || ssl_changed) {
            LOGW("Console config changed, credential_changed: {}, host: {} => {}, port: {} => {}, ssl: {} => {}, will release WS:ConsoleClient and recreate it.",
                 appkey != using_appkey_, using_console_host_, console_host, using_console_port_, console_port, using_console_ssl_, settings_->IsConsoleSslEnabled());
            if (console_client_) {
                console_client_->Stop();
                console_client_ = nullptr;
            }
        }

        if (!console_client_) {
            console_client_ = PxConsoleClient::Make(context_, console_host, console_port, device_id);
        }
        if (!console_client_->IsStarted()) {
            console_client_->Start();
        }
        using_appkey_ = appkey;
        using_console_host_ = console_host;
        using_console_port_ = console_port;
        using_console_ssl_ = settings_->IsConsoleSslEnabled();
    }

    std::shared_ptr<ConsoleScanner> PxApplication::GetConsoleScanner() {
        return console_scanner_;
    }

    SkinInterface* PxApplication::GetSkin() {
        return skin_;
    }

    std::string PxApplication::GetSkinName() {
        return skin_ ? skin_->GetSkinName().toStdString() : "";
    }

    bool PxApplication::IsConsoleClientAlive() {
        return console_client_ && console_client_->IsAlive();
    }

    std::shared_ptr<PxUserManager> PxApplication::GetUserManager() {
        return user_mgr_;
    }

    bool PxApplication::IsDeviceInfoOk() {
        auto device_id = settings_->GetDeviceId();
        auto device_random_pwd = settings_->GetDeviceRandomPwd();
        auto device_safety_pwd = settings_->GetDeviceSecurityPwd();

        if (device_id.empty() || device_random_pwd.empty()) {
            LOGE("Check device info error, device id is empty.");
            return false;
        }
        return true;
    }

    std::shared_ptr<PxDeviceManager> PxApplication::GetDeviceManager() {
        return device_mgr_;
    }

    bool PxApplication::CanConnectConsoleServer() {
        cat r = px_console::ConsoleDeviceApi::Ping(settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort(), this->GetAppkey());
        return r.has_value() ? r.value() : false;
    }

}
