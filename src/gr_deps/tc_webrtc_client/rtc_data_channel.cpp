//
// Created by RGAA
//

#include "rtc_data_channel.h"
#include "rtc_connection.h"
#include "tc_common_new/log.h"
#include "tc_common_new/data.h"
#include "tc_common_new/net_tlv_header.h"

namespace tc
{

    RtcDataChannel::RtcDataChannel(RtcConnection* client, rtc::scoped_refptr<webrtc::DataChannelInterface> ch, const std::string& name) {
        this->client_ = client;
        this->data_channel_ = ch;
        this->data_channel_->RegisterObserver(this);
        this->name_ = name;
    }

    RtcDataChannel::~RtcDataChannel() {
        this->data_channel_->UnregisterObserver();
    }

    void RtcDataChannel::OnStateChange() {
        if (data_channel_->state() == webrtc::DataChannelInterface::kOpen) {
            connected_ = true;
            //context_->SendAppMessage(ClientEvtDataChannelReady{});

        } else if (data_channel_->state() == webrtc::DataChannelInterface::kClosed) {
            connected_ = false;
            //context_->SendAppMessage(ClientEvtDataChannelClosed{});

        }
        LOGI("DataChannel[ {} ] state changed: {}, connected: {}", name_, (int)data_channel_->state(), connected_.load());
    }

    void RtcDataChannel::OnMessage(const webrtc::DataBuffer &buffer) {
        if (buffer.data.size() < sizeof(NetTlvHeader)) {
            LOGE("RtcDataChannel TLV header too small: {}", buffer.data.size());
            return;
        }
        auto header = reinterpret_cast<const NetTlvHeader*>(buffer.data.data());
        const size_t payload_len = buffer.data.size() - sizeof(NetTlvHeader);
        if (header->this_buffer_length_ > payload_len) {
            LOGE("RtcDataChannel TLV length invalid: declared={}, available={}",
                 header->this_buffer_length_, payload_len);
            return;
        }

        total_recv_content_bytes_ += header->this_buffer_length_;

//        std::string data;
//        data.resize(header->this_buffer_length_);
//        memcpy(data.data(), (char*)header + sizeof(NetTlvHeader), header->this_buffer_length_);

        auto data = Data::Make(nullptr, header->this_buffer_length_);
        memcpy(data->DataAddr(), reinterpret_cast<const char*>(header) + sizeof(NetTlvHeader), header->this_buffer_length_);

        if (IsFtChannel()) {
            auto curr_pkt_index = header->pkt_index_;
            if (last_recv_pkt_index_ == 0) {
                last_recv_pkt_index_ = curr_pkt_index;
            }
            auto diff = curr_pkt_index - last_recv_pkt_index_;
            if (diff != 1) {
                LOGE("**** Message Index Error ****\n current index: {}, last index: {}", curr_pkt_index, last_recv_pkt_index_.load());
            }
            //LOGI("from: {}, index: {} => Message size: {}, diff: {}", name_, header->pkt_index_, header->this_buffer_length_, diff);
            last_recv_pkt_index_ = curr_pkt_index;

            //
            std::lock_guard<std::mutex> guard(cached_messages_mtx_);
            cached_ft_messages_.insert({header->pkt_index_, data});

            return;
        }


        if (header->type_ == kNetTlvFull) {
            if (data_cbk_) {
                data_cbk_(data);
            }
        } else {
            //LOGI("DataChannel message type: {}", header->type_);
            if (header->type_ == kNetTlvBegin) {
                cached_messages_.clear();
            }
            cached_messages_.push_back(NetTlvMessage{
                .type_ = header->type_,
                .buffer_ = data,
            });

            if (header->type_ == kNetTlvEnd) {
                uint32_t total_size = 0;
                for (const auto &m: cached_messages_) {
                    if (!m.buffer_) {
                        LOGE("Empty buffer.");
                        continue;
                    }
                    total_size += (uint32_t) m.buffer_->Size();
                }

                //std::string total_data;
                //total_data.resize(total_size);
                auto total_data = Data::Make(nullptr, total_size);
                uint32_t offset = 0;
                for (const auto &m: cached_messages_) {
                    if (!m.buffer_) {
                        LOGE("Empty buffer.");
                        continue;
                    }
                    memcpy((char *)total_data->DataAddr() + offset, m.buffer_->CStr(), m.buffer_->Size());
                    offset += m.buffer_->Size();
                }
                if (data_cbk_) {
                    data_cbk_(total_data);
                }
                cached_messages_.clear();
            }
        }
    }

