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
        : virtual_display_requests_(std::make_shared<PxAsyncRequestRegistry<MsgVirtualDisplayServiceResult>>(executor)),
          frontend_admission_requests_(std::make_shared<PxAsyncRequestRegistry<MsgFrontendAdmissionServiceResult>>(executor)),
          resource_channel_requests_(std::make_shared<PxAsyncRequestRegistry<MsgResourceChannelServiceResult>>(executor)),
          file_transfer_requests_(std::make_shared<PxAsyncRequestRegistry<MsgFileTransferServiceResult>>(std::move(executor))) {}

    RenderServiceRpcState(const RenderServiceRpcState&) = delete;
    RenderServiceRpcState& operator=(const RenderServiceRpcState&) = delete;

    std::shared_ptr<PxAsyncRequestRegistry<MsgVirtualDisplayServiceResult>> virtual_display_requests_;
    std::shared_ptr<PxAsyncRequestRegistry<MsgFrontendAdmissionServiceResult>> frontend_admission_requests_;
    std::shared_ptr<PxAsyncRequestRegistry<MsgResourceChannelServiceResult>> resource_channel_requests_;
    std::shared_ptr<PxAsyncRequestRegistry<MsgFileTransferServiceResult>> file_transfer_requests_;
};

}  // namespace px

#endif  // PX_RENDER_SERVICE_RPC_STATE_H
