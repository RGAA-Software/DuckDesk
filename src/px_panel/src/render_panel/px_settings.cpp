//
// Created by RGAA on 2024/4/10.
//

#include "px_settings.h"
#include <sstream>
#include <QApplication>

#include "px_application.h"
#include "version_config.h"
#include "px_app_messages.h"
#include "companion/panel_companion.h"
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "px_common_new/md5.h"
#include "px_common_new/uuid.h"
#include "px_common_new/base64.h"
#include "px_common_new/const_auto.h"
#include "px_common_new/hardware.h"
#include "px_common_new/http_client.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/shared_preference.h"
#include "px_common_new/win32/dxgi_mon_detector.h"
#include "px_console_client/console_http_client.h"
#include "px_profile_client/profile_api.h"
namespace px
{

    void PxSettings::Init(const std::shared_ptr<MessageNotifier>& notifier) {
        notifier_ = notifier;
    }

    void PxSettings::Load() {
        sp_ = SharedPreference::Instance();
        version_ = std::format("V {}", PROJECT_VERSION);

        log_file_ = sp_->Get(kStLogFile, "true");
        encoder_select_type_ = sp_->Get(kStEncoderSelectType, "auto");
        encoder_name_ = sp_->Get(kStEncoderName, "nvenc");
        //encoder_format_ = sp_->Get(kStEncoderFormat, "h264");
        //encoder_bitrate_ = sp_->Get(kStEncoderBitrate, "10");
        //encoder_fps_ = sp_->Get(kStEncoderFPS, "60");
        //encoder_resolution_type_ = sp_->Get(kStEncoderResolutionType, "origin");
        //encoder_width_ = sp_->Get(kStEncoderWidth, "1280");
        //encoder_height_ = sp_->Get(kStEncoderHeight, "720");

        capture_audio_type_ = sp_->Get(kStCaptureAudioType, "global");
        capture_video_ = sp_->Get(kStCaptureVideo, "true");
        capture_video_type_ = sp_->Get(kStCaptureVideoType, "global");
        // Deprecated: capture always follows OS default playback device in the plugin.
        capture_audio_device_.clear();

        network_listening_ip_ = sp_->Get(kStListeningIp, "");
        webrtc_enabled_ = sp_->Get(kStWebRTCEnabled, kStTrue);
        udp_listen_port_ = sp_->GetInt(kStUdpListenPort, 20381);
        udp_kcp_enabled_ = sp_->Get(kStUdpKcpEnabled, kStTrue);

        file_transfer_folder_ = sp_->Get(kStFileTransferFolder, "");
        if (file_transfer_folder_.empty()) {
            file_transfer_folder_ = qApp->applicationDirPath().toStdString();
        }

        // Console is TLS-only. Normalize stale pre-migration settings before
        // any API or WebSocket client is created.
        SetConsoleSslEnabled(true);
    }

    void PxSettings::Dump() {
        std::stringstream ss;
        ss << "---------------------PxSettings Begin---------------------" << std::endl;
        ss << "log_file_: " << log_file_ << std::endl;
        ss << "encoder_select_type_: " << encoder_select_type_ << std::endl;
        ss << "encoder_name_: " << encoder_name_ << std::endl;
        ss << "encoder_format_: " << GetEncoderFormat() << std::endl;
        ss << "encoder_bitrate_: " << GetBitrate() << std::endl;
        ss << "res resize enabled ? : " << IsResResizeEnabled() << std::endl;
        ss << "encoder_width_: " << GetResWidth() << std::endl;
        ss << "encoder_height_: " << GetResHeight() << std::endl;
        ss << "capture_audio_: " << IsCaptureAudioEnabled() << std::endl;
        ss << "capture_audio_type_: " << capture_audio_type_ << std::endl;
        ss << "capture_video_: " << capture_video_ << std::endl;
        ss << "capture_video_type: " << capture_video_type_ << std::endl;
        ss << "panel_srv_port_: " << GetPanelServerPort() << std::endl;
        ss << "render_srv_port_: " << GetRenderServerPort() << std::endl;
        ss << "websocket_enabled_:" << IsWebSocketEnabled() << std::endl;
        ss << "webrtc_enabled_:" << webrtc_enabled_ << std::endl;
        ss << "capture_audio_device_: " << capture_audio_device_ << std::endl;
        ss << "device_id_: " << GetDeviceId() << std::endl;
        ss << "device_random_pwd: " << GetDeviceRandomPwd() << std::endl;
        ss << "device_security_pwd_md5: " << GetDeviceSecurityPwd() << std::endl;
        ss << "udp_listen_port_:" << udp_listen_port_ << std::endl;
        ss << "relay host: " << GetRelayServerHost() << std::endl;
        ss << "relay port: " << GetRelayServerPort() << std::endl;
        ss << "console server host: " << GetConsoleServerHost() << std::endl;
        ss << "console server port: " << GetConsoleServerPort() << std::endl;
        ss << "---------------------PxSettings End-----------------------" << std::endl;
        LOGI("\n {}", ss.str());
    }

