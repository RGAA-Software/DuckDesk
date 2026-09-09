// Stateless adapter for the fixed FreeRDP 3.31.0 proxy ABI. Upstream owns every
// callback argument and copies proxyPlugin during RegisterPlugin. No context,
// borrowed pointer, per-session userdata or mutable global is retained here.
#include <array>
#include <span>
#include <string>
#include <string_view>

#include <freerdp/server/proxy/proxy_context.h>
#include <freerdp/server/proxy/proxy_modules_api.h>
#include <freerdp/settings.h>

#include "../rdp_proxy_policy.h"
#include "px_common/win32/unique_win_handle.h"

namespace {

// Bounded, private diagnostics: only project-defined event names, never peer
// identities, credentials, configuration bodies or upstream packet dumps.
void Audit(std::string_view event) noexcept {
    std::array<wchar_t, 32768> path{};
    const auto length = GetEnvironmentVariableW(L"GAMMARAY_RDP_AUDIT_PATH", path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return;
    }
    const auto file = px::UniqueWinHandle{CreateFileW(path.data(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!file || file.get() == INVALID_HANDLE_VALUE) {
        return;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.get(), &info) || info.nNumberOfLinks != 1 || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        info.nFileSizeHigh || info.nFileSizeLow > 65536 - event.size()) {
        return;
    }
    LARGE_INTEGER end{};
    if (!SetFilePointerEx(file.get(), end, nullptr, FILE_END)) {
        return;
    }
    DWORD written{};
    static_cast<void>(WriteFile(file.get(), event.data(), static_cast<DWORD>(event.size()), &written, nullptr));
}

bool ValidConfiguration(const proxyData& data) {
    if (!data.config) {
        return false;
    }
    const auto& config = *data.config;
    return config.FixedTarget && config.Host && std::string_view(config.Host) == "127.0.0.1" && config.TargetHost &&
           std::string_view(config.TargetHost) == "127.0.0.1" && config.TargetPort == 3389 && config.TargetUser && config.TargetDomain &&
           config.ServerNlaSecurity && config.ServerTlsSecurity && !config.ServerRdpSecurity && config.ClientNlaSecurity &&
           config.ClientTlsSecurity && !config.ClientRdpSecurity && !config.ClientAllowFallbackToTls && config.DeviceRedirection &&
           !config.AudioInput && !config.CameraRedirection && !config.VideoRedirection && !config.RemoteApp;
}

// NOLINTBEGIN(gammaray-raw-pointer-boundary): transient upstream callback ABI only; immediately access references/spans, never retain.
BOOL PeerLogon(proxyPlugin*, proxyData* data, void* event) { // NOLINT(gammaray-raw-pointer-boundary): borrowed FreeRDP ABI.
    try {
        if (!data || !event || !ValidConfiguration(*data)) {
            Audit("peer.reject.configuration\n");
            return FALSE;
        }
        const auto& logon = *static_cast<const proxyServerPeerLogon*>(event);
        if (!logon.automatic || !data->ps || !data->ps->settings) {
            Audit("peer.reject.nonautomatic\n");
            return FALSE;
        }
        // This pinned core decodes authenticated CredSSP TSPasswordCreds into
        // the front settings, not the legacy peer.identity (which stays empty).
        // Read them before pf_server_post_connect substitutes the fixed target.
        const auto& settings = *data->ps->settings;
        if (!freerdp_settings_get_string(&settings, FreeRDP_Username) || !freerdp_settings_get_string(&settings, FreeRDP_Domain) ||
            !freerdp_settings_get_string(&settings, FreeRDP_Password) || !data->config->TargetPassword) {
            Audit("peer.reject.identity_shape\n");
            return FALSE;
        }
        const auto user = std::string_view{freerdp_settings_get_string(&settings, FreeRDP_Username)};
        const auto domain = std::string_view{freerdp_settings_get_string(&settings, FreeRDP_Domain)};
        const auto password = std::string_view{freerdp_settings_get_string(&settings, FreeRDP_Password)};
        const auto expected = std::string_view{data->config->TargetPassword};
        if (password.size() != expected.size() || password.size() < 32 || password.size() > 256) {
            return FALSE;
        }
        unsigned int difference{};
        for (std::size_t index{}; index < password.size(); ++index) {
            difference |= static_cast<unsigned char>(password[index]) ^ static_cast<unsigned char>(expected[index]);
        }
        const bool accepted = difference == 0 && px::rdp::IsWorkspacePeer(data->config->TargetUser, data->config->TargetDomain, user, domain);
        Audit(accepted ? "peer.accept\n" : "peer.reject.identity_mismatch\n");
        return accepted ? TRUE : FALSE;
    } catch (...) {
        return FALSE;
    }
}

int VerifyBackendCertificate(freerdp* instance, const BYTE* bytes, size_t length, // NOLINT(gammaray-raw-pointer-boundary): borrowed FreeRDP ABI.
                             const char* hostname, UINT16 port, DWORD flags) {    // NOLINT(gammaray-raw-pointer-boundary): borrowed FreeRDP ABI.
    try {
        if (!instance || !bytes || !hostname || std::string_view{hostname} != "127.0.0.1" || port != 3389 || flags != 0 || length == 0 ||
            length > 64 * 1024) {
            return FALSE;
        }
        std::array<char, 65> pin{};
        if (GetEnvironmentVariableA("GAMMARAY_RDP_TARGET_CERT_SHA256", pin.data(), static_cast<DWORD>(pin.size())) != 64) {
            return FALSE;
        }
        const bool accepted = px::rdp::VerifyPinnedCertificate({bytes, length}, {pin.data(), 64});
        Audit(accepted ? "backend.certificate.accept\n" : "backend.certificate.reject\n");
        return accepted ? TRUE : FALSE;
    } catch (...) {
        return FALSE;
    }
}

BOOL PrepareBackend(proxyPlugin*, proxyData* data, void*) { // NOLINT(gammaray-raw-pointer-boundary): borrowed FreeRDP ABI.
    try {
        if (!data || !ValidConfiguration(*data) || !data->pc || !data->pc->instance) {
            Audit("target.reject\n");
            return FALSE;
        }
        // Install the public certificate callback before transport negotiation.
        // No private proxy context layout or session pointer is retained.
        data->pc->instance->VerifyX509Certificate = VerifyBackendCertificate;
        Audit("target.accept\n");
        return TRUE;
    } catch (...) {
        return FALSE;
    }
}

BOOL CheckTarget(proxyPlugin*, proxyData* data, void*) { // NOLINT(gammaray-raw-pointer-boundary): borrowed FreeRDP ABI.
    try {
        const bool accepted = data && ValidConfiguration(*data);
        Audit(accepted ? "target.accept\n" : "target.reject\n");
        return accepted ? TRUE : FALSE;
    } catch (...) {
        return FALSE;
    }
}

BOOL RefuseRedirect(proxyPlugin*, proxyData*, void*) {
    return FALSE;
}

BOOL StaticChannel(proxyPlugin*, proxyData*, void* event) { // NOLINT(gammaray-raw-pointer-boundary): borrowed FreeRDP ABI.
    if (!event) {
        return FALSE;
    }
    const auto& channel = *static_cast<const proxyChannelDataEventInfo*>(event);
    return channel.channel_name && px::rdp::IsAllowedStaticChannel(channel.channel_name) ? TRUE : FALSE;
}

BOOL DynamicChannel(proxyPlugin*, proxyData*, void* event) { // NOLINT(gammaray-raw-pointer-boundary): borrowed FreeRDP ABI.
    if (!event) {
        return FALSE;
    }
    const auto& channel = *static_cast<const proxyChannelDataEventInfo*>(event);
    return channel.channel_name && px::rdp::IsAllowedDynamicChannel(channel.channel_name) ? TRUE : FALSE;
}

BOOL ChannelData(const proxyChannelDataEventInfo& channel, px::rdp::DeviceChannelDirection direction) {
    if (!channel.channel_name || !px::rdp::IsAllowedStaticChannel(channel.channel_name)) {
        return FALSE;
    }
    if (std::string_view{channel.channel_name} != "rdpdr") {
        return TRUE;
    }
    const bool accepted =
        channel.data && px::rdp::IsAudioDeviceHandshake(direction, {channel.data, channel.data_len}, channel.flags, channel.total_size);
    Audit(accepted ? "device.handshake.accept\n" : "device.payload.reject\n");
    return accepted ? TRUE : FALSE;
}

BOOL BackendData(proxyPlugin*, proxyData*, void* event) { // NOLINT(gammaray-raw-pointer-boundary): borrowed FreeRDP ABI.
    return event ? ChannelData(*static_cast<const proxyChannelDataEventInfo*>(event), px::rdp::DeviceChannelDirection::kServerToClient) : FALSE;
}

BOOL FrontendData(proxyPlugin*, proxyData*, void* event) { // NOLINT(gammaray-raw-pointer-boundary): borrowed FreeRDP ABI.
    return event ? ChannelData(*static_cast<const proxyChannelDataEventInfo*>(event), px::rdp::DeviceChannelDirection::kClientToServer) : FALSE;
}

} // namespace

extern "C" __declspec(dllexport) BOOL
proxy_module_entry_point(proxyPluginsManager* manager, void*) { // NOLINT(gammaray-raw-pointer-boundary): synchronous FreeRDP registration ABI.
    if (!manager || !manager->RegisterPlugin) {
        return FALSE;
    }
    proxyPlugin plugin{};
    plugin.name = "gammaray-policy";
    plugin.description = "GammaRay fixed-workspace admission, pinned RDS certificate and channel policy";
    plugin.ServerPeerLogon = PeerLogon;
    plugin.ClientPreConnect = PrepareBackend;
    plugin.ServerFetchTargetAddr = CheckTarget;
    plugin.ClientRedirect = RefuseRedirect;
    plugin.ChannelCreate = StaticChannel;
    // FreeRDP names these for the proxy's client/server roles: Client receives
    // from RDS, Server receives from the end-user Client.
    plugin.ClientChannelData = BackendData;
    plugin.ServerChannelData = FrontendData;
    plugin.DynamicChannelCreate = DynamicChannel;
    // RegisterPlugin copies this ABI value. No custom ownership or unload hook is necessary.
    return manager->RegisterPlugin(manager, &plugin);
}
// NOLINTEND(gammaray-raw-pointer-boundary)
