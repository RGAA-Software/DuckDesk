#include "relay_transport_runtime.h"

#include <cstdlib>
#include <utility>

#include "px_common/client_id_extractor.h"
#include "px_common/async_runtime.h"
#include "px_common/data.h"
#include "px_common/hardware.h"
#include "px_common/ip_util.h"
#include "px_common/log.h"
#include "px_common/time_util.h"
#include "px_relay_client/relay_connected_info.h"
#include "px_relay_client/relay_room.h"
#include "px_relay_client/relay_server_sdk.h"
#include "px_relay_client/relay_server_sdk_param.h"
#include "px_render/architecture/runtime/render_execution_context.h"
#include "px_render/architecture/events/render_event.h"
#include "px_render/modules/module_ids.h"
#include "relay_message.pb.h"

using namespace px_relay;

namespace px {

std::shared_ptr<RelayTransportRuntime> RelayTransportRuntime::Create(
    RelayTransportRuntimeConfig config) {
    return std::make_shared<RelayTransportRuntime>(std::move(config));
}

RelayTransportRuntime::RelayTransportRuntime(RelayTransportRuntimeConfig config)
    : config_(std::move(config)) {}

RelayTransportRuntime::~RelayTransportRuntime() {
    Stop();
}

void RelayTransportRuntime::Start(
    const std::shared_ptr<RenderExecutionContext>& context,
    RenderEventCallback event_callback) {
    {
        std::lock_guard lock(sink_mutex_);
        execution_context_ = context;
        event_callback_ = std::move(event_callback);
    }

    if (stopping_.load(std::memory_order_acquire)) {
        return;
    }
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    const auto config = ConfigSnapshot();
    const auto control = std::make_shared<MonitorControl>();
    const auto weak_self = weak_from_this();
    bool monitor_scheduled{false};
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (!stopping_.load(std::memory_order_acquire)) {
            monitor_control_ = control;
            monitor_scheduled = config.async_runtime && config.async_runtime->DeferBlocking([weak_self, control]() {
                Monitor(weak_self, control);
            });
            if (!monitor_scheduled) {
                monitor_control_.reset();
            }
        }
    }
    if (!monitor_scheduled) {
        started_.store(false, std::memory_order_release);
        if (!stopping_.load(std::memory_order_acquire)) {
            LOGE("event=module.start component=relay code=ASYNC_RUNTIME_UNAVAILABLE operation=start_monitor outcome=failed recoverable=false");
        }
    }
}

void RelayTransportRuntime::Stop() {
    if (stopping_.exchange(true)) {
        return;
    }

    std::shared_ptr<MonitorControl> control;
    {
        std::lock_guard lock(lifecycle_mutex_);
        control = monitor_control_;
    }
    if (control) {
        std::unique_lock lock(control->mutex);
        control->stop_requested = true;
        control->wake_requested = true;
        const auto called_from_monitor = control->worker_thread_id == std::this_thread::get_id();
        lock.unlock();
        control->wake_condition.notify_all();
        if (!called_from_monitor) {
            lock.lock();
            control->stopped_condition.wait(lock, [control]() { return control->completed; });
        }
        std::lock_guard lifecycle_lock(lifecycle_mutex_);
        if (monitor_control_ == control) {
            monitor_control_.reset();
        }
    }

    ReleaseConnections();
    {
        std::lock_guard lock(ft_route_mutex_);
        ft_routes_.clear();
    }
    {
        std::lock_guard lock(sink_mutex_);
        event_callback_ = {};
        execution_context_.reset();
    }
}

void RelayTransportRuntime::WakeMonitor() {
    std::shared_ptr<MonitorControl> control;
    {
        std::lock_guard lock(lifecycle_mutex_);
        control = monitor_control_;
    }
    if (!control) {
        return;
    }
    {
        std::lock_guard lock(control->mutex);
        control->wake_requested = true;
    }
    control->wake_condition.notify_all();
}

