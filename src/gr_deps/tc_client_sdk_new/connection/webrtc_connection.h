//
// Created by RGAA on 16/04/2025.
//

#ifndef GAMMARAY_WEBRTC_CONNECTION_H
#define GAMMARAY_WEBRTC_CONNECTION_H

#include <functional>
#include <string>
#ifdef WIN32
#include <QLibrary>
#endif
#include "sdk_params.h"
#include "sdk_messages.h"
#include "tc_client_sdk_new/connection/connection.h"

namespace tc
{

    class Data;
    class Thread;
    class Message;
    // we need relay to exchange signaling messages
    class RelayConnection;
    class MessageNotifier;
    class RtcClientInterface;
    class MessageListener;

    class WebRtcConnection : public Connection {
    public:
        explicit WebRtcConnection(const std::shared_ptr<RelayConnection>& relay_conn,
                                  const std::shared_ptr<ThunderSdkParams>& params,
                                  const std::shared_ptr<MessageNotifier>& notifier);
        ~WebRtcConnection();

        void Start() override;
        void Stop() override;
        // @see PostMediaMessage
        void PostBinaryMessage(std::shared_ptr<Data> msg) override;

        void PostMediaMessage(std::shared_ptr<Data> msg);
        void PostFtMessage(std::shared_ptr<Data> msg);
        //
        void SetOnMediaMessageCallback(const std::function<void(std::shared_ptr<Data>)>&);
        void SetOnFtMessageCallback(const std::function<void(std::shared_ptr<Data>)>&);

        RtcClientInterface* GetRtcClient();

        // @Deprecated HERE!!
        // DON'T USE IN RTC MODE
        int64_t GetQueuingMsgCount() override;

        // USE THESE
        int64_t GetQueuingMediaMsgCount();
        int64_t GetQueuingFtMsgCount();

        void RequestPauseStream() override;
        void RequestResumeStream() override;

        bool HasEnoughBufferForQueuingMediaMessages();
        bool HasEnoughBufferForQueuingFtMessages();

        bool IsMediaChannelReady();
        bool IsFtChannelReady();

        void On16msTimeout() override;

    private:
        void Init();
        void LoadRtcLibrary();
        void OnRemoteSdp(const SdkMsgRemoteAnswerSdp& m);
        void OnRemoteIce(const SdkMsgRemoteIce& m);

        void SendSdpToRemote(const std::string& sdp);
        void SendIceToRemote(const std::string& ice, const std::string& mid, int sdp_mline_index);

        void RunInRtcThread(std::function<void()>&&);

    private:
        std::shared_ptr<RelayConnection> relay_conn_ = nullptr;

        std::shared_ptr<ThunderSdkParams> sdk_params_;
        std::shared_ptr<Thread> thread_ = nullptr;
#ifdef WIN32
        QLibrary* rtc_lib_ = nullptr;
#endif
        RtcClientInterface* rtc_client_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;

        std::function<void(std::shared_ptr<Data>)> media_msg_cbk_;
        std::function<void(std::shared_ptr<Data>)> ft_msg_cbk_;

    };

}

#endif //GAMMARAY_WEBRTC_CONNECTION_H
