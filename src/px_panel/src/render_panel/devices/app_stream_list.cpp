//
// Created by RGAA on 2023/8/14.
//

#include "app_stream_list.h"

#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QtWidgets/QMenu>
#include <QWidget>
#include <QProcess>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_set>

#include "px_dialog.h"
#include "px_label.h"
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "widget_helper.h"
#include "stream_messages.h"
#include "stream_item_widget.h"
#include "create_stream_dialog.h"
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_app_messages.h"
#include "running_stream_manager.h"
#include "connection_policy.h"
#include "console_device_state.h"
#include "px_common_new/uid_spacer.h"
#include "px_common_new/hardware.h"
#include "edit_relay_stream_dialog.h"
#include "stream_settings_dialog.h"
#include "start_stream_loading.h"
#include "input_remote_pwd_dialog.h"
#include "stream_state_checker.h"
#include "px_console_client/console_user.h"
#include "px_console_client/console_device.h"
#include "px_console_client/console_user_device.h"
#include "px_console_client/console_user_app_api.h"
#include "render_panel/px_application.h"
#include "render_panel/px_workspace.h"
#include "render_panel/network/render_api.h"
#include "render_panel/database/stream_db_operator.h"
#include "px_base/ct_stream_item_net_type.h"
#include "render_panel/user/px_user_manager.h"
#include "render_panel/util/conn_info_parser.h"
#include "px_common_new/const_auto.h"

namespace px
{

    class MainItemDelegate : public QStyledItemDelegate {
    public:
        explicit MainItemDelegate(QObject* pParent) {}
        ~MainItemDelegate() override = default;

