#include "relay_transport.h"

#include <cstdlib>
#include <utility>

#include "px_render/network/relay/relay_transport_runtime.h"
#include "px_render/modules/module_ids.h"


namespace px {

RelayTransport::RelayTransport(std::shared_ptr<PxAsyncRuntime> async_runtime)
    : async_runtime_(std::move(async_runtime)) {}

std::string RelayTransport::Id() const { return kRelayTransportId; }
std::string RelayTransport::Name() const { return "Net Relay"; }
std::string RelayTransport::VersionName() const { return "1.2.0"; }
uint32_t RelayTransport::VersionCode() const { return 120; }
std::string RelayTransport::Description() const {
    return "Network via relay server";
}

bool RelayTransport::Start(const RenderModuleConfiguration& configuration) {
    if (!RenderModule::Start(configuration)) {
        return false;
    }
    const auto runtime = RelayTransportRuntime::Create(RelayTransportRuntimeConfig{
        .relay_device_id = configuration.relay_device_id,
        .configured_host = configuration.relay_host,
        .configured_port = std::atoi(configuration.relay_port.c_str()),
        .settings = settings_,
        .async_runtime = async_runtime_,
    });
    if (!runtime) {
        RenderModule::Stop();
        return false;
    }
    runtime_.store(runtime);
    runtime->Start(module_context_, MakeImmediateCompatibilityEventDispatcher());
    return true;
}

bool RelayTransport::Destroy() {
    RenderModule::Stop();
    const auto runtime = runtime_.exchange({});
    if (runtime) {
        runtime->Stop();
    }
    return RenderModule::Destroy();
}

void RelayTransport::Broadcast(
    std::shared_ptr<Data> message, bool run_through) {
    if (const auto runtime = runtime_.load()) {
        runtime->PostMedia(std::move(message), run_through);
    }
}

bool RelayTransport::SendToStream(
    const std::string& stream_id, std::shared_ptr<Data> message,
    bool run_through) {
    const auto runtime = runtime_.load();
    return runtime && runtime->PostTargetMedia(
        stream_id, std::move(message), run_through);
}

FileTransferSendResult RelayTransport::SendFileTransfer(
    const std::string& stream_id, std::shared_ptr<Data> message,
    bool run_through, const std::string& connection_instance_id) {
    static_cast<void>(run_through);
    const auto runtime = runtime_.load();
    return runtime
        ? runtime->PostFileTransfer(
              stream_id, std::move(message), connection_instance_id)
        : FileTransferSendResult::Disconnected(
              "relay runtime is not available");
}

int RelayTransport::ConnectedClientCount() const {
    const auto runtime = runtime_.load();
    return runtime ? runtime->ConnectedClientsCount() : 0;
}

bool RelayTransport::HasOnlyAudioClients() const noexcept { return false; }

bool RelayTransport::IsWorking() const {
    const auto runtime = runtime_.load();
    return runtime && runtime->IsWorking();
}

void RelayTransport::UpdateRouteInfo(const NetSyncInfo& info) {
    route_info_ = info;
}

void RelayTransport::UpdateSettings(const RenderModuleSettings& settings) {
    RenderModule::UpdateSettings(settings);
    if (const auto runtime = runtime_.load()) {
        runtime->UpdateSettings(settings);
    }
}

int64_t RelayTransport::QueuedMediaCount() const {
    const auto runtime = runtime_.load();
    return runtime ? runtime->QueuingMediaMessageCount() : 0;
}

int64_t RelayTransport::QueuedFileTransferCount() const {
    const auto runtime = runtime_.load();
    return runtime ? runtime->QueuingFileTransferMessageCount() : 0;
}

bool RelayTransport::HasMediaCapacity() const noexcept { return true; }
bool RelayTransport::HasFileTransferCapacity() const noexcept { return true; }

std::vector<std::shared_ptr<PxConnectedClientInfo>>
RelayTransport::ConnectedClients() const {
    const auto runtime = runtime_.load();
    return runtime ? runtime->ConnectedClientInfo()
                   : std::vector<std::shared_ptr<PxConnectedClientInfo>>{};
}

void RelayTransport::HandleMessageAck(const std::shared_ptr<NetMessageAck>& ack) {
    if (const auto runtime = runtime_.load()) {
        runtime->OnMessageAck(ack);
    }
}

} // namespace px