    void PxSettings::ClearData() {
        this->SetDeviceId("");
        if (cat comp = grApp->GetCompanion(); comp) {
            comp->UpdateDeviceId("");
        }
        this->SetDeviceName("");
        this->SetDeviceRandomPwd("");
        this->SetDeviceSecurityPwd("");
        this->SetConsoleServerHost("");
        this->SetConsoleServerPort("");
        this->SetRelayServerHost("");
        this->SetRelayServerPort("");
        this->SetConsoleAccessInfo("");
        sp_->Put(kLegacyStCmsServerHost, "");
        sp_->Put(kLegacyStCmsServerPort, "");
        sp_->Put(kLegacyStCmsAccessInfo, "");
        sp_->Put(kLegacyStCmsSslEnable, "");
    }

    void PxSettings::SetEnableResResize(bool enabled) {
        sp_->Put(kStEncoderResResize, enabled ? kStTrue : kStFalse);
    }

    bool PxSettings::IsResResizeEnabled() {
        auto value = sp_->Get(kStEncoderResResize);
        return !value.empty() && value == kStTrue;
    }

    void PxSettings::SetBitrate(int br) {
        sp_->Put(kStEncoderBitrate, std::to_string(br));
    }

    int PxSettings::GetBitrate() {
        auto value = std::atoi(sp_->Get(kStEncoderBitrate).c_str());
        return value > 0 ? value : 10;
    }

    void PxSettings::SetFPS(int fps) {
        sp_->Put(kStEncoderFPS, std::to_string(fps));
    }

    int PxSettings::GetFPS() {
        auto value = std::atoi(sp_->Get(kStEncoderFPS).c_str());
        return value > 0 ? value : 60;
    }

    void PxSettings::SetResWidth(int width) {
        sp_->Put(kStEncoderWidth, std::to_string(width));
    }

    int PxSettings::GetResWidth() {
        auto value = std::atoi(sp_->Get(kStEncoderWidth).c_str());
        return value > 0 ? value : 1280;
    }

    void PxSettings::SetResHeight(int height) {
        sp_->Put(kStEncoderHeight, std::to_string(height));
    }

    int PxSettings::GetResHeight() {
        auto value = std::atoi(sp_->Get(kStEncoderHeight).c_str());
        return value > 0 ? value : 720;
    }

    void PxSettings::SetEncoderFormat(int idx) {
        // h264
        if (idx == 0) {
            sp_->Put(kStEncoderFormat, "h264");
        } else if (idx == 1) {
            sp_->Put(kStEncoderFormat, "h265");
        }
    }

    std::string PxSettings::GetEncoderFormat() {
        auto value = sp_->Get(kStEncoderFormat);
        return value.empty() ? "h264" : value;
    }

    void PxSettings::SetCaptureVideo(bool enabled) {
        capture_video_ = enabled ? kStTrue : kStFalse;
        sp_->Put(kStCaptureVideo, capture_video_);
    }

    void PxSettings::SetCaptureAudio(bool enabled) {
        sp_->Put(kStCaptureAudio, enabled ? kStTrue : kStFalse);
    }

    bool PxSettings::IsCaptureAudioEnabled() {
        auto value = sp_->Get(kStCaptureAudio);
        return value.empty() || value == kStTrue;
    }

    void PxSettings::SetCaptureAudioDeviceId(const std::string& name) {
        capture_audio_device_ = name;
        sp_->Put(kStCaptureAudioDevice, capture_audio_device_);
    }

    void PxSettings::SetFileTransferFolder(const std::string& path) {
        file_transfer_folder_ = path;
        sp_->Put(kStFileTransferFolder, path);
    }

