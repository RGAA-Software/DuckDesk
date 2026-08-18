//
// Created by RGAA on 2024/4/10.
//

#ifndef TC_SERVER_STEAM_GR_SETTINGS_H
#define TC_SERVER_STEAM_GR_SETTINGS_H

#include <vector>
#include <string>
#include <memory>
#include "px_exe_names.h"

namespace px
{

    static const std::string kStLogFile = "logfile";
    static const std::string kStEncoderSelectType = "encoder_select_type";
    static const std::string kStEncoderName = "encoder_name";
    static const std::string kStEncoderFormat = "encoder_format";
    static const std::string kStEncoderBitrate = "encoder_bitrate";
    static const std::string kStEncoderFPS = "encoder_fps";
    static const std::string kStEncoderResResize = "encoder_res_resize";
    static const std::string kStEncoderWidth = "encoder_width";
    static const std::string kStEncoderHeight = "encoder_height";
    static const std::string kStCaptureAudio = "capture_audio";
    static const std::string kStCaptureAudioType = "capture_audio_type";
    static const std::string kStCaptureVideo = "capture_video";
    static const std::string kStCaptureVideoType = "capture_video_type";
    static const std::string kStWebSocketEnabled = "websocket_enabled";
    static const std::string kStWebRTCEnabled = "webrtc_enabled";
    static const std::string kStUdpKcpEnabled = "udp_kcp_enabled";
    static const std::string kStNetworkListenPort = "network_listen_port";
    static const std::string kStUdpListenPort = "udp_listen_port";
    static const std::string kStAppGamePath = "app_game_path";
    static const std::string kStAppGameArgs = "app_game_args";
    static const std::string kStDebugBlock = "debug_block";
    static const std::string kStMockVideo = "mock_video";
    static const std::string kStPanelListeningPort = "panel_listen_port";
    static const std::string kStPanelServerHost = "panel_server_host";
    static const std::string kStServiceServerHost = "service_server_host";
    static const std::string kStServiceServerPort = "service_server_port";
    static const std::string kStCaptureAudioDevice = "capture_audio_device";
    static const std::string kStFileTransferFolder = "file_transfer_folder";
    static const std::string kStListeningIp = "listening_ip";
    static const std::string kStSigServerAddress = "sig_server_address";
    static const std::string kStSigServerPort = "sig_server_port";
    static const std::string kStCoturnAddress = "coturn_server_address";
    static const std::string kStCoturnPort = "coturn_server_port";
    static const std::string kStDeviceId = "device_id";
    static const std::string kStDeviceName = "device_name";
    static const std::string kStDeviceRandomPwd = "device_random_pwd";
    static const std::string kStDeviceSafetyPwd = "device_safety_pwd";
    static const std::string kStRelayServerHost = "relay_server_host";
    static const std::string kStRelayServerPort = "relay_server_port";
    static const std::string kStCmsServerHost = "cms_server_host";
    static const std::string kStCmsServerPort = "cms_server_port";
    static const std::string kStScreenRecordingPath = "screen_recording_path";
    static const std::string kStShowMaxWindow = "show_max_window";
    static const std::string kStMaxNumOfScreen = "max_num_of_screen";
    static const std::string kStCanBeOperated = "can_be_operated";
    static const std::string kStSSLConnection = "ssl_connection";
    static const std::string kStRecordVisitHistory = "record_visit_history";
    static const std::string kStRecordFileTransferHistory = "record_file_transfer_history";
    static const std::string kStDisconnectAutoLockScreen = "disconnect_auto_lock_screen";
    static const std::string kStRelayEnabled = "relay_enabled";
    static const std::string kStDevelopMode = "develop_mode";
    static const std::string kStDisplayClientLogo = "display_client_logo";
    static const std::string kStFileTransferEnabled = "file_transfer_enabled";
    static const std::string kStColorfulTitlebar = "colorful_titlebar";
    static const std::string kStDisplayRandomPwd = "display_random_pwd";
    static const std::string kStPreferDecoder = "prefer_decoder";
    static const std::string kStCmsAccessInfo = "cms_access_info";
    static const std::string kStCmsSslEnable = "cms_ssl_enable";
    static const std::string kStSkinName = "skin_name";

    static const std::string kStTrue = "true";
    static const std::string kStFalse = "false";
    static const std::string kResTypeOrigin = "origin";
    static const std::string kResTypeResize = "resize";

    static const std::string kPxPanelName = px::kPxPanelExeName;
    static const std::string kPxRenderName = px::kPxRenderExeName;
    static const std::string kPxClientName = px::kPxClientExeName;
    static const std::string kPxServiceName = px::kPxServiceExeName;
    static const std::string kPxOsInfoName = px::kPxOsInfoExeName;

    class SharedPreference;
    class MessageNotifier;
    class HttpClient;

    class PxSettings {
    public:

        static PxSettings* Instance() {
            static PxSettings st;
            return &st;
        }

        void Init(const std::shared_ptr<MessageNotifier>& notifier);
        void Load();
        void Dump();
        void ClearData();

        // Settings -> General
        void SetBitrate(int br);
        int GetBitrate();

        // Settings -> General
        void SetFPS(int fps);
        int GetFPS();

        // Settings -> General
        void SetEnableResResize(bool enabled);
        bool IsResResizeEnabled();

        void SetResWidth(int width);
        int GetResWidth();

        void SetResHeight(int height);
        int GetResHeight();

        // Settings -> General
        void SetEncoderFormat(int idx);
        std::string GetEncoderFormat();

