#ifndef PX_RENDER_RELAY_TRANSPORT_RUNTIME_H
#define PX_RENDER_RELAY_TRANSPORT_RUNTIME_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "px_common/file_transfer_send_result.h"
#include "architecture/modules/render_module.h"
#include "px_render/network/transport_types.h"

namespace px {

class Data;
class FileTransferWritableSignal;
class NetMessageAck;
class PxConnectedClientInfo;
class RenderExecutionContext;
class PxAsyncRuntime;
class RelayServerSdk;

struct RelayTransportRuntimeConfig final {
    std::string relay_device_id;
    std::string configured_host;
    int configured_port = 0;
    RenderModuleSettings settings;
    std::shared_ptr<PxAsyncRuntime> async_runtime;
};

class RelayTransportRuntime final : public std::enable_shared_from_this<RelayTransportRuntime> {
  public:
    static std::shared_ptr<RelayTransportRuntime> Create(RelayTransportRuntimeConfig config);

    explicit RelayTransportRuntime(RelayTransportRuntimeConfig config);
    ~RelayTransportRuntime();

    RelayTransportRuntime(const RelayTransportRuntime&) = delete;
    RelayTransportRuntime& operator=(const RelayTransportRuntime&) = delete;

    void Start(const std::shared_ptr<RenderExecutionContext>& context, RenderEventCallback event_callback);
    void Stop();
    void UpdateSettings(const RenderModuleSettings& settings);

    void PostMedia(std::shared_ptr<Data> message, bool run_through);
    bool PostTargetMedia(const std::string& stream_id, std::shared_ptr<Data> message, bool run_through);
    FileTransferSendResult PostFileTransfer(const std::string& stream_id, std::shared_ptr<Data> message, const std::string& connection_instance_id);

    [[nodiscard]] int ConnectedClientsCount() const;
    [[nodiscard]] bool IsWorking() const;
    [[nodiscard]] int64_t QueuingMediaMessageCount() const;
    [[nodiscard]] int64_t QueuingFileTransferMessageCount() const;
    [[nodiscard]] std::uint64_t MediaChannelInstanceGeneration() const;
    [[nodiscard]] std::uint64_t MediaConnectionAttemptGeneration() const;
    [[nodiscard]] std::vector<std::shared_ptr<PxConnectedClientInfo>> ConnectedClientInfo() const;
    void OnMessageAck(const std::shared_ptr<NetMessageAck>& ack);

  private:
    struct MonitorControl final {
        std::mutex mutex{};
        std::condition_variable wake_condition{};
        std::condition_variable stopped_condition{};
        std::thread::id worker_thread_id{};
        bool stop_requested{false};
        bool wake_requested{false};
        bool completed{false};
    };

    struct FtRelayRouteInfo final {
        std::string stream_id;
        std::string visitor_device_id;
        std::string connection_instance_id;
        int64_t created_timestamp = 0;
        uint64_t last_recv_msg_index = 0;
        bool has_recv_msg_index = false;
    };

    struct MediaRelayRouteInfo final {
        std::string room_id;
        std::string stream_id;
        std::string visitor_device_id;
        std::string connection_instance_id;
        std::string logical_session_id;
        std::vector<std::string> permissions;
        int64_t created_timestamp = 0;
        bool ticket_enforced = false;
    };

    static void Monitor(std::weak_ptr<RelayTransportRuntime> runtime, const std::shared_ptr<MonitorControl>& control);
    static bool WaitFor(const std::shared_ptr<MonitorControl>& control, std::chrono::milliseconds delay);
    void WakeMonitor();
    RelayTransportRuntimeConfig ConfigSnapshot() const;
    void ReleaseConnections();
    void ConnectMedia(const RelayTransportRuntimeConfig& config, const std::string& host, int port,
                      const std::vector<class RelayDeviceNetInfo>& net_info, int connect_count);
    void ConnectFileTransfer(const RelayTransportRuntimeConfig& config, const std::string& host, int port,
                             const std::vector<class RelayDeviceNetInfo>& net_info);

    std::shared_ptr<RelayServerSdk> MediaSdk() const;
    std::shared_ptr<RelayServerSdk> FileTransferSdk() const;
    void SetMediaSdk(std::shared_ptr<RelayServerSdk> sdk);
    void SetFileTransferSdk(std::shared_ptr<RelayServerSdk> sdk);
    [[nodiscard]] bool IsCurrentMediaGeneration(uint64_t generation) const;
    [[nodiscard]] bool IsCurrentFileTransferGeneration(uint64_t generation) const;

    void Emit(RenderEvent event, bool directly = false);
    void EmitNetMessage(std::shared_ptr<Data> message, const TransportChannel& channel, std::string connection_instance_id, bool directly);
    void NotifyClientConnected(const std::string& connection_id, const std::string& stream_id, const std::string& visitor_device_id);
    void NotifyClientDisconnected(const std::string& connection_id, const std::string& stream_id, const std::string& visitor_device_id,
                                  int64_t begin_timestamp, const std::string& logical_session_id = {});
    void ReportRelayAlive(const std::string& device_id);
    void ReportSentDataSize(std::size_t size);
    [[nodiscard]] bool StoreMediaRoute(MediaRelayRouteInfo route, uint64_t generation);
    [[nodiscard]] std::optional<MediaRelayRouteInfo> FindMediaRouteByRoom(const std::string& room_id) const;
    [[nodiscard]] std::optional<MediaRelayRouteInfo> FindMediaRouteByConnection(const std::string& connection_instance_id) const;
    [[nodiscard]] std::vector<std::string> AuthorizedMediaRooms(const std::shared_ptr<Data>& message, const std::string& stream_id = {}) const;
    void CloseMediaRoute(const std::string& room_id);
    void CloseAllMediaRoutes();

    mutable std::mutex lifecycle_mutex_;
    std::shared_ptr<MonitorControl> monitor_control_{};
    std::atomic_bool started_{false};
    std::atomic_bool stopping_{false};

    mutable std::mutex config_mutex_;
    RelayTransportRuntimeConfig config_;
    std::atomic_bool need_reconnect_{false};

    mutable std::mutex sink_mutex_;
    std::shared_ptr<RenderExecutionContext> execution_context_;
    RenderEventCallback event_callback_;

    mutable std::mutex sdk_mutex_;
    std::shared_ptr<RelayServerSdk> relay_media_sdk_;
    std::shared_ptr<RelayServerSdk> relay_ft_sdk_;
    std::atomic_uint64_t media_generation_{0};
    std::atomic_uint64_t file_transfer_generation_{0};

    std::atomic_bool paused_stream_{true};
    mutable std::mutex media_route_mutex_;
    std::unordered_map<std::string, MediaRelayRouteInfo> media_routes_;
    mutable std::mutex ft_route_mutex_;
    std::unordered_map<std::string, FtRelayRouteInfo> ft_routes_;
    uint64_t ft_route_generation_ = 0;

    mutable std::mutex ack_mutex_;
    std::shared_ptr<NetMessageAck> last_ack_;
};

} // namespace px

#endif // PX_RENDER_RELAY_TRANSPORT_RUNTIME_H
