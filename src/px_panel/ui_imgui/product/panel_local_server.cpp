#include "panel_local_server.h"

#include <utility>

#include "px_client_panel_message.pb.h"
#include "px_common/hardware.h"
#include "px_common/log.h"
#include "px_render_panel_message.pb.h"

namespace px::panel::product {
namespace {

std::string QueryValue(const std::string_view query, const std::string_view key) {
    const std::string marker{std::string{key} + "="};
    const auto begin = query.find(marker);
    if (begin == std::string_view::npos) return {};
    const auto valueBegin = begin + marker.size();
    const auto end = query.find('&', valueBegin);
    return std::string{query.substr(valueBegin, end - valueBegin)};
}

int DecrementConnectionCount(std::atomic_int& count) {
    int current{count.load(std::memory_order_acquire)};
    while (current > 0 && !count.compare_exchange_weak(current, current - 1, std::memory_order_acq_rel)) {
    }
    return current > 0 ? current - 1 : 0;
}

} // namespace

std::shared_ptr<PanelLocalServer> PanelLocalServer::Create(const std::shared_ptr<PanelConfigStore>& config,
                                                           const std::shared_ptr<PanelAuditStore>& auditStore) {
    auto result = std::make_shared<PanelLocalServer>(config, auditStore);
    result->Start();
    return result;
}

PanelLocalServer::PanelLocalServer(std::shared_ptr<PanelConfigStore> config, std::shared_ptr<PanelAuditStore> auditStore)
    : config_{std::move(config)}, auditStore_{std::move(auditStore)} {}
PanelLocalServer::~PanelLocalServer() { Stop(); }

LocalServerSnapshot PanelLocalServer::Snapshot() const {
    return {.listening = server_ && server_->is_started(),
            .listenPort = server_ ? server_->listen_port() : 0,
            .rendererConnected = rendererConnections_.load(std::memory_order_acquire) > 0,
            .clientConnections = clientConnections_.load(std::memory_order_acquire)};
}

std::optional<PanelSystemInformation> PanelLocalServer::SystemInformation() const {
    const std::scoped_lock lock{mutex_};
    return systemInformation_;
}

std::optional<VoiceCallRequest> PanelLocalServer::PendingVoiceCall() const {
    const std::scoped_lock lock{mutex_};
    return pendingVoiceCall_;
}

void PanelLocalServer::ResolveVoiceCall(const VoiceCallRequest& request, const bool accepted, const std::string& reason) {
    std::shared_ptr<asio2::http_session> renderer{};
    {
        const std::scoped_lock lock{mutex_};
        if (!pendingVoiceCall_ || pendingVoiceCall_->streamId != request.streamId || pendingVoiceCall_->callId != request.callId ||
            pendingVoiceCall_->requestId != request.requestId) {
            return;
        }
        pendingVoiceCall_.reset();
        renderer = rendererSession_;
    }
    if (!renderer) return;
    pxrp::RpMessage message{};
    message.set_type(pxrp::kRpVoiceCallConsentDecision);
    auto& decision = *message.mutable_voice_call_consent_decision();
    decision.set_stream_id(request.streamId);
    decision.set_call_id(request.callId);
    decision.set_request_id(request.requestId);
    decision.set_accepted(accepted);
    decision.set_reason(reason);
    renderer->async_send(message.SerializeAsString());
}

void PanelLocalServer::SetRestartHandler(std::function<void()> handler) {
    const std::scoped_lock lock{mutex_};
    restartHandler_ = std::move(handler);
}

void PanelLocalServer::RefreshPanelInfo() {
    std::shared_ptr<asio2::http_session> renderer{};
    {
        const std::scoped_lock lock{mutex_};
        renderer = rendererSession_;
    }
    if (!renderer) {
        LOGW("event=desktop_access_policy component=panel operation=publish outcome=deferred reason=renderer_not_connected");
        return;
    }
    const std::weak_ptr<PanelLocalServer> weakSelf{shared_from_this()};
    renderer->post_queued_event([weakSelf, renderer] {
        if (const auto self = weakSelf.lock(); self && renderer->is_started()) {
            self->SendPanelInfo(renderer);
        }
    });
}

bool PanelLocalServer::OpenFileTransfer(const std::string& streamId) {
    std::shared_ptr<asio2::http_session> session{};
    {
        const std::scoped_lock lock{mutex_};
        if (const auto found = clients_.find(streamId); found != clients_.end()) session = found->second;
    }
    if (!session) return false;
    pxcp::CpMessage message{};
    message.set_type(pxcp::CpMessageType::kCpOpenFileTransfer);
    message.set_stream_id(streamId);
    session->async_send(message.SerializeAsString());
    return true;
}

void PanelLocalServer::Stop() {
    stopping_.store(true, std::memory_order_release);
    if (server_) {
        server_->stop_all_timers();
        server_->stop();
        server_.reset();
    }
    const std::scoped_lock lock{mutex_};
    clients_.clear();
    rendererSession_.reset();
    pendingVoiceCall_.reset();
    systemInformation_.reset();
    restartHandler_ = {};
    rendererConnections_.store(0, std::memory_order_release);
    clientConnections_.store(0, std::memory_order_release);
}

void PanelLocalServer::Start() {
    stopping_.store(false, std::memory_order_release);
    server_ = std::make_shared<asio2::http_server>();
    AddRoute("/panel");
    AddRoute("/panel/renderer");
    AddRoute("/sys/info");
    const bool started = server_->start("127.0.0.1", config_->Ports().panel);
    LOGI("Panel local event server: started={}, port={}", started, config_->Ports().panel);
}

void PanelLocalServer::AddRoute(const std::string& path) {
    const std::weak_ptr<PanelLocalServer> weakSelf{shared_from_this()};
    server_->bind(
        path, websocket::listener<asio2::http_session>{}
                            .on("message",
                                [weakSelf, path](std::shared_ptr<asio2::http_session>&, const std::string_view bytes) {
                                    const auto self = weakSelf.lock();
                          if (!self) return;
                                    if (path == "/panel") {
                                        pxcp::CpMessage message{};
                              if (!message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) return;
                                        if (message.type() == pxcp::CpMessageType::kCpHello && !message.stream_id().empty()) {
                                            LOGI("Panel client event channel ready: {}", message.stream_id());
                                        }
                                    } else if (path == "/panel/renderer") {
                                        pxrp::RpMessage message{};
                              if (!message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) return;
                                        self->auditStore_->Consume(message, self->config_->Identity().deviceId);
                                        if (message.type() == pxrp::kRpVoiceCallConsentRequest) {
                                            const auto& request = message.voice_call_consent_request();
                                  if (request.protocol_version() != 1) return;
                                            const std::scoped_lock lock{self->mutex_};
                                            self->pendingVoiceCall_ = VoiceCallRequest{.visitorDeviceId = request.visitor_device_id(),
                                                                                       .streamId = request.stream_id(),
                                                                                       .callId = request.call_id(),
                                                                                       .requestId = request.request_id(),
                                                                                       .expiresAtUnixMs = request.expires_at_unix_ms()};
                                        } else if (message.type() == pxrp::kRpVoiceCallConsentCancel) {
                                            const auto& cancel = message.voice_call_consent_cancel();
                                            const std::scoped_lock lock{self->mutex_};
                                            if (self->pendingVoiceCall_ && self->pendingVoiceCall_->streamId == cancel.stream_id() &&
                                                self->pendingVoiceCall_->callId == cancel.call_id() &&
                                                self->pendingVoiceCall_->requestId == cancel.request_id()) {
                                                self->pendingVoiceCall_.reset();
                                            }
                                        } else if (message.type() == pxrp::kRpRestartServer) {
                                            LOGW("Renderer requested a restart: {}", message.restart_server().reason());
                                            std::function<void()> handler{};
                                            {
                                                const std::scoped_lock lock{self->mutex_};
                                                handler = self->restartHandler_;
                                            }
                                  if (handler) handler();
                                        }
                                    } else if (path == "/sys/info") {
                                        if (auto systemInformation = ParsePanelSystemInformation(bytes)) {
                                            const std::scoped_lock lock{self->mutex_};
                                            self->systemInformation_ = std::move(systemInformation);
                                        }
                                    }
                                })
                            .on("open",
                                [weakSelf, path](std::shared_ptr<asio2::http_session>& session) {
                                    const auto self = weakSelf.lock();
                          if (!self) return;
                                    session->ws_stream().binary(true);
                                    session->set_no_delay(true);
                                    if (path == "/panel") {
                                        const std::string streamId{QueryValue(session->get_request().get_query(), "stream_id")};
                                        if (!streamId.empty()) {
                                            const std::scoped_lock lock{self->mutex_};
                                            self->clients_[streamId] = session;
                                        }
                                        self->clientConnections_.fetch_add(1, std::memory_order_acq_rel);
                                    } else if (path == "/panel/renderer") {
                                        {
                                            const std::scoped_lock lock{self->mutex_};
                                            self->rendererSession_ = session;
                                        }
                                        self->rendererConnections_.fetch_add(1, std::memory_order_acq_rel);
                                        session->post_queued_event([weakSelf, session] {
                                  if (const auto active = weakSelf.lock()) active->SendPanelInfo(session);
                                        });
                                    }
                                })
                            .on("close", [weakSelf, path](std::shared_ptr<asio2::http_session>& session) {
                                const auto self = weakSelf.lock();
                      if (!self) return;
                                if (path == "/panel") {
                                    const std::string streamId{QueryValue(session->get_request().get_query(), "stream_id")};
                                    const std::scoped_lock lock{self->mutex_};
                                    if (const auto found = self->clients_.find(streamId); found != self->clients_.end() && found->second == session) {
                                        self->clients_.erase(found);
                                    }
                                    const int remaining{DecrementConnectionCount(self->clientConnections_)};
                          if (remaining == 0 && !self->stopping_.load(std::memory_order_acquire) && self->config_->Settings().disconnectAutoLock) {
                                        LOGI("Last Panel client disconnected; locking the workstation by policy");
                                        Hardware::LockScreen();
                                    }
                                } else if (path == "/panel/renderer") {
                                    {
                                        const std::scoped_lock lock{self->mutex_};
                              if (self->rendererSession_ == session) self->rendererSession_.reset();
                                    }
                                    static_cast<void>(DecrementConnectionCount(self->rendererConnections_));
                                }
                            }));
}

void PanelLocalServer::SendPanelInfo(const std::shared_ptr<asio2::http_session>& session) const {
    const auto identity = config_->Identity();
    const auto settings = config_->Settings();
    pxrp::RpMessage message{};
    message.set_type(pxrp::kSyncPanelInfo);
    auto& info = *message.mutable_sync_panel_info();
    info.set_device_id(identity.deviceId);
    info.set_device_random_pwd(identity.randomPassword);
    info.set_device_safety_pwd(identity.securityPasswordHash);
    info.set_can_be_operated(true);
    info.set_language(settings.language == ::px::ui::Language::English ? 1 : 0);
    info.set_file_transfer_enabled(true);
    info.set_audio_enabled(settings.general.captureAudio);
    info.set_role(1);
    const bool incomingRemoteAccessEnabled{config_->IncomingRemoteAccessEnabled()};
    info.set_remote_access_disabled(!incomingRemoteAccessEnabled);
    LOGI("event=desktop_access_policy component=panel operation=publish enabled={} outcome=sent", incomingRemoteAccessEnabled);
    session->async_send(message.SerializeAsString());
}

} // namespace px::panel::product
