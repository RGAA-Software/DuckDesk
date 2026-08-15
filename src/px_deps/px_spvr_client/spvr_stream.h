//
// Created by RGAA on 1/11/2025.
//

#ifndef GAMMARAYPREMIUM_SPVR_STREAM_H
#define GAMMARAYPREMIUM_SPVR_STREAM_H

#include <string>

namespace spvr
{

    class SpvrStream {
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

        // use WebRTC in direct connection, enabled by default
        bool use_webrtc_ = true;

        // use UDP(GameStream style) in direct connection, disabled by default
        bool use_udp_ = false;

        // Extra
        ///// NOT in database
        bool direct_online_ = false;

        //
        bool relay_online_ = false;

        //
        bool spvr_online_ = false;
    };

}

#endif //GAMMARAYPREMIUM_SPVR_STREAM_H
