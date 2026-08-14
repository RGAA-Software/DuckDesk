//
// Created by RGAA on 16/04/2025.
//

#ifndef GAMMARAY_SDK_PARAMS_H
#define GAMMARAY_SDK_PARAMS_H

#include "tc_message.pb.h"

extern "C" {
    #include <libavutil/buffer.h>
}

#ifdef WIN32
#include "tc_common_new/win32/d3d11_wrapper.h"
#endif

namespace tc
{

    class ThunderSdkParams {
    public:
        bool ssl_ = false;
        bool enable_audio_ = false;
        bool enable_video_ = false;
        bool enable_controller_ = false;
        std::string ip_;
        int port_;
        // udp_direct(kUdpDirect)模式下 render 的 UDP 媒体端口,与 ws 控制面端口分开
        int udp_port_ = 20371;
        std::string media_path_;
        std::string ft_path_;
        ClientType client_type_;
        //ClientConnectType conn_type_;
        ClientNetworkType nt_type_;
        // id only: xxxxx
        std::string bare_device_id_;
        // id only: xxxxx
        std::string bare_remote_device_id_;
        // client_xxxx_xxxx
        std::string device_id_;
        // server_xxxx
        std::string remote_device_id_;
        std::string ft_device_id_;
        std::string ft_remote_device_id_;
        std::string stream_id_;
        std::string stream_name_;
        bool enable_p2p_ = false;
        std::string display_name_;
        std::string display_remote_name_;

        int language_id_ = 0;

        // device name
        std::string device_name_;

        int titlebar_color_ = -1;
        // appkey
        std::string appkey_;
        // decoder
        std::string decoder_;
#ifdef WIN32
        std::shared_ptr<D3D11DeviceWrapper> d3d11_wrapper_ = nullptr;
#endif

        // relay server info
        std::string relay_host_;
        int relay_port_ = 0;
        std::string relay_appkey_;

        // Device context used for hwaccel decoders (vulkan use)
        AVBufferRef* vulkan_hw_device_ctx_ = nullptr;

        bool support_vulkan_ = false;

        std::string render_type_name_ = "unknow";

        // debug
        bool debug_ = false;

        // force gdi
        bool force_gdi_ = false;

        // remote device passwords, used by the webrtc local(direct) connection(kWebRtc)
        // plain random password, will be md5-ed before sending as safety_pwd_md5
        std::string remote_device_random_pwd_;
        // safety password, already in md5 form, sent as safety_pwd_md5 directly
        std::string remote_device_safety_pwd_;
    };

}

#endif //GAMMARAY_SDK_PARAMS_H
