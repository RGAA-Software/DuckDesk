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
#include <unordered_set>

#include "px_dialog.h"
#include "px_label.h"
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "widget_helper.h"
#include "stream_messages.h"
#include "stream_item_widget.h"
#include "create_stream_dialog.h"
#include "stream_content.h"
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_app_messages.h"
#include "running_stream_manager.h"
#include "px_common_new/uid_spacer.h"
#include "px_common_new/hardware.h"
#include "edit_relay_stream_dialog.h"
#include "stream_settings_dialog.h"
#include "start_stream_loading.h"
#include "input_remote_pwd_dialog.h"
#include "stream_state_checker.h"
#include "px_profile_client/profile_api.h"
#include "px_relay_client/relay_api.h"
#include "px_cms_client/cms_user.h"
#include "px_cms_client/cms_device.h"
#include "px_cms_client/cms_user_device.h"
#include "px_cms_client/cms_user_app_api.h"
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

    AppStreamList::AppStreamList(const std::shared_ptr<PxContext>& ctx, QWidget* parent) : QWidget(parent) {
        context_ = ctx;
        settings_ = PxSettings::Instance();
        db_mgr_ = context_->GetStreamDBManager();
        stream_content_ = (StreamContent*)parent;
        running_stream_mgr_ = context_->GetRunningStreamManager();
        CreateLayout();
        Init();

        setStyleSheet("background-color: #ffffff;");

        //
        state_checker_ = std::make_shared<StreamStateChecker>(context_);
        QPointer<AppStreamList> self(this);
        state_checker_->SetOnCheckedCallback([=, this](const std::vector<std::shared_ptr<px_cms::CmsStream>>& stream_items) {
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
                            widget->SetCmsConnectedState(update_item->cms_online_);
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
                if (!self) {
                    return;
                }
                cat streams = self->CopyStreams();
                self->state_checker_->UpdateCurrentStreamItems(streams);
            });
        }, 2200);

        // Load CMS resources for both signed-in users and anonymous Panel
        // sessions. Public applications must be visible on the first screen;
        // requiring a login event or a manual refresh leaves guest access
        // unreachable.
        context_->PostUIDelayTask([self, ctx = context_]() {
            if (!self) {
                return;
            }
            ctx->PostTask([self]() {
                if (self) {
                    self->RequestBindDevices();
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
        msg_listener_ = context_->GetMessageNotifier()->CreateListener();
        QPointer<AppStreamList> self(this);
        msg_listener_->Listen<StreamItemAdded>([=, this](const StreamItemAdded& msg) {
            auto item = msg.item_;
            std::shared_ptr<px_cms::CmsStream> exist_stream_item = nullptr;
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
                        // update it
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

        msg_listener_->Listen<MsgGrTimer5S>([=, this](const MsgGrTimer5S& msg) {
            context_->PostTask([self]() {
                if (!self) {
                    return;
                }
                cat streams = self->CopyStreams();
                self->state_checker_->UpdateCurrentStreamItems(streams);
            });
            //context_->PostTask([this]() {
            //    this->RequestBindDevices();
            //});
        });

        msg_listener_->Listen<MsgUserLoggedIn>([self, ctx = context_](const MsgUserLoggedIn& msg) {
            ctx->PostTask([self]() {
                if (!self) {
                    return;
                }
                self->RequestBindDevices();
            });
        });

        msg_listener_->Listen<MsgUserLoggedOut>([self, ctx = context_](const MsgUserLoggedOut& msg) {
            ctx->PostTask([self]() {
                if (!self) {
                    return;
                }
                self->RequestBindDevices();
            });
        });

        msg_listener_->Listen<MsgForceClearProgramData>([=, this](const MsgForceClearProgramData& msg) {
            this->LoadStreamItems();
        });
    }

    void AppStreamList::RegisterActions(int index, QListWidgetItem* cur_item) {
        const auto stream = streams_.at(index);
        if (stream->connect_type_ == "cms_app_ticket") {
            auto menu = new QMenu();
            auto connect_action = menu->addAction(tcTr("id_start_control"));
            auto view_action = menu->addAction(tcTr("id_only_viewing"));
            auto stop_action = menu->addAction(tcTr("id_stop_control"));
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

    void AppStreamList::ProcessAction(int index, QListWidgetItem* cur_item, const std::shared_ptr<px_cms::CmsStream>& item) {
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
            // lock device
            LockDevice(item);
        }
        else if (index == 4) {
            // restart device
            RestartDevice(item);
        }
        else if (index == 5) {
            // shutdown device
            ShutdownDevice(item);
        }
        // "" 6
        else if (index == 7) {
            // edit
            EditStream(item);
        }
        else if (index == 8) {
            // delete
            DeleteStream(item);
        }
        // "" 9
        else if (index == 10) {
            ShowSettings(item);
        }
    }

    void AppStreamList::StartStream(QListWidgetItem* cur_item, const std::shared_ptr<px_cms::CmsStream>& item, bool force_only_viewing) {
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

    void AppStreamList::StartStreamInternal(QListWidgetItem* cur_item, const std::shared_ptr<px_cms::CmsStream>& item, bool force_only_viewing) {
        std::shared_ptr<px_cms::CmsStream> target_item;
        const bool uses_cms_app_ticket = item->connect_type_ == "cms_app_ticket";
        if (uses_cms_app_ticket) {
            target_item = item;
        } else {
            auto si = db_mgr_->GetStreamByStreamId(item->stream_id_);
            if (!si.has_value()) {
                LOGE("read stream item from db failed: {}", item->stream_id_);
                return;
            }
            target_item = si.value();
        }
        // may start with [Only Viewing]
        if (force_only_viewing) {
            target_item->only_viewing_ = true;
        }

        const bool uses_cms_ticket = target_item->connect_type_ == "cms_ticket" || uses_cms_app_ticket;
        if (uses_cms_ticket) {
            const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            std::vector<std::string> permissions {"view"};
            if (!target_item->only_viewing_) {
                permissions.push_back("input");
            }
            px::Result<px_cms::CmsConnectionTicket, px_cms::CmsApiError> ticket_result =
                TcErr(px_cms::CmsApiError::kInvalidParams);
            if (uses_cms_app_ticket) {
                if (target_item->cms_instance_id_.empty()) {
                    auto start_result = grApp->GetUserManager()->StartApp(target_item->cms_app_id_, nonce);
                    if (!start_result.has_value() || start_result.value().state != "running") {
                        LOGE("CMS application start failed or did not reach running");
                        TcDialog dialog(tcTr("id_connect_failed"),
                                        "The CMS could not start this application", grWorkspace.get());
                        dialog.exec();
                        return;
                    }
                    target_item->cms_instance_id_ = start_result.value().instance_id;
                }
                ticket_result = grApp->GetUserManager()->IssueInstanceTicket(
                    target_item->cms_instance_id_, nonce, permissions);
            } else {
                ticket_result = grApp->GetUserManager()->IssueDeviceTicket(
                    target_item->remote_device_id_, nonce, permissions);
            }
            if (!ticket_result.has_value()) {
                LOGE("CMS connection ticket request failed: {}", static_cast<int>(ticket_result.error()));
                if (uses_cms_app_ticket) {
                    // The instance may have stopped or belonged to an expired
                    // guest session. A subsequent click should start a fresh
                    // owned instance instead of retrying the stale id.
                    target_item->cms_instance_id_.clear();
                }
                TcDialog dialog(tcTr("id_connect_failed"),
                                "The CMS did not authorize this connection", grWorkspace.get());
                dialog.exec();
                return;
            }
            const auto& ticket = ticket_result.value();
            const QUrl launch_url(QString::fromStdString(ticket.launch_url));
            if (!launch_url.isValid() || launch_url.host().isEmpty() || launch_url.port() <= 0) {
                LOGE("CMS returned an invalid device launch endpoint");
                TcDialog dialog(tcTr("id_connect_failed"),
                                "The CMS returned an invalid device endpoint", grWorkspace.get());
                dialog.exec();
                return;
            }
            target_item->stream_host_ = launch_url.host().toStdString();
            target_item->stream_port_ = launch_url.port();
            if (uses_cms_app_ticket) {
                const QUrlQuery query(launch_url);
                target_item->remote_device_id_ = query.queryItemValue("deviceId").toStdString();
            }
            target_item->connection_ticket_ = ticket.ticket;
            target_item->connection_nonce_ = nonce;
        }

        // get render configuration; to check the render online or not
        auto direct_render_result = RenderApi::GetRenderConfiguration(target_item->stream_host_, target_item->stream_port_);
        if (direct_render_result.has_value() && !target_item->force_relay_) {
            LOGI("We can connect directly: {}:{}", target_item->stream_host_, target_item->stream_port_);
            // verify device password before launching the client(same idea as the relay flow):
            // safety pwd(md5) preferred, fall back to md5(random pwd); re-ask on failure.
            // note: an empty candidate is fine, the render passes it when the device has no password.
            auto candidate_pwd_md5 = !target_item->remote_device_safety_pwd_.empty()
                                     ? target_item->remote_device_safety_pwd_
                                     : (!target_item->remote_device_random_pwd_.empty()
                                        ? MD5::Hex(target_item->remote_device_random_pwd_) : std::string(""));
            auto ok = uses_cms_ticket || RenderApi::VerifySecurityPassword(
                target_item->stream_host_, target_item->stream_port_, candidate_pwd_md5).value_or(false);
            for (; !uses_cms_ticket;) {
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
            if (uses_cms_ticket) {
                running_stream_mgr_->StartStream(target_item, kStreamItemNtTypeWebRTCDirect, true);
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
            // we can't connect directly
            LOGI("We can *NOT* connect directly: {}:{}, will try relay!", target_item->stream_host_, target_item->stream_port_);
            LOGI("Stream id: {}", target_item->stream_id_);
            LOGI("Remote connection credentials copied from the selected device");
            LOGI("stream host: {}, remote device id: {}", target_item->stream_host_, target_item->remote_device_id_);
            if (target_item->HasRelayInfo()) {
                LOGI("Yes, we have relay info: {} {}", target_item->relay_host_, target_item->relay_port_);
                // verify my self
                if (!grApp->CheckLocalDeviceInfoWithPopup()) {
                    return;
                }

                // check the remote device in relay server
                auto appkey = grApp->GetAppkey();
                auto srv_remote_device_id = "server_" + item->remote_device_id_;
                LOGI("Will check remote device: {} on relay server: {}:{}", srv_remote_device_id, item->relay_host_, item->relay_port_);
                auto relay_device_info = px_relay::RelayApi::GetRelayDeviceInfo(item->relay_host_, item->relay_port_, srv_remote_device_id, appkey);
                if (!relay_device_info.has_value()) {
                    if (relay_device_info.error() == px_relay::kRelayRequestFailed) {
                        // network failed
                        TcDialog dialog(tcTr("id_error"), tcTr("id_relay_network_unavailable_recreate"));
                        dialog.exec();
                    }
                    else {
                        //
                        TcDialog dialog(tcTr("id_error"), tcTr("id_cant_get_remote_device_info"));
                        dialog.exec();
                    }
                    return;
                }

                // NO password, just input one
                LOGI("Connecting to device: {}; credentials configured: {}", target_item->device_id_,
                     !target_item->remote_device_random_pwd_.empty() || !target_item->remote_device_safety_pwd_.empty());
                QString input_password;
                if (target_item->remote_device_random_pwd_.empty() && target_item->remote_device_safety_pwd_.empty()) {
                    InputRemotePwdDialog dlg_input_pwd(context_);
                    if (dlg_input_pwd.exec() == 1) {
                        return;
                    }
                    input_password = dlg_input_pwd.GetInputPassword();
                    if (input_password.isEmpty()) {
                        return;
                    }
                }

                auto remote_random_pwd = target_item->remote_device_random_pwd_;
                auto remote_safety_pwd = target_item->remote_device_safety_pwd_;
                if (!input_password.isEmpty() && remote_random_pwd.empty() && remote_safety_pwd.empty()) {
                    remote_random_pwd = input_password.toStdString();
                    remote_safety_pwd = input_password.toStdString();
                }

                // verify remote
                // password from inputting
                // password from database
                auto verify_result = ProfileApi::VerifyDeviceInfo(settings_->GetCmsServerHost(),
                                                                  settings_->GetCmsServerPort(),
                                                                  target_item->remote_device_id_,
                                                                  MD5::Hex(remote_random_pwd),
                                                                  MD5::Hex(remote_safety_pwd),
                                                                  grApp->GetAppkey());
                if (verify_result == ProfileVerifyResult::kVfNetworkFailed) {
                    TcDialog dialog(tcTr("id_connect_failed"), tcTr("id_profile_network_unavailable"), grWorkspace.get());
                    dialog.exec();
                    return;
                }

                if (verify_result != ProfileVerifyResult::kVfSuccessRandomPwd &&
                    verify_result != ProfileVerifyResult::kVfSuccessSafetyPwd &&
                    verify_result != ProfileVerifyResult::kVfSuccessAllPwd) {
                    // tell user, password is invalid
                    TcDialog dialog(tcTr("id_password_invalid"), tcTr("id_password_invalid_msg"), grWorkspace.get());
                    dialog.exec();

                    // clear the password and restart stream, then you need to input a password
                    // clear the memory
                    item->remote_device_random_pwd_ = "";
                    item->remote_device_safety_pwd_ = "";
                    // clear the database
                    db_mgr_->UpdateStreamRandomPwd(target_item->stream_id_, "");
                    db_mgr_->UpdateStreamSafetyPwd(target_item->stream_id_, "");
                    QPointer<AppStreamList> self(this);
                    context_->PostUIDelayTask([self, cur_item, item]() {
                        if (!self) {
                            return;
                        }
                        self->StartStream(cur_item, item, false);
                    }, 100);
                    return;
                }

                LOGI("Verify result, the password type: {}", (int)verify_result);
                // update to database
                if (verify_result == ProfileVerifyResult::kVfSuccessRandomPwd || verify_result == ProfileVerifyResult::kVfSuccessAllPwd) {
                    db_mgr_->UpdateStreamRandomPwd(target_item->stream_id_, remote_random_pwd);
                    target_item->remote_device_random_pwd_ = remote_random_pwd;
                    LOGI("Updated the saved random-password credential");
                }
                else if (verify_result == ProfileVerifyResult::kVfSuccessSafetyPwd || verify_result == ProfileVerifyResult::kVfSuccessAllPwd) {
                    db_mgr_->UpdateStreamSafetyPwd(target_item->stream_id_, remote_safety_pwd);
                    target_item->remote_device_safety_pwd_ = remote_safety_pwd;
                    LOGI("Updated the saved safety-password credential");
                }

                // start via websocket
                running_stream_mgr_->StartStream(target_item, kStreamItemNtTypeRelay, false);
            }
            else {
                LOGI("Yes, we DONT have relay info, force relay? {}, relay_host: {}, relay_port: {}",
                     target_item->force_relay_, target_item->relay_host_, target_item->relay_port_);
                 TcDialog dialog(tcTr("id_error"), tcTr("id_cant_get_remote_device_info"), grWorkspace.get());
                 dialog.exec();
                 return;
            }
        }
    }

    void AppStreamList::StopStream(const std::shared_ptr<px_cms::CmsStream>& item) {
        if (item->connect_type_ == "cms_app_ticket") {
            running_stream_mgr_->StopStream(item);
            if (item->cms_instance_id_.empty()) return;
            const auto instance_id = item->cms_instance_id_;
            QPointer<AppStreamList> self(this);
            context_->PostTask([self, item, instance_id]() {
                if (!self) return;
                const auto result = grApp->GetUserManager()->StopInstance(instance_id);
                if (result.has_value()) {
                    item->cms_instance_id_.clear();
                    self->RequestBindDevices();
                }
            });
            return;
        }
        auto si = db_mgr_->GetStreamByStreamId(item->stream_id_);
        if (!si.has_value()) {
            LOGE("read stream item from db failed: {}", item->stream_id_);
            return;
        }
        running_stream_mgr_->StopStream(si.value());
    }

    void AppStreamList::LockDevice(const std::shared_ptr<px_cms::CmsStream>& item) {
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

    void AppStreamList::RestartDevice(const std::shared_ptr<px_cms::CmsStream>& item) {
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

    void AppStreamList::ShutdownDevice(const std::shared_ptr<px_cms::CmsStream>& item) {
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

    void AppStreamList::EditStream(const std::shared_ptr<px_cms::CmsStream>& item) {
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

    void AppStreamList::DeleteStream(const std::shared_ptr<px_cms::CmsStream>& item) {
        TcDialog dialog(tcTr("id_warning"), tcTr("id_delete_remote_control"), grWorkspace.get());
        if (dialog.exec() == kDoneOk) {
            // stop it if running
            StopStream(item);
            // delete it from database
            auto mgr = context_->GetStreamDBManager();
            mgr->DeleteStream(item->_id);

            LoadStreamItems();
        }
    }

    void AppStreamList::ShowSettings(const std::shared_ptr<px_cms::CmsStream>& item) {
        auto si = db_mgr_->GetStreamByStreamId(item->stream_id_);
        if (!si.has_value()) {
            LOGE("read stream item from db failed: {}", item->stream_id_);
            return;
        }
        auto dialog = new StreamSettingsDialog(context_, si.value(), grWorkspace.get());
        dialog->exec();
    }

    QListWidgetItem* AppStreamList::AddItem(const std::shared_ptr<px_cms::CmsStream>& stream, int index) {
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
                auto db_mgr = self->context_->GetStreamDBManager();
                std::lock_guard<std::mutex> guard(self->streams_mtx_);
                self->streams_ = db_mgr->GetAllStreamsSortByCreatedTime();
                self->streams_.insert(self->streams_.end(), self->cms_app_streams_.begin(),
                                      self->cms_app_streams_.end());

                // bench test
                // auto fn_rand_a_upper_char = []() -> char {
                //     char c = 'A' + rand() % 26;
                //     return c;
                // };
                // for (int i = 0; i < 100; i++) {
                //     auto st = std::make_shared<px_cms::CmsStream>();
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

                if (!self->streams_.empty()) {
                    self->stream_content_->HideEmptyTip();
                }
                else {
                    self->stream_content_->ShowEmptyTip();
                }
            }

            // update to stream state checker
            cat streams = self->CopyStreams();
            self->state_checker_->UpdateCurrentStreamItems(streams);
        });
    }

    void AppStreamList::RequestBindDevices() {
        auto user_mgr = grApp->GetUserManager();
        auto user_devices = user_mgr->QueryBindDevices(1, 200, false);
        std::unordered_set<std::string> authorized_device_ids;
        for (const auto& ud : user_devices) {
            if (!ud->device_id_.empty() && ud->device_) {
                authorized_device_ids.insert(ud->device_id_);
            }
        }

        // CMS-sourced devices are a projection of the current identity, not a
        // permanent local address-book entry. Remove stale cards (and stop a
        // local connection if necessary) on logout, account switch or ACL
        // revocation so another user cannot see the previous user's devices.
        for (const auto& stream : db_mgr_->GetAllStreamsSortByCreatedTime()) {
            if (stream->connect_type_ == "cms_ticket"
                && !authorized_device_ids.contains(stream->remote_device_id_)) {
                running_stream_mgr_->StopStream(stream);
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
                // update info
                const auto& stream = opt_device.value();
                stream->stream_name_ = ud->device_->device_name_;
                stream->remote_device_random_pwd_.clear();
                stream->remote_device_safety_pwd_.clear();
                stream->stream_host_.clear();
                stream->stream_port_ = 0;
                stream->relay_host_.clear();
                stream->relay_port_ = 0;
                stream->connect_type_ = "cms_ticket";
                stream->use_webrtc_ = true;
                db_mgr->UpdateStream(stream);
            }
            else {
                // insert device
                std::shared_ptr<px_cms::CmsStream> item = std::make_shared<px_cms::CmsStream>();
                item->remote_device_id_ = ud->device_id_;
                item->stream_name_ = ud->device_->device_name_;
                item->encode_bps_ = 0;
                item->encode_fps_ = 0;
                item->clipboard_enabled_ = false;
                item->audio_enabled_ = false;
                item->connect_type_ = "cms_ticket";
                item->use_webrtc_ = true;
                db_mgr->AddStream(item);
            }
        }

        std::vector<std::shared_ptr<px_cms::CmsStream>> app_streams;
        if (const auto apps_result = user_mgr->QueryApps(); apps_result.has_value()) {
            for (const auto& app : apps_result.value()) {
                auto stream = std::make_shared<px_cms::CmsStream>();
                stream->stream_id_ = "cms-app-" + app.app_id;
                stream->stream_name_ = app.name
                    + (app.access_mode == "public" ? " [Public]" : " [Authorized]");
                stream->connect_type_ = "cms_app_ticket";
                stream->cms_app_id_ = app.app_id;
                stream->use_webrtc_ = true;
                stream->audio_enabled_ = false;
                stream->clipboard_enabled_ = false;
                stream->cms_online_ = true;
                if (app.running_instance) {
                    stream->cms_instance_id_ = app.running_instance->instance_id;
                    stream->direct_online_ = app.running_instance->state == "running";
                }
                app_streams.push_back(std::move(stream));
            }
        }
        {
            std::lock_guard<std::mutex> guard(streams_mtx_);
            cms_app_streams_ = std::move(app_streams);
        }

        LoadStreamItems();
    }

    std::vector<std::shared_ptr<px_cms::CmsStream>> AppStreamList::CopyStreams() {
        std::lock_guard<std::mutex> guard(streams_mtx_);
        std::vector<std::shared_ptr<px_cms::CmsStream>> items;
        items.insert(items.begin(), streams_.begin(), streams_.end());
        return items;
    }

}
