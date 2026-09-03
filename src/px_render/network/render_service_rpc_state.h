#ifndef PX_RENDER_SERVICE_RPC_STATE_H
#define PX_RENDER_SERVICE_RPC_STATE_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/app_messages.h"
#include "px_common_new/async_operation.h"

namespace px {

struct RedeemedConnectionTicket {
    std::vector<std::string> permissions;
    std::string logical_session_id;
    std::string stream_id;
    std::string join_mode;
    std::string subject_id;
    int64_t expires_at_ms = 0;
    bool allow_observer = true;
    bool allow_takeover = true;
    std::string rtc_ice_config_json;
};

class RenderServiceRpcState final {
public:
    explicit RenderServiceRpcState(asio::any_io_executor executor)
        : ticket_requests_(
              std::make_shared<PxAsyncRequestRegistry<RedeemedConnectionTicket>>(executor)),
          virtual_display_requests_(
              std::make_shared<PxAsyncRequestRegistry<MsgVirtualDisplayServiceResult>>(
                  std::move(executor))) {}

    RenderServiceRpcState(const RenderServiceRpcState&) = delete;
    RenderServiceRpcState& operator=(const RenderServiceRpcState&) = delete;

    std::shared_ptr<PxAsyncRequestRegistry<RedeemedConnectionTicket>> ticket_requests_;
    std::shared_ptr<PxAsyncRequestRegistry<MsgVirtualDisplayServiceResult>>
        virtual_display_requests_;
};

} // namespace px

#endif // PX_RENDER_SERVICE_RPC_STATE_H
