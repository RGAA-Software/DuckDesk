#include "app_stream_list.h"

#include "connection_policy.h"
#include "running_stream_manager.h"
#include "stream_messages.h"
#include "stream_resource_catalog.h"
#include "stream_state_checker.h"

#include "render_panel/console/console_error_presenter.h"
#include "render_panel/database/stream_db_operator.h"
#include "render_panel/network/render_api.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/px_application.h"
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"
#include "render_panel/user/px_user_manager.h"

#include "px_common/log.h"
#include "px_common/md5.h"
#include "px_qt_widget/translator/px_translator.h"

#include <QUrlQuery>
#include <QUuid>
#include <openssl/crypto.h>

#include <algorithm>
#include <chrono>
#include <string_view>
#include <utility>

namespace px {

std::shared_ptr<StreamSessionController> StreamSessionController::Create(const std::shared_ptr<PxContext>& context,
                                                                         const AppStreamListMode mode) {
    const auto controller = std::make_shared<StreamSessionController>(context, mode);
    controller->Initialize();
    return controller;
}

StreamSessionController::StreamSessionController(std::shared_ptr<PxContext> context, const AppStreamListMode mode)
    : settings_{*PxSettings::Instance()}, context_{std::move(context)}, database_{context_->GetStreamDBManager()},
      runningStreams_{context_->GetRunningStreamManager()}, mode_{mode} {}

void StreamSessionController::Initialize() {
    const auto catalogMode = mode_ == AppStreamListMode::kCloudApplications ? StreamCatalogMode::CloudApplications
                                                                            : StreamCatalogMode::RemoteDevices;
    resourceCatalog_ = StreamResourceCatalog::Create(context_, database_, grApp->GetUserManager(), catalogMode,
                                                      settings_.get().GetConsoleServerHost(), settings_.get().GetConsoleServerPort());
    authorization_ = StreamLaunchAuthWorkflow::Create(context_->GetMessageNotifier()->GetAsyncRuntime());
    if (mode_ == AppStreamListMode::kRemoteDevices) {
        int removed{};
        int normalized{};
        for (const auto& stream : database_->GetAllStreamsSortByCreatedTime()) {
            if (!stream) {
                continue;
            }
            if (connection_policy::IsLegacyManagedConnection(stream->connect_type_, stream->remote_device_id_)) {
                database_->DeleteStream(stream->_id);
                ++removed;
            } else if (connection_policy::IsUnclassifiedDirectConnection(stream->connect_type_, stream->remote_device_id_,
                                                                          stream->stream_host_, stream->stream_port_)) {
                stream->connect_type_ = connection_policy::kExplicitDirect;
                database_->UpdateStream(stream);
                ++normalized;
            }
        }
        LOGI("Connection policy cleanup: removed legacy managed={}, normalized direct={}", removed, normalized);
        stateChecker_ = std::make_shared<StreamStateChecker>(context_);
        const std::weak_ptr<StreamSessionController> weakSelf{shared_from_this()};
        stateChecker_->SetOnCheckedCallback([weakSelf](std::vector<std::shared_ptr<px_console::ConsoleStream>> updates) {
            if (const auto self = weakSelf.lock()) {
                std::scoped_lock lock{self->streamsMutex_};
                for (const auto& update : updates) {
                    const auto found = std::ranges::find_if(self->streams_, [&update](const auto& stream) {
                        return stream && update && stream->stream_id_ == update->stream_id_;
                    });
                    if (found != self->streams_.end()) {
                        (*found)->direct_online_ = update->direct_online_;
                        (*found)->relay_online_ = update->relay_online_;
                        (*found)->console_online_ = update->console_online_;
                    }
                }
            }
        });
        stateChecker_->Start();
    }
    RegisterListeners();
    Reload();
    const std::weak_ptr<StreamSessionController> weakSelf{shared_from_this()};
    context_->PostUIDelayTask([weakSelf] {
        if (const auto self = weakSelf.lock()) {
            self->RefreshResources();
        }
    }, 300);
}

StreamSessionController::~StreamSessionController() {
    if (listener_) {
        listener_->UnListenAll();
    }
    if (resourceCatalog_) {
        resourceCatalog_->Stop();
    }
    if (authorization_) {
        authorization_->Stop();
    }
    if (stateChecker_) {
        stateChecker_->Exit();
    }
}

void StreamSessionController::RegisterListeners() {
    listener_ = context_->ObtainUIMessageListener();
    const std::weak_ptr<StreamSessionController> weakSelf{shared_from_this()};
    if (mode_ == AppStreamListMode::kRemoteDevices) {
        listener_->Listen<StreamItemAdded>([weakSelf](const StreamItemAdded& message) {
            const auto self = weakSelf.lock();
            if (!self || !message.item_) {
                return;
            }
            const auto& item = message.item_;
            auto existing = self->database_->GetStreamByStreamId(item->stream_id_);
            if (!item->remote_device_id_.empty()) {
                existing = self->database_->GetStreamByRemoteDeviceId(item->remote_device_id_);
            } else if (!item->stream_host_.empty()) {
                existing = self->database_->GetStreamByHostPort(item->stream_host_, item->stream_port_);
            }
            auto target = existing.value_or(item);
            if (!existing) {
                self->database_->AddStream(target);
            } else {
                if (!item->stream_name_.empty()) target->stream_name_ = item->stream_name_;
                if (!item->remote_device_id_.empty()) target->remote_device_id_ = item->remote_device_id_;
                if (!item->connect_type_.empty()) target->connect_type_ = item->connect_type_;
                if (!item->stream_host_.empty()) target->stream_host_ = item->stream_host_;
                if (item->stream_port_ > 0) target->stream_port_ = item->stream_port_;
                if (!item->relay_host_.empty()) target->relay_host_ = item->relay_host_;
                if (item->relay_port_ > 0) target->relay_port_ = item->relay_port_;
                if (!item->remote_device_random_pwd_.empty()) target->remote_device_random_pwd_ = item->remote_device_random_pwd_;
                if (!item->remote_device_safety_pwd_.empty()) target->remote_device_safety_pwd_ = item->remote_device_safety_pwd_;
                self->database_->UpdateStream(target);
            }
            self->Reload();
            if (message.auto_start_) {
                self->Start(target, false);
            }
        });
        listener_->Listen<StreamItemUpdated>([weakSelf](const StreamItemUpdated& message) {
            if (const auto self = weakSelf.lock(); self && message.item_) {
                self->database_->UpdateStream(message.item_);
                self->Reload();
            }
        });
        listener_->Listen<MsgRemotePeerInfo>([weakSelf](const MsgRemotePeerInfo& message) {
            if (const auto self = weakSelf.lock()) {
                std::scoped_lock lock{self->streamsMutex_};
                for (const auto& stream : self->streams_) {
                    if (stream && stream->stream_id_ == message.stream_id_) {
                        stream->desktop_name_ = message.desktop_name_;
                        stream->os_version_ = message.os_version_;
                        self->database_->UpdateStream(stream);
                        break;
                    }
                }
            }
        });
        listener_->Listen<MsgForceClearProgramData>([weakSelf](const MsgForceClearProgramData&) {
            if (const auto self = weakSelf.lock()) self->Reload();
        });
    }
    listener_->Listen<MsgGrTimer5S>([weakSelf](const MsgGrTimer5S&) {
        if (const auto self = weakSelf.lock()) {
            if (self->stateChecker_) self->stateChecker_->UpdateCurrentStreamItems(self->Snapshot());
            self->RefreshResources();
        }
    });
    const auto identityChanged = [weakSelf] {
        if (const auto self = weakSelf.lock()) {
            self->ClearIdentityResources();
            self->StartResourceRefresh(true);
        }
    };
    listener_->Listen<MsgUserLoggedIn>([identityChanged](const MsgUserLoggedIn&) { identityChanged(); });
    listener_->Listen<MsgUserLoggedOut>([identityChanged](const MsgUserLoggedOut&) { identityChanged(); });
}

void StreamSessionController::Reload() {
    std::vector<std::shared_ptr<px_console::ConsoleStream>> updated{};
    {
        std::scoped_lock lock{streamsMutex_};
        if (mode_ == AppStreamListMode::kCloudApplications) {
            updated = applicationStreams_;
        } else {
            updated = database_->GetAllStreamsSortByCreatedTime();
            std::erase_if(updated, [](const auto& stream) {
                return stream && stream->connect_type_ == connection_policy::kConsoleAppTicket;
            });
            ApplyConsoleDeviceOnlineStates(updated, deviceOnlineStates_);
        }
        for (const auto& stream : updated) {
            if (stream) stream->device_id_ = settings_.get().GetDeviceId();
        }
        streams_ = std::move(updated);
    }
    if (stateChecker_) stateChecker_->UpdateCurrentStreamItems(Snapshot());
}

std::vector<std::shared_ptr<px_console::ConsoleStream>> StreamSessionController::Snapshot() { return CopyStreams(); }

std::vector<std::shared_ptr<px_console::ConsoleStream>> StreamSessionController::CopyStreams() {
    std::scoped_lock lock{streamsMutex_};
    return streams_;
}

void StreamSessionController::Start(const std::shared_ptr<px_console::ConsoleStream>& item, const bool viewOnly) {
    if (!item) {
        LOGE("Ignore stream launch without a stream item");
        context_->NotifyAppErrMessage(tcTr("id_connect_failed"), tcTr("id_console_reason_internal"));
        return;
    }

    // The old QWidget implementation deferred this call so its connecting animation could paint.
    // The ImGui shell paints every frame, so deferring the business action only creates a silent gap
    // between a click and the launch workflow. Enter the workflow synchronously and let its network
    // operations continue on the asynchronous runtime.
    LOGI("Stream launch requested: stream={}, kind={}, view_only={}", item->stream_id_,
         item->connect_type_ == connection_policy::kConsoleAppTicket ? "application" : "device", viewOnly);
    context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_connecting"));
    StartInternal(item, viewOnly);
}

void StreamSessionController::StartInternal(const std::shared_ptr<px_console::ConsoleStream>& item, const bool viewOnly) {
    const bool applicationTicket{item->connect_type_ == connection_policy::kConsoleAppTicket};
    auto target = item;
    if (applicationTicket) {
        const auto preference = database_->GetStreamByStreamId(item->stream_id_);
        target->force_tcp_ = preference && preference.value()->force_tcp_;
        target->force_relay_ = preference && preference.value()->force_relay_;
    } else {
        const auto saved = database_->GetStreamByStreamId(item->stream_id_);
        if (!saved) {
            LOGE("Read stream from database failed: {}", item->stream_id_);
            context_->NotifyAppErrMessage(tcTr("id_connect_failed"), tcTr("id_console_reason_internal"));
            return;
        }
        target = saved.value();
    }
    target->only_viewing_ = viewOnly;
    if (connection_policy::SharedLinkUsesConsoleTicket(target->connect_type_, grApp->GetUserManager()->IsLoggedIn())) {
        target = std::make_shared<px_console::ConsoleStream>(*target);
        target->connect_type_ = connection_policy::kConsoleDeviceTicket;
    }
    const auto policy = connection_policy::Classify(target->connect_type_, target->remote_device_id_, target->stream_host_, target->stream_port_);
    LOGI("Stream launch policy selected: stream={}, policy={}, endpoint_present={}", target->stream_id_, static_cast<int>(policy),
         !target->stream_host_.empty() && target->stream_port_ > 0);
    if (policy == connection_policy::LaunchPolicy::kReject) {
        context_->NotifyAppErrMessage(tcTr("id_connect_failed"), tcTr("id_connection_ticket_required"));
    } else if (policy == connection_policy::LaunchPolicy::kConsoleTicket) {
        StartConsoleTicketLaunch(target, applicationTicket);
    } else {
        ContinueStart(target, false);
    }
}

StreamLaunchAuthHooks StreamSessionController::MakeAuthHooks() const {
    const auto users = grApp->GetUserManager();
    StreamLaunchAuthHooks hooks{};
    hooks.renew_rdp_ticket = [users](const RdpLaunchRecovery& recovery) {
        struct ScopedToken final {
            explicit ScopedToken(const std::string_view source) : value{source} {}
            ~ScopedToken() { OPENSSL_cleanse(value.data(), value.size()); }
            std::string value{};
        } token{recovery.renewal->View()};
        const auto result = users->RenewConnectionTicket(token.value, recovery.nonce);
        return result ? StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(result.value())
                      : StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Failure(result.error(),
                                                                                                px_console::ConsoleApiLastErrorMessage());
    };
    hooks.post_blocking = [context = context_](std::function<void()> task) { context->PostTask(std::move(task)); };
    hooks.start_app = [users](const std::string& appId, const std::string& nonce) {
        const auto result = users->StartApp(appId, nonce);
        return result ? StreamLaunchConsoleCall<px_console::ConsoleUserAppInstance>::Success(result.value())
                      : StreamLaunchConsoleCall<px_console::ConsoleUserAppInstance>::Failure(result.error(),
                                                                                              px_console::ConsoleApiLastErrorMessage());
    };
    hooks.query_apps = [users] {
        const auto result = users->QueryApps();
        return result ? StreamLaunchConsoleCall<std::vector<px_console::ConsoleUserApplication>>::Success(result.value())
                      : StreamLaunchConsoleCall<std::vector<px_console::ConsoleUserApplication>>::Failure(
                            result.error(), px_console::ConsoleApiLastErrorMessage());
    };
    hooks.issue_instance_ticket = [users](const std::string& id, const std::string& nonce, const std::vector<std::string>& permissions) {
        const auto result = users->IssueInstanceTicket(id, nonce, permissions);
        return result ? StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(result.value())
                      : StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Failure(result.error(),
                                                                                                px_console::ConsoleApiLastErrorMessage());
    };
    hooks.issue_device_ticket = [users](const std::string& id, const std::string& nonce, const std::vector<std::string>& permissions) {
        const auto result = users->IssueDeviceTicket(id, nonce, permissions);
        return result ? StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(result.value())
                      : StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Failure(result.error(),
                                                                                                px_console::ConsoleApiLastErrorMessage());
    };
    hooks.resolve_ticket = [](px_console::ConsoleConnectionTicket ticket, const StreamLaunchTicketTarget target) {
        const QUrl url{QString::fromStdString(ticket.launch_url)};
        if (!url.isValid() || url.host().isEmpty() || url.port() <= 0) {
            return PxResult<StreamLaunchResolvedTicket>::Failure(MakePxAsyncError(PxAsyncErrorCode::kProtocolError,
                "stream-launch.resolve-ticket", "Console returned an invalid launch endpoint", false, "INVALID_CONSOLE_ENDPOINT"));
        }
        std::string deviceId{};
        if (target == StreamLaunchTicketTarget::kApplicationInstance) {
            deviceId = QUrlQuery{url}.queryItemValue("deviceId").toStdString();
            if (deviceId.empty()) {
                return PxResult<StreamLaunchResolvedTicket>::Failure(MakePxAsyncError(PxAsyncErrorCode::kProtocolError,
                    "stream-launch.resolve-ticket", "Console application endpoint has no device ID", false, "INVALID_CONSOLE_ENDPOINT"));
            }
        }
        return PxResult<StreamLaunchResolvedTicket>::Success(
            {.ticket = std::move(ticket), .host = url.host().toStdString(), .port = url.port(), .remote_device_id = std::move(deviceId)});
    };
    hooks.probe_direct = [](const std::string& host, const int port) { return RenderApi::GetRenderConfiguration(host, port).has_value(); };
    return hooks;
}

void StreamSessionController::StartConsoleTicketLaunch(const std::shared_ptr<px_console::ConsoleStream>& item, const bool applicationTicket) {
    if (!authorization_) {
        context_->NotifyAppErrMessage(tcTr("id_connect_failed"), tcTr("id_console_reason_internal"));
        return;
    }
    const bool loggedIn{grApp->GetUserManager()->IsLoggedIn()};
    if (!applicationTicket && !loggedIn) {
        LOGW("Reject Console device launch while signed out: stream={}", item->stream_id_);
        context_->NotifyAppErrMessage(tcTr("id_connect_failed"), tcTr("id_connection_ticket_required"));
        return;
    }
    std::vector<std::string> permissions{"view"};
    if (loggedIn) permissions.push_back("file");
    if (!item->only_viewing_) {
        permissions.push_back("input");
        if (loggedIn) permissions.insert(permissions.end(), {"clipboard", "audio"});
    }
    StreamLaunchAuthRequest request{.target = applicationTicket ? StreamLaunchTicketTarget::kApplicationInstance : StreamLaunchTicketTarget::kDevice,
                                    .device_id = item->remote_device_id_, .app_id = item->console_app_id_,
                                    .instance_id = item->console_instance_id_,
                                    .client_nonce = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
                                    .permissions = std::move(permissions),
                                    .deadline = std::chrono::steady_clock::now() + std::chrono::seconds(65)};
    if (applicationTicket && !item->only_viewing_) {
        request.recovery = runningStreams_->TakeRdpRecovery(request.app_id);
        if (request.recovery) {
            request.instance_id = request.recovery->instance_id;
            request.client_nonce = request.recovery->nonce;
        } else if (item->rdp_mode_) {
            request.instance_id.clear();
        }
    }
    const std::weak_ptr<StreamSessionController> weakSelf{shared_from_this()};
    const auto generation = authorization_->Start(std::move(request), MakeAuthHooks(),
        [weakSelf, item, applicationTicket](const std::uint64_t completed, StreamLaunchAuthResult result) mutable {
            if (const auto self = weakSelf.lock()) {
                self->context_->PostUITask([weakSelf, item, applicationTicket, completed, result = std::move(result)]() mutable {
                    if (const auto active = weakSelf.lock()) {
                        active->CompleteConsoleTicketLaunch(item, applicationTicket, completed, std::move(result));
                    }
                });
            }
        });
    if (!generation) {
        LOGE("Unable to start Console authorization workflow: stream={}", item->stream_id_);
        context_->NotifyAppErrMessage(tcTr("id_connect_failed"), tcTr("id_console_reason_internal"));
    } else {
        LOGI("Console authorization started: stream={}, generation={}, target={}", item->stream_id_, *generation,
             applicationTicket ? "application" : "device");
    }
}

void StreamSessionController::CompleteConsoleTicketLaunch(const std::shared_ptr<px_console::ConsoleStream>& item,
                                                          const bool applicationTicket, const std::uint64_t generation,
                                                          StreamLaunchAuthResult result) {
    if (!authorization_ || !authorization_->IsCurrent(generation)) return;
    if (!result) {
        const auto& error = result.Error();
        if (error.code == PxAsyncErrorCode::kCancelled) return;
        LOGE("Console stream launch failed: stream={}, stage={}, code={}, reason={}", item->stream_id_, error.stage,
             error.StableCode(), error.message);
        if (applicationTicket && error.stage == "stream-launch.issue-instance-ticket") item->console_instance_id_.clear();
        QString message{};
        if (error.stage == "stream-launch.resolve-ticket") {
            message = tcTr("id_invalid_console_endpoint");
        } else if (error.code == PxAsyncErrorCode::kTimeout && error.stage == "stream-launch.wait-running") {
            message = tcTr("id_application_start_timeout");
        } else {
            auto apiError = px_console::ConsoleApiError::kInternalError;
            try { apiError = static_cast<px_console::ConsoleApiError>(std::stoi(error.detail_code)); } catch (...) {}
            const auto operation = error.stage == "stream-launch.start-app" || error.stage == "stream-launch.query-apps" ||
                                           error.stage == "stream-launch.wait-running"
                                       ? ConsoleErrorOperation::kStartApplication
                                       : ConsoleErrorOperation::kConnectRemote;
            message = MakeConsoleErrorMessage(operation, apiError, error.message,
                                              MakeConsoleEndpoint(settings_.get().GetConsoleServerHost(), settings_.get().GetConsoleServerPort()));
        }
        context_->NotifyAppErrMessage(tcTr("id_connect_failed"), message);
        return;
    }
    auto payload = result.TakeValue();
    LOGI("Console stream authorization ready: stream={}, generation={}, target={}, direct_available={}", item->stream_id_, generation,
         applicationTicket ? "application" : "device", payload.direct_available);
    if (payload.instance) {
        item->console_instance_id_ = payload.instance->instance_id;
        item->console_instance_state_ = payload.instance->state;
        item->console_online_ = true;
        item->direct_online_ = true;
    }
    auto& resolved = payload.resolved;
    item->stream_host_ = resolved.host;
    item->stream_port_ = resolved.port;
    if (applicationTicket) item->remote_device_id_ = resolved.remote_device_id;
    item->connection_ticket_ = resolved.ticket.ticket;
    item->relay_host_ = resolved.ticket.relay_host;
    item->relay_port_ = resolved.ticket.relay_port;
    item->console_signal_device_id_ = resolved.ticket.signal_device_id;
    item->rdp_configuration_ = std::move(resolved.ticket.rdp_configuration);
    item->rdp_mode_ = static_cast<bool>(item->rdp_configuration_);
    item->connection_renewal_token_ = resolved.ticket.renewal_token;
    item->connection_logical_session_id_ = resolved.ticket.logical_session_id;
    item->connection_nonce_ = payload.client_nonce;
    item->active_session_stream_id_ = resolved.ticket.stream_id;
    const auto allowed = [&resolved](const std::string_view permission) {
        return std::ranges::find(resolved.ticket.permissions, permission) != resolved.ticket.permissions.end();
    };
    item->clipboard_enabled_ = allowed("clipboard");
    item->audio_enabled_ = allowed("audio");
    ContinueStart(item, true, payload.direct_available);
}

void StreamSessionController::ContinueStart(const std::shared_ptr<px_console::ConsoleStream>& item, const bool consoleTicket,
                                             const std::optional<bool> authenticatedDirectAvailable) {
    const bool directAvailable = authenticatedDirectAvailable.value_or(
        RenderApi::GetRenderConfiguration(item->stream_host_, item->stream_port_).has_value());
    const bool relayAvailable = consoleTicket && item->force_relay_ && item->HasRelayInfo();
    if (!directAvailable && !relayAvailable) {
        LOGW("No available stream endpoint: stream={}, direct={}, relay={}", item->stream_id_, directAvailable, relayAvailable);
        context_->NotifyAppErrMessage(tcTr("id_connect_failed"), tcTr("id_device_offline"));
        return;
    }
    const bool passwordDirect{!consoleTicket};
    item->ip_direct_prevalidated_ = false;
    if (passwordDirect) item->connection_nonce_ = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const std::string candidate = !item->remote_device_safety_pwd_.empty()
                                      ? item->remote_device_safety_pwd_
                                      : (item->remote_device_random_pwd_.empty() ? std::string{} : MD5::Hex(item->remote_device_random_pwd_));
    if (passwordDirect) {
        const auto verified = RenderApi::VerifySecurityPassword(item->stream_host_, item->stream_port_, candidate);
        if (!verified || !verified.value()) {
            context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_password_invalid_msg"));
            return;
        }
        if (item->active_session_stream_id_.empty()) item->active_session_stream_id_ = item->stream_id_;
        item->ip_direct_prevalidated_ = true;
        if (item->remote_device_safety_pwd_ != candidate) {
            item->remote_device_safety_pwd_ = candidate;
            database_->UpdateStreamSafetyPwd(item->stream_id_, candidate);
        }
    }
    LOGI("Launching native client: stream={}, session={}, direct={}, relay={}", item->stream_id_, item->active_session_stream_id_, directAvailable,
         relayAvailable);
    runningStreams_->StartStream(item);
}

bool StreamSessionController::Stop(const std::shared_ptr<px_console::ConsoleStream>& item) {
    return item && runningStreams_->StopStream(item);
}

void StreamSessionController::StartFileTransfer(const std::shared_ptr<px_console::ConsoleStream>& item) {
    if (!item || !grApp->GetUserManager()->IsLoggedIn() || item->remote_device_id_.empty()) {
        context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_file_transfer_requires_login"));
        return;
    }
    if (runningStreams_->OpenFileTransferInRunningClient(item)) return;
    const auto target = database_->GetStreamByStreamId(item->stream_id_);
    if (target) StartFileTransferTicketLaunch(target.value());
}

void StreamSessionController::StartFileTransferTicketLaunch(const std::shared_ptr<px_console::ConsoleStream>& item) {
    StreamLaunchAuthRequest request{.target = StreamLaunchTicketTarget::kDevice, .device_id = item->remote_device_id_,
                                    .client_nonce = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
                                    .permissions = {"file"},
                                    .deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10)};
    const std::weak_ptr<StreamSessionController> weakSelf{shared_from_this()};
    const auto generation = authorization_->Start(std::move(request), MakeAuthHooks(),
        [weakSelf, item](const std::uint64_t completed, StreamLaunchAuthResult result) mutable {
            if (const auto self = weakSelf.lock()) {
                self->context_->PostUITask([weakSelf, item, completed, result = std::move(result)]() mutable {
                    if (const auto active = weakSelf.lock()) active->CompleteFileTransferTicketLaunch(item, completed, std::move(result));
                });
            }
        });
    if (!generation) context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_console_reason_internal"));
}

