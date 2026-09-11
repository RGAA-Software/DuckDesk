//
// Created by RGAA on 16/04/2025.
//

#ifndef PX_SDK_PARAMS_H
#define PX_SDK_PARAMS_H

#include "px_message.pb.h"
#include "sdk_connection_params.h"

#include <string>

namespace px {

class ThunderSdkParams {
  public:
    SdkMediaTransport media_transport_{SdkMediaTransport::kUdp};
    SdkConnectionRoute connection_route_{SdkConnectionRoute::kDirect};
    bool ssl_ = false;
    bool enable_audio_ = false;
    bool enable_video_ = false;
    bool enable_controller_ = false;
    // Standalone file manager: authenticated WebSocket only, without UDP media.
    bool file_transfer_only_ = false;
    std::string ip_;
    int port_ = 0;
    // Render UDP media port, separate from the reliable WebSocket control port.
    int udp_port_ = 20371;
    std::string media_path_;
    std::string ft_path_;
    ClientType client_type_ = ClientType::kUnknown;
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
    std::string display_name_;
    std::string display_remote_name_;

    // device name
    std::string device_name_;

    // appkey
    std::string appkey_;
    std::string relay_host_{};
    int relay_port_{0};
    std::string relay_remote_device_id_{};
    std::string relay_ticket_device_id_{};
    bool force_gdi_{false};
    std::string render_type_name_ = "unknow";

    // debug
    bool debug_ = false;

    // One-time authorization for the reliable WebSocket binding. Never persist or log it.
    std::string connection_ticket_;
    std::string connection_nonce_;
    std::string connection_instance_id_;
    // Short-lived opaque key used only to associate the UDP media endpoint
    // with an already authorized WS binding. It is not a session grant.
    std::string udp_media_association_;
};

} // namespace px

#endif // PX_SDK_PARAMS_H
