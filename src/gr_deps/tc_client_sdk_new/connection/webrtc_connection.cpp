//
// Created by RGAA on 16/04/2025.
//

#include "webrtc_connection.h"
#include "tc_message.pb.h"
#include "tc_common_new/log.h"
#include "tc_common_new/data.h"
#include "tc_common_new/thread.h"
#include "tc_webrtc_client/rtc_client_interface.h"
#include "tc_common_new/message_notifier.h"
#include "tc_client_sdk_new/sdk_messages.h"
#include "tc_client_sdk_new/thunder_sdk.h"
#include "tc_client_sdk_new/connection/relay_connection.h"
#ifdef WIN32
#include <QApplication>
#endif

typedef void *(*FnGetInstance)();

namespace tc
{

    WebRtcConnection::WebRtcConnection(const std::shared_ptr<RelayConnection>& relay_conn,
                                       const std::shared_ptr<ThunderSdkParams>& params,
                                       const std::shared_ptr<MessageNotifier>& notifier)
                                       : Connection(params, notifier) {
        this->relay_conn_ = relay_conn;

        relay_conn_ = relay_conn;
        sdk_params_ = params;
        msg_notifier_ = notifier;
        msg_listener_ = notifier->CreateListener();
        thread_ = Thread::Make("rtc_client_thread", 1024 * 8);
        thread_->Poll();

        msg_listener_->Listen<SdkMsgNetworkConnected>([=, this](const SdkMsgNetworkConnected& msg) {
            LOGI("Sdk msg, network connected.");
        });

        msg_listener_->Listen<SdkMsgRoomPrepared>([=, this](const SdkMsgRoomPrepared& msg) {
            if (sdk_params_->enable_p2p_ && msg.room_type_ == kRoomTypeMedia) {
                LOGI("Sdk msg, room prepared, will init webrtc!");
                this->Init();
            }
        });

        msg_listener_->Listen<SdkMsgRemoteAnswerSdp>([=, this](const SdkMsgRemoteAnswerSdp& msg) {
            this->OnRemoteSdp(msg);
        });

        msg_listener_->Listen<SdkMsgRemoteIce>([=, this](const SdkMsgRemoteIce& msg) {
            this->OnRemoteIce(msg);
        });

        this->LoadRtcLibrary();
    }

    WebRtcConnection::~WebRtcConnection() {

    }

    void WebRtcConnection::Start() {

    }

    void WebRtcConnection::Stop() {
        if (rtc_client_) {
            rtc_client_->Exit();
        }
    }

    void WebRtcConnection::Init() {
        RunInRtcThread([=, this]() {
            if (!rtc_client_) {
                return;
            }

            rtc_client_->SetOnLocalSdpSetCallback([=, this](const std::string& sdp) {
                LOGI("Will send sdp to remote, sdp size: {}", sdp.size());
                this->SendSdpToRemote(sdp);
            });

            rtc_client_->SetOnLocalIceCallback([=, this](const std::string& ice, const std::string& mid, int sdp_mline_index) {
                LOGI("Will send ice to remote: {}", ice);
                this->SendIceToRemote(ice, mid, sdp_mline_index);
            });

            rtc_client_->SetMediaMessageCallback([=, this](std::shared_ptr<Data> msg) {
                if (media_msg_cbk_) {
                    media_msg_cbk_(msg);
                }
            });

            rtc_client_->SetFtMessageCallback([=, this](std::shared_ptr<Data> msg) {
                if (ft_msg_cbk_) {
                    ft_msg_cbk_(msg);
                }
            });

            if (!rtc_client_->Init(sdk_params_->bare_remote_device_id_)) {
                LOGE("RTC client init FAILED!");
                return;
            }

            LOGI("RTC client init success");
        });
    }

    void WebRtcConnection::LoadRtcLibrary() {
        RunInRtcThread([=, this]() {
#ifdef WIN32
            LOGI("Begin to load library!");
            auto lib_name = QApplication::applicationDirPath() + "/gr_client/tc_rtc_client.dll";
            rtc_lib_ = new QLibrary(lib_name);
            auto r = rtc_lib_->load();
            if (!r) {
                LOGE("LOAD rtc conn FAILED");
                return;
            }

            auto fn_get_instance = (FnGetInstance)rtc_lib_->resolve("GetInstance");
            if (!fn_get_instance) {
                LOGE("DON'T have GetInstance");
                return;
            }

            rtc_client_ = (RtcClientInterface*)fn_get_instance();
            if (!rtc_client_) {
                LOGE("Can't get rtc client instance.");
                return;
            }
            LOGI("Load Rtc library success.");
#endif
        });
    }

    RtcClientInterface* WebRtcConnection::GetRtcClient() {
        return rtc_client_;
    }

    void WebRtcConnection::PostMediaMessage(std::shared_ptr<Data> msg) {
        RunInRtcThread([=, this]() {
            if (rtc_client_) {
                rtc_client_->PostMediaMessage(msg);
            }
        });
    }

