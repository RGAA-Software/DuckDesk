//
// Created by RGAA on 16/04/2025.
//

#ifndef PX_WEBRTC_CONNECTION_H
#define PX_WEBRTC_CONNECTION_H

#include <functional>
#include <atomic>
#include <string>
#ifdef WIN32
#include <QLibrary>
#endif
#include "sdk_params.h"
#include "sdk_messages.h"
#include "px_client_sdk_new/connection/connection.h"
#include "px_common_new/async_runtime.h"

namespace px
{

    class Data;
    class Thread;
    class Message;
    // we need relay to exchange signaling messages
    class RelayConnection;
    class MessageNotifier;
    class RtcClientInterface;
    class MessageListener;
    class RtcIceRestartWorkflow;
    struct RtcIceRestartBegin;

    class WebRtcConnection : public Connection,
                             public std::enable_shared_from_this<WebRtcConnection> {
    public:
        static std::shared_ptr<WebRtcConnection> Make(
            const std::shared_ptr<RelayConnection>& relay_conn,
            const std::shared_ptr<ThunderSdkParams>& params,
            const std::shared_ptr<MessageNotifier>& notifier);

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
        void SetOnVideoFrameCallback(
            const std::function<void(int w, int h, std::shared_ptr<Data> i420)>&);
        void SetOnAudioDataCallback(
            const std::function<void(std::shared_ptr<Data> pcm, int sample_rate, int channels)>&);

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
        bool RestartIce(const std::string& ice_config_json,
                        const std::string& connection_ticket,
                        const std::string& client_nonce,
                        const std::string& instance_id,
                        std::uint64_t revision);

    private:
        void Init();
        void Prepare();
        void LoadRtcLibrary();
        void OnRemoteSdp(const SdkMsgRemoteAnswerSdp& m);
        void OnRemoteIce(const SdkMsgRemoteIce& m);

        void SendSdpToRemote(const std::string& sdp);
        void SendIceToRemote(const std::string& ice, const std::string& mid, int sdp_mline_index);
        void NotifyConnectedWhenReady();
        void NotifyDisconnectedOnce();
        void UpdateTransportStats(const std::string& json);
        void BeginManagedIceRestart();
        bool SpawnManagedIceRestartWait(const RtcIceRestartBegin& begin);
        static PxAwaitable<void> AwaitManagedIceRestart(
            std::weak_ptr<WebRtcConnection> weak_connection,
            std::shared_ptr<RtcIceRestartWorkflow> workflow,
            RtcIceRestartBegin begin);

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
        std::shared_ptr<PxAsyncScope> async_scope_ = nullptr;
        std::shared_ptr<RtcIceRestartWorkflow> ice_restart_workflow_ = nullptr;

        std::function<void(std::shared_ptr<Data>)> media_msg_cbk_;
        std::function<void(std::shared_ptr<Data>)> ft_msg_cbk_;
        std::function<void(int w, int h, std::shared_ptr<Data> i420)> video_frame_cbk_;
        std::function<void(std::shared_ptr<Data> pcm, int sample_rate, int channels)> audio_data_cbk_;
        std::atomic_bool init_started_{false};
        std::atomic_bool ice_connected_{false};
        std::atomic_bool connected_notified_{false};
        std::atomic_bool disconnected_notified_{false};
        std::atomic_bool stopped_{false};
        std::atomic_bool first_video_frame_forwarded_{false};

    };

}

#endif //PX_WEBRTC_CONNECTION_H