    void PxSettings::SetListeningIp(const std::string& ip) {
        network_listening_ip_ = ip;
        sp_->Put(kStListeningIp, ip);
    }

    void PxSettings::SetWebSocketEnabled(bool enabled) {
        sp_->Put(kStWebSocketEnabled, enabled ? kStTrue : kStFalse);
    }

    bool PxSettings::IsWebSocketEnabled() {
        auto value = sp_->Get(kStWebSocketEnabled);
        return value.empty() || value == kStTrue;
    }

    void PxSettings::SetWebRTCEnabled(bool enabled) {
        webrtc_enabled_ = enabled ? kStTrue : kStFalse;
        sp_->Put(kStWebRTCEnabled, webrtc_enabled_);
    }

    void PxSettings::SetUdpKcpEnabled(bool enabled) {
        udp_kcp_enabled_ = enabled ? kStTrue : kStFalse;
        sp_->Put(kStUdpKcpEnabled, udp_kcp_enabled_);
    }

    // Device ID // Set
    void PxSettings::SetDeviceId(const std::string& id) {
        sp_->Put(kStDeviceId, id);
    }

    // Device ID // Get
    std::string PxSettings::GetDeviceId() {
        return sp_->Get(kStDeviceId, "");
    }

    // Device Name // Set
    void PxSettings::SetDeviceName(const std::string& name) {
        sp_->Put(kStDeviceName, name);
    }

    // Device Name // Get
    std::string PxSettings::GetDeviceName() {
        return sp_->Get(kStDeviceName, "");
    }

    // Device Random Pwd // Set
    void PxSettings::SetDeviceRandomPwd(const std::string& pwd) {
        sp_->Put(kStDeviceRandomPwd, pwd);
    }

    // Device Random Pwd // Get
    std::string PxSettings::GetDeviceRandomPwd() {
        return sp_->Get(kStDeviceRandomPwd, "");
    }

    // Device Security Pwd // Set
    void PxSettings::SetDeviceSecurityPwd(const std::string& pwd) {
        sp_->Put(kStDeviceSafetyPwd, pwd);
    }

    // Device Security Pwd // Set
    std::string PxSettings::GetDeviceSecurityPwd() {
        return sp_->Get(kStDeviceSafetyPwd, "");
    }

    // Panel Server Port // Set
    void PxSettings::SetPanelServerPort(int port) {
        sp_->Put(kStPanelListeningPort, std::to_string(port));
    }

    // Panel Server Port // Get
    int PxSettings::GetPanelServerPort() {
        auto value = std::atoi(sp_->Get(kStPanelListeningPort, "").c_str());
        return value > 0 ? value : 20369;
    }

    // Panel Server Host // Set
    void PxSettings::SetPanelServerHost(const std::string& host) {
        sp_->Put(kStPanelServerHost, host);
    }

    // Panel Server Host // Get
    std::string PxSettings::GetPanelServerHost() {
        return sp_->Get(kStPanelServerHost, "127.0.0.1");
    }

    // Service Server Host // Set
    void PxSettings::SetServiceServerHost(const std::string& host) {
        sp_->Put(kStServiceServerHost, host);
    }

    // Service Server Host // Get
    std::string PxSettings::GetServiceServerHost() {
        return sp_->Get(kStServiceServerHost, "127.0.0.1");
    }

    // Service Server Port // Set
    void PxSettings::SetServiceServerPort(int port) {
        sp_->Put(kStServiceServerPort, std::to_string(port));
    }

    // Service Server Port // Get
    int PxSettings::GetServiceServerPort() {
        auto value = std::atoi(sp_->Get(kStServiceServerPort, "").c_str());
        return value > 0 ? value : sys_service_port_;
    }

    // Render Server Port // Set
    void PxSettings::SetRenderServerPort(int port) {
        sp_->Put(kStNetworkListenPort, std::to_string(port));
    }

    // Render Server Port // Get
    int PxSettings::GetRenderServerPort() {
        auto value = std::atoi(sp_->Get(kStNetworkListenPort, "").c_str());
        return value > 0 ? value : 20371;
    }

    // Relay
    // Host
    void PxSettings::SetRelayServerHost(const std::string& host) {
        sp_->Put(kStRelayServerHost, host);
    }

    std::string PxSettings::GetRelayServerHost() {
        return sp_->Get(kStRelayServerHost, "");
    }

