//
// Created by RGAA on 10/08/2026.
//

#include "webrtc_local_connection.h"
#include "tc_common_new/log.h"
#include "tc_common_new/data.h"
#include "tc_common_new/thread.h"
#include "tc_common_new/time_util.h"
#include "tc_common_new/http_client.h"
#include "tc_common_new/md5.h"
#include "tc_common_new/message_notifier.h"
#include "tc_client_sdk_new/sdk_messages.h"
#include "tc_message.pb.h"
#include "tc_webrtc_client/rtc_client_interface.h"
#include <nlohmann/json.hpp>
#ifdef WIN32
#include <QApplication>
#endif

typedef void *(*FnGetInstance)();

namespace tc
{

    // render side keeps at most one video track per monitor, capped at 4
    // (see kMaxRtcVideoTracks in gr_render/plugins/net_rtc_local/rtc_local_plugin.h)
    static constexpr int kMaxRtcLocalVideoTracks = 4;

    // render side error code, see gr_render/plugins/net_ws/http_handler.cpp(kHandlerErrRtcLocalOccupied)
    static constexpr int kRtcLocalRespOccupied = 704;

    // webrtc::PeerConnectionInterface::IceConnectionState, mirrored as int
    // to keep webrtc types out of the sdk
    static constexpr int kIceStateConnected = 2;    // kIceConnectionConnected
    static constexpr int kIceStateCompleted = 3;    // kIceConnectionCompleted
    static constexpr int kIceStateFailed = 4;       // kIceConnectionFailed
    static constexpr int kIceStateDisconnected = 5; // kIceConnectionDisconnected
    static constexpr int kIceStateClosed = 6;       // kIceConnectionClosed

    WebRtcLocalConnection::WebRtcLocalConnection(const std::shared_ptr<ThunderSdkParams>& params,
                                                 const std::shared_ptr<MessageNotifier>& notifier)
                                                 : Connection(params, notifier) {
        sdk_params_ = params;
        msg_notifier_ = notifier;
        thread_ = Thread::Make("rtc_local_conn", 1024 * 8);
        thread_->Poll();
    }

    WebRtcLocalConnection::~WebRtcLocalConnection() {

    }

    void WebRtcLocalConnection::Start() {
        RunInRtcThread([=, this]() {
            this->LoadRtcLibrary();
            this->InitRtcClient();
        });
    }

    void WebRtcLocalConnection::Stop() {
        if (stopped_.exchange(true)) {
            return;
        }
        if (rtc_client_) {
            rtc_client_->Exit();
        }
        if (thread_) {
            thread_->Exit();
        }
    }

