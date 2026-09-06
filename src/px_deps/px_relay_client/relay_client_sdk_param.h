//
// Created by RGAA on 27/02/2025.
//

#ifndef PX_MGR_CLIENT_SDK_PARAM_H
#define PX_MGR_CLIENT_SDK_PARAM_H

#include <string>
#include <functional>

namespace px
{
    enum class RelayTicketScope {
        kLegacy,
        kFileTransfer,
        kMedia,
    };

    // RelayClientSdkParam
    class RelayClientSdkParam {
    public:
        std::string host_;
        int port_{0};
        std::string path_;
        bool ssl_ = false;
        std::string device_id_;
        // Has value in client
        std::string remote_device_id_;
        // stream id
        std::string stream_id_;
        // device name
        std::string device_name_;
        // appkey
        std::string appkey_;
        // force gdi
        bool force_gdi_ = false;
        std::string connection_ticket_;
        std::string connection_nonce_;
        std::string connection_ticket_device_id_;
        std::string connection_instance_id_;
        RelayTicketScope ticket_scope_{RelayTicketScope::kLegacy};
    };

}

#endif //PX_MGR_CLIENT_SDK_PARAM_H