        void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
            editor->setGeometry(option.rect);
        }
    };

    // - - -- - - -- - - - -- -

    AppStreamList::AppStreamList(const std::shared_ptr<PxContext>& ctx,
                                 AppStreamListMode mode,
                                 std::function<void(bool)> on_empty_changed,
                                 QWidget* parent)
        : QWidget(parent), mode_(mode), on_empty_changed_(std::move(on_empty_changed)) {
        context_ = ctx;
        settings_ = PxSettings::Instance();
        db_mgr_ = context_->GetStreamDBManager();
        running_stream_mgr_ = context_->GetRunningStreamManager();
        if (mode_ == AppStreamListMode::kRemoteDevices) {
            int removed_legacy_managed = 0;
            int normalized_direct = 0;
            for (const auto& stream : db_mgr_->GetAllStreamsSortByCreatedTime()) {
                if (!stream) {
                    continue;
                }
                if (connection_policy::IsLegacyManagedConnection(
                        stream->connect_type_, stream->remote_device_id_)) {
                    db_mgr_->DeleteStream(stream->_id);
                    ++removed_legacy_managed;
                }
                else if (connection_policy::IsUnclassifiedDirectConnection(
                             stream->connect_type_, stream->remote_device_id_,
                             stream->stream_host_, stream->stream_port_)) {
                    stream->connect_type_ = connection_policy::kExplicitDirect;
                    stream->force_direct_ = true;
                    db_mgr_->UpdateStream(stream);
                    ++normalized_direct;
                }
            }
            if (removed_legacy_managed > 0 || normalized_direct > 0) {
                LOGI("Connection policy cleanup: removed legacy managed={}, normalized direct={}",
                     removed_legacy_managed, normalized_direct);
            }
        }
        CreateLayout();
        Init();

        setStyleSheet("background-color: #ffffff;");

        //
        QPointer<AppStreamList> self(this);
        if (mode_ == AppStreamListMode::kRemoteDevices) {
            state_checker_ = std::make_shared<StreamStateChecker>(context_);
            state_checker_->SetOnCheckedCallback([=, this](const std::vector<std::shared_ptr<px_console::ConsoleStream>>& stream_items) {
                context_->PostUITask([self, stream_items]() {
                    if (!self) {
                        return;
                    }
                    int count = self->stream_list_->count();
                    for (int i = 0; i < count; i++) {
                        auto item = self->stream_list_->item(i);
                        auto widget = (StreamItemWidget*)self->stream_list_->itemWidget(item);
                        auto stream_id_for_widget = widget->GetStreamId();
                        for (const auto& update_item : stream_items) {
                            if (update_item->stream_id_ == stream_id_for_widget) {
                                widget->SetDirectConnectedState(update_item->direct_online_);
                                widget->SetRelayConnectedState(update_item->relay_online_);
                                widget->SetConsoleConnectedState(update_item->console_online_);
                                widget->Update();
                                break;
                            }
                        }
                    }
                });
            });
            state_checker_->Start();
            context_->PostUIDelayTask([self, ctx = context_]() {
                if (!self) {
                    return;
                }
                ctx->PostTask([self]() {
                    if (!self || !self->state_checker_) {
                        return;
                    }
                    cat streams = self->CopyStreams();
                    self->state_checker_->UpdateCurrentStreamItems(streams);
                });
            }, 2200);
        }

        // Load Console resources for both signed-in users and anonymous Panel
        // sessions. Public applications must be visible on the first screen;
        // requiring a login event or a manual refresh leaves guest access
        // unreachable.
        context_->PostUIDelayTask([self, ctx = context_]() {
            if (!self) {
                return;
            }
            ctx->PostTask([self]() {
                if (self) {
                    self->RefreshResources();
                }
            });
        }, 300);
    }

    AppStreamList::~AppStreamList() = default;

    void AppStreamList::CreateLayout() {
        auto root_layout = new QHBoxLayout();
        WidgetHelper::ClearMargins(root_layout);

        auto delegate = new MainItemDelegate(this);
        stream_list_ = new QListWidget(this);
        stream_list_->setItemDelegate(delegate);

        stream_list_->setMovement(QListView::Static);
        stream_list_->setViewMode(QListView::IconMode);
        stream_list_->setFlow(QListView::LeftToRight);
        stream_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        stream_list_->setResizeMode(QListWidget::Adjust);
        stream_list_->setContextMenuPolicy(Qt::CustomContextMenu);
        stream_list_->setSpacing(15);
        stream_list_->setStyleSheet(R"(
            QListWidget::item {
                color: #000000;
                border: transparent;
                border-bottom: 0px solid #dbdbdb;
            }

            QListWidget::item:hover {
                background-color: none;
            }

            QListWidget::item:selected {
                border-left: 0px solid #777777;
                background-color: none;
            }
        )");

        connect(stream_list_, &QListWidget::customContextMenuRequested, this, [=, this](const QPoint& pos) {
            QListWidgetItem* cur_item = stream_list_->itemAt(pos);
            if (cur_item == nullptr) { return; }
            int index = stream_list_->row(cur_item);
            RegisterActions(index, cur_item);
        });

        connect(stream_list_, &QListWidget::itemDoubleClicked, this, [=, this](QListWidgetItem *item) {
            const int index = stream_list_->row(item);
            const auto stream_item = streams_.at(index);
            StartStream(item, stream_item, false);
        });

        root_layout->addSpacing(10);
        root_layout->addWidget(stream_list_);
        root_layout->addSpacing(10);

        setLayout(root_layout);
    }

    void AppStreamList::Init() {
        msg_listener_ = context_->ObtainUIMessageListener();
        QPointer<AppStreamList> self(this);
        if (mode_ == AppStreamListMode::kRemoteDevices) {
            msg_listener_->Listen<StreamItemAdded>([=, this](const StreamItemAdded& msg) {
            auto item = msg.item_;
            std::shared_ptr<px_console::ConsoleStream> exist_stream_item = nullptr;
            // by stream id
            {
                auto opt_stream = db_mgr_->GetStreamByStreamId(item->stream_id_);
                if (opt_stream.has_value()) {
                    exist_stream_item = opt_stream.value();
                }
            }

            if (!item->remote_device_id_.empty()) {
                // by remote device id
                auto opt_stream = db_mgr_->GetStreamByRemoteDeviceId(item->remote_device_id_);
                if (opt_stream.has_value()) {
                    exist_stream_item = opt_stream.value();
                }
            }
            else {
                // by host & port
                auto opt_stream = db_mgr_->GetStreamByHostPort(item->stream_host_, item->stream_port_);
                if (opt_stream.has_value()) {
                    exist_stream_item = opt_stream.value();
                }
            }
            if (!exist_stream_item) {
                db_mgr_->AddStream(item);
                exist_stream_item = item;
            }
            else {
                // todo: Check stream info.
                // check password type: random / safety
                // then update it in database
                if (!item->stream_name_.empty()) {
                    exist_stream_item->stream_name_ = item->stream_name_;
                }
                if (!item->remote_device_id_.empty()) {
                    exist_stream_item->remote_device_id_ = item->remote_device_id_;
                }
                if (!item->connect_type_.empty()) {
                    exist_stream_item->connect_type_ = item->connect_type_;
                    exist_stream_item->force_direct_ = item->force_direct_;
                    exist_stream_item->use_webrtc_ = item->use_webrtc_;
                    if (connection_policy::IsConsoleTicket(item->connect_type_)) {
                        exist_stream_item->stream_host_.clear();
                        exist_stream_item->stream_port_ = 0;
                        exist_stream_item->relay_host_.clear();
                        exist_stream_item->relay_port_ = 0;
                        exist_stream_item->remote_device_random_pwd_.clear();
                        exist_stream_item->remote_device_safety_pwd_.clear();
                    }
                }
                if (!item->stream_host_.empty()) {
                    exist_stream_item->stream_host_ = item->stream_host_;
                }
                if (item->stream_port_ > 0) {
                    exist_stream_item->stream_port_ = item->stream_port_;
                }
                if (!item->relay_host_.empty()) {
                    exist_stream_item->relay_host_ = item->relay_host_;
                }
                if (item->relay_port_ > 0) {
                    exist_stream_item->relay_port_ = item->relay_port_;
                }
                //if (!item->relay_appkey_.empty()) {
                //    exist_stream_item->relay_appkey_ = item->relay_appkey_;
                //}
                if (exist_stream_item->remote_device_random_pwd_ != item->remote_device_random_pwd_ && !item->remote_device_random_pwd_.empty()) {
                    exist_stream_item->remote_device_random_pwd_ = item->remote_device_random_pwd_;
                }
                if (exist_stream_item->remote_device_safety_pwd_ != item->remote_device_safety_pwd_ && !item->remote_device_safety_pwd_.empty()) {
                    exist_stream_item->remote_device_safety_pwd_ = item->remote_device_safety_pwd_;
                }
                db_mgr_->UpdateStream(exist_stream_item);
            }
            LoadStreamItems();

            LOGI("Auto start stream: {}", msg.auto_start_);
            context_->PostUIDelayTask([self, auto_start = msg.auto_start_, exist_stream_item]() {
                if (!self) {
                    return;
                }
                if (auto_start) {
                    self->StartStream(nullptr, exist_stream_item, false);
                }
            }, 70);
            });

            msg_listener_->Listen<StreamItemUpdated>([=, this](const StreamItemUpdated& msg) {
                db_mgr_->UpdateStream(msg.item_);
                LoadStreamItems();
                LOGI("Update stream : {}", msg.item_->stream_id_);
            });

            msg_listener_->Listen<MsgRemotePeerInfo>([=, this](const MsgRemotePeerInfo& msg) {
                std::lock_guard<std::mutex> guard(streams_mtx_);
                for (const auto& stream : streams_) {
                    if (stream->stream_id_ == msg.stream_id_) {
                        if (stream->desktop_name_ != msg.desktop_name_ || stream->os_version_ != msg.os_version_) {
                            stream->desktop_name_ = msg.desktop_name_;
                            stream->os_version_ = msg.os_version_;
                            db_mgr_->UpdateStream(stream);
                        }
                        break;
                    }
                }
            });

            msg_listener_->Listen<MsgClientConnectedPanel>([=, this](const MsgClientConnectedPanel& msg) {

            });

            msg_listener_->Listen<MsgForceClearProgramData>([=, this](const MsgForceClearProgramData& msg) {
                this->LoadStreamItems();
            });
        }

        msg_listener_->Listen<MsgGrTimer5S>([=, this](const MsgGrTimer5S& msg) {
            context_->PostTask([self]() {
                if (!self) {
                    return;
                }
                if (self->state_checker_) {
                    cat streams = self->CopyStreams();
                    self->state_checker_->UpdateCurrentStreamItems(streams);
                }
                self->RefreshResources();
            });
        });

        msg_listener_->Listen<MsgUserLoggedIn>([self, ctx = context_](const MsgUserLoggedIn& msg) {
            ctx->PostTask([self]() {
                if (!self) {
                    return;
                }
                self->ClearIdentityResources();
                self->RefreshResources();
            });
        });

        msg_listener_->Listen<MsgUserLoggedOut>([self, ctx = context_](const MsgUserLoggedOut& msg) {
            ctx->PostTask([self]() {
                if (!self) {
                    return;
                }
                self->ClearIdentityResources();
                self->RefreshResources();
            });
        });
    }

    void AppStreamList::RegisterActions(int index, QListWidgetItem* cur_item) {
        const auto stream = streams_.at(index);
        if (stream->connect_type_ == connection_policy::kConsoleAppTicket) {
            auto menu = new QMenu();
            auto connect_action = menu->addAction(tcTr(
                stream->console_instance_state_ == "running"
                    ? "id_enter_application"
                    : "id_start_application"));
            auto view_action = menu->addAction(tcTr("id_only_viewing"));
            auto stop_action = menu->addAction(tcTr("id_stop_application"));
            connect(connect_action, &QAction::triggered, this,
                    [=, this]() { StartStream(cur_item, stream, false); });
            connect(view_action, &QAction::triggered, this,
                    [=, this]() { StartStream(cur_item, stream, true); });
            connect(stop_action, &QAction::triggered, this,
                    [=, this]() { StopStream(stream); });
            menu->exec(QCursor::pos());
            delete menu;
            return;
        }
        std::vector<QString> actions = {
            tcTr("id_start_control"),
            tcTr("id_stop_control"),
            tcTr("id_only_viewing"),
            tcTr("id_file_transfer"),
            tcTr("id_lock_device"),
            tcTr("id_restart_device"),
            tcTr("id_shutdown_device"),
            "",
            tcTr("id_edit"),
            tcTr("id_delete"),
            "",
            tcTr("id_settings"),
        };
        auto menu = new QMenu();
        for (int i = 0; i < actions.size(); i++) {
            const QString& action_name = actions.at(i);
            if (action_name.isEmpty()) {
                menu->addSeparator();
                continue;
            }

            auto action = new QAction(action_name, menu);
            menu->addAction(action);
            connect(action, &QAction::triggered, this, [=, this]() {
                ProcessAction(i, cur_item, streams_.at(index));
            });
        }
        menu->exec(QCursor::pos());
        delete menu;
    }

    void AppStreamList::ProcessAction(int index, QListWidgetItem* cur_item, const std::shared_ptr<px_console::ConsoleStream>& item) {
        if (index == 0) {
            // connect
            StartStream(cur_item, item, false);
        }
        else if (index == 1) {
            // stop
            StopStream(item);
        }
        else if (index == 2) {
            // only viewing
            StartStream(cur_item, item, true);
        }
        else if (index == 3) {
            StartFileTransfer(item);
        }
        else if (index == 4) {
            // lock device
            LockDevice(item);
        }
        else if (index == 5) {
            // restart device
            RestartDevice(item);
        }
        else if (index == 6) {
            // shutdown device
            ShutdownDevice(item);
        }
        // "" 7
        else if (index == 8) {
            // edit
            EditStream(item);
        }
        else if (index == 9) {
            // delete
            DeleteStream(item);
        }
        // "" 10
        else if (index == 11) {
            ShowSettings(item);
        }
    }

    void AppStreamList::StartStream(QListWidgetItem* cur_item, const std::shared_ptr<px_console::ConsoleStream>& item, bool force_only_viewing) {
        if (cur_item) {
            if (const auto widget = static_cast<StreamItemWidget *>(stream_list_->itemWidget(cur_item))) {
                widget->ShowConnecting();
            }
        }
        QPointer<AppStreamList> self(this);
        context_->PostUIDelayTask([self, cur_item, item, force_only_viewing]() {
            if (!self) {
                return;
            }
            self->StartStreamInternal(cur_item, item, force_only_viewing);
        }, 40);
    }

    void AppStreamList::StartStreamInternal(QListWidgetItem* cur_item, const std::shared_ptr<px_console::ConsoleStream>& item, bool force_only_viewing) {
        std::shared_ptr<px_console::ConsoleStream> target_item;
        const bool uses_console_app_ticket = item->connect_type_ == connection_policy::kConsoleAppTicket;
        if (uses_console_app_ticket) {
            target_item = item;
        } else {
            auto si = db_mgr_->GetStreamByStreamId(item->stream_id_);
            if (!si.has_value()) {
                LOGE("read stream item from db failed: {}", item->stream_id_);
                return;
            }
            target_item = si.value();
        }
        // This is a per-launch choice. Do not let an earlier view-only
        // connection permanently downgrade later normal connections.
        target_item->only_viewing_ = force_only_viewing;

        // A saved link:// share remains a password-bearing direct entry while
        // signed out. When a Console user is signed in, upgrade only this launch
        // to a device ticket without overwriting the saved share credentials.
        if (connection_policy::SharedLinkUsesConsoleTicket(
                target_item->connect_type_, grApp->GetUserManager()->IsLoggedIn())) {
            target_item = std::make_shared<px_console::ConsoleStream>(*target_item);
            target_item->connect_type_ = connection_policy::kConsoleDeviceTicket;
        }

        const auto launch_policy = connection_policy::Classify(
            target_item->connect_type_, target_item->remote_device_id_,
            target_item->stream_host_, target_item->stream_port_);
        if (launch_policy == connection_policy::LaunchPolicy::kReject) {
            LOGE("Reject stream with unsupported connection policy: type={}, remote_device_id={}, endpoint={}:{}",
                 target_item->connect_type_, target_item->remote_device_id_,
                 target_item->stream_host_, target_item->stream_port_);
            TcDialog dialog(tcTr("id_connect_failed"), tcTr("id_connection_ticket_required"),
                            grWorkspace.get());
            dialog.exec();
            return;
        }
        const bool uses_console_ticket = launch_policy == connection_policy::LaunchPolicy::kConsoleTicket;
        if (uses_console_ticket) {
            const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            std::vector<std::string> permissions {"view"};
            // File transfer is a separate authenticated capability, not an
            // input-control capability. A signed-in user's view-only session
            // must still be reusable by the Panel file-transfer entry.
            if (grApp->GetUserManager()->IsLoggedIn()) {
                permissions.push_back("file");
            }
            if (!target_item->only_viewing_) {
                permissions.push_back("input");
                // Anonymous public-app sessions deliberately remain view/input
                // only. Sensitive capabilities are granted only to an
                // authenticated user and are still enforced by the render end.
                if (grApp->GetUserManager()->IsLoggedIn()) {
                    permissions.insert(permissions.end(), {"clipboard", "audio"});
                }
            }
            px::Result<px_console::ConsoleConnectionTicket, px_console::ConsoleApiError> ticket_result =
                TcErr(px_console::ConsoleApiError::kInvalidParams);
            if (uses_console_app_ticket) {
                if (target_item->console_instance_id_.empty()) {
                    auto start_result = grApp->GetUserManager()->StartApp(target_item->console_app_id_, nonce);
                    if (!start_result.has_value()) {
                        LOGE("Console application start failed or did not reach running");
                        auto error_message = px_console::ConsoleApiLastErrorMessage();
                        TcDialog dialog(tcTr("id_connect_failed"),
                                        error_message.empty()
                                            ? tcTr("id_start_failed")
                                            : QString::fromStdString(error_message),
                                        grWorkspace.get());
                        dialog.exec();
                        return;
                    }
                    auto instance = start_result.value();
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                    while (instance.state == "starting" && std::chrono::steady_clock::now() < deadline) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(800));
                        const auto apps_result = grApp->GetUserManager()->QueryApps();
                        if (!apps_result.has_value()) continue;
                        const auto app_it = std::find_if(apps_result.value().begin(), apps_result.value().end(),
                            [&target_item](const px_console::ConsoleUserApplication& app) {
                                return app.app_id == target_item->console_app_id_;
                            });
                        if (app_it != apps_result.value().end() && app_it->running_instance
                            && app_it->running_instance->instance_id == instance.instance_id) {
                            instance = *app_it->running_instance;
                        }
                    }
                    if (instance.state != "running") {
                        LOGE("Console application instance did not reach running, state: {}", instance.state);
                        TcDialog dialog(tcTr("id_connect_failed"), tcTr("id_start_failed"),
                                        grWorkspace.get());
                        dialog.exec();
                        return;
                    }
                    target_item->console_instance_id_ = instance.instance_id;
                    target_item->console_instance_state_ = instance.state;
                    target_item->console_online_ = true;
                    target_item->direct_online_ = true;
                }
                ticket_result = grApp->GetUserManager()->IssueInstanceTicket(
                    target_item->console_instance_id_, nonce, permissions);
            } else {
                ticket_result = grApp->GetUserManager()->IssueDeviceTicket(
                    target_item->remote_device_id_, nonce, permissions);
            }
            if (!ticket_result.has_value()) {
                LOGE("Console connection ticket request failed: {}", static_cast<int>(ticket_result.error()));
                if (uses_console_app_ticket) {
                    // The instance may have stopped or belonged to an expired
                    // guest session. A subsequent click should start a fresh
                    // owned instance instead of retrying the stale id.
                    target_item->console_instance_id_.clear();
                }
                auto error_message = px_console::ConsoleApiLastErrorMessage();
                TcDialog dialog(tcTr("id_connect_failed"),
                                error_message.empty()
                                    ? tcTr("id_op_error")
                                    : QString::fromStdString(error_message),
                                grWorkspace.get());
                dialog.exec();
                return;
            }
            const auto& ticket = ticket_result.value();
            const QUrl launch_url(QString::fromStdString(ticket.launch_url));
            if (!launch_url.isValid() || launch_url.host().isEmpty() || launch_url.port() <= 0) {
                LOGE("Console returned an invalid device launch endpoint");
                TcDialog dialog(tcTr("id_connect_failed"),
                                "The Console returned an invalid device endpoint", grWorkspace.get());
                dialog.exec();
                return;
            }
            target_item->stream_host_ = launch_url.host().toStdString();
            target_item->stream_port_ = launch_url.port();
            if (uses_console_app_ticket) {
                const QUrlQuery query(launch_url);
                target_item->remote_device_id_ = query.queryItemValue("deviceId").toStdString();
            }
            target_item->connection_ticket_ = ticket.ticket;
            target_item->connection_nonce_ = nonce;
            target_item->rtc_ice_config_json_ = ticket.rtc_ice_config_json;
            // Console tickets may be issued for newly scheduled instances
            // that have no persisted stream row yet. Standard RTC still needs
            // the Console Relay for SDP/ICE signaling, so fill it from the
            // authenticated Console access configuration when absent.
            target_item->relay_host_ = !ticket.relay_host.empty()
                ? ticket.relay_host : settings_->GetRelayServerHost();
            target_item->relay_port_ = ticket.relay_port > 0
                ? ticket.relay_port : settings_->GetRelayServerPort();
            const auto has_permission = [&ticket](const char* permission) {
                return std::find(ticket.permissions.begin(), ticket.permissions.end(), permission)
                    != ticket.permissions.end();
            };
            target_item->clipboard_enabled_ = has_permission("clipboard");
            target_item->audio_enabled_ = has_permission("audio");
        }

        bool direct_probe_enabled = true;
        if (uses_console_ticket) {
            try {
                if (!target_item->rtc_ice_config_json_.empty()) {
                    direct_probe_enabled = nlohmann::json::parse(
                        target_item->rtc_ice_config_json_).value("direct_probe_enabled", true);
                }
            }
            catch (const std::exception& error) {
                LOGW("Invalid RTC ICE config in ticket response: {}", error.what());
                direct_probe_enabled = false;
            }
        }

        // During the standard-RTC test phase the Console switch is off and no
        // direct request is made. Once enabled, this lightweight Render HTTP
        // request is the first-stage reachability probe for net_rtc_local.
        bool direct_available = false;
        if (!uses_console_ticket || direct_probe_enabled) {
            direct_available = RenderApi::GetRenderConfiguration(
                target_item->stream_host_, target_item->stream_port_).has_value();
        }

        if (uses_console_ticket && (!direct_available || target_item->force_relay_)) {
            if (!target_item->HasRelayInfo()) {
                LOGE("Standard RTC requires signaling Relay information");
                TcDialog dialog(tcTr("id_connect_failed"), tcTr("id_cant_get_remote_device_info"),
                                grWorkspace.get());
                dialog.exec();
                return;
            }
            LOGI("RTC route selected: {}, direct_probe_enabled={}, direct_available={}",
                 kStreamItemNtTypeWebRTC, direct_probe_enabled, direct_available);
            running_stream_mgr_->StartStream(target_item, kStreamItemNtTypeWebRTC, false);
            return;
        }

        if (direct_available && !target_item->force_relay_) {
            LOGI("We can connect directly: {}:{}", target_item->stream_host_, target_item->stream_port_);
            // verify device password before launching the client(same idea as the relay flow):
            // safety pwd(md5) preferred, fall back to md5(random pwd); re-ask on failure.
            // note: an empty candidate is fine, the render passes it when the device has no password.
            auto candidate_pwd_md5 = !target_item->remote_device_safety_pwd_.empty()
                                     ? target_item->remote_device_safety_pwd_
                                     : (!target_item->remote_device_random_pwd_.empty()
                                        ? MD5::Hex(target_item->remote_device_random_pwd_) : std::string(""));
            auto ok = uses_console_ticket || RenderApi::VerifySecurityPassword(
                target_item->stream_host_, target_item->stream_port_, candidate_pwd_md5).value_or(false);
            for (; !uses_console_ticket;) {
                LOGI("VerifySecurityPassword result: {}", ok);
                if (ok) {
                    break;
                }
                InputRemotePwdDialog dlg_input_pwd(context_);
                if (dlg_input_pwd.exec() == 1) {
                    return;
                }
                auto input_password = dlg_input_pwd.GetInputPassword();
                if (input_password.isEmpty()) {
                    context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_input_necessary_info"));
                    continue;
                }

                // md5 pwd
                auto pwd_md5 = MD5::Hex(input_password.toStdString());

                ok = RenderApi::VerifySecurityPassword(target_item->stream_host_, target_item->stream_port_,
                                                       pwd_md5).value_or(false);
                if (!ok) {
                    context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_password_invalid_msg"));
                }
                else {
                    // use it right away for this launch(DB update below is async)
                    target_item->remote_device_safety_pwd_ = pwd_md5;
                    // update to database
                    QPointer<AppStreamList> self(this);
                    context_->PostDBTask([self, target_item, pwd_md5]() {
                        if (!self) {
                            return;
                        }
                        auto mgr = self->context_->GetStreamDBManager();
                        mgr->UpdateStreamSafetyPwd(target_item->stream_id_, pwd_md5);
                        self->LoadStreamItems();
                    });
                    break;
                }
            }

            // start via udp, webrtc or websocket, depends on the "use_udp"/"use_webrtc" options
            if (uses_console_ticket) {
                LOGI("RTC route selected: {}, direct probe succeeded",
                     kStreamItemNtTypeWebRTCDirect);
                running_stream_mgr_->StartStream(
                    target_item, kStreamItemNtTypeWebRTCDirect, true);
            }
            else if (target_item->use_udp_) {
                running_stream_mgr_->StartStream(target_item, kStreamItemNtTypeUdpDirect, true);
            }
            else if (target_item->use_webrtc_) {
                running_stream_mgr_->StartStream(target_item, kStreamItemNtTypeWebRTCDirect, true);
            }
            else {
                running_stream_mgr_->StartStream(target_item, kStreamItemNtTypeWebSocket, true);
            }
        }
        else {
            // Explicit IP:port connections never fall back to the Console Relay.
            // Console-managed connections returned earlier through the standard
            // RTC route, so reaching here means the direct endpoint is offline.
            LOGW("Explicit direct endpoint is unavailable: {}:{}",
                 target_item->stream_host_, target_item->stream_port_);
            TcDialog dialog(tcTr("id_connect_failed"), tcTr("id_device_offline"),
                            grWorkspace.get());
            dialog.exec();
        }
    }

    bool AppStreamList::StopStream(const std::shared_ptr<px_console::ConsoleStream>& item) {
        if (item->connect_type_ == connection_policy::kConsoleAppTicket) {
            if (!running_stream_mgr_->StopStream(item)) {
                return false;
            }
            if (item->console_instance_id_.empty()) return true;
            const auto instance_id = item->console_instance_id_;
            QPointer<AppStreamList> self(this);
            context_->PostTask([self, item, instance_id]() {
                if (!self) return;
                const auto result = grApp->GetUserManager()->StopInstance(instance_id);
                if (result.has_value()) {
                    item->console_instance_id_.clear();
                    item->console_instance_state_ = "stopped";
                    self->RefreshResources();
                } else {
                    const auto error_message = px_console::ConsoleApiLastErrorMessage();
                    self->context_->NotifyAppErrMessage(
                        tcTr("id_error"),
                        error_message.empty() ? tcTr("id_op_error") : QString::fromStdString(error_message));
                }
            });
            return true;
        }
        auto si = db_mgr_->GetStreamByStreamId(item->stream_id_);
        if (!si.has_value()) {
            LOGE("read stream item from db failed: {}", item->stream_id_);
            return false;
        }
        return running_stream_mgr_->StopStream(si.value());
    }

    void AppStreamList::LockDevice(const std::shared_ptr<px_console::ConsoleStream>& item) {
        if (!item->direct_online_ && !item->relay_online_) {
            context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_device_offline"));
            return;
        }

        TcDialog dialog(tcTr("id_warning"), tcTr("id_ask_lock_screen"));
        if (dialog.exec() == kDoneOk) {
            auto msg = std::make_shared<PxSmLockScreen>();
            msg->stream_item_ = item;
            grApp->PostMessage2RemoteRender(msg);
        }
    }

    void AppStreamList::RestartDevice(const std::shared_ptr<px_console::ConsoleStream>& item) {
        if (!item->direct_online_ && !item->relay_online_) {
            context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_device_offline"));
            return;
        }

        TcDialog dialog(tcTr("id_warning"), tcTr("id_ask_restart_device"));
        if (dialog.exec() == kDoneOk) {
            auto msg = std::make_shared<PxSmRestartDevice>();
            msg->stream_item_ = item;
            grApp->PostMessage2RemoteRender(msg);
        }
    }

    void AppStreamList::ShutdownDevice(const std::shared_ptr<px_console::ConsoleStream>& item) {
        if (!item->direct_online_ && !item->relay_online_) {
            context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_device_offline"));
            return;
        }

        TcDialog dialog(tcTr("id_warning"), tcTr("id_ask_shutdown_device"));
        if (dialog.exec() == kDoneOk) {
            auto msg = std::make_shared<PxSmShutdownDevice>();
            msg->stream_item_ = item;
            grApp->PostMessage2RemoteRender(msg);
        }
    }

    void AppStreamList::EditStream(const std::shared_ptr<px_console::ConsoleStream>& item) {
        auto si = db_mgr_->GetStreamByStreamId(item->stream_id_);
        if (!si.has_value()) {
            LOGE("read stream item from db failed: {}", item->stream_id_);
            return;
        }

        auto dialog = new EditRelayStreamDialog(context_, si.value(), grWorkspace.get());
        dialog->exec();

//        if (item->HasRelayInfo()) {
//            auto dialog = new EditRelayStreamDialog(context_, si.value(), grWorkspace.get());
//            dialog->exec();
//        }
//        else {
//            auto dialog = new CreateStreamDialog(context_, si.value(), grWorkspace.get());
//            dialog->exec();
//        }
    }

    void AppStreamList::DeleteStream(const std::shared_ptr<px_console::ConsoleStream>& item) {
        TcDialog dialog(tcTr("id_warning"), tcTr("id_delete_remote_control"), grWorkspace.get());
        if (dialog.exec() == kDoneOk) {
            // stop it if running
            if (!StopStream(item)) {
                return;
            }
            // delete it from database
            auto mgr = context_->GetStreamDBManager();
            mgr->DeleteStream(item->_id);

            LoadStreamItems();
        }
    }

    void AppStreamList::ShowSettings(const std::shared_ptr<px_console::ConsoleStream>& item) {
        auto si = db_mgr_->GetStreamByStreamId(item->stream_id_);
        if (!si.has_value()) {
            LOGE("read stream item from db failed: {}", item->stream_id_);
            return;
        }
        auto dialog = new StreamSettingsDialog(context_, si.value(), grWorkspace.get());
        dialog->exec();
    }

    QListWidgetItem* AppStreamList::AddItem(const std::shared_ptr<px_console::ConsoleStream>& stream, int index) {
        auto item = new QListWidgetItem(stream_list_);
        item->setSizeHint(QSize(230, 150));
        auto widget = new StreamItemWidget(stream, stream->bg_color_, stream_list_);
        widget->setObjectName(stream->stream_id_.c_str());
        WidgetHelper::AddShadow(widget, 0xbbbbbb, 8);
        widget->SetOnConnectListener([=, this]() {
            StartStream(item, stream, false);
        });
        widget->SetOnMenuListener([=, this]() {
            RegisterActions(index, item);
        });
        widget->SetDirectConnectedState(stream->direct_online_);
        widget->SetRelayConnectedState(stream->relay_online_);
        widget->SetConsoleConnectedState(stream->console_online_);

        auto root_layout = new QVBoxLayout();
        WidgetHelper::ClearMargins(root_layout);
        root_layout->setContentsMargins(2, 0, 2, 0);

        auto layout = new QVBoxLayout();
        layout->addStretch();
        WidgetHelper::ClearMargins(layout);
        root_layout->addLayout(layout);

        auto gap = 0;//5;

        // name
        auto name = new QLabel(stream_list_);
        name->hide();
        name->setObjectName("st_name");
        auto stream_name = stream->stream_name_;
        if (stream->HasRelayInfo()) {
            stream_name = px::SpaceId(stream_name);
        }
        name->setText(stream_name.c_str());
        name->setStyleSheet(R"(color:#386487; font-size:14px; font-weight:bold; background-color:#909099;)");
        layout->addWidget(name);

        // host
        auto host = new QLabel(stream_list_);
        host->hide();
        host->setObjectName("st_host");
        host->setText(stream->stream_host_.c_str());
        host->setStyleSheet(R"(color:#386487; font-size:14px; )");
        layout->addSpacing(gap);
        layout->addWidget(host);

        //
        auto port = new QLabel(stream_list_);
        port->hide();
        port->setObjectName("st_port");
        port->setText(std::to_string(stream->stream_port_).c_str());
        port->setStyleSheet(R"(color:#386487; font-size:14px; )");
        layout->addSpacing(gap);
        layout->addWidget(port);

        //
        auto bitrate = new QLabel(stream_list_);
        bitrate->hide();
        bitrate->setObjectName("st_bitrate");
        std::string bt_str = std::to_string(stream->encode_bps_) + " Mbps";
        bitrate->setText(bt_str.c_str());
        bitrate->setStyleSheet(R"(color:#386487; font-size:14px; )");
        layout->addSpacing(gap);
        layout->addWidget(bitrate);

        auto fps = new QLabel(stream_list_);
        fps->hide();
        fps->setObjectName("st_fps");
        std::string fps_str = std::to_string(stream->encode_fps_) + " FPS";
        fps->setText(fps_str.c_str());
        fps->setStyleSheet(R"(color:#386487; font-size:14px; )");
        layout->addSpacing(gap);
        layout->addWidget(fps);

        //layout->addStretch();

        root_layout->addLayout(layout);
        //layout->addSpacing(6);
        widget->setLayout(root_layout);
        stream_list_->setItemWidget(item, widget);
        return item;
    }

    QWidget* AppStreamList::GetItemByStreamId(const std::string& stream_id) {
        int count = stream_list_->count();
        for (int i = 0; i < count; i++) {
            auto item = stream_list_->item(i);
            auto widget = stream_list_->itemWidget(item);
            if (widget->objectName().toStdString() == stream_id) {
                return widget;
            }
        }
        return nullptr;
    }

    void AppStreamList::LoadStreamItems() {
        QPointer<AppStreamList> self(this);
        context_->PostUITask([self]() {
            if (!self) {
                return;
            }
            {
                std::lock_guard<std::mutex> guard(self->streams_mtx_);
                if (self->mode_ == AppStreamListMode::kCloudApplications) {
                    self->streams_ = self->console_app_streams_;
                } else {
                    auto db_mgr = self->context_->GetStreamDBManager();
                    self->streams_ = db_mgr->GetAllStreamsSortByCreatedTime();
                    std::erase_if(self->streams_, [](const auto& stream) {
                        return stream && stream->connect_type_ == connection_policy::kConsoleAppTicket;
                    });
                    ApplyConsoleDeviceOnlineStates(
                        self->streams_, self->console_device_online_states_);
                }

                // bench test
                // auto fn_rand_a_upper_char = []() -> char {
                //     char c = 'A' + rand() % 26;
                //     return c;
                // };
                // for (int i = 0; i < 100; i++) {
                //     auto st = std::make_shared<px_console::ConsoleStream>();
                //     st->stream_name_ = std::format("Desktop: {}", i+1);
                //     st->stream_host_ = std::format("192.168.1.{}", i+5);
                //     st->desktop_name_ = StringUtil::ToUpperCpy(std::format("DESKTOP-{}{}{}{}{}", fn_rand_a_upper_char(), fn_rand_a_upper_char(), fn_rand_a_upper_char(), fn_rand_a_upper_char(), fn_rand_a_upper_char()));
                //     streams_.push_back(st);
                // }

                int count = self->stream_list_->count();
                for (int i = 0; i < count; i++) {
                    auto item = self->stream_list_->takeItem(0);
                    delete item;
                }

                int index = 0;
                for (auto& stream : self->streams_) {
                    stream->device_id_ = self->settings_->GetDeviceId();
                    self->AddItem(stream, index++);
                }

                if (self->on_empty_changed_) {
                    self->on_empty_changed_(self->streams_.empty());
                }
            }

            // update to stream state checker
            if (self->state_checker_) {
                cat streams = self->CopyStreams();
                self->state_checker_->UpdateCurrentStreamItems(streams);
            }
        });
    }

    void AppStreamList::StartFileTransfer(const std::shared_ptr<px_console::ConsoleStream>& item) {
        if (!grApp->GetUserManager()->IsLoggedIn() || item->remote_device_id_.empty()) {
            context_->NotifyAppErrMessage(tcTr("id_error"), "File transfer requires a signed-in Console user.");
            return;
        }
        // A normal remote-control client already has a file-capable transport
        // (signed-in control tickets include the file permission). Reuse it and
        // avoid a second process, ticket and competing RTC LAN connection.
        if (running_stream_mgr_->OpenFileTransferInRunningClient(item)) {
            return;
        }
        auto target = db_mgr_->GetStreamByStreamId(item->stream_id_);
        if (!target.has_value()) return;
        auto launch = target.value();
        const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        auto ticket = grApp->GetUserManager()->IssueDeviceTicket(launch->remote_device_id_, nonce, {"file"});
        if (!ticket.has_value()) {
            const auto message = ticket.error() == px_console::ConsoleApiError::kNotFound
                ? tcTr("id_file_transfer_device_unavailable")
                : QString::fromStdString(px_console::ConsoleApiLastErrorMessage());
            context_->NotifyAppErrMessage(
                tcTr("id_error"), message.isEmpty() ? tcTr("id_op_error") : message);
            return;
        }
        const QUrl url(QString::fromStdString(ticket.value().launch_url));
        if (!url.isValid() || url.host().isEmpty() || url.port() <= 0) return;
        launch->stream_host_ = url.host().toStdString(); launch->stream_port_ = url.port();
        launch->connection_ticket_ = ticket.value().ticket; launch->connection_nonce_ = nonce;
        const bool direct_available = RenderApi::GetRenderConfiguration(
            launch->stream_host_, launch->stream_port_).has_value();
        if (!launch->force_relay_ && direct_available) {
            // Standalone file transfer uses the reliable WS endpoint. RTC LAN
            // on the Render side is single-session; trying to create another
            // RTC client can take over an active control connection and consume
            // the one-time ticket during its retry. When a normal RTC client is
            // present, the branch above reuses that RTC transport instead.
            running_stream_mgr_->StartFileTransfer(launch, kStreamItemNtTypeWebSocket);
            return;
        }
        if (!launch->HasRelayInfo()) {
            context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_cant_get_remote_device_info"));
            return;
        }
        running_stream_mgr_->StartFileTransfer(launch, kStreamItemNtTypeRelay);
    }

    void AppStreamList::RefreshResources() {
        if (resource_refresh_inflight_.exchange(true)) {
            return;
        }
        struct RefreshGuard {
            std::atomic_bool& flag;
            ~RefreshGuard() { flag.store(false); }
        } refresh_guard {resource_refresh_inflight_};

        if (mode_ == AppStreamListMode::kCloudApplications) {
            RefreshCloudApplications();
        } else {
            RefreshRemoteDevices();
        }
        LoadStreamItems();
    }

    void AppStreamList::ClearIdentityResources() {
        if (mode_ == AppStreamListMode::kCloudApplications) {
            {
                std::lock_guard<std::mutex> guard(streams_mtx_);
                console_app_streams_.clear();
            }
            LoadStreamItems();
            return;
        }

        {
            std::lock_guard<std::mutex> guard(streams_mtx_);
            console_device_online_states_.clear();
        }

        for (const auto& stream : db_mgr_->GetAllStreamsSortByCreatedTime()) {
            if (stream && stream->connect_type_ == connection_policy::kConsoleDeviceTicket) {
                // Identity changes revoke future ticket creation but do not
                // terminate an already-established client process.
                db_mgr_->DeleteStream(stream->_id);
            }
        }
        LoadStreamItems();
    }

    void AppStreamList::RefreshRemoteDevices() {
        auto user_mgr = grApp->GetUserManager();
        const auto user_devices_result = user_mgr->QueryBindDevices(1, 200, false);
        if (user_devices_result.has_value()) {
            const auto& user_devices = user_devices_result.value();
            std::unordered_set<std::string> available_device_ids;
            ConsoleDeviceOnlineStates online_states;
            for (const auto& ud : user_devices) {
                if (!ud->device_id_.empty() && ud->device_) {
                    available_device_ids.insert(ud->device_id_);
                    online_states[ud->device_id_] = ud->device_->active_;
                }
            }
            {
                std::lock_guard<std::mutex> guard(streams_mtx_);
                console_device_online_states_ = std::move(online_states);
            }

            // Console-sourced devices are a projection of the current identity, not a
            // permanent local address-book entry. Only reconcile after a successful
            // response; a network error must not look like an empty Console catalog.
            for (const auto& stream : db_mgr_->GetAllStreamsSortByCreatedTime()) {
                if (stream->connect_type_ == connection_policy::kConsoleDeviceTicket
                    && !available_device_ids.contains(stream->remote_device_id_)) {
                    // The device was removed from Console. Remove only the projected
                    // card; do not interrupt an already-established local client.
                    db_mgr_->DeleteStream(stream->_id);
                }
            }
            for (const auto& ud : user_devices) {
                if (ud->device_id_.empty() || !ud->device_) {
                    LOGE("Invalid user-device, user-device: {}", ud->Dump());
                    continue;
                }

                auto db_mgr = context_->GetStreamDBManager();
                auto opt_device = db_mgr->GetStreamByRemoteDeviceId(ud->device_id_);
                if (opt_device.has_value()) {
                    const auto& stream = opt_device.value();
                    stream->stream_name_ = ud->device_->device_name_;
                    stream->remote_device_random_pwd_.clear();
                    stream->remote_device_safety_pwd_.clear();
                    stream->stream_host_.clear();
                    stream->stream_port_ = 0;
                    stream->relay_host_.clear();
                    stream->relay_port_ = 0;
                    stream->connect_type_ = connection_policy::kConsoleDeviceTicket;
                    stream->use_webrtc_ = true;
                    stream->console_online_ = ud->device_->active_;
                    db_mgr->UpdateStream(stream);
                }
                else {
                    auto item = std::make_shared<px_console::ConsoleStream>();
                    item->remote_device_id_ = ud->device_id_;
                    item->stream_name_ = ud->device_->device_name_;
                    item->encode_bps_ = 0;
                    item->encode_fps_ = 0;
                    item->clipboard_enabled_ = false;
                    item->audio_enabled_ = false;
                    item->connect_type_ = connection_policy::kConsoleDeviceTicket;
                    item->use_webrtc_ = true;
                    item->console_online_ = ud->device_->active_;
                    db_mgr->AddStream(item);
                }
            }
        } else {
            LOGW("Keep current Console device cards because resource refresh failed: {}",
                 static_cast<int>(user_devices_result.error()));
        }
    }

    void AppStreamList::RefreshCloudApplications() {
        auto user_mgr = grApp->GetUserManager();
        if (const auto apps_result = user_mgr->QueryApps(); apps_result.has_value()) {
            std::vector<std::shared_ptr<px_console::ConsoleStream>> app_streams;
            for (const auto& app : apps_result.value()) {
                auto stream = std::make_shared<px_console::ConsoleStream>();
                stream->stream_id_ = "console-app-" + app.app_id;
                stream->stream_name_ = app.name;
                stream->connect_type_ = connection_policy::kConsoleAppTicket;
                stream->console_app_id_ = app.app_id;
                stream->console_access_mode_ = app.access_mode;
                stream->console_instance_state_ = "stopped";
                if (!app.cover_url.empty()) {
                    QUrl cover_url(QString::fromStdString(app.cover_url));
                    if (cover_url.isRelative()) {
                        QUrl console_base;
                        console_base.setScheme("https");
                        console_base.setHost(QString::fromStdString(settings_->GetConsoleServerHost()));
                        console_base.setPort(settings_->GetConsoleServerPort());
                        console_base.setPath("/");
                        cover_url = console_base.resolved(cover_url);
                    }
                    stream->console_cover_url_ = cover_url.toString().toStdString();
                }
                stream->use_webrtc_ = true;
                stream->audio_enabled_ = false;
                stream->clipboard_enabled_ = false;
                stream->console_online_ = true;
                if (app.running_instance) {
                    stream->console_instance_id_ = app.running_instance->instance_id;
                    stream->console_instance_state_ = app.running_instance->state;
                    stream->direct_online_ = app.running_instance->state == "running";
                }
                app_streams.push_back(std::move(stream));
            }
            {
                std::lock_guard<std::mutex> guard(streams_mtx_);
                console_app_streams_ = std::move(app_streams);
            }
        } else {
            LOGW("Keep current Console application cards because catalog refresh failed: {}",
                 static_cast<int>(apps_result.error()));
        }
    }

    std::vector<std::shared_ptr<px_console::ConsoleStream>> AppStreamList::CopyStreams() {
        std::lock_guard<std::mutex> guard(streams_mtx_);
        std::vector<std::shared_ptr<px_console::ConsoleStream>> items;
        items.insert(items.begin(), streams_.begin(), streams_.end());
        return items;
    }

}
