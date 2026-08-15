//
// Created by RGAA on 2023/12/26.
//

#ifndef TC_CLIENT_PC_THUNDERSDK_H
#define TC_CLIENT_PC_THUNDERSDK_H

#include <iostream>
#include <string>
#include <mutex>
#include <atomic>

#include "tc_message.pb.h"
#include "sdk_params.h"
#include "sdk_messages.h"
#include "sdk_net_client.h"
#include "sdk_decoder_render_type.h"

namespace tc
{
    class Data;
    class Thread;
    class VideoDecoder;
    class RawImage;
    class MessageNotifier;
    class MessageListener;
    class OpusAudioDecoder;
    class WebRtcClient;
    class CastReceiver;
    class SdkTimer;
    class SdkStatistics;
    class File;

    // [LAT-roundtrip] 客户端最近一次发送鼠标事件的单调时间戳(µs),由 ct_video_widget 写入,
    // thunder_sdk 在解码出帧时读取,计算「鼠标发出 -> 看到新帧」的操作往返延迟。
    extern std::atomic<uint64_t> g_last_mouse_send_us;

    // callbacks
    using OnVideoFrameDecodedCallback = std::function<void(std::shared_ptr<RawImage>, const SdkCaptureMonitorInfo&)>;
    using OnAudioFrameDecodedCallback = std::function<void(std::shared_ptr<Data>, int samples, int channels, int bits)>;
    using OnVideoFrameDecodeThreadDiscardedCallback = std::function<void()>;

    class ThunderSdk : public std::enable_shared_from_this<ThunderSdk> {
    public:

        static std::shared_ptr<ThunderSdk> Make(const std::shared_ptr<MessageNotifier>& notifier);

        explicit ThunderSdk(const std::shared_ptr<MessageNotifier>& notifier);
        ~ThunderSdk();

        bool Init(const std::shared_ptr<ThunderSdkParams>& params, void* surface, const DecoderRenderType& drt);
        void Start();
        void Exit();

        void SetOnVideoFrameDecodedCallback(OnVideoFrameDecodedCallback&& cbk) { this->video_frame_cbk_ = std::move(cbk); }
        void SetOnAudioFrameDecodedCallback(OnAudioFrameDecodedCallback&& cbk) { this->audio_frame_cbk_ = std::move(cbk); }
        void SetOnAudioSpectrumCallback(OnAudioSpectrumCallback&& cbk);
        void SetOnCursorInfoCallback(OnCursorInfoSyncMsgCallback&& cbk);
        void SetOnHeartBeatCallback(OnHeartBeatInfoCallback&& cbk);
        void SetOnClipboardCallback(OnClipboardInfoCallback&& cbk);
        void SetOnServerConfigurationCallback(OnConfigCallback&& cbk);
        void SetOnMonitorSwitchedCallback(OnMonitorSwitchedCallback&& cbk);
        void SetOnRawMessageCallback(OnRawMessageCallback&& cbk);
        void SetOnVideoFrameDecodeThreadDiscardedCallback(OnVideoFrameDecodeThreadDiscardedCallback&& cbk);

        void PostMediaMessage(std::shared_ptr<Data> msg);
        void PostFileTransferMessage(std::shared_ptr<Data> msg);
        void PostVideoTask(std::function<void()>&& task, int64_t frame_index, const std::string& monitor_name);
        void PostAudioTask(std::function<void()>&& task);
        void PostMiscTask(std::function<void()>&& task);

        int GetProgressSteps() const;
        std::shared_ptr<ThunderSdkParams> GetSdkParams();
        std::shared_ptr<MessageNotifier> GetMessageNotifier();
        int64_t GetQueuingMediaMsgCount();
        int64_t GetQueuingFtMsgCount();
        // retry connection
        void RetryConnection();

        // last update timestamp
        uint64_t GetLastHeartbeatTimestamp();

    private:
        void SendFirstFrameMessage(std::shared_ptr<RawImage> image, const SdkCaptureMonitorInfo& info);
        void OnRtcLocalVideoFrame(int w, int h, std::shared_ptr<Data> i420);
        void RegisterEventListeners();
        void SendHelloMessage();
        void RequestIFrame();
        void ReportStatistics();
        void ClearFirstFrameState();
        void IncreaseDecodeFailedCount(const std::string& mon_name);
        int GetDecodeFailedCount(const std::string& mon_name);
        void ResetDecodeFailedCount(const std::string& mon_name);
        void DisableHardwareDecoder(const std::string& mon_name);
        bool IsDisabledHardwareDecoder(const std::string& mon_name);

    private:
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        std::shared_ptr<ThunderSdkParams> sdk_params_;
        std::shared_ptr<NetClient> net_client_ = nullptr;
        std::map<std::string, std::shared_ptr<VideoDecoder>> video_decoders_;
        std::map<std::string, int> decode_failed_counts_;
        std::map<std::string, bool> hw_disabled_states_;

        // for android
        void* render_surface_ = nullptr;

        // callbacks
        OnVideoFrameDecodedCallback video_frame_cbk_ = nullptr;
        OnAudioFrameDecodedCallback audio_frame_cbk_ = nullptr;
        OnAudioSpectrumCallback audio_spectrum_cbk_ = nullptr;
        OnVideoFrameDecodeThreadDiscardedCallback  video_frame_thread_discarded_cbk_ = nullptr;

        DecoderRenderType drt_;
        bool exit_ = false;

        std::shared_ptr<OpusAudioDecoder> audio_decoder_ = nullptr;
        bool debug_audio_decoder_ = false;

        std::shared_ptr<WebRtcClient> webrtc_client_ = nullptr;
        std::shared_ptr<CastReceiver> cast_receiver_ = nullptr;
        std::shared_ptr<SdkTimer> sdk_timer_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        SdkStatistics* statistics_ = nullptr;
        std::shared_ptr<Thread> video_thread_ = nullptr;
        std::shared_ptr<Thread> audio_thread_ = nullptr;
        std::shared_ptr<Thread> misc_thread_ = nullptr;

        std::map<std::string, uint64_t> last_received_video_timestamps_ ;

        std::atomic_bool has_config_msg_ = false;
        std::atomic_bool has_video_frame_msg_ = false;

        bool need_clear_video_tasks_ = false;

        std::map<std::string, int64_t> last_frame_indices_;

        // save received stream
        std::map<std::string, std::shared_ptr<File>> received_files_;

        // last heartbeat callback
        uint64_t last_heartbeat_callback_ = 0;

        // capturing monitor name from ServerConfiguration, used by the webrtc local
        // video frames so that mouse events carry the REAL monitor name
        // (render's event replayer drops events with unknown monitor names)
        std::mutex rtc_cap_mon_mtx_;
        std::string rtc_capturing_monitor_name_;
    };

}

#endif //TC_CLIENT_PC_THUNDERSDK_H
