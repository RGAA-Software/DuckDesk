//
// Created by RGAA on 28/03/2025.
//

#include "running_stream_manager.h"
#include <QApplication>
#include <QDateTime>
#include <filesystem>

#include "px_device_manager.h"
#include "px_common_new/base64.h"
#include "px_common_new/folder_util.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_context.h"
#include "px_common_new/log.h"
#include "px_common_new/const_auto.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/px_application.h"
#include "px_profile_client/profile_api.h"
#include "px_qt_widget/px_dialog.h"
#include "start_stream_loading.h"
#include "px_qt_widget/translator/px_translator.h"
#include "px_base/ct_stream_item_net_type.h"
#include "render_panel/companion/panel_companion.h"
#include "render_panel/cms/px_cms_manager.h"
#include "px_cms_client/cms_device.h"

namespace px
{

    RunningStreamManager::RunningStreamManager(const std::shared_ptr<PxContext>& ctx) {
        context_ = ctx;
        settings_ = PxSettings::Instance();
        msg_listener_ = context_->GetMessageNotifier()->CreateListener();
    }

    void RunningStreamManager::InitMessageListeners() {
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<MsgClientConnectedPanel>([=, this](const MsgClientConnectedPanel& msg) {
            // clear loading dialog
            context_->PostUIDelayTask([weak_self, msg]() {
                const auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                if (self->loading_dialogs_.contains(msg.stream_id_)) {
                    self->loading_dialogs_[msg.stream_id_]->hide();
                }
            }, 200);
        });

        msg_listener_->Listen<MsgNoAvailableConnection>([=, this](const MsgNoAvailableConnection& msg) {
            context_->PostUITask([weak_self, msg]() {
                const auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                if (self->loading_dialogs_.contains(msg.stream_id_)) {
                    self->loading_dialogs_[msg.stream_id_]->hide();
                    self->loading_dialogs_.erase(msg.stream_id_);
                }
            });
        });
    }

    RunningStreamManager::~RunningStreamManager() {

    }

    void RunningStreamManager::StartStream(const std::shared_ptr<px_cms::CmsStream>& item, const std::string& network_type, bool direct) {
        // loading dialog
        auto loading = std::make_shared<StartStreamLoading>(context_, item, network_type);
        loading->setWindowFlag(Qt::WindowStaysOnTopHint, true);
        loading->show();
        auto stream_id = item->stream_id_;
        loading_dialogs_.insert({stream_id, loading});
        const auto weak_self = weak_from_this();
        QTimer::singleShot(7000, context_.get(), [weak_self, stream_id]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            if (self->loading_dialogs_.contains(stream_id)) {
                self->loading_dialogs_[stream_id]->hide();
                self->loading_dialogs_.erase(stream_id);
            }
        });

