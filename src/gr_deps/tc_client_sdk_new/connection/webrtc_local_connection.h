//
// Created by RGAA on 10/08/2026.
//

#ifndef GAMMARAY_WEBRTC_LOCAL_CONNECTION_H
#define GAMMARAY_WEBRTC_LOCAL_CONNECTION_H

#include <functional>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>
#ifdef WIN32
#include <QLibrary>
#endif
#include "sdk_params.h"
#include "tc_client_sdk_new/connection/connection.h"

namespace tc
{

    class Data;
    class Thread;
    class Message;
    class MessageNotifier;
    class RtcClientInterface;

    // one entry of the "monitors" array in the /alloc/local/rtc answer,
    // in video track order: track #i shows monitor[i]
    class RtcLocalTrackMonitor {
    public:
        std::string name_;
        int width_ = 0;
        int height_ = 0;
        // virtual desktop rect, needed for multi-monitor layout & mouse mapping
        int left_ = 0;
        int top_ = 0;
        int right_ = 0;
        int bottom_ = 0;
    };

    // WebRTC local(direct) connection, the same link the web_client uses:
    // - signaling: HTTP POST http://{ip}:{port}/alloc/local/rtc, non-trickle(candidates embedded in sdp)
    // - video: RTP track decoded by the built-in webrtc decoder, delivered as packed I420
    // - media/ft messages: webrtc data channels(media_data_channel / ft_data_channel)
    class WebRtcLocalConnection : public Connection {
    public:
        explicit WebRtcLocalConnection(const std::shared_ptr<ThunderSdkParams>& params,
                                       const std::shared_ptr<MessageNotifier>& notifier);
        ~WebRtcLocalConnection() override;

        void Start() override;
        void Stop() override;

        // media channel
        void PostBinaryMessage(std::shared_ptr<Data> msg) override;
        void PostMediaMessage(std::shared_ptr<Data> msg);
        void PostFtMessage(std::shared_ptr<Data> msg);

        // media messages are reported via Connection::RegisterOnMessageCallback
        void SetOnFtMessageCallback(const std::function<void(std::shared_ptr<Data>)>& cbk);
        void SetOnRtcVideoFrameCallback(const std::function<void(int w, int h, std::shared_ptr<Data> i420)>& cbk);

        // encoded-sink mode: synthesized kVideoFrame protos(one per rtp video track,
        // pre-decode H264). Delivered WITHOUT an app-level ack - rtp has its own nack/pli.
        void SetOnVideoMessageCallback(const std::function<void(std::shared_ptr<tc::Message>)>& cbk);

        // decoded audio from the rtp audio track: 16-bit interleaved PCM,
        // played by the sdk's own AudioPlayer(the dll runs a dummy ADM)
        void SetOnAudioDataCallback(const std::function<void(std::shared_ptr<Data> pcm, int sample_rate, int channels)>& cbk);

        // old-render compat(the answer has no "monitors" array): the single dynamic
        // track follows the capturing monitor, reported via ServerConfiguration
        void SetCapturingMonitorNameProvider(std::function<std::string()>&& provider);

        int64_t GetQueuingMsgCount() override;
        int64_t GetQueuingMediaMsgCount();
        int64_t GetQueuingFtMsgCount();

        bool HasEnoughBufferForQueuingMediaMessages();
        bool HasEnoughBufferForQueuingFtMessages();

        bool IsMediaChannelReady();
        bool IsFtChannelReady();
        bool IsAlive() override;

        void On16msTimeout() override;

    private:
        void LoadRtcLibrary();
        void InitRtcClient();
        void RequestAnswerSdp(const std::string& offer_sdp, bool takeover);
        void OnEncodedVideoFrame(int track_index, bool key, int w, int h, std::shared_ptr<Data> encoded);
        std::string MakeSafetyPwdMd5();
        void RunInRtcThread(std::function<void()>&& task);

    private:
        std::shared_ptr<ThunderSdkParams> sdk_params_;
        std::shared_ptr<Thread> thread_ = nullptr;
#ifdef WIN32
        QLibrary* rtc_lib_ = nullptr;
#endif
        RtcClientInterface* rtc_client_ = nullptr;

        std::function<void(std::shared_ptr<Data>)> ft_msg_cbk_;
        std::function<void(int w, int h, std::shared_ptr<Data> i420)> video_frame_cbk_;
        std::function<void(std::shared_ptr<tc::Message>)> video_msg_cbk_;
        std::function<void(std::shared_ptr<Data> pcm, int sample_rate, int channels)> audio_data_cbk_;
        std::function<std::string()> capturing_monitor_provider_;

        // encoded-sink mode per-track state, guarded by track_mtx_
        // (frames arrive on webrtc decoder queue threads, monitors on our own thread)
        std::mutex track_mtx_;
        std::vector<RtcLocalTrackMonitor> track_monitors_;
        std::vector<uint64_t> track_frame_indices_;
        std::vector<bool> track_got_keyframe_;

        std::atomic_bool connected_ = false;
        std::atomic_bool stopped_ = false;
    };

}

#endif //GAMMARAY_WEBRTC_LOCAL_CONNECTION_H
