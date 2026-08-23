//
// Created by RGAA on 1/11/2025.
//

#include "console_stream.h"

namespace px_console
{

    bool ConsoleStream::IsValid() const {
        return !stream_id_.empty();
    }

    bool ConsoleStream::HasRelayInfo() const {
        return !remote_device_id_.empty() && !relay_host_.empty() && relay_port_ > 0;
    }

}