#pragma once

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "px_common/file_transfer_send_result.h"
#include "px_common/async_result.h"
#include "px_common/async_runtime.h"
#include "px_render/network/webrtc/webrtc_transport_types.h"

namespace px {

class CaptureMonitorInfoMessage;
class Data;
class Image;
class MsgRtcRemoteIce;
class MsgRtcRemoteSdp;
class PxConnectedClientInfo;
class PxLocalRtcReplyInfo;
class PxLocalRtcRequestInfo;
class PxLogicalSessionCapabilityUpdate;
enum class PxLocalRtcAllocResult;

// Concrete handle over one directly linked WebRTC C++ DLL transport. There is
// no discovery, singleton ABI, runtime symbol lookup, or common virtual base.
//
// Lifetime:
// - Owned by RenderModuleRegistry through shared_ptr.
// - State retains the directly linked DLL object for every in-flight operation.
// - Stop and Destroy run before the final library owner is released.
//
// Threading:
// - Lifecycle calls are serialized by RenderModuleRegistry.
// - Media and network calls follow the existing WebRTC library contract.
class WebRtcTransportHandle final {
  private:
    class State;

  public:
    explicit WebRtcTransportHandle(std::shared_ptr<State> state);
    ~WebRtcTransportHandle();

    WebRtcTransportHandle(const WebRtcTransportHandle&) = delete;
    WebRtcTransportHandle& operator=(const WebRtcTransportHandle&) = delete;

    [[nodiscard]] WebRtcTransportKind Kind() const;
    [[nodiscard]] std::string BaseName() const;
    [[nodiscard]] WebRtcTransportInfo Info() const;
    [[nodiscard]] bool Start(const WebRtcTransportConfiguration& configuration);
    void Stop();
    void Destroy();
    [[nodiscard]] static PxAwaitable<PxResult<void>> StopAsync(std::shared_ptr<WebRtcTransportHandle> owner,
                                                               std::chrono::steady_clock::time_point deadline);
    [[nodiscard]] std::uint64_t OutstandingCallbacks() const;
    void SetEventCallback(WebRtcEventCallback callback);
    void SetEnabled(bool enabled);
    [[nodiscard]] bool IsWorking() const;
    void UpdateSettings(const WebRtcTransportSettings& settings);

    void Send(const std::shared_ptr<Data>& message, bool run_through);
    [[nodiscard]] bool SendToStream(const std::string& stream_id, const std::shared_ptr<Data>& message, bool run_through);
    [[nodiscard]] FileTransferSendResult SendFileTransfer(const std::string& stream_id, const std::shared_ptr<Data>& message, bool run_through,
                                                          const std::string& connection_instance_id = {});
    void SubmitRawAudio(const std::shared_ptr<Data>& data, int samples, int channels, int bits);
    void SubmitEncodedVideo(const std::string& monitor_name, WebRtcEncodedVideoType video_type, const std::shared_ptr<Data>& data,
                            std::uint64_t frame_index, int frame_width, int frame_height, bool key_frame);
    void ApplyLogicalSessionCapabilities(const PxLogicalSessionCapabilityUpdate& update);
    [[nodiscard]] int ConnectedClientCount() const;
    [[nodiscard]] int MediaConsumerCount() const;
    [[nodiscard]] bool HasVideoClient() const;
    [[nodiscard]] std::int64_t QueuedMediaMessageCount() const;
    [[nodiscard]] std::int64_t QueuedFileTransferMessageCount() const;
    [[nodiscard]] std::vector<std::shared_ptr<PxConnectedClientInfo>> ConnectedClients() const;

    void SubmitLocalSharedTexture(const std::string& monitor_name, std::uint64_t frame_index, int frame_width, int frame_height,
                                  std::uint64_t shared_handle, std::int64_t adapter_id, std::uint64_t frame_format);
    void SubmitLocalYuv(const std::string& monitor_name, std::uint64_t frame_index, int frame_width, int frame_height,
                        const std::shared_ptr<Image>& image);
    void UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage& message);
    void ApplyRemoteSdp(const MsgRtcRemoteSdp& message);
    void ApplyRemoteIce(const MsgRtcRemoteIce& message);
    [[nodiscard]] PxLocalRtcAllocResult AllocateLocalInstance(const std::shared_ptr<PxLocalRtcRequestInfo>& request,
                                                              std::function<void(const std::shared_ptr<PxLocalRtcReplyInfo>&)> completion);
    [[nodiscard]] bool SetVoiceAuthorization(const std::string& stream_id, const std::string& call_id, bool authorized);
    [[nodiscard]] bool SubmitVoicePcm(const std::string& stream_id, const std::string& call_id,
                                      const std::shared_ptr<const std::vector<std::int16_t>>& samples, int sample_rate, int channels);

  private:
    std::shared_ptr<State> state_;

    friend class WebRtcTransportHost;
};

// Composes the two fixed WebRTC network transports. No discovery, generic
// transport interface, runtime loader, or plug-in object exists here.
class WebRtcTransportHost final {
  public:
    [[nodiscard]] static std::shared_ptr<WebRtcTransportHost> Create();

    WebRtcTransportHost() = default;
    ~WebRtcTransportHost();

    WebRtcTransportHost(const WebRtcTransportHost&) = delete;
    WebRtcTransportHost& operator=(const WebRtcTransportHost&) = delete;

    [[nodiscard]] std::vector<std::shared_ptr<WebRtcTransportHandle>> CreateTransports();
    void Reset();

  private:
    std::vector<std::shared_ptr<WebRtcTransportHandle>> transports_;
};

} // namespace px
