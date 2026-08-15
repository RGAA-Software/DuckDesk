//
// Created by RGAA on 27/02/2025.
//

#ifndef GAMMARAY_MGR_CLIENT_SDK_PARAM_H
#define GAMMARAY_MGR_CLIENT_SDK_PARAM_H

#include <string>
#include <functional>

namespace tc
{
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
    };

}

#endif //GAMMARAY_MGR_CLIENT_SDK_PARAM_H