void RelayTransportRuntime::UpdateSettings(const RenderModuleSettings& settings) {
    bool connection_changed = false;
    {
        std::lock_guard lock(config_mutex_);
        connection_changed = settings.device_id != config_.settings.device_id ||
                             settings.relay_host != config_.settings.relay_host ||
                             settings.relay_port != config_.settings.relay_port ||
                             settings.appkey != config_.settings.appkey;
        config_.settings = settings;
    }
    if (connection_changed && started_) {
        need_reconnect_ = true;
        LOGW("event=transport.configuration_changed component=relay operation=schedule_reconnect "
             "code=RELAY_CONFIGURATION_CHANGED outcome=pending recoverable=true");
    }
    WakeMonitor();
}

RelayTransportRuntimeConfig RelayTransportRuntime::ConfigSnapshot() const {
    std::lock_guard lock(config_mutex_);
    return config_;
}

bool RelayTransportRuntime::WaitFor(const std::shared_ptr<MonitorControl>& control, const std::chrono::milliseconds delay) {
    if (!control) {
        return false;
    }
    std::unique_lock lock(control->mutex);
    static_cast<void>(control->wake_condition.wait_for(lock, delay, [control]() {
        return control->stop_requested || control->wake_requested;
    }));
    control->wake_requested = false;
    return !control->stop_requested;
}

void RelayTransportRuntime::Monitor(std::weak_ptr<RelayTransportRuntime> runtime, const std::shared_ptr<MonitorControl>& control) {
    {
        std::lock_guard lock(control->mutex);
        control->worker_thread_id = std::this_thread::get_id();
    }
    int connect_count = 0;
    std::vector<RelayDeviceNetInfo> net_info;
    for (const auto& info : IPUtil::ScanIPs()) {
        net_info.push_back(RelayDeviceNetInfo{
            .ip_ = info.ip_addr_,
            .mac_ = info.mac_address_,
        });
    }

    while (true) {
        const auto self = runtime.lock();
        if (!self || self->stopping_.load(std::memory_order_acquire)) {
            break;
        }
        const auto config = self->ConfigSnapshot();
        auto relay_host = config.configured_host;
        auto relay_port = config.configured_port;
        if (!config.settings.relay_host.empty()) {
            relay_host = config.settings.relay_host;
        }
        const auto settings_port = std::atoi(config.settings.relay_port.c_str());
        if (settings_port > 0) {
            relay_port = settings_port;
        }

        if (self->need_reconnect_.exchange(false)) {
            LOGW("event=transport.connection_replaced component=relay operation=apply_configuration "
                 "code=RELAY_CONFIGURATION_CHANGED outcome=restarting recoverable=true");
            self->ReleaseConnections();
            if (!WaitFor(control, std::chrono::milliseconds(500))) {
                break;
            }
        }

        if (config.settings.device_id.empty() || relay_host.empty() ||
            relay_port <= 0 || config.settings.appkey.empty()) {
            if (!WaitFor(control, std::chrono::milliseconds(500))) {
                break;
            }
            continue;
        }

        auto media_sdk = self->MediaSdk();
        if (!media_sdk) {
            self->ConnectMedia(config, relay_host, relay_port, net_info, connect_count++);
        }

        if (!WaitFor(control, std::chrono::seconds(2))) {
            break;
        }

        media_sdk = self->MediaSdk();
        if (media_sdk && media_sdk->IsAlive()) {
            const auto ft_sdk = self->FileTransferSdk();
            if (!ft_sdk) {
                self->ConnectFileTransfer(config, relay_host, relay_port, net_info);
            }
        }
    }
    {
        std::lock_guard lock(control->mutex);
        control->completed = true;
    }
    control->stopped_condition.notify_all();
}

void RelayTransportRuntime::ReleaseConnections() {
    ++media_generation_;
    ++file_transfer_generation_;
    auto media_sdk = MediaSdk();
    auto ft_sdk = FileTransferSdk();
    SetMediaSdk({});
    SetFileTransferSdk({});
    if (media_sdk) {
        media_sdk->Stop();
    }
    if (ft_sdk) {
        ft_sdk->Stop();
    }
}

