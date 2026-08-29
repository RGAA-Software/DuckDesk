#include "relay_plugin.h"

#include <cstdlib>

#include "px_render/plugins/net_relay/relay_plugin_runtime.h"
#include "px_render/plugins/plugin_ids.h"

PX_PLUGIN_EXPORT(px::RelayPlugin)

namespace px {

std::string RelayPlugin::GetPluginId() { return kRelayPluginId; }
std::string RelayPlugin::GetPluginName() { return "Net Relay"; }
std::string RelayPlugin::GetVersionName() { return "1.2.0"; }
uint32_t RelayPlugin::GetVersionCode() { return 120; }
std::string RelayPlugin::GetPluginDescription() {
    return "Network via relay server";
}

bool RelayPlugin::OnCreate(const PxPluginParam& param) {
    if (!PxNetPlugin::OnCreate(param)) {
        return false;
    }
    runtime_.store(RelayPluginRuntime::Create(RelayPluginRuntimeConfig{
        .relay_device_id = GetConfigParam<std::string>("relay_device_id"),
        .configured_host = GetConfigParam<std::string>("relay_host"),
        .configured_port = std::atoi(
            GetConfigParam<std::string>("relay_port").c_str()),
        .settings = sys_settings_,
    }));
    return true;
}

void RelayPlugin::On1Second() {
    PxPluginInterface::On1Second();
    const auto runtime = runtime_.load();
    if (!runtime) {
        return;
    }
    runtime->UpdateSettings(sys_settings_);
    runtime->Start(plugin_context_, event_cbk_);
}

bool RelayPlugin::OnDestroy() {
    PxNetPlugin::OnStop();
    const auto runtime = runtime_.exchange({});
    if (runtime) {
        runtime->Stop();
    }
    return PxNetPlugin::OnDestroy();
}

void RelayPlugin::PostProtoMessage(
    std::shared_ptr<Data> message, bool run_through) {
    if (const auto runtime = runtime_.load()) {
        runtime->PostMedia(std::move(message), run_through);
    }
}

bool RelayPlugin::PostTargetStreamProtoMessage(
    const std::string& stream_id, std::shared_ptr<Data> message,
    bool run_through) {
    const auto runtime = runtime_.load();
    return runtime && runtime->PostTargetMedia(
        stream_id, std::move(message), run_through);
}

FileTransferSendResult RelayPlugin::PostTargetFileTransferProtoMessage(
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

int RelayPlugin::GetConnectedClientsCount() {
    const auto runtime = runtime_.load();
    return runtime ? runtime->ConnectedClientsCount() : 0;
}

bool RelayPlugin::IsOnlyAudioClients() { return false; }

bool RelayPlugin::IsWorking() {
    const auto runtime = runtime_.load();
    return runtime && runtime->IsWorking();
}

void RelayPlugin::SyncInfo(const NetSyncInfo& info) {
    PxNetPlugin::SyncInfo(info);
}

void RelayPlugin::OnSyncPluginSettingsInfo(
    const PxPluginSettingsInfo& settings) {
    PxPluginInterface::OnSyncPluginSettingsInfo(settings);
    if (const auto runtime = runtime_.load()) {
        runtime->UpdateSettings(settings);
    }
}

int64_t RelayPlugin::GetQueuingMediaMsgCount() {
    const auto runtime = runtime_.load();
    return runtime ? runtime->QueuingMediaMessageCount() : 0;
}

int64_t RelayPlugin::GetQueuingFtMsgCount() {
    const auto runtime = runtime_.load();
    return runtime ? runtime->QueuingFileTransferMessageCount() : 0;
}

bool RelayPlugin::HasEnoughBufferForQueuingMediaMessages() { return true; }
bool RelayPlugin::HasEnoughBufferForQueuingFtMessages() { return true; }

std::vector<std::shared_ptr<PxConnectedClientInfo>>
RelayPlugin::GetConnectedClientInfo() {
    const auto runtime = runtime_.load();
    return runtime ? runtime->ConnectedClientInfo()
                   : std::vector<std::shared_ptr<PxConnectedClientInfo>>{};
}

void RelayPlugin::OnMessageAck(const std::shared_ptr<NetMessageAck>& ack) {
    if (const auto runtime = runtime_.load()) {
        runtime->OnMessageAck(ack);
    }
}

} // namespace px
