//
// Created by RGAA on 1/11/2025.
//

#ifndef GAMMARAYPREMIUM_CONSOLE_STREAM_H
#define GAMMARAYPREMIUM_CONSOLE_STREAM_H

#include <string>

namespace px_console
{

    class ConsoleStream {
    public:
        [[nodiscard]] bool IsValid() const;
        [[nodiscard]] bool HasRelayInfo() const;

    public:

        int _id = 0;

        // stream id
        std::string stream_id_;

        // stream name
        std::string stream_name_;

        // encode bitrate, for example : 5, that means 5Mbps
        int encode_bps_ = 5;

        // audio capture status
        int audio_enabled_ = 0;

        // clipboard
        int clipboard_enabled_ = 0;

        // only viewing
        int only_viewing_ = 0;

        // show max window
        int auto_layout_screens_ = 0;

        // split windows
        int split_windows_ = 0;

        // enable p2p
        int enable_p2p_ = 0;

        // audio source, global / app_only
        std::string audio_capture_mode_;;

        // direct mode
        std::string stream_host_;

        // direct mode
        int stream_port_ = 0;

        // relay host
        std::string relay_host_;

        // repay port
        int relay_port_ = 0;

        // relay appkey
        //std::string relay_appkey_;

        int bg_color_ = 0;

        int encode_fps_;

        // direct / signaling
        std::string connect_type_;

        // 9 numbers
        std::string device_id_;

        // random password
        std::string device_random_pwd_;

        // safety password
        std::string device_safety_pwd_;

        // remote device id
        std::string remote_device_id_;

        // remote device random pwd
        std::string remote_device_random_pwd_;

        // remote device safety pwd
        std::string remote_device_safety_pwd_;

        // created timestamp
        int64_t created_timestamp_ {0};

        // update timestamp
        int64_t updated_timestamp_ {0};

        // desktop name
        std::string desktop_name_;

        // os version
        std::string os_version_;

        // force relay
        bool force_relay_ = false;

        // force direct
        bool force_direct_ = false;

        // force software
        bool force_software_ = false;

        // wait debug
        bool wait_debug_ = false;

        // force gdi capture
        bool force_gdi_capture_ = false;

        // disable vulkan render
        bool disable_vulkan_render_ = false;

        // Force RTC. False means automatic connection selection.
        bool use_webrtc_ = false;

        // Force UDP(GameStream style). False means automatic selection.
        bool use_udp_ = false;

        // Extra
        ///// NOT in database
        bool direct_online_ = false;

        //
        bool relay_online_ = false;

        //
        bool console_online_ = false;

        // Ephemeral Console capability grant. Never persisted by the stream DB.
        std::string connection_ticket_;
        std::string connection_renewal_token_;
        std::string connection_nonce_;
        // Panel has already validated the id-less IP-direct password and
        // prepared active_session_stream_id_ on Render. Never persisted.
        bool ip_direct_prevalidated_ = false;
        // Ticket stream IDs are per logical remote-control session. Keep them
        // separate from stream_id_, which identifies the saved Panel entry.
        std::string active_session_stream_id_;
        std::string rtc_ice_config_json_;
        // Full standard-RTC/Relay target identity returned by Console. This
        // differs from remote_device_id_ for scheduled application instances.
        std::string console_signal_device_id_;
        std::string console_app_id_;
        std::string console_instance_id_;
        std::string console_access_mode_;
        std::string console_instance_state_;
        std::string console_cover_url_;
    };

}

#endif //GAMMARAYPREMIUM_CONSOLE_STREAM_H
