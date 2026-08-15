//
// Created by RGAA on 28/02/2025.
//

#ifndef GAMMARAY_RELAY_APIS_H
#define GAMMARAY_RELAY_APIS_H

#include <string>
#include <memory>
#include "relay_errors.h"
#include "px_common_new/expected.h"

namespace px_relay
{

    const std::string kRelayGetDeviceInfo = "/query/device";
    const std::string kRelayNotifyEvent = "/notify/event";
    const std::string kRelayPing = "/ping";

    class RelayDeviceInfo;

    class RelayApi {
    public:
        static px::Result<bool, int> Ping(const std::string& host, int port, const std::string& appkey);

        // id has prefix, eg: server_xxxx
        static px::Result<std::shared_ptr<RelayDeviceInfo>, int>
                GetRelayDeviceInfo(const std::string& host, int port, const std::string& device_id, const std::string& appkey);

        // id has prefix, eg: server_xxxx
        // event in json format
        static px::Result<int, int> NotifyEvent(const std::string& host,
                                                        int port,
                                                        const std::string& from_device_id, // this device
                                                        const std::string& to_device_id,   // remote device, id starts with: server_
                                                        const std::string& event,
                                                        const std::string& appkey);

        static bool IsRelayDeviceValid(const std::shared_ptr<RelayDeviceInfo>& info);

    };

}

#endif //GAMMARAY_RELAY_APIS_H