std::shared_ptr<RelayServerSdk> RelayTransportRuntime::MediaSdk() const {
    std::lock_guard lock(sdk_mutex_);
    return relay_media_sdk_;
}

std::shared_ptr<RelayServerSdk> RelayTransportRuntime::FileTransferSdk() const {
    std::lock_guard lock(sdk_mutex_);
    return relay_ft_sdk_;
}

void RelayTransportRuntime::SetMediaSdk(std::shared_ptr<RelayServerSdk> sdk) {
    std::lock_guard lock(sdk_mutex_);
    relay_media_sdk_ = std::move(sdk);
}

void RelayTransportRuntime::SetFileTransferSdk(std::shared_ptr<RelayServerSdk> sdk) {
    std::lock_guard lock(sdk_mutex_);
    relay_ft_sdk_ = std::move(sdk);
}

bool RelayTransportRuntime::IsCurrentMediaGeneration(uint64_t generation) const {
    return !stopping_ && media_generation_.load() == generation;
}

bool RelayTransportRuntime::IsCurrentFileTransferGeneration(
    uint64_t generation) const {
    return !stopping_ && file_transfer_generation_.load() == generation;
}

void RelayTransportRuntime::ConnectMedia(
    const RelayTransportRuntimeConfig& config,
    const std::string& host, int port,
    const std::vector<RelayDeviceNetInfo>& net_info,
    int connect_count) {
    const auto relay_identity = config.relay_device_id.empty()
        ? config.settings.device_id
        : config.relay_device_id;
    const auto server_device_id = "server_" + relay_identity;
    LOGI("Connecting relay media channel, attempt: {}, device: {}, host: {}, port: {}",
         connect_count, server_device_id, host, port);

    const auto sdk = std::make_shared<RelayServerSdk>(RelayServerSdkParam{
        .host_ = host,
        .port_ = port,
        .ssl_ = false,
        .device_id_ = server_device_id,
        .net_info_ = net_info,
        .appkey_ = config.settings.appkey,
        .async_runtime_ = config.async_runtime,
    });
    const auto generation = ++media_generation_;
    SetMediaSdk(sdk);

    const auto weak_self = weak_from_this();
    sdk->SetOnConnectedCallback([]() {});
    sdk->SetOnDisConnectedCallback([]() {});
    sdk->SetOnRelayHelloCallback([weak_self, generation](const std::string& device_id) {
        if (const auto self = weak_self.lock();
            self && self->IsCurrentMediaGeneration(generation)) {
            self->ReportRelayAlive(device_id);
        }
    });
    sdk->SetOnRelayHeartbeatCallback(
        [weak_self, generation](const std::string& device_id, int64_t) {
            if (const auto self = weak_self.lock();
                self && self->IsCurrentMediaGeneration(generation)) {
                self->ReportRelayAlive(device_id);
            }
        });
    sdk->SetOnRequestControlCallback(
        [weak_self, generation](const std::shared_ptr<RelayMessage>& message) {
            const auto self = weak_self.lock();
            if (!self || !self->IsCurrentMediaGeneration(generation)) {
                return;
            }
            const auto& request = message->request_control();
            LOGI("Relay control request, device: {}, remote: {}, stream: {}, force GDI: {}",
                 request.device_id(), request.remote_device_id(),
                 request.stream_id(), request.force_gdi());
            const auto event = std::make_shared<StreamingParametersRequestedEvent>();
            event->stream_id_ = request.stream_id();
            event->force_gdi_ = request.force_gdi();
            self->Emit(event);
        });
    sdk->SetOnRoomPreparedCallback(
        [weak_self, generation](const std::shared_ptr<RelayMessage>& message) {
            const auto self = weak_self.lock();
            if (!self || !self->IsCurrentMediaGeneration(generation)) {
                return;
            }
            const auto prepared = message->room_prepared();
            const auto media_sdk = self->MediaSdk();
            const auto room = media_sdk
                ? media_sdk->GetRoomById(prepared.room_id())
                : std::shared_ptr<RelayRoom>{};
            if (!room) {
                return;
            }
            if (room->creator_stream_id_.empty()) {
                LOGE("event=transport.protocol_error component=relay code=RELAY_CREATOR_STREAM_MISSING "
                     "operation=prepare_room outcome=reconnecting recoverable=true");
                self->need_reconnect_ = true;
                self->WakeMonitor();
                return;
            }
            self->NotifyClientConnected(
                room->conn_id_, room->creator_stream_id_,
                ExtractClientId(prepared.device_id()));
        });
    sdk->SetOnRoomDestroyedCallback(
        [weak_self, generation](const std::shared_ptr<RelayMessage>& message) {
            const auto self = weak_self.lock();
            if (!self || !self->IsCurrentMediaGeneration(generation)) {
                return;
            }
            const auto destroyed = message->room_destroyed();
            const auto media_sdk = self->MediaSdk();
            const auto room = media_sdk
                ? media_sdk->GetRoomById(destroyed.room_id())
                : std::shared_ptr<RelayRoom>{};
            if (!room) {
                LOGE("event=transport.protocol_error component=relay code=RELAY_ROOM_NOT_FOUND operation=destroy_room "
                     "outcome=ignored recoverable=true room={}", destroyed.room_id());
                return;
            }
            self->NotifyClientDisconnected(
                room->conn_id_, room->creator_stream_id_,
                ExtractClientId(destroyed.device_id()), room->created_timestamp_);
            if (!media_sdk->HasRelayRooms()) {
                self->paused_stream_ = true;
            }
        });
    sdk->SetOnRequestPauseStreamCallback([weak_self, generation]() {
        if (const auto self = weak_self.lock();
            self && self->IsCurrentMediaGeneration(generation)) {
            self->paused_stream_ = true;
            self->Emit(std::make_shared<RelayPausedEvent>());
        }
    });
    sdk->SetOnRequestResumeStreamCallback([weak_self, generation]() {
        if (const auto self = weak_self.lock();
            self && self->IsCurrentMediaGeneration(generation)) {
            self->paused_stream_ = false;
            self->Emit(std::make_shared<RelayResumedEvent>());
        }
    });
    sdk->SetOnRelayProtoMessageCallback(
        [weak_self, generation](const std::shared_ptr<RelayMessage>& message) {
            const auto self = weak_self.lock();
            if (!self || !self->IsCurrentMediaGeneration(generation) ||
                message->type() != RelayMessageType::kRelayTargetMessage) {
                return;
            }
            const auto& payload = message->relay().payload();
            self->EmitNetMessage(
                Data::From(payload),
                TransportChannel::kMedia, {}, false);
        });
    sdk->SetOnNotificationCallback(
        [weak_self, generation](const std::shared_ptr<RelayMessage>& message) {
            if (const auto self = weak_self.lock();
                self && self->IsCurrentMediaGeneration(generation)) {
                const auto event = std::make_shared<PanelStreamMessageEvent>();
                event->body_ = Data::From(message->notification().body());
                self->Emit(event);
            }
        });
    sdk->Start();
}

