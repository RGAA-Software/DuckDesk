//
// Created by RGAA on 2023-12-27.
//

#ifndef TC_CLIENT_PC_WS_CLIENT_H
#define TC_CLIENT_PC_WS_CLIENT_H

#include "px_message.pb.h"
#include <atomic>
#include <mutex>
#include "sdk_connection_params.h"
#include <functional>
#include <memory>
#include "px_common/file_transfer_send_result.h"
#include "connection/udp_media_state.h"

namespace asio2 {
class ws_client;
class timer;
} // namespace asio2

namespace px {

using OnRawMessageCallback = std::function<void(std::shared_ptr<px::Message>)>;
using OnVideoFrameMsgCallback = std::function<void(std::shared_ptr<px::Message>)>;
using OnAudioFrameMsgCallback = std::function<void(std::shared_ptr<px::Message>)>;
using OnCursorInfoSyncMsgCallback = std::function<void(std::shared_ptr<px::Message>)>;
using OnAudioSpectrumCallback = std::function<void(std::shared_ptr<px::Message>)>;
using OnConnectedCallback = std::function<void()>;
using OnDisconnectedCallback = std::function<void()>;
using OnHeartBeatInfoCallback = std::function<void(std::shared_ptr<px::Message>)>;
using OnClipboardInfoCallback = std::function<void(std::shared_ptr<px::Message>)>;
using OnConfigCallback = std::function<void(std::shared_ptr<px::Message>)>;
using OnMonitorSwitchedCallback = std::function<void(std::shared_ptr<px::Message>)>;

class Data;
class Thread;
class MessageNotifier;
class MessageListener;
class Connection;
class UdpDirectConnection;
class SdkStatistics;

class NetClient : public std::enable_shared_from_this<NetClient> {
  public:
    explicit NetClient(SdkConnectionParams params, const std::shared_ptr<MessageNotifier>& notifier);
    ~NetClient();

    void Start();
    void Exit();

    void PostMediaMessage(std::shared_ptr<Data> msg);
    [[nodiscard]] FileTransferSendResult PostFileTransferMessage(std::shared_ptr<Data> msg);

    void SetOnVideoFrameMsgCallback(OnVideoFrameMsgCallback&& cbk);
    void SetOnAudioFrameMsgCallback(OnAudioFrameMsgCallback&& cbk);
    void SetOnCursorInfoSyncMsgCallback(OnCursorInfoSyncMsgCallback&& cbk);
    void SetOnAudioSpectrumCallback(OnAudioSpectrumCallback&& cbk);
    void SetOnConnectCallback(OnConnectedCallback&& cbk);
    void SetOnDisconnectedCallback(OnDisconnectedCallback&& cbk);
    void SetOnHeartBeatCallback(OnHeartBeatInfoCallback&& cbk);
    void SetOnClipboardCallback(OnClipboardInfoCallback&& cbk);
    void SetOnServerConfigurationCallback(OnConfigCallback&& cbk);
    void SetOnMonitorSwitchedCallback(OnMonitorSwitchedCallback&& cbk);
    void SetOnRawMessageCallback(OnRawMessageCallback&& cbk);

    int64_t GetQueuingMediaMsgCount();
    int64_t GetQueuingFtMsgCount();

    void On16msTimeout();


  private:
    std::shared_ptr<px::Message> ParseMessage(std::shared_ptr<Data> msg);
    void HeartBeat();
    void CheckUdpMediaProbeTimeout();
    void OnUdpMediaReady();
    void ReportUdpMediaUnavailable();
    void StartUdpDirectMedia();
    void StartFileTransferConnection();
    [[nodiscard]] std::string MakeAuthenticatedWebSocketPath(std::string path, bool file_only = false) const;
    std::shared_ptr<Connection> MakeDirectWebSocketMediaConnection() const;
    void StartManagedUdpMediaConnection(const std::shared_ptr<Connection>& connection, uint64_t generation);
    [[nodiscard]] bool IsCurrentManagedMediaConnection(uint64_t generation) const;
    [[nodiscard]] std::shared_ptr<Connection> CurrentMediaConnection() const;
    void ReplaceMediaConnection(std::shared_ptr<Connection> connection);
    [[nodiscard]] std::shared_ptr<UdpDirectConnection> CurrentUdpDirectConnection() const;
    void ReplaceUdpDirectConnection(std::shared_ptr<UdpDirectConnection> connection);

  private:
    mutable std::mutex media_connection_mutex_;
    std::shared_ptr<Connection> media_conn_ = nullptr;
    mutable std::mutex udp_direct_connection_mutex_;
    std::shared_ptr<Connection> ft_conn_ = nullptr;
    std::shared_ptr<UdpDirectConnection> udp_direct_conn_ = nullptr;
    OnVideoFrameMsgCallback video_frame_cbk_;
    OnAudioFrameMsgCallback audio_frame_cbk_;
    OnCursorInfoSyncMsgCallback cursor_info_sync_cbk_;
    OnAudioSpectrumCallback audio_spectrum_cbk_;
    OnConnectedCallback conn_cbk_;
    OnDisconnectedCallback dis_conn_cbk_;
    OnHeartBeatInfoCallback hb_cbk_;
    OnClipboardInfoCallback clipboard_cbk_;
    OnConfigCallback config_cbk_;
    OnMonitorSwitchedCallback monitor_switched_cbk_;
    OnRawMessageCallback raw_msg_cbk_;

    const SdkConnectionParams params_{};
    const std::string udp_media_association_{};

    std::atomic_int queuing_message_count_ = 0;
    std::atomic_bool exited_{false};
    std::atomic_bool started_{false};
    // UDP availability never changes the reliable control/file transport.
    UdpMediaState udp_media_state_;
    std::atomic_uint64_t managed_media_generation_{0};
    std::atomic_int64_t udp_media_probe_deadline_ms_{0};
    std::atomic_bool udp_direct_started_{false};
    std::atomic_bool file_transfer_started_{false};
    std::atomic_bool connection_notified_{false};
    static constexpr int64_t kUdpMediaProbeTimeoutMs = 4000;
    uint64_t hb_idx_ = 0;

    std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
    std::shared_ptr<MessageListener> msg_listener_ = nullptr;

    std::shared_ptr<SdkStatistics> stat_;
};

} // namespace px

#endif // TC_CLIENT_PC_WS_CLIENT_H