void StreamSessionController::CompleteFileTransferTicketLaunch(const std::shared_ptr<px_console::ConsoleStream>& item,
                                                               const std::uint64_t generation, StreamLaunchAuthResult result) {
    if (!authorization_ || !authorization_->IsCurrent(generation)) return;
    if (!result) {
        if (result.Error().code != PxAsyncErrorCode::kCancelled) {
            context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_file_transfer_device_unavailable"));
        }
        return;
    }
    auto payload = result.TakeValue();
    if (!payload.direct_available) {
        context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_device_offline"));
        return;
    }
    item->stream_host_ = payload.resolved.host;
    item->stream_port_ = payload.resolved.port;
    item->connection_ticket_ = payload.resolved.ticket.ticket;
    item->connection_nonce_ = payload.client_nonce;
    item->active_session_stream_id_ = payload.resolved.ticket.stream_id;
    runningStreams_->StartFileTransfer(item);
}

void StreamSessionController::RefreshResources() { StartResourceRefresh(false); }

void StreamSessionController::StartResourceRefresh(const bool identityChanged) {
    const auto mode = mode_;
    const std::weak_ptr<StreamSessionController> weakSelf{shared_from_this()};
    resourceCatalog_->Refresh(identityChanged, [weakSelf, mode](StreamResourceSnapshot snapshot) mutable {
        const auto self = weakSelf.lock();
        if (!self) return;
        {
            std::scoped_lock lock{self->streamsMutex_};
            if (mode == AppStreamListMode::kCloudApplications && snapshot.applicationStreams) {
                self->applicationStreams_ = std::move(*snapshot.applicationStreams);
            } else if (mode == AppStreamListMode::kRemoteDevices && snapshot.onlineStates) {
                self->deviceOnlineStates_ = std::move(*snapshot.onlineStates);
            }
        }
        self->Reload();
    });
}

void StreamSessionController::ClearIdentityResources() {
    {
        std::scoped_lock lock{streamsMutex_};
        if (mode_ == AppStreamListMode::kCloudApplications) applicationStreams_.clear();
        else deviceOnlineStates_.clear();
    }
    Reload();
}

} // namespace px