void RelayTransportRuntime::ConnectFileTransfer(
    const RelayTransportRuntimeConfig& config,
    const std::string& host, int port,
    const std::vector<RelayDeviceNetInfo>& net_info) {
    const auto relay_identity = config.relay_device_id.empty()
        ? config.settings.device_id
        : config.relay_device_id;
    const auto device_id = "ft_server_" + relay_identity;
    LOGI("Connecting relay file-transfer channel, device: {}", device_id);
    const auto sdk = std::make_shared<RelayServerSdk>(RelayServerSdkParam{
        .host_ = host,
        .port_ = port,
        .ssl_ = false,
        .device_id_ = device_id,
        .net_info_ = net_info,
        .device_name_ = Hardware::GetDesktopName(),
        .stream_id_ = device_id,
        .appkey_ = config.settings.appkey,
        .async_runtime_ = config.async_runtime,
    });
    const auto generation = ++file_transfer_generation_;
    SetFileTransferSdk(sdk);

    const auto weak_self = weak_from_this();
    sdk->SetOnRelayHelloCallback([weak_self, generation](const std::string& id) {
        if (const auto self = weak_self.lock();
            self && self->IsCurrentFileTransferGeneration(generation)) {
            self->ReportRelayAlive(id);
        }
    });
    sdk->SetOnRelayHeartbeatCallback(
        [weak_self, generation](const std::string& id, int64_t) {
            if (const auto self = weak_self.lock();
                self && self->IsCurrentFileTransferGeneration(generation)) {
                self->ReportRelayAlive(id);
            }
        });
    sdk->SetOnRelayProtoMessageCallback(
        [weak_self, generation](const std::shared_ptr<RelayMessage>& message) {
            const auto self = weak_self.lock();
            if (!self || !self->IsCurrentFileTransferGeneration(generation)) {
                return;
            }
            const auto type = message->type();
            if (type == RelayMessageType::kRelayTargetMessage) {
                const auto& relay = message->relay();
                const auto room_id = relay.room_ids_size() > 0
                    ? relay.room_ids(0)
                    : std::string{};
                std::string connection_id;
                if (!room_id.empty()) {
                    std::lock_guard lock(self->ft_route_mutex_);
                    auto [route_it, inserted] = self->ft_routes_.try_emplace(room_id);
                    auto& route = route_it->second;
                    if (inserted || route.connection_instance_id.empty()) {
                        route.connection_instance_id = room_id + "#" +
                            std::to_string(++self->ft_route_generation_);
                    }
                    if (route.has_recv_msg_index &&
                        relay.relay_msg_index() != route.last_recv_msg_index + 1) {
                        LOGE("event=transport.sequence_gap component=relay_ft code=RELAY_FT_SEQUENCE_GAP operation=receive "
                             "outcome=accepted recoverable=true room={} current={} last={}",
                             room_id, relay.relay_msg_index(), route.last_recv_msg_index);
                    }
                    route.last_recv_msg_index = relay.relay_msg_index();
                    route.has_recv_msg_index = true;
                    connection_id = route.connection_instance_id;
                }
                const auto& payload = relay.payload();
                self->EmitNetMessage(
                    Data::From(payload),
                    TransportChannel::kFileTransfer,
                    std::move(connection_id), true);
            }
            else if (type == RelayMessageType::kRelayRoomPrepared) {
                const auto& prepared = message->room_prepared();
                std::lock_guard lock(self->ft_route_mutex_);
                auto [route_it, inserted] =
                    self->ft_routes_.try_emplace(prepared.room_id());
                auto& route = route_it->second;
                if (inserted || route.connection_instance_id.empty()) {
                    route.connection_instance_id = prepared.room_id() + "#" +
                        std::to_string(++self->ft_route_generation_);
                }
                route.stream_id = prepared.creator_stream_id();
                const auto& creator = prepared.creator_device_id();
                route.visitor_device_id = ExtractClientId(
                    creator.starts_with("ft_") ? creator.substr(3) : creator);
                if (route.created_timestamp == 0) {
                    route.created_timestamp = static_cast<int64_t>(
                        TimeUtil::GetCurrentTimestamp());
                }
            }
            else if (type == RelayMessageType::kRelayRoomDestroyed) {
                const auto& destroyed = message->room_destroyed();
                FtRelayRouteInfo route;
                bool found = false;
                {
                    std::lock_guard lock(self->ft_route_mutex_);
                    const auto current = self->ft_routes_.find(destroyed.room_id());
                    if (current != self->ft_routes_.end()) {
                        route = current->second;
                        self->ft_routes_.erase(current);
                        found = true;
                    }
                }
                if (found) {
                    self->NotifyClientDisconnected(
                        route.connection_instance_id, route.stream_id,
                        route.visitor_device_id, route.created_timestamp);
                }
            }
        });
    sdk->Start();
}