    void WebRtcLocalConnection::LoadRtcLibrary() {
#ifdef WIN32
        LOGI("Begin to load rtc library!");
        auto lib_name = QApplication::applicationDirPath() + "/gr_client/tc_rtc_client.dll";
        rtc_lib_ = new QLibrary(lib_name);
        auto r = rtc_lib_->load();
        if (!r) {
            LOGE("LOAD rtc library FAILED: {}", lib_name.toStdString());
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
        LOGI("Load rtc library success.");
#endif
    }

    void WebRtcLocalConnection::InitRtcClient() {
        if (!rtc_client_) {
            LOGE("Rtc client is null, init failed.");
            return;
        }

        // local(direct) mode: no STUN server, non-trickle,
        // the final offer sdp is reported after ice gathering complete
        rtc_client_->SetLocalRtcMode(true);

        // multi-track: ask for one video track per monitor(capped). an old render
        // just answers a single track, the extra m-lines stay inactive.
        rtc_client_->SetVideoTrackCount(kMaxRtcLocalVideoTracks);

        // encoded-sink mode: video tracks are consumed as pre-decode H264 and decoded
        // by the sdk's own FFmpegVulkanDecoder chain(zero-copy d3d11/pl_vulkan),
        // not by webrtc's built-in software decoder
        rtc_client_->SetOnEncodedVideoFrameCallback([=, this](int track_index, bool key, int w, int h, std::shared_ptr<Data> encoded) {
            this->OnEncodedVideoFrame(track_index, key, w, h, encoded);
        });

        rtc_client_->SetOnLocalSdpSetCallback([=, this](const std::string& sdp) {
            // called on the webrtc signaling thread, hand off to our own thread
            // before doing the blocking http request
            LOGI("Got the final offer sdp, size: {}, will request the answer by http.", sdp.size());
            RunInRtcThread([=, this]() {
                this->RequestAnswerSdp(sdp, false);
            });
        });

        rtc_client_->SetMediaMessageCallback([=, this](std::shared_ptr<Data> msg) {
            if (msg_cbk_) {
                msg_cbk_(msg);
            }
        });

        rtc_client_->SetFtMessageCallback([=, this](std::shared_ptr<Data> msg) {
            if (ft_msg_cbk_) {
                ft_msg_cbk_(msg);
            }
        });

        rtc_client_->SetOnVideoFrameCallback([=, this](int w, int h, std::shared_ptr<Data> i420) {
            if (video_frame_cbk_) {
                video_frame_cbk_(w, h, i420);
            }
        });

        // audio rtp track: decoded PCM via the dll's AudioSinkInterface(dummy ADM
        // would discard it otherwise), played by the sdk's own AudioPlayer
        rtc_client_->SetOnAudioDataCallback([=, this](std::shared_ptr<Data> pcm, int sample_rate, int channels) {
            if (audio_data_cbk_) {
                audio_data_cbk_(pcm, sample_rate, channels);
            }
        });

        rtc_client_->SetOnIceStateCallback([=, this](int state) {
            LOGI("Rtc local, ice state changed: {}", state);
            if (state == kIceStateConnected || state == kIceStateCompleted) {
                RunInRtcThread([=, this]() {
                    // ice connected doesn't mean the sctp data channels are open yet,
                    // wait for the media channel before reporting connected(hello goes through it)
                    for (int i = 0; i < 100 && !stopped_; ++i) {
                        if (this->IsMediaChannelReady()) {
                            break;
                        }
                        TimeUtil::DelayBySleep(50);
                    }
                    if (stopped_) {
                        return;
                    }
                    if (!connected_.exchange(true)) {
                        LOGI("Rtc local, connected.");
                        if (conn_cbk_) {
                            conn_cbk_();
                        }
                    }
                });
            }
            else if (state == kIceStateDisconnected) {
                // 与 render 端 peer_callback 保持一致:ICE Disconnected 是瞬态,
                // WebRTC 通常还能自行恢复到 Connected/Completed。这里不弹“断开重连”,
                // 否则登录/显示切换或 take-over 的短暂抖动都会打断用户会话。
                LOGI("Rtc local ice transient disconnected, keep connection state.");
            }
            else if (state == kIceStateFailed || state == kIceStateClosed) {
                if (stopped_) {
                    return;
                }
                if (connected_.exchange(false)) {
                    LOGW("Rtc local, disconnected, ice state: {}", state);
                    if (dis_conn_cbk_) {
                        dis_conn_cbk_();
                    }
                }
            }
        });

        if (!rtc_client_->Init(sdk_params_->bare_remote_device_id_)) {
            LOGE("Rtc local client init FAILED!");
            return;
        }
        LOGI("Rtc local client init success.");
    }

    void WebRtcLocalConnection::RequestAnswerSdp(const std::string& offer_sdp, bool takeover) {
        if (stopped_) {
            return;
        }

        std::map<std::string, std::string> query;
        query["device_id"] = sdk_params_->bare_remote_device_id_;
        query["stream_id"] = sdk_params_->stream_id_;
        auto pwd_md5 = MakeSafetyPwdMd5();
        if (!pwd_md5.empty()) {
            query["safety_pwd_md5"] = pwd_md5;
        }
        if (takeover) {
            query["takeover"] = "1";
        }

        nlohmann::json body;
        body["sdp"] = offer_sdp;

        auto client = HttpClient::Make(sdk_params_->ip_, sdk_params_->port_, "/alloc/local/rtc", 15000);
        auto resp = client->Post(query, body.dump(), "application/json");
        if (resp.status != 200) {
            LOGE("Request rtc local answer failed, http status: {}, error: {}", resp.status, resp.error_message);
            if (resp.status == 403 && msg_notifier_) {
                // wrong device password(random or safety) - let the UI tell the user instead of hanging
                msg_notifier_->SendAppMessage(SdkMsgRtcLocalAuthFailed{});
                this->Stop();
            }
            return;
        }

        int code = -1;
        std::string message;
        std::string answer_sdp;
        try {
            auto obj = nlohmann::json::parse(resp.body);
            code = obj.value("code", -1);
            message = obj.value("message", "");
            if (obj.contains("data") && obj["data"].is_object()) {
                answer_sdp = obj["data"].value("answer_sdp", "");
                // track→monitor mapping(multi-track render). missing on old renders,
                // then the single track falls back to the capturing monitor from config
                if (obj["data"].contains("monitors") && obj["data"]["monitors"].is_array()) {
                    std::lock_guard<std::mutex> guard(track_mtx_);
                    track_monitors_.clear();
                    for (const auto& m : obj["data"]["monitors"]) {
                        track_monitors_.push_back(RtcLocalTrackMonitor {
                            .name_ = m.value("name", ""),
                            .width_ = m.value("width", 0),
                            .height_ = m.value("height", 0),
                            .left_ = m.value("left", 0),
                            .top_ = m.value("top", 0),
                            .right_ = m.value("right", 0),
                            .bottom_ = m.value("bottom", 0),
                        });
                    }
                    track_frame_indices_.assign(track_monitors_.size(), 0);
                    track_got_keyframe_.assign(track_monitors_.size(), false);
                }
            }
        }
        catch (std::exception& e) {
            LOGE("Parse rtc local answer failed: {}, body: {}", e.what(), resp.body);
            return;
        }

        if (code == kRtcLocalRespOccupied && !takeover) {
            // occupied by another connection, retry once with takeover=1
            LOGW("Rtc local connection is occupied, retry with takeover=1.");
            this->RequestAnswerSdp(offer_sdp, true);
            return;
        }
        if (code != 200) {
            LOGE("Request rtc local answer rejected, code: {}, message: {}", code, message);
            return;
        }
        if (answer_sdp.empty()) {
            LOGE("Rtc local answer sdp is empty.");
            return;
        }

        LOGI("Got rtc local answer sdp, size: {}, will set remote desc.", answer_sdp.size());
        {
            std::lock_guard<std::mutex> guard(track_mtx_);
            if (!track_monitors_.empty()) {
                LOGI("Rtc local multi-track, {} monitor track(s):", track_monitors_.size());
                for (size_t i = 0; i < track_monitors_.size(); ++i) {
                    const auto& m = track_monitors_[i];
                    LOGI("  track #{} -> {} {}x{} @({},{})-({},{})", i, m.name_,
                         m.width_, m.height_, m.left_, m.top_, m.right_, m.bottom_);
                }
            }
            else {
                LOGI("Rtc local answer has no monitors info, single track, fallback to capturing monitor name.");
            }
        }
        if (rtc_client_) {
            rtc_client_->OnRemoteSdp(answer_sdp);
        }
    }

    void WebRtcLocalConnection::OnEncodedVideoFrame(int track_index, bool key, int w, int h, std::shared_ptr<Data> encoded) {
        if (stopped_ || !video_msg_cbk_ || !encoded || encoded->Size() == 0) {
            return;
        }

        RtcLocalTrackMonitor mon;
        uint64_t frame_index = 0;
        {
            std::lock_guard<std::mutex> guard(track_mtx_);
            if (track_index >= 0 && track_index < (int)track_monitors_.size()) {
                // multi-track render: track #i is monitors[i]
                mon = track_monitors_[track_index];
            }
            else {
                // old render: single dynamic track following the capturing monitor.
                // the name comes from ServerConfiguration; before the first config
                // arrives the name is empty and frames are dropped(a key frame is
                // re-requested by the pipeline, so nothing is lost permanently)
                mon.name_ = capturing_monitor_provider_ ? capturing_monitor_provider_() : "";
                mon.width_ = w;
                mon.height_ = h;
                mon.right_ = w;
                mon.bottom_ = h;
                track_index = 0;
                if ((int)track_frame_indices_.size() <= track_index) {
                    track_frame_indices_.resize(track_index + 1, 0);
                    track_got_keyframe_.resize(track_index + 1, false);
                }
            }
            if (mon.name_.empty()) {
                return;
            }
            // the sdk decode chain inits a decoder with the FIRST frame it sees for
            // a monitor - that must be an IDR. AddEncodedSink requests a key frame
            // when attached, until it arrives drop deltas.
            if (!track_got_keyframe_[track_index]) {
                if (!key) {
                    return;
                }
                track_got_keyframe_[track_index] = true;
                LOGI("Rtc local track #{}({}): first key frame, start feeding the decoder chain.",
                     track_index, mon.name_);
            }
            frame_index = ++track_frame_indices_[track_index];
        }

        // synthesize the exact kVideoFrame proto the relay/ws path delivers,
        // so the standard per-monitor decode chain picks it up unchanged
        auto msg = std::make_shared<tc::Message>();
        msg->set_type(tc::kVideoFrame);
        auto* frame = msg->mutable_video_frame();
        frame->set_type(tc::kNetH264);
        frame->set_data(encoded->CStr(), encoded->Size());
        frame->set_frame_index(frame_index);
        frame->set_key(key);
        // the encoded resolution may be 0x0(not always parsed), fall back to the monitor rect
        frame->set_frame_width(w > 0 ? w : mon.width_);
        frame->set_frame_height(h > 0 ? h : mon.height_);
        frame->set_mon_name(mon.name_);
        frame->set_mon_left(mon.left_);
        frame->set_mon_top(mon.top_);
        frame->set_mon_right(mon.right_);
        frame->set_mon_bottom(mon.bottom_);
        frame->set_mon_index(track_index);
        frame->set_image_format(tc::kI420);
        // debug tag: lets the sdk side tell synthesized frames apart from any
        // other kVideoFrame producer(see "Video frame came" log in thunder_sdk)
        frame->set_extra("rtc_synth");

        video_msg_cbk_(msg);
    }

    void WebRtcLocalConnection::SetOnVideoMessageCallback(const std::function<void(std::shared_ptr<tc::Message>)>& cbk) {
        video_msg_cbk_ = cbk;
    }

    void WebRtcLocalConnection::SetOnAudioDataCallback(const std::function<void(std::shared_ptr<Data>, int, int)>& cbk) {
        audio_data_cbk_ = cbk;
    }

    void WebRtcLocalConnection::SetCapturingMonitorNameProvider(std::function<std::string()>&& provider) {
        capturing_monitor_provider_ = std::move(provider);
    }

    std::string WebRtcLocalConnection::MakeSafetyPwdMd5() {
        // priority: safety password(already in md5 form) > random password(plain, md5 it)
        if (!sdk_params_->remote_device_safety_pwd_.empty()) {
            return sdk_params_->remote_device_safety_pwd_;
        }
        if (!sdk_params_->remote_device_random_pwd_.empty()) {
            return MD5::Hex(sdk_params_->remote_device_random_pwd_);
        }
        return "";
    }

    void WebRtcLocalConnection::PostBinaryMessage(std::shared_ptr<Data> msg) {
        this->PostMediaMessage(msg);
    }

    void WebRtcLocalConnection::PostMediaMessage(std::shared_ptr<Data> msg) {
        if (!rtc_client_) {
            return;
        }
        // keyboard/mouse go through the dedicated input channel: the render dispatches
        // it via the direct fast path(CallbackEventDirectly) instead of queueing on the
        // plugin work thread. channel is reliable+ordered, no events are lost.
        if (msg && rtc_client_->IsInputChannelReady()) {
            tc::Message proto_msg;
            if (proto_msg.ParseFromArray(msg->DataAddr(), (int)msg->Size())) {
                const auto type = proto_msg.type();
                if (type == tc::kKeyEvent || type == tc::kMouseEvent) {
                    rtc_client_->PostInputMessage(msg);
                    return;
                }
            }
        }
        rtc_client_->PostMediaMessage(msg);
    }

    void WebRtcLocalConnection::PostFtMessage(std::shared_ptr<Data> msg) {
        if (rtc_client_) {
            rtc_client_->PostFtMessage(msg);
        }
    }

    void WebRtcLocalConnection::SetOnFtMessageCallback(const std::function<void(std::shared_ptr<Data>)>& cbk) {
        ft_msg_cbk_ = cbk;
    }

    void WebRtcLocalConnection::SetOnRtcVideoFrameCallback(const std::function<void(int w, int h, std::shared_ptr<Data> i420)>& cbk) {
        video_frame_cbk_ = cbk;
    }

    int64_t WebRtcLocalConnection::GetQueuingMsgCount() {
        return this->GetQueuingMediaMsgCount();
    }

    int64_t WebRtcLocalConnection::GetQueuingMediaMsgCount() {
        return rtc_client_ ? rtc_client_->GetQueuingMediaMsgCount() : 0;
    }

    int64_t WebRtcLocalConnection::GetQueuingFtMsgCount() {
        return rtc_client_ ? rtc_client_->GetQueuingFtMsgCount() : 0;
    }

    bool WebRtcLocalConnection::HasEnoughBufferForQueuingMediaMessages() {
        return rtc_client_ && rtc_client_->HasEnoughBufferForQueuingMediaMessages();
    }

    bool WebRtcLocalConnection::HasEnoughBufferForQueuingFtMessages() {
        return rtc_client_ && rtc_client_->HasEnoughBufferForQueuingFtMessages();
    }

    bool WebRtcLocalConnection::IsMediaChannelReady() {
        return rtc_client_ && rtc_client_->IsMediaChannelReady();
    }

    bool WebRtcLocalConnection::IsFtChannelReady() {
        return rtc_client_ && rtc_client_->IsFtChannelReady();
    }

    bool WebRtcLocalConnection::IsAlive() {
        return connected_.load();
    }

    void WebRtcLocalConnection::On16msTimeout() {
        if (rtc_client_) {
            rtc_client_->On16msTimeout();
        }
    }

    void WebRtcLocalConnection::RunInRtcThread(std::function<void()>&& task) {
        thread_->Post(std::move(task));
    }

}
