//
// Created by RGAA on 28/02/2025.
//

#ifndef PX_RELAY_ROOM_H
#define PX_RELAY_ROOM_H

#include <string>

namespace px
{
    // which room I'm in
    class RelayRoom {
    public:
        [[nodiscard]] bool IsValid() const;
        void Clear();

    public:
        std::string room_id_;
        // creator
        std::string device_id_;
        //
        std::string remote_device_id_;
        //
        int64_t created_timestamp_{0};
        // creator device id == device id
        std::string creator_device_id_;
        std::string creator_device_name_;
        std::string creator_stream_id_;

        // random id for this connection
        // 1. used for logging records
        std::string conn_id_;
    };

}

#endif //PX_RELAY_ROOM_H
