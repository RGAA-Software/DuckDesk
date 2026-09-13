#include "client_launch_config.h"

#include <nlohmann/json.hpp>

namespace px::client::imgui {
namespace {

template <typename T> T Value(const nlohmann::json& object, const std::string_view name, T fallback = {}) {
    const auto found = object.find(name);
    return found == object.end() || found->is_null() ? std::move(fallback) : found->get<T>();
}

} // namespace

std::optional<ClientLaunchConfig> ParseClientLaunchEnvelope(const std::string_view envelope) {
    if (envelope.empty() || envelope.size() > 65'536U) {
        return std::nullopt;
    }
    try {
        const auto values = nlohmann::json::parse(envelope);
        if (!values.is_object() || Value<int>(values, "schema") != 1) {
            return std::nullopt;
        }
        ClientLaunchConfig result{.host = Value<std::string>(values, "host"),
                                  .port = Value<int>(values, "port"),
                                  .streamId = Value<std::string>(values, "stream_id"),
                                  .streamName = Value<std::string>(values, "stream_name"),
                                  .localDeviceId = Value<std::string>(values, "device_id"),
                                  .remoteDeviceId = Value<std::string>(values, "remote_device_id"),
                                  .remotePlatform = Value<std::string>(values, "remote_platform"),
                                  .remotePasswordHash = Value<std::string>(values, "remote_password_hash"),
                                  .nonce = Value<std::string>(values, "connection_nonce"),
                                  .instanceId = Value<std::string>(values, "connection_instance_id"),
                                  .appKey = Value<std::string>(values, "appkey"),
                                  .relayHost = Value<std::string>(values, "relay_host"),
                                  .relayPort = Value<int>(values, "relay_port"),
                                  .relayRemoteDeviceId = Value<std::string>(values, "relay_remote_device_id"),
                                  .audio = Value<bool>(values, "audio", true),
                                  .clipboard = Value<bool>(values, "clipboard", true),
                                  .viewOnly = Value<bool>(values, "only_viewing"),
                                  .forceTcp = Value<bool>(values, "force_tcp"),
                                  .forceRelay = Value<bool>(values, "force_relay"),
                                  .fileTransferOnly = Value<std::string>(values, "mode") == "file-transfer",
                                  .splitWindows = Value<bool>(values, "split_windows"),
                                  .forceGdiCapture = Value<bool>(values, "force_gdi_capture"),
                                  .disableVulkan = Value<bool>(values, "disable_vulkan_render"),
                                  .waitForDebugger = Value<bool>(values, "wait_debug"),
                                  .language = Value<std::string>(values, "language", "zh-CN"),
                                  .lightTheme = Value<std::string>(values, "theme", "dark") == "light",
                                  .enhancedVisualEffects = Value<bool>(values, "enhanced_visual_effects", true),
                                  .decoder = Value<std::string>(values, "decoder", "Auto"),
                                  .recordingPath = Value<std::string>(values, "recording_path")};
        if (const auto rdp = values.find("rdp"); rdp != values.end() && rdp->is_object()) {
            result.rdp = true;
            result.rdpAccount = Value<std::string>(*rdp, "account_name");
            result.rdpDomain = Value<std::string>(*rdp, "domain");
            result.rdpProxyCertificateSha256 = Value<std::string>(*rdp, "proxy_certificate_sha256");
            auto password = Value<std::string>(*rdp, "password");
            result.rdpPassword = px::SecretBuffer::Take(std::move(password));
        }
        if (result.host.empty() || result.port <= 0 || result.port > 65535 || result.streamId.empty() || result.localDeviceId.empty() ||
            result.remoteDeviceId.empty() || result.nonce.empty() || result.remotePasswordHash.empty()) {
            return std::nullopt;
        }
        if (result.rdp && (result.rdpAccount.empty() || result.rdpDomain.empty() || result.rdpProxyCertificateSha256.size() != 64U ||
                           !result.rdpPassword || result.rdpPassword->Bytes().empty())) {
            return std::nullopt;
        }
        return result;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

} // namespace px::client::imgui
