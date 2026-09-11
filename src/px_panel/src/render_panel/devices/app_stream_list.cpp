//
// Created by RGAA on 2023/8/14.
//

#include "app_stream_list.h"

#include <QtWidgets/QMenu>
#include <QWidget>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <unordered_set>

#include "px_dialog.h"
#include "px_label.h"
#include "px_common/log.h"
#include "px_common/md5.h"
#include "px_common/string_util.h"
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
#include "px_common/uid_spacer.h"
#include "px_common/hardware.h"
#include "edit_relay_stream_dialog.h"
#include "stream_settings_dialog.h"
#include "start_stream_loading.h"
#include "input_remote_pwd_dialog.h"
#include "stream_state_checker.h"
#include "stream_launch_auth_workflow.h"
#include "stream_resource_refresh_gate.h"
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
#include "render_panel/console/console_error_presenter.h"
#include "render_panel/util/conn_info_parser.h"
namespace px
{

    namespace {

        struct StreamResourceRefreshResult {
            std::optional<ConsoleDeviceOnlineStates> online_states;
            std::optional<std::vector<std::shared_ptr<px_console::ConsoleStream>>>
                application_streams;
        };

        void RemoveIdentityDeviceProjections(
            const std::shared_ptr<StreamDBOperator>& db_manager) {
            for (const auto& stream : db_manager->GetAllStreamsSortByCreatedTime()) {
                if (stream
                    && stream->connect_type_ == connection_policy::kConsoleDeviceTicket) {
                    db_manager->DeleteStream(stream->_id);
                }
            }
        }

        void RefreshRemoteDeviceResources(
            const std::shared_ptr<PxUserManager>& user_manager,
            const std::shared_ptr<StreamDBOperator>& db_manager,
            StreamResourceRefreshResult& refresh_result) {
            const auto user_devices_result =
                user_manager->QueryBindDevices(1, 200, false);
            if (!user_devices_result.has_value()) {
                LOGW("Keep current Console device cards because resource refresh failed: {}",
                     static_cast<int>(user_devices_result.error()));
                return;
            }

            const auto& user_devices = user_devices_result.value();
            std::unordered_set<std::string> available_device_ids;
            ConsoleDeviceOnlineStates online_states;
            for (const auto& user_device : user_devices) {
                if (!user_device->device_id_.empty() && user_device->device_) {
                    available_device_ids.insert(user_device->device_id_);
                    online_states[user_device->device_id_] =
                        user_device->device_->active_;
                }
            }

            // Console-sourced devices project the current identity. Reconcile
            // only after a successful response so a network failure cannot be
            // mistaken for an empty catalog.
            for (const auto& stream : db_manager->GetAllStreamsSortByCreatedTime()) {
                if (stream
                    && stream->connect_type_ == connection_policy::kConsoleDeviceTicket
                    && !available_device_ids.contains(stream->remote_device_id_)) {
                    db_manager->DeleteStream(stream->_id);
                }
            }
            for (const auto& user_device : user_devices) {
                if (user_device->device_id_.empty() || !user_device->device_) {
                    LOGE("Invalid user-device, user-device: {}", user_device->Dump());
                    continue;
                }

                const auto existing = db_manager->GetStreamByRemoteDeviceId(
                    user_device->device_id_);
                if (existing.has_value()) {
                    const auto& stream = existing.value();
                    stream->stream_name_ = user_device->device_->device_name_;
                    stream->remote_device_random_pwd_.clear();
                    stream->remote_device_safety_pwd_.clear();
                    stream->stream_host_.clear();
                    stream->stream_port_ = 0;
                    stream->relay_host_.clear();
                    stream->relay_port_ = 0;
                    stream->connect_type_ = connection_policy::kConsoleDeviceTicket;
                    stream->console_online_ = user_device->device_->active_;
                    db_manager->UpdateStream(stream);
                    continue;
                }

                auto stream = std::make_shared<px_console::ConsoleStream>();
                stream->remote_device_id_ = user_device->device_id_;
                stream->stream_name_ = user_device->device_->device_name_;
                stream->encode_bps_ = 0;
                stream->encode_fps_ = 0;
                stream->clipboard_enabled_ = false;
                stream->audio_enabled_ = false;
                stream->connect_type_ = connection_policy::kConsoleDeviceTicket;
                stream->console_online_ = user_device->device_->active_;
                db_manager->AddStream(stream);
            }
            refresh_result.online_states = std::move(online_states);
        }

