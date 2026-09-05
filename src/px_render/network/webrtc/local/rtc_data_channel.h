//
// Created by RGAA
//

#ifndef TC_PLUGIN_DATA_CHANNEL_OBSERVER_H
#define TC_PLUGIN_DATA_CHANNEL_OBSERVER_H

#include <atomic>
#include <mutex>
#include <map>
#include "px_webrtc_client/webrtc_helper.h"
#include "px_common_new/file_transfer_send_result.h"

namespace px {
using OnDataCallback = std::function<void(const std::string&)>;

class Data;
class RtcServer;
class WebRtcExecutionContext;

class RtcDataChannel : public webrtc::DataChannelObserver, public std::enable_shared_from_this<RtcDataChannel> {
  public:
    RtcDataChannel(const std::string& name, const std::shared_ptr<RtcServer>& rtc_server, rtc::scoped_refptr<webrtc::DataChannelInterface> ch);
    ~RtcDataChannel() override;

    void OnStateChange() override;
    void OnMessage(const webrtc::DataBuffer& buffer) override;
    void OnBufferedAmountChange(uint64_t sent_data_size) override;
    bool IsConnected();
    void SendData(std::shared_ptr<Data> msg);
    int GetPendingDataCount();
    void Close();

    void SetOnDataCallback(OnDataCallback&&);
    bool HasEnoughBufferForQueuingMessages();
    [[nodiscard]] std::shared_ptr<FileTransferWritableSignal> AcquireFileTransferWritableSignal();

    void On100msTimeout();

  private:
    bool IsMediaChannel();
    bool IsFtChannel();

  private:
    std::shared_ptr<WebRtcExecutionContext> execution_context_;
    std::string name_;
    rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel_ = nullptr;
    std::weak_ptr<RtcServer> rtc_server_;
    std::atomic<bool> connected_ = false;
    std::atomic<int> pending_data_count_ = 0;
    std::atomic<uint64_t> send_pkt_index_ = 0;
    OnDataCallback data_cbk_;

    std::atomic<uint64_t> last_recv_pkt_index_ = 0;
    uint64_t total_send_content_bytes_ = 0;
    uint64_t last_recv_msg_timestamp_ = 0;

    std::mutex cached_messages_mtx_;
    std::map<uint64_t, std::string> cached_ft_messages_;
    std::mutex writable_signal_mutex_;
    std::shared_ptr<FileTransferWritableSignal> writable_signal_;

    void NotifyFileTransferWritable();
    void NotifyFileTransferClosed();
    void FlushCachedMessages();

  public:
    std::string the_connection_id_;
    int64_t created_timestamp_{0};
};

} // namespace px

#endif // TEST_WEBRTC_DATA_CHANNEL_OBSERVER_IMPL_H