        auto func_hide_loading_dialog = [weak_self, stream_id]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            if (self->loading_dialogs_.contains(stream_id)) {
                self->loading_dialogs_[stream_id]->hide();
                self->loading_dialogs_.erase(stream_id);
            }
        };

        const bool uses_cms_ticket = item->connect_type_ == "cms_ticket"
                                  || item->connect_type_ == "cms_app_ticket";
        bool has_cms_info = !item->remote_device_id_.empty() && !settings_->GetCmsServerHost().empty() && settings_->GetCmsServerPort() > 0;
        // check the authorization
        // NOT force direct
        // A CMS connection ticket has already passed identity, application
        // access and quota checks. Do not reject guest/public-application
        // launches with the legacy device-license checks below.
        if (!uses_cms_ticket && has_cms_info && !item->force_direct_) {
            // check network firstly
            if (!context_->GetApplication()->CanConnectCmsServer()) {
                func_hide_loading_dialog();
                TcDialog dialog(tcTr("id_error"), tcTr("id_net_error_no_cms_connection"), nullptr);
                dialog.exec();
                return;
            }

            // check authorization secondly
            if (cat companion = context_->GetApplication()->GetCompanion(); companion) {
                if (!companion->IsAuthValid()) {
                    func_hide_loading_dialog();
                    TcDialog dialog(tcTr("id_error"), tcTr("id_auth_invalid"), nullptr);
                    dialog.exec();
                    return;
                }
            }
        }

        // NOT in force connecting directly mode, check the Cms server.
        if (!uses_cms_ticket && !item->force_direct_) {
            if (grApp->GetSkinName() != "OpenSource" && !item->remote_device_id_.empty()/* && !direct*/) {
                // 1. check available or not
                auto ac = context_->GetCmsManager()->QueryNewConnection(false);
                if (ac == std::nullopt) {
                    func_hide_loading_dialog();
                    LOGE("Not available connection for : {}", item->remote_device_id_);
                    return;
                }
                auto c = ac.value();
                if (!c.available_) {
                    context_->PostTask([ctx = context_, stream_id]() {
                        ctx->SendAppMessage(MsgNoAvailableConnection {
                            .stream_id_ = stream_id,
                        });
                    });

                    func_hide_loading_dialog();

                    const QString msg = tcTr("id_no_available_connection");
                    no_conn_dialog_ = std::make_shared<TcDialog>(tcTr("id_error"), msg);
                    no_conn_dialog_->show();

                    return;
                }
            }
        }

        if (!uses_cms_ticket && has_cms_info && !item->force_direct_) {
            // 2. check alive or not
            cat device_mgr = context_->GetApplication()->GetDeviceManager();
            if (auto r = device_mgr->QueryDevice(item->remote_device_id_); r.has_value()) {
                cat remote_device = r.value();
                if (!remote_device->active_) {
                    func_hide_loading_dialog();
                    TcDialog dialog(tcTr("id_warning"), tcTr("id_device_inactive"), nullptr);
                    dialog.exec();
                    return;
                }
            }
            else {
                func_hide_loading_dialog();
                TcDialog dialog(tcTr("id_warning"), tcTr("id_cant_get_remote_device_info"), nullptr);
                dialog.exec();
                return;
            }
        }

        std::string screen_recording_path = settings_->GetScreenRecordingPath();
        if (screen_recording_path.empty()) {
            // 默认: C:\Users\Public\Pixels\px_client_records (与数据根同约定)
            screen_recording_path =
                (std::filesystem::path(FolderUtil::GetProgramDataPath()) / "px_client_records").string();
        }

        bool show_watermark = true;
        if (grApp->GetSkinName() == "OpenSource" || !item->remote_device_id_.empty()) {
            show_watermark = false;
        }

        // start it
        auto process = std::make_shared<QProcess>();
        QStringList arguments;
        arguments
            << std::format("--host={}", item->stream_host_).c_str()
            << std::format("--port={}", item->stream_port_).c_str()
            << std::format("--appkey={}", grApp->GetAppkey()).c_str()
            << std::format("--cms_host={}", settings_->GetCmsServerHost()).c_str()
            << std::format("--cms_port={}", settings_->GetCmsServerPort()).c_str()
            << std::format("--cms_ssl={}", settings_->IsCmsSslEnabled()).c_str()
            << std::format("--audio={}", item->audio_enabled_).c_str()
            << std::format("--clipboard={}", item->clipboard_enabled_).c_str()
            << std::format("--stream_id={}", item->stream_id_).c_str()
            << std::format("--conn_type={}", item->connect_type_).c_str()
            << std::format("--network_type={}", network_type).c_str()
            << std::format("--stream_name={}", Base64::Base64Encode(item->stream_name_)).c_str()
            << std::format("--device_id={}", settings_->GetDeviceId()).c_str()
            << std::format("--device_rp={}", Base64::Base64Encode(settings_->GetDeviceRandomPwd())).c_str()
            << std::format("--device_sp={}", Base64::Base64Encode(settings_->GetDeviceSecurityPwd())).c_str()
            << std::format("--remote_device_id={}", item->remote_device_id_).c_str()
            << std::format("--remote_device_rp={}", Base64::Base64Encode(item->remote_device_random_pwd_)).c_str()
            << std::format("--remote_device_sp={}", Base64::Base64Encode(item->remote_device_safety_pwd_)).c_str()
            << std::format("--enable_p2p={}", item->enable_p2p_).c_str()
            << std::format("--auto_layout_screens={}", settings_->IsMaxWindowEnabled() ? 1 : 0).c_str()
            << std::format("--display_name={}", [=, this]() -> std::string {
                if (network_type == kStreamItemNtTypeRelay) {
                    return settings_->GetDeviceId();
                }
                else {
                    return "My Computer";
                }
            }()).c_str()
            << std::format("--display_remote_name={}", [=, this]() -> std::string {
                if (network_type == kStreamItemNtTypeRelay) {
                    return item->remote_device_id_;
                }
                else {
                    return item->stream_name_.empty() ? item->stream_host_ : item->stream_name_;
                }
            } ()).c_str()
            << std::format("--panel_server_port={}", settings_->GetPanelServerPort()).c_str()
            << std::format("--screen_recording_path={}", screen_recording_path).c_str()
            << std::format("--my_host={}", [=, this]() -> std::string {
                auto ips = context_->GetIps();
                if (!ips.empty()) {
                    return ips[0].ip_addr_;
                }
                return "";
            }()).c_str()
            << std::format("--language={}", (int)tcTrMgr()->GetSelectedLanguage()).c_str()
            << std::format("--only_viewing={}", item->only_viewing_).c_str()
            << std::format("--split_windows={}", item->split_windows_).c_str()
            << std::format("--max_num_of_screen={}", settings_->GetMaxNumOfScreen()).c_str()
            << std::format("--display_logo={}", settings_->IsClientLogoDisplaying() ? 1 : 0).c_str()
            << std::format("--develop_mode={}", settings_->IsDevelopMode() ? 1 : 0).c_str()
            << std::format("--titlebar_color={}", settings_->IsColorfulTitleBarEnabled() ? item->bg_color_ : -1).c_str()
            << std::format("--decoder={}", settings_->GetPreferDecoder()).c_str()
            << std::format("--relay_host={}", item->relay_host_).c_str()
            << std::format("--relay_port={}", item->relay_port_).c_str()
            << std::format("--relay_appkey={}", grApp->GetAppkey()).c_str() //item->relay_appkey_
            << std::format("--force_software={}", item->force_software_ ? 1 : 0).c_str()
            << std::format("--wait_debug={}", item->wait_debug_ ? 1 : 0).c_str()
            << std::format("--force_gdi_capture={}", item->force_gdi_capture_ ? 1 : 0).c_str()
            << std::format("--disable_vulkan_render={}", item->disable_vulkan_render_ ? 1 : 0).c_str()
            << std::format("--show_watermark={}", show_watermark ? 1 : 0).c_str()
            << std::format("--gl_backend={}", settings_->gl_backend_).c_str()
            << std::format("--force_direct={}", item->force_direct_ ? 1 : 0).c_str()
            ;
        if (!item->connection_ticket_.empty()) {
            arguments
                << std::format("--connection_ticket={}", Base64::Base64Encode(item->connection_ticket_)).c_str()
                << std::format("--connection_nonce={}", item->connection_nonce_).c_str()
                << std::format("--connection_instance_id={}", item->cms_instance_id_).c_str();
        }
        LOGI("Start client inner args:");
        for (auto& arg : arguments) {
            const auto value = arg.toStdString();
            if (value.starts_with("--connection_ticket=")
                || value.starts_with("--remote_device_rp=")
                || value.starts_with("--remote_device_sp=")
                || value.starts_with("--device_rp=")
                || value.starts_with("--device_sp=")) {
                LOGI("{}=<redacted>", value.substr(0, value.find('=')));
            }
            else {
                LOGI("{}", value);
            }
        }

        auto client_inner_path = qApp->applicationDirPath() + "/" + kPxClientName.c_str();
        process->start(client_inner_path, arguments);
        running_processes_.erase(item->stream_id_);
        running_processes_.insert({item->stream_id_, process});
        LOGI("After start client: {}", client_inner_path.toStdString());
    }

    bool RunningStreamManager::StopStream(const std::shared_ptr<px_cms::CmsStream>& item) {
        if (running_processes_.contains(item->stream_id_)) {
            auto process = running_processes_[item->stream_id_];
            if (process) {
                TcDialog dialog(tcTr("id_warning"), tcTr("id_exit_client"), nullptr);
                if (dialog.exec() != kDoneOk) {
                    return false;
                }
                process->kill();
                running_processes_.erase(item->stream_id_);
            }
        }
        context_->SendAppMessage(ClearWorkspace {
            .item_ = item,
        });
        return true;
    }

    void RunningStreamManager::StartFileTransfer(const std::shared_ptr<px_cms::CmsStream>& item, const std::string& network_type) {
        const auto session_id = "ft_" + item->stream_id_ + "_" + std::to_string(QDateTime::currentMSecsSinceEpoch());
        auto process = std::make_shared<QProcess>();
        QStringList args;
        args << "--mode=file-transfer"
             << std::format("--host={}", item->stream_host_).c_str()
             << std::format("--port={}", item->stream_port_).c_str()
             << std::format("--appkey={}", grApp->GetAppkey()).c_str()
             << std::format("--cms_host={}", settings_->GetCmsServerHost()).c_str()
             << std::format("--cms_port={}", settings_->GetCmsServerPort()).c_str()
             << std::format("--cms_ssl={}", settings_->IsCmsSslEnabled()).c_str()
             << std::format("--stream_id={}", session_id).c_str()
             << std::format("--network_type={}", network_type).c_str()
             << std::format("--device_id={}", settings_->GetDeviceId()).c_str()
             << std::format("--remote_device_id={}", item->remote_device_id_).c_str()
             << std::format("--stream_name={}", Base64::Base64Encode(item->stream_name_)).c_str()
             << std::format("--connection_ticket={}", Base64::Base64Encode(item->connection_ticket_)).c_str()
             << std::format("--connection_nonce={}", item->connection_nonce_).c_str()
             << std::format("--relay_host={}", item->relay_host_).c_str()
             << std::format("--relay_port={}", item->relay_port_).c_str()
             << std::format("--relay_appkey={}", grApp->GetAppkey()).c_str()
             << std::format("--language={}", (int)tcTrMgr()->GetSelectedLanguage()).c_str();
        const auto path = qApp->applicationDirPath() + "/" + kPxClientName.c_str();
        process->start(path, args);
        running_processes_.insert({session_id, process});
    }

}
