#pragma once

#include "iroh_session.h"
#include "px_render/architecture/modules/render_module.h"

namespace px {
// Registry-owned composition of admitted sessions and the existing capture/encoder pipeline.
class IrohTransport final : public RenderModule {
public:
    IrohTransport(std::shared_ptr<WsTransport> services, std::shared_ptr<PxAsyncRuntime> runtime);
    ~IrohTransport() override;
    std::string Id() const override;
    std::string Name() const override { return "iroh transport"; }
    RenderModuleKind Kind() const override { return RenderModuleKind::kNetwork; }
    bool Start(const RenderModuleConfiguration& configuration) override;
    bool Stop() override;
    bool Destroy() override;
    bool IsWorking() const override;
    void Tick1Second() override;
    [[nodiscard]] std::expected<std::string, transport::Error> Address() const;
    [[nodiscard]] bool UpdateRelays(const std::string& relays_json);
    [[nodiscard]] std::string EndpointConfiguration() const;
    [[nodiscard]] int ConnectedClientCount() const;
    [[nodiscard]] bool HasVideoClient() const;
    [[nodiscard]] std::uint64_t VideoEncodingBitrate(std::uint64_t requested_bps) const;
    [[nodiscard]] int VideoFrameRate(int requested_fps) const;
    [[nodiscard]] bool CanEncodeVideo(const std::string& monitor) const;
    void Broadcast(const std::shared_ptr<Data>& message);
    [[nodiscard]] bool SendToStream(const std::string& stream_id, const std::shared_ptr<Data>& message);
    [[nodiscard]] FileTransferSendResult SendFile(const std::string& stream_id, const std::shared_ptr<Data>& message,
                                                  const std::string& binding_id = {});
    // True means the sequenced transport handled this frame, not delivery confirmation.
    // Per-peer busy drops must not trigger the pre-packetization backlog's IDR recovery.
    [[nodiscard]] bool SubmitVideo(const std::string& monitor, const EncodedVideoFrameEvent& encoded);
    void UpdatePermissions(const std::string& stream_id, const std::vector<std::string>& permissions);

private:
    [[nodiscard]] std::vector<std::shared_ptr<IrohSession>> Sessions() const;
    [[nodiscard]] bool Accept(AcceptedIrohFrontend accepted);
    [[nodiscard]] bool SendToSession(const std::shared_ptr<IrohSession>& session, const std::shared_ptr<Data>& message);
    std::shared_ptr<WsTransport> services_{};
    std::shared_ptr<PxAsyncRuntime> runtime_{};
    mutable std::mutex mutex_{};
    std::shared_ptr<IrohServer> server_{};
    std::string endpoint_configuration_{};
    std::vector<std::shared_ptr<IrohSession>> sessions_{};
    std::map<std::string, std::uint8_t> monitor_slots_{};
    bool started_once_{};
    std::atomic_bool running_{};
};
}  // namespace px
