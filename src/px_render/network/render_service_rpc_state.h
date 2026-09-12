#ifndef PX_RENDER_SERVICE_RPC_STATE_H
#define PX_RENDER_SERVICE_RPC_STATE_H

#include <memory>
#include <string>
#include <utility>

#include "app/app_messages.h"
#include "px_common/async_operation.h"

namespace px {

class RenderServiceRpcState final {
public:
    explicit RenderServiceRpcState(asio::any_io_executor executor)
        : virtual_display_requests_(
              std::make_shared<PxAsyncRequestRegistry<MsgVirtualDisplayServiceResult>>(
                  std::move(executor))) {}

    RenderServiceRpcState(const RenderServiceRpcState&) = delete;
    RenderServiceRpcState& operator=(const RenderServiceRpcState&) = delete;

    std::shared_ptr<PxAsyncRequestRegistry<MsgVirtualDisplayServiceResult>>
        virtual_display_requests_;
};

} // namespace px

#endif // PX_RENDER_SERVICE_RPC_STATE_H
