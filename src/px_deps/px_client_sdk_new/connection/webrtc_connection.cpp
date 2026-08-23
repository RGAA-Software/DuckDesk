//
// Created by RGAA on 16/04/2025.
//

#include "webrtc_connection.h"
#include "px_message.pb.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/thread.h"
#include "px_webrtc_client/rtc_client_interface.h"
#include "px_common_new/message_notifier.h"
#include "px_client_sdk_new/sdk_messages.h"
#include "px_client_sdk_new/thunder_sdk.h"
#include "px_client_sdk_new/connection/relay_connection.h"
#ifdef WIN32
#include <QApplication>
#endif

typedef void *(*FnGetInstance)();

namespace px
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
        stopped_ = true;
        if (rtc_client_) {
            rtc_client_->Exit();
        }
    }

    void WebRtcConnection::Init() {
        bool expected = false;
        if (!init_started_.compare_exchange_strong(expected, true)) {
            return;
        }
        RunInRtcThread([=, this]() {
            if (!rtc_client_) {
                LOGE("RTC client library is unavailable");
                NotifyDisconnectedOnce();
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

            rtc_client_->SetOnIceStateCallback([=, this](int state) {
                // libwebrtc IceConnectionState: connected=2, completed=3,
                // failed=4, disconnected=5, closed=6. A disconnected state
                // may recover, so only terminal states close the SDK session.
                if (state == 2 || state == 3) {
                    ice_connected_ = true;
                    NotifyConnectedWhenReady();
                }
                else if (state == 4 || state == 6) {
                    ice_connected_ = false;
                    NotifyDisconnectedOnce();
                }
                else if (state == 5) {
                    ice_connected_ = false;
                }
            });

            rtc_client_->SetLocalRtcMode(false);
            rtc_client_->SetFileTransferOnly(sdk_params_->file_transfer_only_);
            rtc_client_->SetIceServersJson(sdk_params_->rtc_ice_config_json_);

            if (!rtc_client_->Init(sdk_params_->bare_remote_device_id_)) {
                LOGE("RTC client init FAILED!");
                NotifyDisconnectedOnce();
                return;
            }

            LOGI("RTC client init success");
        });
    }

    void WebRtcConnection::LoadRtcLibrary() {
        RunInRtcThread([=, this]() {
#ifdef WIN32
            LOGI("Begin to load library!");
            auto lib_name = QApplication::applicationDirPath() + "/px_client_rtc.dll";
            rtc_lib_ = new QLibrary(lib_name);
            auto r = rtc_lib_->load();
            if (!r) {
                LOGE("LOAD rtc conn FAILED");
                NotifyDisconnectedOnce();
                return;
            }

            auto fn_get_instance = (FnGetInstance)rtc_lib_->resolve("GetInstance");
            if (!fn_get_instance) {
                LOGE("DON'T have GetInstance");
                NotifyDisconnectedOnce();
                return;
            }

            rtc_client_ = (RtcClientInterface*)fn_get_instance();
            if (!rtc_client_) {
                LOGE("Can't get rtc client instance.");
                NotifyDisconnectedOnce();
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
        if (msg && rtc_client_ && rtc_client_->IsInputChannelReady()) {
            px::Message proto_msg;
            if (proto_msg.ParseFromArray(msg->DataAddr(), static_cast<int>(msg->Size()))) {
                const auto type = proto_msg.type();
                if (type == px::kKeyEvent || type == px::kMouseEvent
                    || type == px::kGamepadState || type == px::kTextInput) {
                    rtc_client_->PostInputMessage(msg);
                    return;
                }
            }
        }
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
        px::Message pt_msg;
        pt_msg.set_device_id(sdk_params_->device_id_);
        pt_msg.set_stream_id(sdk_params_->stream_id_);
        pt_msg.set_type(px::MessageType::kSigOfferSdpMessage);
        auto sub = pt_msg.mutable_sig_offer_sdp();
        sub->set_device_id(sdk_params_->device_id_);
        sub->set_sdp(sdp);
        sub->set_connection_ticket(sdk_params_->connection_ticket_);
        sub->set_client_nonce(sdk_params_->connection_nonce_);
        sub->set_instance_id(sdk_params_->connection_instance_id_);
        auto buffer = Data::Make(nullptr, pt_msg.ByteSizeLong());
        pt_msg.SerializeToArray(buffer->DataAddr(), buffer->Size());
        relay_conn_->PostBinaryMessage(buffer);
    }

    void WebRtcConnection::SendIceToRemote(const std::string& ice, const std::string& mid, int sdp_mline_index) {
        // pack to proto & send it
        px::Message pt_msg;
        pt_msg.set_device_id(sdk_params_->device_id_);
        pt_msg.set_stream_id(sdk_params_->stream_id_);
        pt_msg.set_type(px::MessageType::kSigIceMessage);
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
        NotifyConnectedWhenReady();
    }

    void WebRtcConnection::NotifyConnectedWhenReady() {
        if (stopped_ || !ice_connected_ || connected_notified_) {
            return;
        }
        const bool channel_ready = sdk_params_->file_transfer_only_
            ? IsFtChannelReady()
            : IsMediaChannelReady();
        if (!channel_ready) {
            return;
        }
        bool expected = false;
        if (connected_notified_.compare_exchange_strong(expected, true) && conn_cbk_) {
            LOGI("Full WebRTC transport is ready");
            conn_cbk_();
        }
    }

    void WebRtcConnection::NotifyDisconnectedOnce() {
        if (stopped_) {
            return;
        }
        bool expected = false;
        if (disconnected_notified_.compare_exchange_strong(expected, true) && dis_conn_cbk_) {
            dis_conn_cbk_();
        }
    }

}
