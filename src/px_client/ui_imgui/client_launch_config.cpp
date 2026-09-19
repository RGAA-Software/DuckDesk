#include "client_launch_config.h"

#include <algorithm>
#include <format>
#include <nlohmann/json.hpp>

#include "px_common/url_helper.h"

namespace px::client::imgui {
namespace {

template <typename T>
T Value(const nlohmann::json& object, const std::string_view name, T fallback = {}) {
    const auto found = object.find(name);
    return found == object.end() || found->is_null() ? std::move(fallback) : found->get<T>();
}

std::string AuthenticationQuery(const ClientLaunchConfig& config) {
    if (config.frontendToken) {
        return std::format("session_id={}&session_revision={}&frontend_token={}", px::UrlHelper::EncodeQueryComponent(config.frontendSessionId),
                           config.frontendSessionRevision, px::UrlHelper::EncodeQueryComponent(std::string(config.frontendToken->View())));
    }
    return "safety_pwd_md5=" + px::UrlHelper::EncodeQueryComponent(config.remotePasswordHash);
}

}  // namespace

std::optional<ClientLaunchConfig> ParseClientLaunchEnvelope(const std::string_view envelope) {
    if (envelope.empty() || envelope.size() > 65'536U) {
        return std::nullopt;
    }
    try {
        auto values = nlohmann::json::parse(envelope);
        if (!values.is_object() || Value<int>(values, "schema") != 1) {
            return std::nullopt;
        }
        ClientLaunchConfig result{.host = Value<std::string>(values, "host"),
                                  .localHost = Value<std::string>(values, "local_host"),
                                  .port = Value<int>(values, "port"),
                                  .streamId = Value<std::string>(values, "stream_id"),
                                  .streamName = Value<std::string>(values, "stream_name"),
                                  .localDeviceId = Value<std::string>(values, "device_id"),
                                  .remoteDeviceId = Value<std::string>(values, "remote_device_id"),
                                  .remotePlatform = Value<std::string>(values, "remote_platform"),
                                  .remotePasswordHash = Value<std::string>(values, "remote_password_hash"),
                                  .frontendSessionId = Value<std::string>(values, "frontend_session_id"),
                                  .frontendSessionRevision = Value<std::int64_t>(values, "frontend_session_revision"),
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
        auto frontendToken = Value<std::string>(values, "frontend_token");
        if (!frontendToken.empty()) {
            result.frontendToken = px::SecretBuffer::Take(std::move(frontendToken));
            if (const auto tokenEntry = values.find("frontend_token"); tokenEntry != values.end() && tokenEntry->is_string()) {
                auto& tokenValue = tokenEntry->get_ref<std::string&>();
                std::fill(tokenValue.begin(), tokenValue.end(), '\0');
            }
        }
        if (const auto rdp = values.find("rdp"); rdp != values.end() && rdp->is_object()) {
            result.rdp = true;
            result.rdpAccount = Value<std::string>(*rdp, "account_name");
            result.rdpDomain = Value<std::string>(*rdp, "domain");
            result.rdpProxyCertificateSha256 = Value<std::string>(*rdp, "proxy_certificate_sha256");
            auto password = Value<std::string>(*rdp, "password");
            result.rdpPassword = px::SecretBuffer::Take(std::move(password));
        }
        const bool hasConsoleFrontendFields = result.frontendToken || !result.frontendSessionId.empty() || result.frontendSessionRevision != 0;
        const bool consoleFrontend = result.frontendToken && !result.frontendToken->Bytes().empty();
        const bool validConsoleFrontend =
            consoleFrontend && !result.frontendSessionId.empty() && result.frontendSessionRevision > 0 && result.streamId == result.frontendSessionId;
        if (result.host.empty() || result.port <= 0 || result.port > 65535 || result.streamId.empty() || result.localDeviceId.empty() ||
            result.remoteDeviceId.empty() || result.nonce.empty() || (hasConsoleFrontendFields && !validConsoleFrontend) ||
            (!hasConsoleFrontendFields && result.remotePasswordHash.empty())) {
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

std::string BuildClientMediaPath(const ClientLaunchConfig& config) {
    return std::format("/media?only_audio=0&remote_device_id={}&stream_id={}&visitor_device_id={}&{}",
                       px::UrlHelper::EncodeQueryComponent(config.remoteDeviceId), px::UrlHelper::EncodeQueryComponent(config.streamId),
                       px::UrlHelper::EncodeQueryComponent(config.localDeviceId), AuthenticationQuery(config));
}

std::string BuildClientFileTransferPath(const ClientLaunchConfig& config) {
    return std::format("/file/transfer?remote_device_id={}&stream_id={}&visitor_device_id={}&{}",
                       px::UrlHelper::EncodeQueryComponent(config.remoteDeviceId), px::UrlHelper::EncodeQueryComponent(config.streamId),
                       px::UrlHelper::EncodeQueryComponent(config.localDeviceId), AuthenticationQuery(config));
}

}  // namespace px::client::imgui
