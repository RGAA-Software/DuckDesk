#ifndef GAMMARAYPREMIUM_CONSOLE_DEVICE_STATE_H
#define GAMMARAYPREMIUM_CONSOLE_DEVICE_STATE_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "connection_policy.h"
#include "px_console_client/console_stream.h"

namespace px
{

    using ConsoleDeviceOnlineStates = std::unordered_map<std::string, bool>;

    inline void ApplyConsoleDeviceOnlineStates(
        std::vector<std::shared_ptr<px_console::ConsoleStream>>& streams,
        const ConsoleDeviceOnlineStates& states) {
        for (const auto& stream : streams) {
            if (!stream || stream->connect_type_ != connection_policy::kConsoleDeviceTicket) {
                continue;
            }
            const auto state = states.find(stream->remote_device_id_);
            if (state != states.end()) {
                stream->console_online_ = state->second;
            }
        }
    }

}

#endif // GAMMARAYPREMIUM_CONSOLE_DEVICE_STATE_H
