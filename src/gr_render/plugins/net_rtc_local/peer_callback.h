//
// Created by hy on 2024/2/18.
//

#ifndef PEER_CALLBACK_H
#define PEER_CALLBACK_H

#include "tc_common_new/webrtc_helper.h"
#include <nlohmann/json.hpp>

using namespace nlohmann;

namespace tc
{

    using OnCreateSdpSuccessCallback = std::function<void(webrtc::SessionDescriptionInterface*)>;
    using OnCreateSdpFailedCallback = std::function<void(const std::string&)>;
    using OnSetSdpSuccessCallback = std::function<void()>;
    using OnSetSdpFailedCallback = std::function<void(const std::string&)>;
    using OnIceCallback = std::function<void(const std::string& ice, const std::string& mid, int sdp_mline_index)>;
    using OnDataChannelCallback = std::function<void(const std::string& name, rtc::scoped_refptr<webrtc::DataChannelInterface> ch)>;
    using OnIceConnectedCallback = std::function<void()>;
    using OnIceDisConnectedCallback = std::function<void()>;
    // ICE 进入终态(Failed/Closed,不可恢复);Disconnected 为瞬态,不触发此回调
    using OnIceTerminalCallback = std::function<void()>;
    using OnIceGatherCompletedCallback = std::function<void()>;
    // 远端新增/移除音频轨(浏览器麦克风上行)
    using OnAudioTrackCallback = std::function<void(rtc::scoped_refptr<webrtc::AudioTrackInterface> track)>;

    class RtcServer;

    class PeerCallback : public webrtc::PeerConnectionObserver {
    public:

        static std::shared_ptr<PeerCallback> Make(const std::shared_ptr<RtcServer>& client);

        explicit PeerCallback(const std::shared_ptr<RtcServer>& client);

        // PeerConnection overrides
        void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override;
        void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
        void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
        void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override;
        void OnRenegotiationNeeded() override;
        void OnNegotiationNeededEvent(uint32_t event_id) override;
        void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
        void OnStandardizedIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
        void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
        void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override;
        void OnIceCandidate(const webrtc::IceCandidateInterface *candidate) override;
        void OnIceCandidateError(const std::string &address, int port, const std::string &url, int error_code,
                                 const std::string &error_text) override;
        void OnIceCandidatesRemoved(const std::vector<cricket::Candidate> &candidates) override;
        void OnIceConnectionReceivingChange(bool receiving) override;
        void OnIceSelectedCandidatePairChanged(const cricket::CandidatePairChangeEvent &event) override;
        void OnAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                        const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>> &streams) override;
        void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
        void OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;
        void OnInterestingUsage(int usage_pattern) override;

        //
        void SetOnIceCallback(OnIceCallback&& cbk) {
            ice_callback_ = cbk;
        }

        void SetOnDataChannelCallback(OnDataChannelCallback&& cbk) {
            ch_callback_ = cbk;
        }

        void SetOnIceConnectedCallback(OnIceConnectedCallback&& cbk) {
            ice_conn_cbk_ = cbk;
        }

        void SetOnIceDisConnectedCallback(OnIceDisConnectedCallback&& cbk) {
            ice_disconn_cbk_ = cbk;
        }

        void SetOnIceTerminalCallback(OnIceTerminalCallback&& cbk) {
            ice_terminal_cbk_ = cbk;
        }

        void SetOnIceGatherCompletedCallback(OnIceGatherCompletedCallback&& cbk) {
            ice_gather_completed_cbk_ = cbk;
        }

        void SetOnAudioTrackCallback(OnAudioTrackCallback&& cbk) {
            audio_track_cbk_ = cbk;
        }

        void SetOnRemoveAudioTrackCallback(OnAudioTrackCallback&& cbk) {
            remove_audio_track_cbk_ = cbk;
        }

    private:
        OnIceCallback ice_callback_;
        OnDataChannelCallback ch_callback_;
        OnIceConnectedCallback  ice_conn_cbk_;
        OnIceDisConnectedCallback ice_disconn_cbk_;
        OnIceTerminalCallback ice_terminal_cbk_;
        OnIceGatherCompletedCallback ice_gather_completed_cbk_;
        OnAudioTrackCallback audio_track_cbk_;
        OnAudioTrackCallback remove_audio_track_cbk_;
    };

    class CreateSessCallback : public webrtc::CreateSessionDescriptionObserver {
    public:
        static rtc::scoped_refptr<CreateSessCallback> Make(const std::shared_ptr<RtcServer>& srv);
        explicit CreateSessCallback(const std::shared_ptr<RtcServer>& srv);
        void OnSuccess(webrtc::SessionDescriptionInterface *desc) override;
        void OnFailure(webrtc::RTCError error) override;

        void SetOnCreateSdpSuccessCallback(OnCreateSdpSuccessCallback&& cbk) {
            cbk_success_ = cbk;
        }

        void SetOnCreateSdpFailedCallback(OnCreateSdpFailedCallback&& cbk) {
            cbk_failed_ = cbk;
        }
    private:
        OnCreateSdpSuccessCallback cbk_success_;
        OnCreateSdpFailedCallback cbk_failed_;
    };

    class SetSessCallback : public webrtc::SetSessionDescriptionObserver {
    public:
        static rtc::scoped_refptr<SetSessCallback> Make(const std::shared_ptr<RtcServer>& srv);
        explicit SetSessCallback(const std::shared_ptr<RtcServer>& srv);
        void OnSuccess() override;
        void OnFailure(webrtc::RTCError error) override;

        void SetSdpSuccessCallback(OnSetSdpSuccessCallback&& cbk) {
            cbk_success_ = cbk;
        }

        void SetSdpFailedCallback(OnSetSdpFailedCallback&& cbk) {
            cbk_failed_ = cbk;
        }
    private:
        OnSetSdpSuccessCallback cbk_success_;
        OnSetSdpFailedCallback cbk_failed_;

    };

}

#endif //PEER_CALLBACK_H
