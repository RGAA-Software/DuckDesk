//
// Created by RGAA on 1/11/2025.
//

#include "spvr_stream.h"

namespace spvr
{

    bool SpvrStream::IsValid() const {
        return !stream_id_.empty();
    }

    bool SpvrStream::HasRelayInfo() const {
        return !remote_device_id_.empty() && !relay_host_.empty() && relay_port_ > 0;
    }

}