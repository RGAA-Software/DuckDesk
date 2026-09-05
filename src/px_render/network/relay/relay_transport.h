//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RELAY_TRANSPORT_H
#define PX_RELAY_TRANSPORT_H

#include "architecture/modules/render_module.h"
#include "px_common/file_transfer_send_result.h"
#include "px_render/network/transport_types.h"

namespace px
{

    class RelayTransportRuntime;
    class PxAsyncRuntime;

    class RelayTransport final : public RenderModule {
    public:
        explicit RelayTransport(std::shared_ptr<PxAsyncRuntime> async_runtime = {});
        std::string Id() const override;
        std::string Name() const override;
        std::string VersionName() const override;
        uint32_t VersionCode() const override;
        std::string Description() const override;
        RenderModuleKind Kind() const override { return RenderModuleKind::kNetwork; }
        bool Start(const RenderModuleConfiguration& configuration) override;
        bool Destroy() override;
        void Broadcast(std::shared_ptr<Data> message, bool run_through);
        bool SendToStream(const std::string& stream_id, std::shared_ptr<Data> message, bool run_through);
        FileTransferSendResult SendFileTransfer(
            const std::string& stream_id,
            std::shared_ptr<Data> msg,
            bool run_through,
            const std::string& connection_instance_id = {});
        int ConnectedClientCount() const;
        bool HasOnlyAudioClients() const noexcept;
        bool IsWorking() const override;
        void UpdateRouteInfo(const NetSyncInfo& info);
        void UpdateSettings(const RenderModuleSettings& settings) override;
        int64_t QueuedMediaCount() const;
        int64_t QueuedFileTransferCount() const;
        bool HasMediaCapacity() const noexcept;
        bool HasFileTransferCapacity() const noexcept;
        std::vector<std::shared_ptr<PxConnectedClientInfo>> ConnectedClients() const;
        void HandleMessageAck(const std::shared_ptr<NetMessageAck>& ack);

    private:
        const std::shared_ptr<PxAsyncRuntime> async_runtime_;
        std::atomic<std::shared_ptr<RelayTransportRuntime>> runtime_;
        NetSyncInfo route_info_;
    };

}



#endif  // PX_RELAY_TRANSPORT_H
