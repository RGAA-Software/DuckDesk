//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_WEBRTC_REMOTE_TRANSPORT_H
#define PX_RENDER_WEBRTC_REMOTE_TRANSPORT_H

#include <functional>
#include <mutex>
#include <atomic>

#include "px_render/network/webrtc/webrtc_execution_context.h"
#include "rtc_messages.h"
#include "px_common_new/concurrent_hashmap.h"
#include "px_common_new/file_transfer_send_result.h"

#if defined(_WIN32)
#if defined(PX_NET_RTC_BUILD)
#define PX_NET_RTC_API __declspec(dllexport)
#else
#define PX_NET_RTC_API __declspec(dllimport)
#endif
#else
#define PX_NET_RTC_API
#endif

namespace px {

class RtcServer;
class WebRtcRemoteTransport;

class WebRtcRemoteRuntime final {
  public:
    WebRtcRemoteRuntime(std::weak_ptr<WebRtcRemoteTransport> owner, std::weak_ptr<WebRtcExecutionContext> context);

    void DeactivateOwner();
    [[nodiscard]] std::shared_ptr<WebRtcExecutionContext> GetContext() const;
    void QueueEvent(WebRtcEvent event, bool immediate = false) const;
    void DispatchClientEvent(bool direct, const TransportChannel& channel_type, std::shared_ptr<Data> message,
                             const std::string& connection_instance_id = {});

    ConcurrentHashMap<std::string, std::shared_ptr<RtcServer>> servers;

  private:
    std::mutex owner_mutex_;
    std::weak_ptr<WebRtcRemoteTransport> owner_;
    std::weak_ptr<WebRtcExecutionContext> context_;
};

class PX_NET_RTC_API WebRtcRemoteTransport final : public std::enable_shared_from_this<WebRtcRemoteTransport> {
  public:
    WebRtcRemoteTransport() = default;
    ~WebRtcRemoteTransport();

    [[nodiscard]] WebRtcTransportInfo Info() const;
    [[nodiscard]] bool Start(const WebRtcTransportConfiguration& configuration);
    void Stop();
    void Destroy();
    void SetEventCallback(WebRtcEventCallback callback);
    void SetEnabled(bool enabled);
    [[nodiscard]] bool IsWorking() const;
    void UpdateSettings(const WebRtcTransportSettings& settings);
    void ApplyRtcRemoteSdp(const MsgRtcRemoteSdp& message);
    void ApplyRtcRemoteIce(const MsgRtcRemoteIce& message);
    void ApplyLogicalSessionCapabilities(const PxLogicalSessionCapabilityUpdate& update);
    void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through);
    bool PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through);
    FileTransferSendResult PostTargetFileTransferProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through,
                                                              const std::string& connection_instance_id = {});
    int GetConnectedClientsCount();
    int64_t GetQueuingMediaMsgCount();
    int64_t GetQueuingFtMsgCount();
    bool HasEnoughBufferForQueuingMediaMessages();
    bool HasEnoughBufferForQueuingFtMessages();

  private:
    [[nodiscard]] bool PostWork(std::function<void()> task) const;
    void OnRemoteSdp(const MsgRtcRemoteSdp& m);
    void OnRemoteIce(const MsgRtcRemoteIce& m);
    void WaitForMediaChannelActive();

  private:
    std::shared_ptr<WebRtcRemoteRuntime> runtime_;
    std::shared_ptr<WebRtcExecutionContext> execution_context_;
    WebRtcTransportSettings settings_{};
    std::atomic<WebRtcTransportLifecycle> lifecycle_{WebRtcTransportLifecycle::kCreated};
    std::atomic_bool enabled_{true};
};

[[nodiscard]] PX_NET_RTC_API std::shared_ptr<WebRtcRemoteTransport> CreateWebRtcRemoteTransport();

} // namespace px

#endif // PX_RENDER_WEBRTC_REMOTE_TRANSPORT_H