    // Relay
    // Port
    void PxSettings::SetRelayServerPort(const std::string& port) {
        sp_->Put(kStRelayServerPort, port);
    }

    int PxSettings::GetRelayServerPort() {
        return std::atoi(sp_->Get(kStRelayServerPort, "").c_str());
    }

    // Relay
    bool PxSettings::HasRelayServerConfig() {
        return !GetRelayServerHost().empty() && GetRelayServerPort() > 0;
    }

    // Console
    // Set Host
    void PxSettings::SetConsoleServerHost(const std::string& host) {
        sp_->Put(kStConsoleServerHost, host);
    }

    // Console
    // Get Host
    std::string PxSettings::GetConsoleServerHost() {
        auto value = sp_->Get(kStConsoleServerHost, "");
        if (value.empty()) {
            value = sp_->Get(kLegacyStCmsServerHost, "");
            if (!value.empty()) {
                sp_->Put(kStConsoleServerHost, value);
            }
        }
        return value;
    }

    // Console
    // Set Port
    void PxSettings::SetConsoleServerPort(const std::string& port) {
        sp_->Put(kStConsoleServerPort, port);
    }

    // Console
    // Get Port
    int PxSettings::GetConsoleServerPort() {
        auto value = sp_->Get(kStConsoleServerPort, "");
        if (value.empty()) {
            value = sp_->Get(kLegacyStCmsServerPort, "");
            if (!value.empty()) {
                sp_->Put(kStConsoleServerPort, value);
            }
        }
        return std::atoi(value.c_str());
    }

    bool PxSettings::HasConsoleServerConfig() {
        return !GetConsoleServerHost().empty() && GetConsoleServerPort() > 0;
    }

    void PxSettings::SetScreenRecordingPath(const std::string& path) {
        sp_->Put(kStScreenRecordingPath, path);
    }

    std::string PxSettings::GetScreenRecordingPath() const {
        return sp_->Get(kStScreenRecordingPath, "");
    }

    // show max window
    void PxSettings::SetShowingMaxWindow(bool enable) {
        sp_->Put(kStShowMaxWindow, enable ? kStTrue : kStFalse);
    }

    bool PxSettings::IsMaxWindowEnabled() {
        return sp_->Get(kStShowMaxWindow) == kStTrue;
    }

    void PxSettings::SetMaxNumOfScreen(const std::string& num) {
        sp_->Put(kStMaxNumOfScreen, num);
    }

    std::string PxSettings::GetMaxNumOfScreen() {
        auto value = sp_->Get(kStMaxNumOfScreen);
        return value.empty() ? "4" : value;
    }

    void PxSettings::SetDisplayClientLogo(int enable) {
        sp_->Put(kStDisplayClientLogo, enable ? kStTrue : kStFalse);
    }

    bool PxSettings::IsClientLogoDisplaying() {
        auto value = sp_->Get(kStDisplayClientLogo);
        return !value.empty() && value == kStTrue;
    }

    // can be operated
    // Settings->Security Settings
    void PxSettings::SetCanBeOperated(bool enable) {
        sp_->Put(kStCanBeOperated, enable ? kStTrue : kStFalse);
    }

    bool PxSettings::IsBeingOperatedEnabled() {
        auto value = sp_->Get(kStCanBeOperated);
        return value.empty() || value == kStTrue;
    }

    // use ssl connection
    // Settings->Security Settings
    void PxSettings::SetUsingSSLConnection(bool enable) {
        sp_->Put(kStSSLConnection, enable ? kStTrue : kStFalse);
    }

    bool PxSettings::IsSSLConnectionEnabled() {
        auto value = sp_->Get(kStSSLConnection);
        return value.empty() || value == kStTrue;
    }

    // record visit history
    // Settings->Security Settings
    void PxSettings::SetRecordingVisitHistory(bool enable) {
        sp_->Put(kStRecordVisitHistory, enable ? kStTrue : kStFalse);
    }

    bool PxSettings::IsVisitHistoryEnabled() {
        auto value = sp_->Get(kStRecordVisitHistory);
        return value.empty() || value == kStTrue;
    }

    // record file transfer history
    // Settings->Security Settings
    void PxSettings::SetRecordingFileTransferHistory(bool enable) {
        sp_->Put(kStRecordFileTransferHistory, enable ? kStTrue : kStFalse);
    }

    bool PxSettings::IsFileTransferHistoryEnabled() {
        auto value = sp_->Get(kStRecordFileTransferHistory);
        return value.empty() || value == kStTrue;
    }