        void RefreshCloudApplicationResources(
            const std::shared_ptr<PxUserManager>& user_manager,
            const std::string& console_host,
            int console_port,
            StreamResourceRefreshResult& refresh_result) {
            const auto applications_result = user_manager->QueryApps();
            if (!applications_result.has_value()) {
                LOGW("Keep current Console application cards because catalog refresh failed: {}",
                     static_cast<int>(applications_result.error()));
                return;
            }

            std::vector<std::shared_ptr<px_console::ConsoleStream>> application_streams;
            for (const auto& application : applications_result.value()) {
                auto stream = std::make_shared<px_console::ConsoleStream>();
                stream->stream_id_ = "console-app-" + application.app_id;
                stream->stream_name_ = application.name;
                stream->connect_type_ = connection_policy::kConsoleAppTicket;
                stream->console_app_id_ = application.app_id;
                stream->rdp_mode_ = application.app_type == "rdp";
                stream->console_access_mode_ = application.access_mode;
                stream->console_instance_state_ = "stopped";
                if (!application.cover_url.empty()) {
                    QUrl cover_url(QString::fromStdString(application.cover_url));
                    if (cover_url.isRelative()) {
                        QUrl console_base;
                        console_base.setScheme("https");
                        console_base.setHost(QString::fromStdString(console_host));
                        console_base.setPort(console_port);
                        console_base.setPath("/");
                        cover_url = console_base.resolved(cover_url);
                    }
                    stream->console_cover_url_ = cover_url.toString().toStdString();
                }
                stream->audio_enabled_ = false;
                stream->clipboard_enabled_ = false;
                stream->console_online_ = true;
                if (application.running_instance) {
                    stream->console_instance_id_ =
                        application.running_instance->instance_id;
                    stream->console_instance_state_ =
                        application.running_instance->state;
                    stream->direct_online_ =
                        application.running_instance->state == "running";
                }
                application_streams.push_back(std::move(stream));
            }
            refresh_result.application_streams = std::move(application_streams);
        }

    } // namespace

    // - - -- - - -- - - - -- -

