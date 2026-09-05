#ifndef GAMMARAYPREMIUM_STREAM_ITEM_DISPLAY_H
#define GAMMARAYPREMIUM_STREAM_ITEM_DISPLAY_H

#include <string>

#include "px_common/uid_spacer.h"
#include "px_console_client/console_stream.h"
#include "connection_policy.h"

namespace px
{

    inline std::string StreamItemPrimaryText(const px_console::ConsoleStream& item) {
        if (item.connect_type_ == "console_app_ticket") {
            return item.stream_name_;
        }
        // A Console-managed device has an ID even though its Relay endpoint is
        // intentionally resolved only when a connection ticket is issued.
        if (!item.remote_device_id_.empty()) {
            return SpaceId(item.remote_device_id_);
        }
        return item.stream_host_;
    }

    inline bool StreamItemIsOnline(const px_console::ConsoleStream& item) {
        if (item.connect_type_ == connection_policy::kConsoleDeviceTicket) {
            return item.console_online_;
        }
        return item.direct_online_ || item.relay_online_ || item.console_online_;
    }

}

#endif // GAMMARAYPREMIUM_STREAM_ITEM_DISPLAY_H
