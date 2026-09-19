#include "panel_service_bridge.h"

#include <asio2/websocket/ws_client.hpp>
#include <chrono>
#include <exception>
#include <format>
#include <utility>
#include <vector>

#include "panel_device_name.h"
#include "px_common/log.h"
#include "px_common/uuid.h"
#include "px_service_message.pb.h"

namespace px::panel::product {

struct PanelServiceBridge::State final {
    explicit State(std::shared_ptr<PanelConfigStore> value) : config{std::move(value)} {}
    std::shared_ptr<PanelConfigStore> config{};
    mutable std::mutex mutex{};
    std::condition_variable_any wakeup{};
    std::shared_ptr<asio2::ws_client> client{};
    std::atomic_bool connected{};
    std::atomic_bool renderRunning{};
    bool nodeControlReady{};
    std::string nodeId{};
    std::string deviceId{};
    std::string nodeAccessHost{};
    std::int64_t nodeGeneration{};
    std::int64_t controlEpoch{};
    std::atomic_int64_t heartbeatIndex{};
};

std::shared_ptr<PanelServiceBridge> PanelServiceBridge::Create(const std::shared_ptr<PanelConfigStore>& config) {
    return std::make_shared<PanelServiceBridge>(config);
}

PanelServiceBridge::PanelServiceBridge(std::shared_ptr<PanelConfigStore> config)
    : state_{std::make_shared<State>(std::move(config))}, thread_{[state = state_](const std::stop_token token) { Run(state, token); }} {}

PanelServiceBridge::~PanelServiceBridge() { Stop(); }

ServiceSnapshot PanelServiceBridge::Snapshot() const {
    if (!state_) return {};
    const std::scoped_lock lock{state_->mutex};
    return {.connected = state_->connected.load(std::memory_order_acquire),
            .renderRunning = state_->renderRunning.load(std::memory_order_acquire),
            .nodeControlReady = state_->nodeControlReady,
            .nodeId = state_->nodeId,
            .deviceId = state_->deviceId,
            .nodeAccessHost = state_->nodeAccessHost,
            .nodeGeneration = state_->nodeGeneration,
            .controlEpoch = state_->controlEpoch};
}

bool PanelServiceBridge::RestartRender() {
    if (!state_ || !state_->connected.load(std::memory_order_acquire)) return false;
    SendRenderCommand(state_, true);
    return true;
}

void PanelServiceBridge::Stop() {
    if (!state_) return;
    thread_.request_stop();
    state_->wakeup.notify_all();
    std::shared_ptr<asio2::ws_client> client{};
    {
        const std::scoped_lock lock{state_->mutex};
        client = state_->client;
    }
    if (client) client->stop();
    if (thread_.joinable()) thread_.join();
    state_.reset();
}

void PanelServiceBridge::Run(const std::shared_ptr<State>& state, const std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        const auto client = std::make_shared<asio2::ws_client>();
        const std::weak_ptr<State> weakState{state};
        client->set_auto_reconnect(false);
        client->keep_alive(true);
        client->set_timeout(std::chrono::milliseconds{1500});
        client->bind_init([weakState] {
            const auto active = weakState.lock();
            std::shared_ptr<asio2::ws_client> current{};
            if (active) {
                const std::scoped_lock lock{active->mutex};
                current = active->client;
            }
            if (current) {
                current->ws_stream().binary(true);
                current->set_no_delay(true);
            }
        });
        client->bind_upgrade([weakState] {
            const auto active = weakState.lock();
            if (!active || asio2::get_last_error()) return;
            active->connected.store(true, std::memory_order_release);
            std::shared_ptr<asio2::ws_client> current{};
            {
                const std::scoped_lock lock{active->mutex};
                current = active->client;
            }
            if (current) {
                current->post_queued_event([weakState] {
                    if (const auto ready = weakState.lock()) {
                        try {
                            SendRenderCommand(ready, false);
                        } catch (const std::exception& error) {
                            LOGE("Panel service startup command failed: {}", error.what());
                        } catch (...) {
                            LOGE("Panel service startup command failed with an unknown exception");
                        }
                    }
                });
            }
        });
        client->bind_disconnect([weakState] {
            if (const auto active = weakState.lock()) {
                active->connected.store(false, std::memory_order_release);
                active->renderRunning.store(false, std::memory_order_release);
                {
                    const std::scoped_lock lock{active->mutex};
                    active->nodeControlReady = false;
                    active->nodeId.clear();
                    active->deviceId.clear();
                    active->nodeAccessHost.clear();
                    active->nodeGeneration = 0;
                    active->controlEpoch = 0;
                }
                active->wakeup.notify_all();
            }
        });
        client->bind_recv([weakState](const std::string_view bytes) {
            const auto active = weakState.lock();
            if (!active) return;
            ServiceMessage message{};
            if (!message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) return;
            if (message.type() == ServiceMessageType::kSrvHeartBeatResp) {
                const auto& heartbeat = message.heart_beat_resp();
                active->renderRunning.store(heartbeat.render_status() == RenderStatus::kWorking, std::memory_order_release);
                bool identityChanged{};
                {
                    const std::scoped_lock lock{active->mutex};
                    active->nodeControlReady = heartbeat.node_control_ready();
                    active->nodeId = heartbeat.node_id();
                    active->deviceId = heartbeat.device_id();
                    active->nodeAccessHost = heartbeat.node_access_host();
                    active->nodeGeneration = heartbeat.node_generation();
                    active->controlEpoch = heartbeat.control_epoch();
                    const auto identity = active->config->Identity();
                    identityChanged = active->nodeControlReady && !active->deviceId.empty() && identity.deviceId != active->deviceId;
                }
                if (identityChanged) {
                    auto identity = active->config->Identity();
                    identity.deviceId = heartbeat.device_id();
                    if (identity.deviceName.empty()) identity.deviceName = BuildDefaultDeviceName();
                    if (identity.randomPassword.empty()) identity.randomPassword = px::GetUUID();
                    if (active->config->SaveIdentity(identity)) {
                        LOGI("Panel adopted the Service node device identity: {}", identity.deviceId);
                        SendRenderCommand(active, true);
                    } else {
                        LOGE("Panel could not persist the Service node device identity");
                    }
                }
            }
        });
        {
            const std::scoped_lock lock{state->mutex};
            state->client = client;
        }
        const auto ports = state->config->Ports();
        static_cast<void>(client->start("127.0.0.1", ports.service, "/service/message?from=panel"));
        while (!stopToken.stop_requested() && client->is_started()) {
            SendHeartbeat(state);
            std::unique_lock lock{state->mutex};
            state->wakeup.wait_for(lock, stopToken, std::chrono::seconds{1}, [] { return false; });
        }
        client->stop();
        {
            const std::scoped_lock lock{state->mutex};
            if (state->client == client) state->client.reset();
        }
        state->connected.store(false, std::memory_order_release);
        if (!stopToken.stop_requested()) {
            std::unique_lock lock{state->mutex};
            state->wakeup.wait_for(lock, stopToken, std::chrono::milliseconds{500}, [] { return false; });
        }
    }
}

