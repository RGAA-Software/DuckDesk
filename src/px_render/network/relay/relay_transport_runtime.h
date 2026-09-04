#ifndef PX_RENDER_RELAY_TRANSPORT_RUNTIME_H
#define PX_RENDER_RELAY_TRANSPORT_RUNTIME_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "px_common_new/file_transfer_send_result.h"
#include "architecture/modules/render_module.h"
#include "px_render/network/transport_types.h"

namespace px {

class Data;
class FileTransferWritableSignal;
class NetMessageAck;
class PxConnectedClientInfo;
class PxPluginContext;
class RelayServerSdk;

struct RelayTransportRuntimeConfig final {
    std::string relay_device_id;
    std::string configured_host;
    int configured_port = 0;
    RenderModuleSettings settings;
};

class RelayTransportRuntime final
    : public std::enable_shared_from_this<RelayTransportRuntime> {
public:
    static std::shared_ptr<RelayTransportRuntime> Create(
        RelayTransportRuntimeConfig config);

    explicit RelayTransportRuntime(RelayTransportRuntimeConfig config);
    ~RelayTransportRuntime();

    RelayTransportRuntime(const RelayTransportRuntime&) = delete;
    RelayTransportRuntime& operator=(const RelayTransportRuntime&) = delete;

    void Start(const std::shared_ptr<PxPluginContext>& context,
               CompatibilityEventCallback event_callback);
    void Stop();
    void UpdateSettings(const RenderModuleSettings& settings);

    void PostMedia(std::shared_ptr<Data> message, bool run_through);
    bool PostTargetMedia(const std::string& stream_id,
                         std::shared_ptr<Data> message,
                         bool run_through);
    FileTransferSendResult PostFileTransfer(
        const std::string& stream_id,
        std::shared_ptr<Data> message,
        const std::string& connection_instance_id);

    [[nodiscard]] int ConnectedClientsCount() const;
    [[nodiscard]] bool IsWorking() const;
    [[nodiscard]] int64_t QueuingMediaMessageCount() const;
    [[nodiscard]] int64_t QueuingFileTransferMessageCount() const;
    [[nodiscard]] std::vector<std::shared_ptr<PxConnectedClientInfo>>
        ConnectedClientInfo() const;
    void OnMessageAck(const std::shared_ptr<NetMessageAck>& ack);

private:
    struct FtRelayRouteInfo final {
        std::string stream_id;
        std::string visitor_device_id;
        std::string connection_instance_id;
        int64_t created_timestamp = 0;
        uint64_t last_recv_msg_index = 0;
        bool has_recv_msg_index = false;
    };

    void Monitor(std::stop_token stop_token);
    bool WaitFor(std::stop_token stop_token, std::chrono::milliseconds delay);
    RelayTransportRuntimeConfig ConfigSnapshot() const;
    void ReleaseConnections();
    void ConnectMedia(const RelayTransportRuntimeConfig& config,
                      const std::string& host, int port,
                      const std::vector<class RelayDeviceNetInfo>& net_info,
                      int connect_count);
    void ConnectFileTransfer(
        const RelayTransportRuntimeConfig& config,
        const std::string& host, int port,
        const std::vector<class RelayDeviceNetInfo>& net_info);

    std::shared_ptr<RelayServerSdk> MediaSdk() const;
    std::shared_ptr<RelayServerSdk> FileTransferSdk() const;
    void SetMediaSdk(std::shared_ptr<RelayServerSdk> sdk);
    void SetFileTransferSdk(std::shared_ptr<RelayServerSdk> sdk);
    [[nodiscard]] bool IsCurrentMediaGeneration(uint64_t generation) const;
    [[nodiscard]] bool IsCurrentFileTransferGeneration(uint64_t generation) const;

    void Emit(const std::shared_ptr<PxPluginBaseEvent>& event, bool directly = false);
    void EmitNetMessage(std::shared_ptr<Data> message,
                        const NetChannelType& channel,
                        std::string connection_instance_id,
                        bool directly);
    void NotifyClientConnected(const std::string& connection_id,
                               const std::string& stream_id,
                               const std::string& visitor_device_id);
    void NotifyClientDisconnected(const std::string& connection_id,
                                  const std::string& stream_id,
                                  const std::string& visitor_device_id,
                                  int64_t begin_timestamp);
    void ReportRelayAlive(const std::string& device_id);
    void ReportSentDataSize(std::size_t size);

    mutable std::mutex lifecycle_mutex_;
    std::jthread monitor_;
    std::condition_variable_any wake_condition_;
    std::atomic_bool started_{false};
    std::atomic_bool stopping_{false};

    mutable std::mutex config_mutex_;
    RelayTransportRuntimeConfig config_;
    std::atomic_bool need_reconnect_{false};

    mutable std::mutex sink_mutex_;
    std::shared_ptr<PxPluginContext> module_context_;
    CompatibilityEventCallback event_callback_;

    mutable std::mutex sdk_mutex_;
    std::shared_ptr<RelayServerSdk> relay_media_sdk_;
    std::shared_ptr<RelayServerSdk> relay_ft_sdk_;
    std::atomic_uint64_t media_generation_{0};
    std::atomic_uint64_t file_transfer_generation_{0};

    std::atomic_bool paused_stream_{true};
    mutable std::mutex ft_route_mutex_;
    std::unordered_map<std::string, FtRelayRouteInfo> ft_routes_;
    uint64_t ft_route_generation_ = 0;

    mutable std::mutex ack_mutex_;
    std::shared_ptr<NetMessageAck> last_ack_;
};

} // namespace px

#endif // PX_RENDER_RELAY_TRANSPORT_RUNTIME_H