void RelayTransportRuntime::Emit(RenderEvent event, const bool directly) {
    if (stopping_) {
        return;
    }
    RenderEventCallback callback;
    std::shared_ptr<RenderExecutionContext> context;
    {
        std::lock_guard lock(sink_mutex_);
        callback = event_callback_;
        context = execution_context_;
    }
    if (!callback) {
        return;
    }
    const auto envelope = std::make_shared<RenderEventEnvelope>(RenderEventEnvelope{
        .source_id = kRelayTransportId,
        .payload = std::move(event),
    });
    if (directly || !context) {
        callback(*envelope);
        return;
    }
    const auto weak_self = weak_from_this();
    static_cast<void>(context->Post([weak_self, envelope]() {
        const auto self = weak_self.lock();
        if (!self || self->stopping_) {
            return;
        }
        RenderEventCallback queued_callback;
        {
            std::lock_guard lock(self->sink_mutex_);
            queued_callback = self->event_callback_;
        }
        if (queued_callback) {
            queued_callback(*envelope);
        }
    }));
}

void RelayTransportRuntime::EmitNetMessage(
    std::shared_ptr<Data> message, const TransportChannel& channel,
    std::string connection_instance_id, bool directly) {
    const auto event = std::make_shared<NetworkClientEvent>();
    event->is_proto_ = true;
    event->socket_fd_ = 0;
    event->transport_type_ = TransportKind::kWebSocket;
    event->channel_type_ = channel;
    event->message_ = std::move(message);
    event->connection_instance_id_ = std::move(connection_instance_id);
    const auto weak_self = weak_from_this();
    event->ack_callback_ = [weak_self](const std::shared_ptr<NetMessageAck>& ack) {
        if (const auto self = weak_self.lock()) {
            self->OnMessageAck(ack);
        }
    };
    Emit(event, directly);
}

