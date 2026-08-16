//
// Created by RGAA on 1/07/2025.
//

#ifndef PX_RELAY_CONNECTED_INFO_H
#define PX_RELAY_CONNECTED_INFO_H

#include <string>

namespace px
{
    class RelayConnectedClientInfo {
    public:
        std::string room_id_;
        std::string device_id_;
        std::string stream_id_;
        std::string device_name_;
    };
}

#endif //PX_RELAY_CONNECTED_INFO_H
