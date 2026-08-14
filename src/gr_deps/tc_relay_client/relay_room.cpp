//
// Created by RGAA on 28/02/2025.
//

#include "relay_room.h"

namespace tc
{

    bool RelayRoom::IsValid() const {
        return !room_id_.empty();
    }

    void RelayRoom::Clear() {
        room_id_ = "";
        device_id_ = "";
        remote_device_id_ = "";
    }

}