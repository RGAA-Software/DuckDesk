//
// Created by RGAA on 2023-08-10.
//

#ifndef SAILFISH_CLIENT_PC_SETTINGS_H
#define SAILFISH_CLIENT_PC_SETTINGS_H

#include <memory>
#include <string>
#include "px_message.pb.h"

namespace px
{

    enum class ScaleMode {
        kKeepAspectRatio,
        kFillWindow,
        kOriginSize,
    };

    class SharedPreference;

    class Settings {
    public:

        static Settings* Instance() {
            static Settings sts;
            return &sts;
        }

        void LoadSettings();
        bool IsAudioEnabled() const;
        bool IsFullColorEnabled() const;
        void SetAudioEnabled(bool enabled);
        void SetClipboardEnabled(bool enabled);
        void SetWorkMode(SwitchWorkMode::WorkMode mode);
        void SetScaleMode(ScaleMode mode);
        void SetFullColorEnabled(bool enabled);
        void SetFps(int fps);
        int GetFps() const;
        bool IsRelayMode();
        bool IsDirectMode();
        void Dump();

    public:
        // 1. direct mode
        // host: remote device ip address
        // port: remote device port
        std::string host_;
        int port_{0};
        // udp_direct 模式下 render 的 UDP 媒体端口(与 ws 控制面端口分开)
        int udp_port_{20371};

        // Cms
        std::string cms_host_;
        int cms_port_ = 0;
        // whether the CMS connection uses wss(true, default) or plain ws(false)
        bool cms_ssl_ = true;

        std::string version_;
        bool audio_on_ = false;
        bool clipboard_on_ = false;
        bool full_color_on_ = false;
        SharedPreference* sp_ = nullptr;
        SwitchWorkMode::WorkMode work_mode_ = SwitchWorkMode::kGame;
        ScaleMode scale_mode_ = ScaleMode::kFillWindow;
        // for client render process --- below
        std::string stream_id_;
        // network type
        ClientNetworkType network_type_;
        // stream name
        std::string stream_name_;
        // device id
        std::string device_id_;
        // full device id
        // client_xxx_xxx
        std::string full_device_id_;
        // device random pwd
        std::string device_random_pwd_;
        // device safety pwd
        std::string device_safety_pwd_;
        // remote device
        std::string remote_device_id_;
        // full remote device id
        // server_xxx_xxx
        std::string full_remote_device_id_;
        // remote device random pwd
        std::string remote_device_random_pwd_;
        // remote device safety pwd
        std::string remote_device_safety_pwd_;
        std::string connection_ticket_;
        std::string connection_nonce_;
        std::string connection_instance_id_;
        // enable p2p
        bool enable_p2p_ = false;
        // show max window
        bool auto_layout_screens_ = false;
        std::string display_name_;
        std::string display_remote_name_;
        // panel ws server port
        int panel_server_port_ = 0;

        //  screen recording path
        std::string screen_recording_path_;

        // fps 当前流路的帧率
        int fps_ = 30;

        // this device host/ip address
        std::string my_host_;

        // language
        int language_ = 3; // default English

        // don't send mouse/keyboard events if enabled
        bool only_viewing_ = false;

        // show all windows
        bool split_windows_ = false;

        // max_number_of_screen_window
        int max_number_of_screen_window_ = 2;

        // display logo
        bool display_logo_ = false;

        // develop mode
        bool develop_mode_ = false;
		
		// titlebar color
		int titlebar_color_ = -1;

        std::string appkey_;

        std::string decoder_;

        // 2. relay mode
        // host: relay server address
        // port: relay server port
        std::string relay_host_;
        int relay_port_ = 0;
        std::string relay_appkey_;

        // force software to decode & render
        bool force_software_ = false;

        // wait debug
        bool wait_debug_ = false;

        // show watermark
        bool show_watermark_ = false;

        // force gdi
        bool force_gdi_ = false;

        // disable_vulkan_
        bool disable_vulkan_ = false;

        // opengl backend
        std::string gl_backend_;

        // force direct
        bool force_direct_ = false;

        // skin plugin name
        std::string skin_name_;

        ///////
        ///////
        // from render //
        // audio capture enabled in render
        bool is_render_audio_capture_enabled_ = true;

        // can be operated by mouse/keyboard in render
        bool is_render_be_operated_by_mk_ = true;

        // 被控端文件传输协议版本(rustdesk 语义 = 2;0/缺省 = 旧版,不兼容,入口置灰)
        uint32_t render_ft_protocol_version_ = 0;

        // max speed of remote ethernet
        uint64_t max_transmit_speed_ = 0;
        uint64_t max_receive_speed_ = 0;
        ///////
        ///////
    };

}

#endif //SAILFISH_CLIENT_PC_SETTINGS_H