void RelayTransportRuntime::NotifyClientConnected(
    const std::string& connection_id, const std::string& stream_id,
    const std::string& visitor_device_id) {
    const auto event = std::make_shared<ClientConnectedEvent>();
    event->connection_id_ = connection_id;
    event->stream_id_ = stream_id;
    event->connection_type_ = "Relay";
    event->visitor_device_id_ = visitor_device_id;
    event->begin_timestamp_ = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());
    Emit(event);
}

void RelayTransportRuntime::NotifyClientDisconnected(
    const std::string& connection_id, const std::string& stream_id,
    const std::string& visitor_device_id, int64_t begin_timestamp) {
    const auto event = std::make_shared<ClientDisconnectedEvent>();
    event->connection_id_ = connection_id;
    event->stream_id_ = stream_id;
    event->visitor_device_id_ = visitor_device_id;
    event->end_timestamp_ = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());
    event->duration_ = event->end_timestamp_ - begin_timestamp;
    Emit(event);
}

void RelayTransportRuntime::ReportRelayAlive(const std::string& device_id) {
    const auto event = std::make_shared<RelayAliveEvent>();
    event->device_id_ = device_id;
    Emit(event);
}

void RelayTransportRuntime::ReportSentDataSize(std::size_t size) {
    const auto event = std::make_shared<DataSentEvent>();
    event->size_ = size;
    Emit(event);
}

void RelayTransportRuntime::PostMedia(
    std::shared_ptr<Data> message, bool run_through) {
    if (!message || !IsWorking() || (paused_stream_ && !run_through)) {
        return;
    }
    const auto sdk = MediaSdk();
    if (!sdk) {
        return;
    }
    std::shared_ptr<RenderExecutionContext> context;
    {
        std::lock_guard lock(sink_mutex_);
        context = execution_context_;
    }
    if (context) {
        static_cast<void>(context->Post([sdk, message]() {
            sdk->RelayProtoMessage({}, message);
        }));
    }
    ReportSentDataSize(message->Size());
}