    void WebRtcConnection::PostFtMessage(std::shared_ptr<Data> msg) {
        if (!rtc_client_) {
            return;
        }

        // test beg //
        if (false) {
            tc::Message pt_msg;
            if (pt_msg.ParseFromArray(msg->CStr(), msg->Size())) {
                if (pt_msg.type() == tc::kFileTransDataPacket) {
                    auto pkt = pt_msg.file_trans_data_packet();
                    LOGI("Send Ft pkt index: {}", pkt.index());
                }
            }
        }
        // test end //

        rtc_client_->PostFtMessage(msg);
    }

    int64_t WebRtcConnection::GetQueuingMediaMsgCount() {
        return rtc_client_ ? rtc_client_->GetQueuingMediaMsgCount() : -1;
    }

    int64_t WebRtcConnection::GetQueuingFtMsgCount() {
        return rtc_client_ ? rtc_client_->GetQueuingFtMsgCount() : -1;
    }

    void WebRtcConnection::SetOnMediaMessageCallback(const std::function<void(std::shared_ptr<Data>)>& cbk) {
        media_msg_cbk_ = cbk;
    }

    void WebRtcConnection::SetOnFtMessageCallback(const std::function<void(std::shared_ptr<Data>)>& cbk) {
        ft_msg_cbk_ = cbk;
    }

    void WebRtcConnection::OnRemoteSdp(const SdkMsgRemoteAnswerSdp& m) {
        RunInRtcThread([=, this]() {
            if (rtc_client_) {
                rtc_client_->OnRemoteSdp(m.answer_sdp_.sdp());
            }
        });
    }

    void WebRtcConnection::OnRemoteIce(const SdkMsgRemoteIce& m) {
        RunInRtcThread([=, this]() {
            if (rtc_client_) {
                auto sub = m.ice_;
                rtc_client_->OnRemoteIce(sub.ice(), sub.mid(), sub.sdp_mline_index());
            }
        });
    }

    void WebRtcConnection::RunInRtcThread(std::function<void()>&& task) {
        thread_->Post([=]() {
            task();
        });
    }

    void WebRtcConnection::SendSdpToRemote(const std::string& sdp) {
        // pack to proto & send it
        tc::Message pt_msg;
        pt_msg.set_device_id(sdk_params_->device_id_);
        pt_msg.set_stream_id(sdk_params_->stream_id_);
        pt_msg.set_type(tc::MessageType::kSigOfferSdpMessage);
        auto sub = pt_msg.mutable_sig_offer_sdp();
        sub->set_device_id(sdk_params_->device_id_);
        sub->set_sdp(sdp);
        auto buffer = Data::Make(nullptr, pt_msg.ByteSizeLong());
        pt_msg.SerializeToArray(buffer->DataAddr(), buffer->Size());
        relay_conn_->PostBinaryMessage(buffer);
    }

    void WebRtcConnection::SendIceToRemote(const std::string& ice, const std::string& mid, int sdp_mline_index) {
        // pack to proto & send it
        tc::Message pt_msg;
        pt_msg.set_device_id(sdk_params_->device_id_);
        pt_msg.set_stream_id(sdk_params_->stream_id_);
        pt_msg.set_type(tc::MessageType::kSigIceMessage);
        auto sub = pt_msg.mutable_sig_ice();
        sub->set_device_id(sdk_params_->device_id_);
        sub->set_ice(ice);
        sub->set_mid(mid);
        sub->set_sdp_mline_index(sdp_mline_index);
        auto buffer = Data::Make(nullptr, pt_msg.ByteSizeLong());
        pt_msg.SerializeToArray(buffer->DataAddr(), buffer->Size());
        relay_conn_->PostBinaryMessage(buffer);
    }

    void WebRtcConnection::PostBinaryMessage(std::shared_ptr<Data> msg) {
        this->PostMediaMessage(msg);
    }

    int64_t WebRtcConnection::GetQueuingMsgCount() {
        return 0;
    }

    void WebRtcConnection::RequestPauseStream() {

    }

    void WebRtcConnection::RequestResumeStream() {

    }

    bool WebRtcConnection::HasEnoughBufferForQueuingMediaMessages() {
        return rtc_client_ && rtc_client_->HasEnoughBufferForQueuingMediaMessages();
    }

    bool WebRtcConnection::HasEnoughBufferForQueuingFtMessages() {
        return rtc_client_ && rtc_client_->HasEnoughBufferForQueuingFtMessages();
    }

    bool WebRtcConnection::IsMediaChannelReady() {
        return rtc_client_ && rtc_client_->IsMediaChannelReady();
    }

    bool WebRtcConnection::IsFtChannelReady() {
        return rtc_client_ && rtc_client_->IsFtChannelReady();
    }

    void WebRtcConnection::On16msTimeout() {
        if (rtc_client_) {
            rtc_client_->On16msTimeout();
        }
    }

}
