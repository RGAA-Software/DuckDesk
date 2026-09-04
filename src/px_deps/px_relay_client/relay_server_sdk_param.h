//
// Created by RGAA on 1/03/2025.
//

#ifndef PX_RELAY_SERVER_SDK_PARAM_H
#define PX_RELAY_SERVER_SDK_PARAM_H

#include <memory>
#include <string>
#include "relay_device_info.h"

namespace px
{
    class PxAsyncRuntime;

    // RelayServerSdkParam
    class RelayServerSdkParam {
    public:
        std::string host_;
        int port_{0};
        bool ssl_ = false;
        std::string device_id_;
        std::vector<RelayDeviceNetInfo> net_info_;
        std::string device_name_;
        std::string stream_id_;
        std::string appkey_;
        std::shared_ptr<PxAsyncRuntime> async_runtime_{};
    };


}

#endif //PX_RELAY_SERVER_SDK_PARAM_H