bool RelayTransportRuntime::PostTargetMedia(
    const std::string& stream_id, std::shared_ptr<Data> message,
    bool run_through) {
    if (!message || !IsWorking()) {
        return false;
    }
    if (paused_stream_ && !run_through) {
        return true;
    }
    const auto sdk = MediaSdk();
    if (!sdk) {
        return false;
    }
    std::shared_ptr<RenderExecutionContext> context;
    {
        std::lock_guard lock(sink_mutex_);
        context = execution_context_;
    }
    if (!context) {
        return false;
    }
    static_cast<void>(context->Post([sdk, stream_id, message]() {
        sdk->RelayProtoMessage(stream_id, message);
    }));
    ReportSentDataSize(message->Size());
    return true;
}

FileTransferSendResult RelayTransportRuntime::PostFileTransfer(
    const std::string& stream_id, std::shared_ptr<Data> message,
    const std::string&) {
    if (!message) {
        return FileTransferSendResult::TransportError(
            "relay file-transfer payload is empty");
    }
    if (!IsWorking()) {
        return FileTransferSendResult::Disconnected(
            "relay transport is not working");
    }
    const auto sdk = FileTransferSdk();
    if (!sdk) {
        return FileTransferSendResult::Disconnected(
            "relay file-transfer connection is unavailable");
    }
    if (sdk->GetQueuingMsgCount() >= kMaxFileTransferQueuedMessages) {
        return FileTransferSendResult::Busy(
            "relay file-transfer queue is full",
            sdk->AcquireFileTransferWritableSignal());
    }
    sdk->RelayProtoMessage(stream_id, message);
    ReportSentDataSize(message->Size());
    return FileTransferSendResult::Accepted();
}

int RelayTransportRuntime::ConnectedClientsCount() const {
    const auto sdk = MediaSdk();
    return IsWorking() && sdk ? sdk->GetConnectedClientsCount() : 0;
}

bool RelayTransportRuntime::IsWorking() const {
    const auto config = ConfigSnapshot();
    const auto sdk = MediaSdk();
    return !stopping_ && config.settings.relay_enabled && sdk && sdk->IsAlive();
}

int64_t RelayTransportRuntime::QueuingMediaMessageCount() const {
    const auto sdk = MediaSdk();
    return sdk ? sdk->GetQueuingMsgCount() : 0;
}

int64_t RelayTransportRuntime::QueuingFileTransferMessageCount() const {
    const auto sdk = FileTransferSdk();
    return sdk ? sdk->GetQueuingMsgCount() : 0;
}

std::uint64_t RelayTransportRuntime::MediaChannelInstanceGeneration() const {
    return media_generation_.load(std::memory_order_acquire);
}

std::uint64_t RelayTransportRuntime::MediaConnectionAttemptGeneration() const {
    const auto sdk = MediaSdk();
    return sdk ? sdk->ConnectionGeneration() : 0;
}

std::vector<std::shared_ptr<PxConnectedClientInfo>>
RelayTransportRuntime::ConnectedClientInfo() const {
    const auto sdk = MediaSdk();
    if (!IsWorking() || !sdk) {
        return {};
    }
    std::vector<std::shared_ptr<PxConnectedClientInfo>> result;
    for (const auto& item : sdk->GetConnectedClientInfo()) {
        result.push_back(std::make_shared<PxConnectedClientInfo>(
            PxConnectedClientInfo{
                .device_id_ = item->device_id_,
                .stream_id_ = item->stream_id_,
                .relay_room_id_ = item->room_id_,
                .device_name_ = item->device_name_,
            }));
    }
    return result;
}

void RelayTransportRuntime::OnMessageAck(
    const std::shared_ptr<NetMessageAck>& ack) {
    if (!ack || ack->ch_type_ != TransportChannel::kFileTransfer) {
        return;
    }
    std::lock_guard lock(ack_mutex_);
    if (last_ack_) {
        const auto difference = static_cast<int64_t>(ack->resp_time_) -
                                static_cast<int64_t>(last_ack_->resp_time_);
        LOGI("Relay FT ack interval: {}ms, current: {}, previous: {}",
             difference, ack->resp_time_, last_ack_->resp_time_);
    }
    last_ack_ = ack;
}

} // namespace px
