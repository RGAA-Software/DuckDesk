//
// Created by RGAA
//

#ifndef TC_DATA_CHANNEL_OBSERVER_H
#define TC_DATA_CHANNEL_OBSERVER_H
#include <atomic>
#include <vector>
#include <mutex>
#include "tc_common_new/webrtc_helper.h"

namespace tc
{

    class Data;
    class RtcConnection;

    using OnDataCallback = std::function<void(std::shared_ptr<Data>)>;

    class NetTlvMessage {
    public:
        uint32_t type_{0};
        std::shared_ptr<Data> buffer_ = nullptr;
    };

    class RtcDataChannel :  public webrtc::DataChannelObserver {
    public:
        RtcDataChannel(RtcConnection* client, rtc::scoped_refptr<webrtc::DataChannelInterface> ch, const std::string& name);
        ~RtcDataChannel() override;

        void OnStateChange() override;
        void OnMessage(const webrtc::DataBuffer& buffer) override;
        void OnBufferedAmountChange(uint64_t sent_data_size) override;
        bool IsConnected();
        void SendData(std::shared_ptr<Data> msg);
        int GetPendingDataCount();
        void Close();

        bool HasEnoughBufferForQueuingMessages();
        void SetOnDataCallback(OnDataCallback&& cbk);

        bool IsMediaChannel();
        bool IsFtChannel();

        void On16msTimeout();

    private:
        rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel_ = nullptr;
        std::atomic<bool> connected_ = false;
        RtcConnection* client_ = nullptr;
        std::atomic<int> pending_data_count_ = 0;
        OnDataCallback data_cbk_;
        std::vector<NetTlvMessage> cached_messages_;
        std::string name_;
        std::atomic<uint64_t> send_pkt_index_ = 0;
        std::atomic<uint64_t> last_recv_pkt_index_ = 0;

        std::mutex cached_messages_mtx_;
        std::map<uint64_t, std::shared_ptr<Data>> cached_ft_messages_;

        // test beg //
        uint64_t total_recv_content_bytes_ = 0;
        // test end //
    };

} // namespace tc

#endif //TEST_WEBRTC_DATA_CHANNEL_OBSERVER_IMPL_H