        void SetCaptureVideo(bool enabled);
        void SetCaptureAudio(bool enabled);
        bool IsCaptureAudioEnabled();
        void SetCaptureAudioDeviceId(const std::string& name);
        void SetFileTransferFolder(const std::string& path);
        void SetListeningIp(const std::string& ip);

        // Settings -> Network Settings
        void SetWebSocketEnabled(bool enabled);
        // default is enabled
        bool IsWebSocketEnabled();

        void SetWebRTCEnabled(bool enabled);
        void SetUdpKcpEnabled(bool enabled);

        // Device Id
        void SetDeviceId(const std::string& id);
        std::string GetDeviceId();

        void SetDeviceName(const std::string& name);
        std::string GetDeviceName();

        // Random PWD
        void SetDeviceRandomPwd(const std::string& pwd);
        std::string GetDeviceRandomPwd();

        // Security PWD
        void SetDeviceSecurityPwd(const std::string& pwd);
        std::string GetDeviceSecurityPwd();

        // Panel Server Port
        void SetPanelServerPort(int port);
        int GetPanelServerPort();

        // Panel Server Host
        void SetPanelServerHost(const std::string& host);
        std::string GetPanelServerHost();

        // Service Server Host
        void SetServiceServerHost(const std::string& host);
        std::string GetServiceServerHost();

        // Service Server Port
        void SetServiceServerPort(int port);
        int GetServiceServerPort();

        // Render Server Port
        void SetRenderServerPort(int port);
        int GetRenderServerPort();

        // Cms
        // Host
        void SetCmsServerHost(const std::string& host);
        std::string GetCmsServerHost();

        // Port
        void SetCmsServerPort(const std::string& port);
        int GetCmsServerPort();

        bool HasCmsServerConfig();

        // Relay
        // Host
        void SetRelayServerHost(const std::string& host);
        std::string GetRelayServerHost();

        // Port
        void SetRelayServerPort(const std::string& port);
        int GetRelayServerPort();

        bool HasRelayServerConfig();

        // Settings -> Controller
        void SetScreenRecordingPath(const std::string& path);
        [[nodiscard]] std::string GetScreenRecordingPath() const;

        // show max window
        // Settings-> Controller Settings
        void SetShowingMaxWindow(bool enable);
        bool IsMaxWindowEnabled();

        // allow max num of screen  允许客户端显示的最大屏幕数
        // Settings-> Controller Settings
        void SetMaxNumOfScreen(const std::string& num);
        std::string GetMaxNumOfScreen();

        // kStDisplayClientLogo
        // Settings -> Controller Settings
        void SetDisplayClientLogo(int enable);
        bool IsClientLogoDisplaying();

        // can be operated
        // Settings->Security Settings
        void SetCanBeOperated(bool enable);
        bool IsBeingOperatedEnabled();

        // use ssl connection
        // Settings->Security Settings
        void SetUsingSSLConnection(bool enable);
        bool IsSSLConnectionEnabled();

        // record visit history
        // Settings->Security Settings
        void SetRecordingVisitHistory(bool enable);
        bool IsVisitHistoryEnabled();

        // record file transfer history
        // Settings->Security Settings
        void SetRecordingFileTransferHistory(bool enable);
        bool IsFileTransferHistoryEnabled();

        // disconnect auto lock screen
        // Settings->Security Settings
        void SetDisconnectAutoLockScreen(bool enable);
        bool IsDisconnectAutoLockScreenEnabled();

        // relay enabled
        // Settings -> Network Settings -> Supervisor Server
        void SetRelayEnabled(bool enabled);
        bool IsRelayEnabled();

        // develop mode
        // Settings -> Security Settings
        void SetDevelopModeEnabled(bool enable);
        bool IsDevelopMode();

        // file transfer enabled
        // Settings -> Security Settings
        void SetFileTransferEnabled(bool enable);
        bool IsFileTransferEnabled();

        void SetColorfulTitleBar(bool enable);
        bool IsColorfulTitleBarEnabled();

        void SetDisplayRandomPwd(bool enable);
        bool IsDisplayRandomPwd();

        // prefer decoder
        void SetPreferDecoder(const std::string& decoder);
        std::string GetPreferDecoder();

        // Cms access info
        void SetCmsAccessInfo(const std::string& info);
        std::string GetCmsAccessInfo();

        // Cms ssl enabled (from cms access broadcast, default true)
        void SetCmsSslEnabled(bool enabled);
        bool IsCmsSslEnabled();

        // make a http(s) client to CMS, scheme selected by IsCmsSslEnabled()
        static std::shared_ptr<HttpClient> MakeCmsHttpClient(const std::string& host, int port, const std::string& path, int timeout_ms = 2000);
        // "https" / "http" selected by IsCmsSslEnabled()
        static std::string GetCmsHttpScheme();

        // skin name
        void SetSkinName(const std::string& name);
        std::string GetSkinName();

        //..../px_data
        std::string GetGrDataPath();

        //..../px_data/cache
        std::string GetGrDataCachePath();

    public:
        std::shared_ptr<MessageNotifier> notifier_ = nullptr;
        SharedPreference* sp_ = nullptr;
        std::string version_;
        // @deprecated
        int udp_listen_port_{0};

        std::string log_file_;
        std::string encoder_select_type_;
        std::string encoder_name_;

        std::string capture_audio_type_;
        std::string capture_video_;
        std::string capture_video_type_;
        std::string capture_audio_device_;

        std::string network_listening_ip_{};
        std::string webrtc_enabled_ = kStTrue;
        std::string udp_kcp_enabled_ = kStTrue;

        std::string file_transfer_folder_;

        int sys_service_port_ = 20375;

        std::string px_data_path_;

        std::string gl_backend_;

    };

}

#endif //TC_SERVER_STEAM_GR_SETTINGS_H