    AppStreamList::AppStreamList(const std::shared_ptr<PxContext>& ctx,
                                 AppStreamListMode mode,
                                 std::function<void(bool)> on_empty_changed,
                                 QWidget* parent) // NOLINT(gammaray-raw-pointer-boundary) Qt parent ABI; QWidget owns the child.
        : QWidget(parent),
          settings_(*PxSettings::Instance()),
          mode_(mode),
          on_empty_changed_(std::move(on_empty_changed)) {
        context_ = ctx;
        db_mgr_ = context_->GetStreamDBManager();
        running_stream_mgr_ = context_->GetRunningStreamManager();
        resource_refresh_gate_ = StreamResourceRefreshGate::Create();
        stream_launch_auth_workflow_ = StreamLaunchAuthWorkflow::Create(
            context_->GetMessageNotifier()->GetAsyncRuntime());
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
                    continue;
                }
                bool stream_changed = false;
                if (connection_policy::IsUnclassifiedDirectConnection(
                        stream->connect_type_, stream->remote_device_id_,
                        stream->stream_host_, stream->stream_port_)) {
                    stream->connect_type_ = connection_policy::kExplicitDirect;
                    stream_changed = true;
                    ++normalized_direct;
                }
                if (stream_changed) db_mgr_->UpdateStream(stream);
            }

            if (removed_legacy_managed > 0 || normalized_direct > 0) {
                LOGI("Connection policy cleanup: removed legacy managed={}, normalized direct={}", removed_legacy_managed, normalized_direct);
            }
        }
        CreateLayout();
        Init();

        setStyleSheet("background-color: #ffffff;");

        //
        QPointer<AppStreamList> self(this);
        if (mode_ == AppStreamListMode::kRemoteDevices) {
            state_checker_ = std::make_shared<StreamStateChecker>(context_);
            state_checker_->SetOnCheckedCallback([self, ctx = context_](const std::vector<std::shared_ptr<px_console::ConsoleStream>>& stream_items) {
                ctx->PostUITask([self, stream_items]() {
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
                if (!self || !self->state_checker_) {
                    return;
                }
                const auto state_checker = self->state_checker_;
                auto streams = self->CopyStreams();
                ctx->PostTask([state_checker, streams = std::move(streams)]() mutable {
                    state_checker->UpdateCurrentStreamItems(streams);
                });
            }, 2200);
        }

        // Load Console resources for both signed-in users and anonymous Panel
        // sessions. Public applications must be visible on the first screen;
        // requiring a login event or a manual refresh leaves guest access
        // unreachable.
        context_->PostUIDelayTask([self]() {
            if (!self) {
                return;
            }
            self->RefreshResources();
        }, 300);
    }

    AppStreamList::~AppStreamList() {
        if (resource_refresh_gate_) {
            resource_refresh_gate_->Stop();
        }
        if (stream_launch_auth_workflow_) {
            stream_launch_auth_workflow_->Stop();
            stream_launch_auth_workflow_.reset();
        }
        if (state_checker_) {
            state_checker_->Exit();
        }
    }

    void AppStreamList::CreateLayout() {
        auto root_layout = new QHBoxLayout();
        WidgetHelper::ClearMargins(root_layout);

        stream_list_ = new QListWidget(this);

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

        QPointer<AppStreamList> self(this);
        connect(stream_list_, &QListWidget::customContextMenuRequested, this, [self](const QPoint& pos) {
            if (!self) {
                return;
            }
            const auto item = self->stream_list_->itemAt(pos);
            if (!item) {
                return;
            }
            self->RegisterActions(self->stream_list_->row(item));
        });

        connect(stream_list_, &QListWidget::itemDoubleClicked, this,
                [self](QListWidgetItem* item) { // NOLINT(gammaray-raw-pointer-boundary) Qt signal ABI; used only during delivery.
            if (!self || !item) {
                return;
            }
            const int index = self->stream_list_->row(item);
            self->StartStream(self->streams_.at(index), false);
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
            msg_listener_->Listen<StreamItemAdded>([self](const StreamItemAdded& msg) {
            if (!self) {
                return;
            }
            auto item = msg.item_;
            std::shared_ptr<px_console::ConsoleStream> exist_stream_item = nullptr;
            // by stream id
            {
                auto opt_stream = self->db_mgr_->GetStreamByStreamId(item->stream_id_);
                if (opt_stream.has_value()) {
                    exist_stream_item = opt_stream.value();
                }
            }

            if (!item->remote_device_id_.empty()) {
                // by remote device id
                auto opt_stream = self->db_mgr_->GetStreamByRemoteDeviceId(item->remote_device_id_);
                if (opt_stream.has_value()) {
                    exist_stream_item = opt_stream.value();
                }
            }
            else {
                // by host & port
                auto opt_stream = self->db_mgr_->GetStreamByHostPort(item->stream_host_, item->stream_port_);
                if (opt_stream.has_value()) {
                    exist_stream_item = opt_stream.value();
                }
            }
            if (!exist_stream_item) {
                self->db_mgr_->AddStream(item);
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
                self->db_mgr_->UpdateStream(exist_stream_item);
            }
            self->LoadStreamItems();

            LOGI("Auto start stream: {}", msg.auto_start_);
            self->context_->PostUIDelayTask([self, auto_start = msg.auto_start_, exist_stream_item]() {
                if (!self) {
                    return;
                }
                if (auto_start) {
                    self->StartStream(exist_stream_item, false);
                }
            }, 70);
            });

            msg_listener_->Listen<StreamItemUpdated>([self](const StreamItemUpdated& msg) {
                if (!self) {
                    return;
                }
                self->db_mgr_->UpdateStream(msg.item_);
                self->LoadStreamItems();
                LOGI("Update stream : {}", msg.item_->stream_id_);
            });

            msg_listener_->Listen<MsgRemotePeerInfo>([self](const MsgRemotePeerInfo& msg) {
                if (!self) {
                    return;
                }
                std::lock_guard<std::mutex> guard(self->streams_mtx_);
                for (const auto& stream : self->streams_) {
                    if (stream->stream_id_ == msg.stream_id_) {
                        if (stream->desktop_name_ != msg.desktop_name_ || stream->os_version_ != msg.os_version_) {
                            stream->desktop_name_ = msg.desktop_name_;
                            stream->os_version_ = msg.os_version_;
                            self->db_mgr_->UpdateStream(stream);
                        }
                        break;
                    }
                }
            });

            msg_listener_->Listen<MsgClientConnectedPanel>([](const MsgClientConnectedPanel&) {

            });

            msg_listener_->Listen<MsgForceClearProgramData>([self](const MsgForceClearProgramData&) {
                if (self) {
                    self->LoadStreamItems();
                }
            });
        }

        msg_listener_->Listen<MsgGrTimer5S>([self](const MsgGrTimer5S&) {
            if (!self) {
                return;
            }
            if (self->state_checker_) {
                const auto state_checker = self->state_checker_;
                auto streams = self->CopyStreams();
                self->context_->PostTask([
                    state_checker, streams = std::move(streams)]() mutable {
                    state_checker->UpdateCurrentStreamItems(streams);
                });
            }
            self->RefreshResources();
        });

        msg_listener_->Listen<MsgUserLoggedIn>([self](const MsgUserLoggedIn&) {
            if (!self) {
                return;
            }
            self->ClearIdentityResources();
            self->StartResourceRefresh(true);
        });

        msg_listener_->Listen<MsgUserLoggedOut>([self](const MsgUserLoggedOut&) {
            if (!self) {
                return;
            }
            self->ClearIdentityResources();
            self->StartResourceRefresh(true);
        });
    }

    void AppStreamList::RegisterActions(int index) {
        const auto stream = streams_.at(index);
        QPointer<AppStreamList> self(this);
        if (stream->connect_type_ == connection_policy::kConsoleAppTicket) {
            const auto menu = std::make_unique<QMenu>();
            const QPointer<QAction> connect_action =
                menu->addAction(tcTr(stream->console_instance_state_ == "running" ? "id_enter_application" : "id_start_application"));
            const QPointer<QAction> view_action = menu->addAction(tcTr("id_only_viewing"));
            const QPointer<QAction> stop_action = menu->addAction(tcTr("id_stop_application"));
            const QPointer<QAction> tcp_action = menu->addAction(tcTr("id_force_tcp"));
            tcp_action->setCheckable(true);
            const auto preference = db_mgr_->GetStreamByStreamId(stream->stream_id_);
            tcp_action->setChecked(preference.has_value() && preference.value()->force_tcp_);
            tcp_action->setEnabled(!stream->rdp_mode_);
            connect(tcp_action.data(), &QAction::triggered, this, [self, stream](bool enabled) {
                if (!self) {
                    return;
                }
                const auto saved = self->db_mgr_->GetStreamByStreamId(stream->stream_id_);
                // Persist only display/preferences, never the live app instance or its authorization ticket.
                const auto preference = saved.value_or(std::make_shared<px_console::ConsoleStream>());
                preference->stream_id_ = stream->stream_id_;
                preference->stream_name_ = stream->stream_name_;
                preference->connect_type_ = connection_policy::kConsoleAppTicket;
                preference->force_tcp_ = enabled;
                if (saved.has_value()) {
                    self->db_mgr_->UpdateStream(preference);
                } else {
                    self->db_mgr_->AddStream(preference);
                }
            });
            connect(connect_action.data(), &QAction::triggered, this, [self, stream]() {
                if (self)
                    self->StartStream(stream, false);
            });
            connect(view_action.data(), &QAction::triggered, this, [self, stream]() {
                if (self)
                    self->StartStream(stream, true);
            });
            connect(stop_action.data(), &QAction::triggered, this, [self, stream]() {
                if (self)
                    self->StopStream(stream);
            });
            menu->exec(QCursor::pos());
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
        const auto menu = std::make_unique<QMenu>();
        for (int i = 0; i < actions.size(); i++) {
            const QString& action_name = actions.at(i);
            if (action_name.isEmpty()) {
                menu->addSeparator();
                continue;
            }

            const QPointer<QAction> action = menu->addAction(action_name);
            connect(action.data(), &QAction::triggered, this, [self, i, index]() {
                if (self && index >= 0 && index < self->streams_.size()) {
                    self->ProcessAction(i, self->streams_.at(index));
                }
            });
        }
        menu->exec(QCursor::pos());
    }

    void AppStreamList::ProcessAction(int index, const std::shared_ptr<px_console::ConsoleStream>& item) {
        if (index == 0) {
            // connect
            StartStream(item, false);
        } else if (index == 1) {
            // stop
            StopStream(item);
        } else if (index == 2) {
            // only viewing
            StartStream(item, true);
        } else if (index == 3) {
            StartFileTransfer(item);
        } else if (index == 4) {
            // lock device
            LockDevice(item);
        } else if (index == 5) {
            // restart device
            RestartDevice(item);
        } else if (index == 6) {
            // shutdown device
            ShutdownDevice(item);
        }
        // "" 7
        else if (index == 8) {
            // edit
            EditStream(item);
        } else if (index == 9) {
            // delete
            DeleteStream(item);
        }
        // "" 10
        else if (index == 11) {
            ShowSettings(item);
        }
    }

    void AppStreamList::StartStream(
        const std::shared_ptr<px_console::ConsoleStream>& item,
        bool force_only_viewing) {
        if (const auto widget = GetItemByStreamId(item->stream_id_)) {
            widget->ShowConnecting();
        }
        QPointer<AppStreamList> self(this);
        context_->PostUIDelayTask([self, item, force_only_viewing]() {
            if (!self) {
                return;
            }
            self->StartStreamInternal(item, force_only_viewing);
        }, 40);
    }

    void AppStreamList::StartStreamInternal(
        const std::shared_ptr<px_console::ConsoleStream>& item,
        bool force_only_viewing) {
        std::shared_ptr<px_console::ConsoleStream> target_item;
        const bool uses_console_app_ticket = item->connect_type_ == connection_policy::kConsoleAppTicket;
        if (uses_console_app_ticket) {
            target_item = item;
            const auto preference = db_mgr_->GetStreamByStreamId(item->stream_id_);
            target_item->force_tcp_ = preference.has_value() && preference.value()->force_tcp_;
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
            StartConsoleTicketLaunch(target_item, uses_console_app_ticket);
            return;
        }

        ContinueStartStream(target_item, false);
    }

    StreamLaunchAuthHooks AppStreamList::MakeStreamLaunchAuthHooks() const {
        const auto user_manager = grApp->GetUserManager();
        const auto context = context_;
        StreamLaunchAuthHooks hooks;
        hooks.renew_rdp_ticket = [user_manager](const RdpLaunchRecovery& recovery) {
            struct ScopedToken final {
                explicit ScopedToken(std::string_view source) : value(source) {}
                ~ScopedToken() {
                    OPENSSL_cleanse(value.data(), value.size());
                }
                ScopedToken(const ScopedToken&) = delete;
                ScopedToken& operator=(const ScopedToken&) = delete;
                std::string value{};
            };
            const ScopedToken token{recovery.renewal->View()};
            auto result = user_manager->RenewConnectionTicket(token.value, recovery.nonce);
            if (result.has_value()) {
                return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(result.value());
            }
            return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Failure(result.error(), px_console::ConsoleApiLastErrorMessage());
        };
        hooks.post_blocking = [context](std::function<void()> task) { context->PostTask(std::move(task)); };
        hooks.start_app = [user_manager](const std::string& app_id, const std::string& nonce) {
            auto result = user_manager->StartApp(app_id, nonce);
            if (result.has_value()) {
                return StreamLaunchConsoleCall<px_console::ConsoleUserAppInstance>::Success(
                    result.value());
            }
            return StreamLaunchConsoleCall<px_console::ConsoleUserAppInstance>::Failure(
                result.error(), px_console::ConsoleApiLastErrorMessage());
        };
        hooks.query_apps = [user_manager]() {
            auto result = user_manager->QueryApps();
            if (result.has_value()) {
                return StreamLaunchConsoleCall<
                    std::vector<px_console::ConsoleUserApplication>>::Success(
                        result.value());
            }
            return StreamLaunchConsoleCall<
                std::vector<px_console::ConsoleUserApplication>>::Failure(
                    result.error(), px_console::ConsoleApiLastErrorMessage());
        };
        hooks.issue_instance_ticket = [user_manager](
            const std::string& instance_id,
            const std::string& nonce,
            const std::vector<std::string>& permissions) {
            auto result = user_manager->IssueInstanceTicket(
                instance_id, nonce, permissions);
            if (result.has_value()) {
                return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(
                    result.value());
            }
            return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Failure(
                result.error(), px_console::ConsoleApiLastErrorMessage());
        };
        hooks.issue_device_ticket = [user_manager](
            const std::string& device_id,
            const std::string& nonce,
            const std::vector<std::string>& permissions) {
            auto result = user_manager->IssueDeviceTicket(
                device_id, nonce, permissions);
            if (result.has_value()) {
                return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(
                    result.value());
            }
            return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Failure(
                result.error(), px_console::ConsoleApiLastErrorMessage());
        };
        hooks.resolve_ticket = [](
            px_console::ConsoleConnectionTicket ticket,
            StreamLaunchTicketTarget target) {
            const QUrl launch_url(QString::fromStdString(ticket.launch_url));
            if (!launch_url.isValid() || launch_url.host().isEmpty()
                || launch_url.port() <= 0) {
                return PxResult<StreamLaunchResolvedTicket>::Failure(MakePxAsyncError(
                    PxAsyncErrorCode::kProtocolError,
                    "stream-launch.resolve-ticket",
                    "Console returned an invalid launch endpoint",
                    false,
                    "INVALID_CONSOLE_ENDPOINT"));
            }
            std::string remote_device_id;
            if (target == StreamLaunchTicketTarget::kApplicationInstance) {
                const QUrlQuery query(launch_url);
                remote_device_id = query.queryItemValue("deviceId").toStdString();
                if (remote_device_id.empty()) {
                    return PxResult<StreamLaunchResolvedTicket>::Failure(MakePxAsyncError(
                        PxAsyncErrorCode::kProtocolError,
                        "stream-launch.resolve-ticket",
                        "Console application endpoint has no device ID",
                        false,
                        "INVALID_CONSOLE_ENDPOINT"));
                }
            }
            return PxResult<StreamLaunchResolvedTicket>::Success({
                .ticket = std::move(ticket),
                .host = launch_url.host().toStdString(),
                .port = launch_url.port(),
                .remote_device_id = std::move(remote_device_id),
            });
        };
        hooks.probe_direct = [](const std::string& host, int port) {
            return RenderApi::GetRenderConfiguration(host, port).has_value();
        };
        return hooks;
    }

    void AppStreamList::StartConsoleTicketLaunch(
        const std::shared_ptr<px_console::ConsoleStream>& target_item,
        bool uses_console_app_ticket) {
        if (!stream_launch_auth_workflow_) {
            TcDialog dialog(tcTr("id_connect_failed"),
                            tcTr("id_console_reason_internal"), grWorkspace.get());
            dialog.exec();
            return;
        }

        const bool logged_in = grApp->GetUserManager()->IsLoggedIn();
        std::vector<std::string> permissions{"view"};
        if (logged_in) {
            permissions.push_back("file");
        }
        if (!target_item->only_viewing_) {
            permissions.push_back("input");
            if (logged_in) {
                permissions.insert(permissions.end(), {"clipboard", "audio"});
            }
        }

        StreamLaunchAuthRequest request{
            .target = uses_console_app_ticket
                ? StreamLaunchTicketTarget::kApplicationInstance
                : StreamLaunchTicketTarget::kDevice,
            .device_id = target_item->remote_device_id_,
            .app_id = target_item->console_app_id_,
            .instance_id = target_item->console_instance_id_,
            .client_nonce = QUuid::createUuid().toString(
                QUuid::WithoutBraces).toStdString(),
            .permissions = std::move(permissions),
            .deadline = std::chrono::steady_clock::now() + std::chrono::seconds(65),
        };
        QPointer<AppStreamList> self(this);
        const auto context = context_;
        if (uses_console_app_ticket && !target_item->only_viewing_) {
            request.recovery = running_stream_mgr_->TakeRdpRecovery(request.app_id);
            if (request.recovery) {
                request.instance_id = request.recovery->instance_id;
                request.client_nonce = request.recovery->nonce;
                LOGI("event=rdp.panel.recovery outcome=renewing");
            } else if (target_item->rdp_mode_) {
                // Reconcile a stale card with Console after the old runtime's
                // grace has ended. Start is idempotent and still enforces busy.
                request.instance_id.clear();
            }
        }
        const auto generation = stream_launch_auth_workflow_->Start(
            std::move(request), MakeStreamLaunchAuthHooks(),
            [self, context, target_item, uses_console_app_ticket](
                std::uint64_t completed_generation,
                StreamLaunchAuthResult result) mutable {
                context->PostUITask([
                    self, target_item, uses_console_app_ticket,
                    completed_generation, result = std::move(result)]() mutable {
                    if (self) {
                        self->CompleteConsoleTicketLaunch(
                            target_item, uses_console_app_ticket,
                            completed_generation, std::move(result));
                    }
                });
            });
        if (!generation) {
            TcDialog dialog(tcTr("id_connect_failed"),
                            tcTr("id_console_reason_internal"), grWorkspace.get());
            dialog.exec();
        }
    }

    void AppStreamList::CompleteConsoleTicketLaunch(
        const std::shared_ptr<px_console::ConsoleStream>& target_item,
        bool uses_console_app_ticket,
        std::uint64_t generation,
        StreamLaunchAuthResult result) {
        if (!stream_launch_auth_workflow_
            || !stream_launch_auth_workflow_->IsCurrent(generation)) {
            return;
        }
        if (!result) {
            const auto& error = result.Error();
            if (error.code == PxAsyncErrorCode::kCancelled) {
                return;
            }
            LOGE("Console stream launch failed: stage={}, code={}, reason={}",
                 error.stage, error.StableCode(), error.message);
            if (uses_console_app_ticket
                && error.stage == "stream-launch.issue-instance-ticket") {
                target_item->console_instance_id_.clear();
            }
            QString message;
            if (error.stage == "stream-launch.resolve-ticket") {
                message = tcTr("id_invalid_console_endpoint");
            }
            else if (error.code == PxAsyncErrorCode::kTimeout
                && error.stage == "stream-launch.wait-running") {
                message = tcTr("id_application_start_timeout");
            }
            else {
                auto api_error = px_console::ConsoleApiError::kInternalError;
                try {
                    api_error = static_cast<px_console::ConsoleApiError>(
                        std::stoi(error.detail_code));
                }
                catch (...) {
                }
                const auto operation = error.stage == "stream-launch.start-app"
                    || error.stage == "stream-launch.query-apps"
                    || error.stage == "stream-launch.wait-running"
                    ? ConsoleErrorOperation::kStartApplication
                    : ConsoleErrorOperation::kConnectRemote;
                message = MakeConsoleErrorMessage(
                    operation, api_error, error.message,
                    MakeConsoleEndpoint(settings_.get().GetConsoleServerHost(),
                                        settings_.get().GetConsoleServerPort()));
            }
            TcDialog dialog(tcTr("id_connect_failed"), message, grWorkspace.get());
            dialog.exec();
            return;
        }

        auto payload = result.TakeValue();
        LOGI("Console stream authorization ready: generation={}, target={}, device={}, direct_available={}",
             generation,
             uses_console_app_ticket ? "application" : "device",
             target_item->remote_device_id_,
             payload.direct_available);
        if (payload.instance) {
            target_item->console_instance_id_ = payload.instance->instance_id;
            target_item->console_instance_state_ = payload.instance->state;
            target_item->console_online_ = true;
            target_item->direct_online_ = true;
        }
        auto& resolved = payload.resolved;
        target_item->stream_host_ = resolved.host;
        target_item->stream_port_ = resolved.port;
        if (uses_console_app_ticket) {
            target_item->remote_device_id_ = resolved.remote_device_id;
        }
        target_item->connection_ticket_ = resolved.ticket.ticket;
        target_item->rdp_configuration_ = std::move(resolved.ticket.rdp_configuration);
        target_item->rdp_mode_ = static_cast<bool>(target_item->rdp_configuration_);
        target_item->connection_renewal_token_ = resolved.ticket.renewal_token;
        target_item->connection_logical_session_id_ = resolved.ticket.logical_session_id;
        target_item->connection_nonce_ = payload.client_nonce;
        target_item->active_session_stream_id_ = resolved.ticket.stream_id;
        const auto has_permission = [&resolved](std::string_view permission) {
            return std::find(
                resolved.ticket.permissions.begin(),
                resolved.ticket.permissions.end(),
                permission) != resolved.ticket.permissions.end();
        };
        target_item->clipboard_enabled_ = has_permission("clipboard");
        target_item->audio_enabled_ = has_permission("audio");
        ContinueStartStream(target_item, true, payload.direct_available);
    }

    void AppStreamList::ContinueStartStream(
        const std::shared_ptr<px_console::ConsoleStream>& target_item,
        bool uses_console_ticket,
        std::optional<bool> authenticated_direct_available) {

        const bool direct_available = authenticated_direct_available.has_value()
                                          ? *authenticated_direct_available
                                          : RenderApi::GetRenderConfiguration(target_item->stream_host_, target_item->stream_port_).has_value();

        if (direct_available) {
            LOGI("We can connect directly: {}:{}", target_item->stream_host_, target_item->stream_port_);
            // Prepare the authenticated native session before launching its client:
            // safety pwd(md5) preferred, fall back to md5(random pwd); re-ask on failure.
            // note: an empty candidate is fine, the render passes it when the device has no password.
            auto candidate_pwd_md5 = !target_item->remote_device_safety_pwd_.empty()
                                     ? target_item->remote_device_safety_pwd_
                                     : (!target_item->remote_device_random_pwd_.empty()
                                        ? MD5::Hex(target_item->remote_device_random_pwd_) : std::string(""));
            const bool password_direct = !uses_console_ticket;
            target_item->ip_direct_prevalidated_ = false;
            if (password_direct) {
                target_item->connection_nonce_ = QUuid::createUuid()
                    .toString(QUuid::WithoutBraces).toStdString();
            }
            const auto verify_before_launch = [target_item](const std::string& password_md5) {
                auto launch = RenderApi::PrepareIpDirectLaunch(
                    target_item->stream_host_, target_item->stream_port_, password_md5,
                    target_item->connection_nonce_);
                if (!launch.has_value()) {
                    target_item->active_session_stream_id_.clear();
                    target_item->ip_direct_prevalidated_ = false;
                    return false;
                }
                target_item->active_session_stream_id_ = launch.value().stream_id_;
                target_item->ip_direct_prevalidated_ =
                    !target_item->active_session_stream_id_.empty();
                return target_item->ip_direct_prevalidated_;
            };
            auto ok = uses_console_ticket || verify_before_launch(candidate_pwd_md5);
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

                ok = verify_before_launch(pwd_md5);
                if (!ok) {
                    context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_password_invalid_msg"));
                }
                else {
                    // use it right away for this launch(DB update below is async)
                    target_item->remote_device_safety_pwd_ = pwd_md5;
                    // update to database
                    QPointer<AppStreamList> self(this);
                    const auto context = context_;
                    const auto database = db_mgr_;
                    context->PostDBTask([
                        context, database, self, target_item, pwd_md5]() {
                        database->UpdateStreamSafetyPwd(
                            target_item->stream_id_, pwd_md5);
                        context->PostUITask([self]() {
                            if (self) {
                                self->LoadStreamItems();
                            }
                        });
                    });
                    break;
                }
            }

            running_stream_mgr_->StartStream(target_item);
        }
        else {
            LOGW("Selected direct endpoint is unavailable: {}:{}",
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
            const auto context = context_;
            const auto user_manager = grApp->GetUserManager();
            const auto console_endpoint = MakeConsoleEndpoint(
                settings_.get().GetConsoleServerHost(),
                settings_.get().GetConsoleServerPort());
            context->PostNetworkTask([
                context, user_manager, self, item, instance_id,
                console_endpoint]() {
                const auto result = user_manager->StopInstance(instance_id);
                if (result.has_value()) {
                    context->PostUITask([self, item]() {
                        if (!self) {
                            return;
                        }
                        item->console_instance_id_.clear();
                        item->console_instance_state_ = "stopped";
                        self->RefreshResources();
                    });
                    return;
                }
                const auto error = result.error();
                const auto error_message = px_console::ConsoleApiLastErrorMessage();
                context->PostUITask([
                    self, error, error_message, console_endpoint]() {
                    if (!self) {
                        return;
                    }
                    self->context_->NotifyAppErrMessage(
                        tcTr("id_error"),
                        MakeConsoleErrorMessage(
                            ConsoleErrorOperation::kStopApplication, error,
                            error_message, console_endpoint));
                });
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
        const auto dialog = std::make_unique<StreamSettingsDialog>(context_, si.value());
        dialog->exec();
    }

    void AppStreamList::AddItem(
        const std::shared_ptr<px_console::ConsoleStream>& stream, int index) {
        auto item = new QListWidgetItem(stream_list_);
        item->setSizeHint(QSize(230, 150));
        auto widget = new StreamItemWidget(stream, stream->bg_color_, stream_list_);
        widget->setObjectName(stream->stream_id_.c_str());
        WidgetHelper::AddShadow(widget, 0xbbbbbb, 8);
        QPointer<AppStreamList> self(this);
        widget->SetOnConnectListener([self, stream]() {
            if (self) {
                self->StartStream(stream, false);
            }
        });
        widget->SetOnMenuListener([self, index]() {
            if (self) {
                self->RegisterActions(index);
            }
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
    }

    QPointer<StreamItemWidget> AppStreamList::GetItemByStreamId(
        const std::string& stream_id) {
        int count = stream_list_->count();
        for (int i = 0; i < count; i++) {
            const QPointer<StreamItemWidget> widget(
                static_cast<StreamItemWidget*>(
                    stream_list_->itemWidget(stream_list_->item(i))));
            if (widget && widget->objectName().toStdString() == stream_id) {
                return widget;
            }
        }
        return {};
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
                    stream->device_id_ = self->settings_.get().GetDeviceId();
                    self->AddItem(stream, index++);
                }

                if (self->on_empty_changed_) {
                    self->on_empty_changed_(self->streams_.empty());
                }
            }

            // update to stream state checker
            if (self->state_checker_) {
                const auto streams = self->CopyStreams();
                self->state_checker_->UpdateCurrentStreamItems(streams);
            }
        });
    }

    void AppStreamList::StartFileTransfer(const std::shared_ptr<px_console::ConsoleStream>& item) {
        if (!grApp->GetUserManager()->IsLoggedIn() || item->remote_device_id_.empty()) {
            context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_file_transfer_requires_login"));
            return;
        }
        // A normal remote-control client already has a file-capable transport
        // (signed-in control tickets include the file permission). Reuse it and
        // avoid a second process and ticket redemption.
        if (running_stream_mgr_->OpenFileTransferInRunningClient(item)) {
            return;
        }
        auto target = db_mgr_->GetStreamByStreamId(item->stream_id_);
        if (!target.has_value()) {
            return;
        }
        StartFileTransferTicketLaunch(target.value());
    }

    void AppStreamList::StartFileTransferTicketLaunch(
        const std::shared_ptr<px_console::ConsoleStream>& target_item) {
        if (!stream_launch_auth_workflow_) {
            context_->NotifyAppErrMessage(
                tcTr("id_error"), tcTr("id_console_reason_internal"));
            return;
        }
        StreamLaunchAuthRequest request{
            .target = StreamLaunchTicketTarget::kDevice,
            .device_id = target_item->remote_device_id_,
            .client_nonce = QUuid::createUuid().toString(
                QUuid::WithoutBraces).toStdString(),
            .permissions = {"file"},
            .deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10),
        };
        QPointer<AppStreamList> self(this);
        const auto context = context_;
        const auto generation = stream_launch_auth_workflow_->Start(
            std::move(request), MakeStreamLaunchAuthHooks(),
            [self, context, target_item](
                std::uint64_t completed_generation,
                StreamLaunchAuthResult result) mutable {
                context->PostUITask([
                    self, target_item, completed_generation,
                    result = std::move(result)]() mutable {
                    if (self) {
                        self->CompleteFileTransferTicketLaunch(
                            target_item, completed_generation, std::move(result));
                    }
                });
            });
        if (!generation) {
            context_->NotifyAppErrMessage(
                tcTr("id_error"), tcTr("id_console_reason_internal"));
        }
    }

    void AppStreamList::CompleteFileTransferTicketLaunch(
        const std::shared_ptr<px_console::ConsoleStream>& target_item,
        std::uint64_t generation,
        StreamLaunchAuthResult result) {
        if (!stream_launch_auth_workflow_
            || !stream_launch_auth_workflow_->IsCurrent(generation)) {
            return;
        }
        if (!result) {
            const auto& error = result.Error();
            if (error.code == PxAsyncErrorCode::kCancelled) {
                return;
            }
            if (error.stage == "stream-launch.resolve-ticket") {
                context_->NotifyAppErrMessage(
                    tcTr("id_error"), tcTr("id_invalid_console_endpoint"));
                return;
            }
            auto api_error = px_console::ConsoleApiError::kInternalError;
            try {
                api_error = static_cast<px_console::ConsoleApiError>(
                    std::stoi(error.detail_code));
            }
            catch (...) {
            }
            const auto message = api_error == px_console::ConsoleApiError::kNotFound
                ? tcTr("id_file_transfer_device_unavailable")
                : MakeConsoleErrorMessage(
                    ConsoleErrorOperation::kFileTransfer,
                    api_error,
                    error.message,
                    MakeConsoleEndpoint(settings_.get().GetConsoleServerHost(),
                                        settings_.get().GetConsoleServerPort()));
            context_->NotifyAppErrMessage(tcTr("id_error"), message);
            return;
        }

        auto payload = result.TakeValue();
        LOGI("Console file-transfer authorization ready: generation={}, device={}, direct_available={}",
             generation, target_item->remote_device_id_, payload.direct_available);
        target_item->stream_host_ = payload.resolved.host;
        target_item->stream_port_ = payload.resolved.port;
        target_item->connection_ticket_ = payload.resolved.ticket.ticket;
        target_item->connection_nonce_ = payload.client_nonce;
        if (!payload.direct_available) {
            context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_device_offline"));
            return;
        }
        target_item->active_session_stream_id_ = payload.resolved.ticket.stream_id;
        running_stream_mgr_->StartFileTransfer(target_item);
    }

    void AppStreamList::RefreshResources() {
        StartResourceRefresh(false);
    }

    void AppStreamList::StartResourceRefresh(bool identity_changed) {
        const auto generation = resource_refresh_gate_->Begin(identity_changed);
        if (!generation.has_value()) {
            return;
        }

        const auto mode = mode_;
        const auto context = context_;
        const auto database = db_mgr_;
        const auto user_manager = grApp->GetUserManager();
        const auto refresh_gate = resource_refresh_gate_;
        const auto console_host = settings_.get().GetConsoleServerHost();
        const auto console_port = settings_.get().GetConsoleServerPort();
        QPointer<AppStreamList> self(this);

        context->PostNetworkTask([
            context, database, user_manager, refresh_gate, self, mode,
            identity_changed, generation = *generation, console_host,
            console_port]() mutable {
            const auto refresh_result =
                std::make_shared<StreamResourceRefreshResult>();
            (void)refresh_gate->RunIfCurrent(generation, [
                database, user_manager, refresh_result, mode,
                identity_changed, console_host, console_port]() {
                if (identity_changed
                    && mode == AppStreamListMode::kRemoteDevices) {
                    RemoveIdentityDeviceProjections(database);
                }
                if (mode == AppStreamListMode::kCloudApplications) {
                    RefreshCloudApplicationResources(
                        user_manager, console_host, console_port, *refresh_result);
                }
                else {
                    RefreshRemoteDeviceResources(
                        user_manager, database, *refresh_result);
                }
            });

            context->PostUITask([
                refresh_gate, self, mode, generation,
                refresh_result]() mutable {
                if (!refresh_gate->Complete(generation) || !self) {
                    return;
                }
                {
                    std::lock_guard lock(self->streams_mtx_);
                    if (mode == AppStreamListMode::kCloudApplications
                        && refresh_result->application_streams.has_value()) {
                        self->console_app_streams_ = std::move(
                            *refresh_result->application_streams);
                    }
                    else if (mode == AppStreamListMode::kRemoteDevices
                        && refresh_result->online_states.has_value()) {
                        self->console_device_online_states_ = std::move(
                            *refresh_result->online_states);
                    }
                }
                self->LoadStreamItems();
            });
        });
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

        LoadStreamItems();
    }

    std::vector<std::shared_ptr<px_console::ConsoleStream>> AppStreamList::CopyStreams() {
        std::lock_guard<std::mutex> guard(streams_mtx_);
        std::vector<std::shared_ptr<px_console::ConsoleStream>> items;
        items.insert(items.begin(), streams_.begin(), streams_.end());
        return items;
    }

}