    void RtcDataChannel::On16msTimeout() {
        this->client_->PostWorkTask([=, this]() {
            std::lock_guard<std::mutex> guard(cached_messages_mtx_);
            uint64_t beg_idx = 0;
            bool lack_messages = false;
            for (const auto& [k, data] : cached_ft_messages_) {
                if (beg_idx == 0) {
                    beg_idx = k;
                }
                if (k - beg_idx > 1) {
                    lack_messages = true;
                    break;
                }
                beg_idx = k;
            }

            if (lack_messages) {
                LOGW("Lack messages! cached message size: {}", cached_ft_messages_.size());
                std::stringstream ss;
                for (const auto& [k, data] : cached_ft_messages_) {
                    ss << k << ",";
                }
                LOGW("cached message sort: {}", ss.str());
                if (cached_ft_messages_.size() > 1024*8) {
                    // clear it
                    LOGE("Clear all cached messages, count: {}", cached_ft_messages_.size());
                    cached_ft_messages_.clear();

                    // TODO: Notify error
                }
                return;
            }

            if (cached_ft_messages_.size() > 0) {
                LOGI("Cached message size: {}", cached_ft_messages_.size());
            }
            for (const auto& [k, data] : cached_ft_messages_) {
                if (data_cbk_) {
                    data_cbk_(data);
                }
            }
            cached_ft_messages_.clear();
        });
    }

    void RtcDataChannel::OnBufferedAmountChange(uint64_t sent_data_size) {
        DataChannelObserver::OnBufferedAmountChange(sent_data_size);
        //LOGI("OnBufferAmount changed: {}", sent_data_size);
    }

    bool RtcDataChannel::IsConnected() {
        return connected_;
    }

    void RtcDataChannel::SendData(std::shared_ptr<Data> msg) {
        if (!connected_) {
            LOGW("DataChannel is invalid: {}", name_);
            return;
        }

        // test beg //
        auto buffered_amount = data_channel_->buffered_amount();
        auto max_queue_size = data_channel_->MaxSendQueueSize();
        if (buffered_amount >= max_queue_size/2) {
            //LOGW("Client, buffered amount: {}, max queue size: {}", buffered_amount, max_queue_size);
        }
        // test end //

        // wrap message
        auto header = NetTlvHeader {
            .type_ = kNetTlvFull,
            .this_buffer_length_ = (uint32_t)msg->Size(),
            .this_buffer_begin_ = 0,
            .this_buffer_end_ = (uint32_t)msg->Size(),
            .pkt_index_ = send_pkt_index_++,
            .parent_buffer_length_ = (uint32_t)msg->Size(),
        };

        std::string buffer;
        buffer.resize(sizeof(NetTlvHeader) + msg->Size());
        memcpy((char*)buffer.data(), (char*)&header, sizeof(NetTlvHeader));
        memcpy((char*)buffer.data() + sizeof(NetTlvHeader), msg->DataAddr(), msg->Size());

        auto rtc_buffer = webrtc::DataBuffer(rtc::CopyOnWriteBuffer(buffer), true);

        ++pending_data_count_;
        if (!this->data_channel_->Send(rtc_buffer)) {
            connected_ = false;
            pending_data_count_ = 0;
        }
        //RLogI("send data via data channel: {}", msg.size());
        --pending_data_count_;
        if (name_ == "ft_data_channel") {
            //LOGI("send data via data channel, size: {}, pending count: {}", msg.size(), pending_data_count_);
        }
    }

    int RtcDataChannel::GetPendingDataCount() {
        return pending_data_count_;
    }

    bool RtcDataChannel::HasEnoughBufferForQueuingMessages() {
        return data_channel_
            && data_channel_->state() == webrtc::DataChannelInterface::DataState::kOpen
            && data_channel_->buffered_amount() <= data_channel_->MaxSendQueueSize()*1/4;
    }

    void RtcDataChannel::Close() {
        LOGI("RtcDataChannel Close!");
        connected_ = false;
        if (data_channel_) {
            data_channel_->Close();
        }
    }

    void RtcDataChannel::SetOnDataCallback(OnDataCallback&& cbk) {
        data_cbk_ = cbk;
    }

    bool RtcDataChannel::IsMediaChannel() {
        return name_ == "media_data_channel";
    }

    bool RtcDataChannel::IsFtChannel() {
        return name_ == "ft_data_channel";
    }

} // namespace tc