    void PxSettings::SetDisconnectAutoLockScreen(bool enable) {
        sp_->Put(kStDisconnectAutoLockScreen, enable ? kStTrue : kStFalse);
    }

    bool PxSettings::IsDisconnectAutoLockScreenEnabled() {
        auto value = sp_->Get(kStDisconnectAutoLockScreen);
        return !value.empty() && value == kStTrue;
    }

    void PxSettings::SetRelayEnabled(bool enabled) {
        sp_->Put(kStRelayEnabled, enabled ? kStTrue : kStFalse);
    }

    bool PxSettings::IsRelayEnabled() {
        auto value = sp_->Get(kStRelayEnabled);
        return value.empty() || value == kStTrue;
    }

    void PxSettings::SetDevelopModeEnabled(bool enable) {
        sp_->Put(kStDevelopMode, enable ? kStTrue : kStFalse);
    }

    bool PxSettings::IsDevelopMode() {
        auto value = sp_->Get(kStDevelopMode);
        return !value.empty() && value == kStTrue;
    }

    void PxSettings::SetFileTransferEnabled(bool enable) {
        sp_->Put(kStFileTransferEnabled, enable ? kStTrue : kStFalse);
    }

    bool PxSettings::IsFileTransferEnabled() {
        auto value = sp_->Get(kStFileTransferEnabled);
        return value.empty() || value == kStTrue;
    }

    void PxSettings::SetColorfulTitleBar(bool enable) {
        sp_->Put(kStColorfulTitlebar, enable ? kStTrue : kStFalse);
    }

    bool PxSettings::IsColorfulTitleBarEnabled() {
        auto value = sp_->Get(kStColorfulTitlebar);
        return value.empty() || value == kStTrue;
    }

    void PxSettings::SetDisplayRandomPwd(bool enable) {
        sp_->Put(kStDisplayRandomPwd, enable ? kStTrue : kStFalse);
    }

    bool PxSettings::IsDisplayRandomPwd() {
        auto value = sp_->Get(kStDisplayRandomPwd);
        return value.empty() || value == kStTrue;
    }

    void PxSettings::SetPreferDecoder(const std::string& decoder) {
        sp_->Put(kStPreferDecoder, decoder);
    }

    std::string PxSettings::GetPreferDecoder() {
        return sp_->Get(kStPreferDecoder, "Auto");
    }

    void PxSettings::SetConsoleAccessInfo(const std::string& info) {
        sp_->Put(kStConsoleAccessInfo, info);
    }

    std::string PxSettings::GetConsoleAccessInfo() {
        auto value = sp_->Get(kStConsoleAccessInfo, "");
        if (value.empty()) {
            value = sp_->Get(kLegacyStCmsAccessInfo, "");
            if (!value.empty()) {
                sp_->Put(kStConsoleAccessInfo, value);
            }
        }
        return value;
    }

    void PxSettings::SetConsoleSslEnabled(bool enabled) {
        if (!enabled) {
            LOGW("Ignoring legacy request to disable Console TLS");
        }
        sp_->Put(kStConsoleSslEnable, kStTrue);
        // Native clients intentionally accept the deployment's self-signed
        // certificate, but the transport to Console is always HTTPS/WSS.
        px_console::SetConsoleSslEnabled(true);
        ProfileApi::SetSslEnabled(true);
    }

    bool PxSettings::IsConsoleSslEnabled() {
        if (sp_->Get(kStConsoleSslEnable) != kStTrue) {
            sp_->Put(kStConsoleSslEnable, kStTrue);
        }
        return true;
    }

    std::shared_ptr<HttpClient> PxSettings::MakeConsoleHttpClient(const std::string& host, int port, const std::string& path, int timeout_ms) {
        return HttpClient::MakeSSL(host, port, path, timeout_ms);
    }

    std::string PxSettings::GetConsoleHttpScheme() {
        return "https";
    }

    void PxSettings::SetSkinName(const std::string& name) {
        sp_->Put(kStSkinName, name);
    }

    std::string PxSettings::GetSkinName() {
        return sp_->Get(kStSkinName, "");
    }

    std::string PxSettings::GetGrDataPath() {
        return px_data_path_;
    }

    std::string PxSettings::GetGrDataCachePath() {
        return px_data_path_ + "/cache";
    }

}