void PanelServiceBridge::SendHeartbeat(const std::shared_ptr<State>& state) {
    ServiceMessage message{};
    message.set_type(ServiceMessageType::kSrvHeartBeat);
    auto& heartbeat = *message.mutable_heart_beat();
    heartbeat.set_index(state->heartbeatIndex.fetch_add(1, std::memory_order_acq_rel));
    heartbeat.set_from("panel");
    std::shared_ptr<asio2::ws_client> client{};
    {
        const std::scoped_lock lock{state->mutex};
        client = state->client;
    }
    if (client && client->is_started()) client->async_send(message.SerializeAsString());
}

void PanelServiceBridge::SendRenderCommand(const std::shared_ptr<State>& state, const bool restart) {
    const auto identity = state->config->Identity();
    const auto ports = state->config->Ports();
    const auto settings = state->config->Settings();
    std::vector<std::string> arguments{
        "--app_mode=desktop",
        "--encoder_select_type=auto",
        "--encoder_name=nvenc",
        std::format("--encoder_format={}", settings.general.codec == ui::VideoCodec::H265 ? "h265" : "h264"),
        std::format("--encoder_bitrate={}", settings.general.bitrateMbps),
        std::format("--encoder_fps={}", settings.general.frameRate),
        std::format("--encoder_resolution_type={}", settings.general.resizeEnabled ? "resize" : "origin"),
        std::format("--encoder_width={}", settings.general.width),
        std::format("--encoder_height={}", settings.general.height),
        std::format("--capture_audio={}", settings.general.captureAudio),
        "--capture_audio_type=global",
        "--capture_video=true",
        "--capture_video_type=global",
        "--websocket_enabled=true",
        std::format("--network_listen_port={}", ports.desktop),
        "--webrtc_enabled=true",
        "--udp_kcp_enabled=true",
        "--app_game_path=",
        "--app_game_args=",
        "--debug_block=false",
        std::format("--device_id={}", identity.deviceId),
        std::format("--device_random_pwd={}", identity.randomPassword),
        std::format("--device_safety_pwd={}", identity.securityPasswordHash),
        "--panel_server_host=127.0.0.1",
        std::format("--panel_server_port={}", ports.panel),
        "--service_server_host=127.0.0.1",
        std::format("--service_server_port={}", ports.service),
        "--can_be_operated=true",
        state->config->IncomingRemoteAccessEnabled() ? "--incoming_remote_access_enabled=true" : "--incoming_remote_access_enabled=false",
        std::format("--language={}", settings.language == ::px::ui::Language::English ? 1 : 0),
        "--logfile=true"};
    ServiceMessage message{};
    message.set_type(restart ? ServiceMessageType::kSrvRestartServer : ServiceMessageType::kSrvStartServer);
    auto setPayload = [&arguments, &state](const auto& payload) {
        payload->set_work_dir(state->config->ExecutableDirectory().string());
        payload->set_app_path((state->config->ExecutableDirectory() / "px_render.exe").string());
        for (const auto& argument : arguments) payload->add_args(argument);
    };
    if (restart)
        setPayload(message.mutable_restart_server());
    else
        setPayload(message.mutable_start_server());
    std::shared_ptr<asio2::ws_client> client{};
    {
        const std::scoped_lock lock{state->mutex};
        client = state->client;
    }
    if (client && client->is_started()) client->async_send(message.SerializeAsString());
}

}  // namespace px::panel::product
