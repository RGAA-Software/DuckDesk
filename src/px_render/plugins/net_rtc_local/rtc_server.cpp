//
// Created by hy on 2024/4/25.
//

#include "rtc_server.h"
#include "peer_callback.h"
#include "rtc_local_plugin.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "rtc_data_channel.h"
#include "px_common_new/log.h"
#include "rtc_video_encoder_factory.h"
#include "video_source_impl.h"
#include "audio_source_impl.h"
#include "remote_audio_sink.h"
#include "px_common_new/data.h"
#include "px_common_new/time_util.h"
#include <atomic>
#include <format>

using namespace webrtc;

namespace px
{

    std::shared_ptr<RtcServer> RtcServer::Make(RtcLocalPlugin* plugin) {
        return std::make_shared<RtcServer>(plugin);
    }

    RtcServer::RtcServer(RtcLocalPlugin* plugin) {
        plugin_ = plugin;
    }

    RtcLocalPlugin* RtcServer::GetPlugin() {
        return plugin_;
    }

    bool RtcServer::Start(const std::string& stream_id, const std::string& offer_sdp) {
        this->stream_id_ = stream_id;
        this->offer_sdp_ = offer_sdp;
        webrtc::field_trial::InitFieldTrialsFromString("");
        rtc::LogMessage::LogToDebug(rtc::LS_ERROR);
        rtc::InitializeSSL();

        set_remote_offer_sdp_callback_ = SetSessCallback::Make(shared_from_this());
        set_local_answer_sdp_callback_ = SetSessCallback::Make(shared_from_this());
        create_answer_callback_ = CreateSessCallback::Make(shared_from_this());
        peer_callback_ = PeerCallback::Make(shared_from_this());

        // set remote offer sdp
        set_remote_offer_sdp_callback_->SetSdpSuccessCallback([=]() {
            LOGI("Set remote sdp success");
        });

        set_remote_offer_sdp_callback_->SetSdpFailedCallback([=](const std::string& m) {
            LOGE("Set remote sdp failed: {}", m);
        });

        // set local answer sdp
        set_local_answer_sdp_callback_->SetSdpSuccessCallback([=]() {
            LOGI("Set local answer sdp success.");
        });

        set_local_answer_sdp_callback_->SetSdpFailedCallback([=, this](const std::string& m) {
            LOGI("Set local answer sdp failed:{}", m);
        });

        // create answer sdp callback
        create_answer_callback_->SetOnCreateSdpSuccessCallback([=, this](webrtc::SessionDescriptionInterface* desc) {
            peer_conn_->SetLocalDescription(this->set_local_answer_sdp_callback_.get(), desc);
        });

        create_answer_callback_->SetOnCreateSdpFailedCallback([=, this](const std::string& m) {
            LOGE("Create answer sdp failed: {}", m);
            if (answer_sdp_callback_) {
                answer_sdp_callback_("");
            }
        });

        // peer connection
        peer_callback_->SetOnIceCallback([=, this](const std::string& ice, const std::string& mid, int sdp_mline_index) {
            LOGI("ICE: {}", ice);
            this->SendIceToRemote(ice, mid, sdp_mline_index);
        });

        peer_callback_->SetOnDataChannelCallback([=, this](const std::string& name, rtc::scoped_refptr<webrtc::DataChannelInterface> ch) {
            if (name == "media_data_channel") {
                media_data_channel_ = std::make_shared<RtcDataChannel>(name, shared_from_this(), ch);

                // data callback
                media_data_channel_->SetOnDataCallback([=, this](const std::string& data) {
                    auto payload_msg = Data::Make(data.data(), data.size());
                    plugin_->OnClientEventCame(true, 0, NetPluginType::kWebRtc, NetChannelType::kMedia, payload_msg);
                });
            }
            else if (name == "ft_data_channel") {
                ft_data_channel_ = std::make_shared<RtcDataChannel>(name, shared_from_this(), ch);

                // data callback
                ft_data_channel_->SetOnDataCallback([=, this](const std::string& data) {
                    auto payload_msg = Data::Make(data.data(), data.size());
                    plugin_->OnClientEventCame(true, 0, NetPluginType::kWebRtc, NetChannelType::kFileTransfer, payload_msg);
                });
            }
            else if (name == "input_data_channel") {
                // web client 的低延迟输入通道(unreliable/unordered)。
                // 走 CallbackEventDirectly:跳过 OnClientEventCame→PostWorkTask
                // 排队(插件 work 线程在高负载时可能多等数 ms),在 WebRTC
                // 回调线程直接投递到 event_replayer→SendInput。
                // 鼠标消息解析+SendInput 极轻量,不阻塞网络线程。
                input_data_channel_ = std::make_shared<RtcDataChannel>(name, shared_from_this(), ch);

                input_data_channel_->SetOnDataCallback([=, this](const std::string& data) {
                    auto payload_msg = Data::Make(data.data(), data.size());
                    auto event = std::make_shared<PxPluginNetClientEvent>();
                    event->is_proto_ = true;
                    event->socket_fd_ = 0;
                    event->nt_plugin_type_ = NetPluginType::kWebRtc;
                    event->nt_channel_type_ = NetChannelType::kMedia;
                    event->message_ = payload_msg;
                    event->from_plugin_ = plugin_;
                    plugin_->CallbackEventDirectly(event);
                });
            }
            else if (name == "ping_data_channel") {
                // 诊断通道:RtcDataChannel::OnMessage 里收到即原样回显
                ping_data_channel_ = std::make_shared<RtcDataChannel>(name, shared_from_this(), ch);
            }
        });

        // network state
        peer_callback_->SetOnIceConnectedCallback([=, this]() {
            ice_disconnected_since_ms_ = 0;
        });

        // 远端音频轨(浏览器麦克风上行):接收解码后经 WASAPI 播放
        peer_callback_->SetOnAudioTrackCallback([=, this](rtc::scoped_refptr<webrtc::AudioTrackInterface> track) {
            this->OnRemoteAudioTrack(std::move(track));
        });

        peer_callback_->SetOnRemoveAudioTrackCallback([=, this](rtc::scoped_refptr<webrtc::AudioTrackInterface> track) {
            this->OnRemoteAudioTrackRemoved(std::move(track));
        });

        peer_callback_->SetOnIceDisConnectedCallback([=, this]() {
            // 记录 Disconnected 起始时刻,On100msTimeout 负责超时判死。
            // 若 ICE 之后恢复为 Connected,OnIceConnectedCallback 会清零该标记。
            int64_t expect = 0;
            auto now = (int64_t)TimeUtil::GetCurrentTimestamp();
            ice_disconnected_since_ms_.compare_exchange_strong(expect, now);
            if (!media_data_channel_) {return;}
            auto event = std::make_shared<PxPluginClientDisConnectedEvent>();
            event->stream_id_ = media_data_channel_->the_conn_id_;
            event->end_timestamp_ = (int64_t) TimeUtil::GetCurrentTimestamp();
            event->duration_ =   event->end_timestamp_ - media_data_channel_->created_timestamp_;
            this->plugin_->CallbackEvent(event);
        });

        // ICE 终态(Failed/Closed):立即置退出标记停止收发,并通知 plugin 将其
        // 从 rtc_servers_ 中清除。注意:此回调运行在 libwebrtc 线程上,不能在此
        // 直接 Exit()(会 Stop/join 当前线程),真正的资源回收由 plugin 延迟 Sweep。
        peer_callback_->SetOnIceTerminalCallback([=, this]() {
            LOGW("Rtc server terminal, conn_id: {}, will be swept by plugin.", conn_id_);
            exit_ = true;
            if (plugin_) {
                plugin_->NotifyRtcServerTerminal(conn_id_, this);
            }
        });

        peer_callback_->SetOnIceGatherCompletedCallback([=, this]() {
            LOGI("Ice Gather completed.");
            std::string answer_sdp;
            if (!this->peer_conn_->local_description()->ToString(&answer_sdp)) {
                LOGE("Get local answer failed");
                if (answer_sdp_callback_) {
                    answer_sdp_callback_("");
                }
            }
            else {
                this->answer_sdp_ = answer_sdp;
                LOGI("Get answer sdp success");
                if (answer_sdp_callback_) {
                    answer_sdp_callback_(answer_sdp);
                }
            }
        });

        CreatePeerConnectionFactory();
        CreatePeerConnection();
        return true;
    }

    void RtcServer::CreateSomeMediaDeps(PeerConnectionFactoryDependencies& media_deps) {
        media_deps.adm = AudioDeviceModule::CreateForTest(
                AudioDeviceModule::kDummyAudio, media_deps.task_queue_factory.get());
        media_deps.audio_encoder_factory =
                webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>();
        media_deps.audio_decoder_factory =
                webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>();
        // custom encoders
        media_deps.video_encoder_factory = std::make_unique<RtcVideoEncoderFactory>(plugin_, shared_from_this()),
        // default encoders
        // media_deps.video_encoder_factory = std::make_unique<VideoEncoderFactoryTemplate<
        //         LibvpxVp8EncoderTemplateAdapter, LibvpxVp9EncoderTemplateAdapter,
        //         OpenH264EncoderTemplateAdapter, LibaomAv1EncoderTemplateAdapter>>();
        media_deps.video_decoder_factory =
                std::make_unique<VideoDecoderFactoryTemplate<
                        LibvpxVp8DecoderTemplateAdapter, LibvpxVp9DecoderTemplateAdapter,
                        OpenH264DecoderTemplateAdapter, Dav1dDecoderTemplateAdapter>>();
        media_deps.audio_processing = webrtc::AudioProcessingBuilder().Create();
    }

    void RtcServer::CreatePeerConnectionFactory() {
        configuration_.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
        configuration_.media_config.video.periodic_alr_bandwidth_probing = true;
        //configuration_.enable_dtls_srtp = true;

        network_thread_ = rtc::Thread::CreateWithSocketServer();
        network_thread_->Start();
        worker_thread_ = rtc::Thread::Create();
        worker_thread_->Start();
        sig_thread_ = rtc::Thread::Create();
        sig_thread_->Start();

        webrtc::PeerConnectionFactoryDependencies media_deps;
        media_deps.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
        CreateSomeMediaDeps(media_deps);

        // Use dummy ADM from CreateSomeMediaDeps. nullptr would create Windows
        // CoreAudio ADM whose capture thread races AudioSourceImpl::SendAudio
        // into AudioSendStream::SendAudioData (FatalLog / 0x80000003).
        // Outbound game audio uses AudioSourceImpl; inbound mic playout needs
        // a dedicated WASAPI path when using dummy ADM.
        adm_ = media_deps.adm;
        peer_conn_factory_ = webrtc::CreatePeerConnectionFactory(
            network_thread_.get(), worker_thread_.get(), sig_thread_.get(),
            adm_,
            std::move(media_deps.audio_encoder_factory),
            std::move(media_deps.audio_decoder_factory),
            std::move(media_deps.video_encoder_factory),
            std::move(media_deps.video_decoder_factory),
            nullptr, nullptr);

        if (peer_conn_factory_.get() == nullptr) {
            LOGE("Error on CreateModularPeerConnectionFactory.");
            return;
        }
        LOGI("CreatePeerConnectionFactory success.");
    }

    void RtcServer::CreatePeerConnection() {
        configuration_.port_allocator_config.min_port = 60430;
        configuration_.port_allocator_config.max_port = 60490;
        auto result = peer_conn_factory_->
                CreatePeerConnectionOrError(configuration_, webrtc::PeerConnectionDependencies(peer_callback_.get()));
        if (!result.ok()) {
            std::cerr << "create peer connection failed: " << result.error().message() << std::endl;
            return;
        }
        this->peer_conn_ = result.value();

        // video sources/tracks
        // offer 里的 video m-line 数决定布局:
        // - 1 条(web/旧客户端): 单动态 track,接收所有屏的帧(旧行为,
        //   编码器侧的切屏等 IDR 逻辑不变,web 端切屏继续可用);
        // - >=2 条(新 Windows 客户端): 每台显示器一条静态 track,帧按 mon_name
        //   路由,根治单 track 混流(两屏帧交替 → 反复"切屏等 IDR"的风暴)。
        int offer_video_mlines = 0;
        {
            size_t pos = 0;
            while ((pos = offer_sdp_.find("m=video", pos)) != std::string::npos) {
                ++offer_video_mlines;
                pos += 7;
            }
        }
        auto monitors = plugin_->GetRtcTrackMonitors();
        multi_track_mode_ = offer_video_mlines > 1 && !monitors.empty();
        static constexpr const char* kMediaStreamId = "godesk_media";
        if (multi_track_mode_) {
            int track_index = 0;
            for (const auto& m : monitors) {
                MonitorVideoTrack mvt;
                mvt.mon_name_ = m.name_;
                mvt.source_ = std::make_shared<VideoSourceImpl>(plugin_);
                mvt.track_source_ = rtc::make_ref_counted<VideoTrackSourceImpl>(plugin_, mvt.source_);
                auto video_track = peer_conn_factory_->CreateVideoTrack(mvt.track_source_, std::format("video_track_{}", track_index));
                // 每条 track 独立 stream id,客户端按 receiver->stream_ids() 区分屏
                auto rtc_error_or = peer_conn_->AddTrack(video_track, { std::format("{}_{}", kMediaStreamId, track_index) });
                if (!rtc_error_or.ok()) {
                    LOGE("peer connection add video track {} failed. with {}", track_index, rtc_error_or.error().message());
                    return;
                }
                video_tracks_.push_back(mvt);
                ++track_index;
            }
            LOGI("Multi-track mode: created {} video track(s), offer video m-lines: {}", video_tracks_.size(), offer_video_mlines);
            // 多 track = 客户端声明要多屏:让采集端产出所有显示器的帧,
            // 否则非当前屏的 track 永远等不到帧(采集端默认只采当前屏)
            plugin_->EnableAllMonitorCapture();
        }
        else {
            MonitorVideoTrack mvt;  // mon_name_ 为空 = 接收所有屏的动态 track(旧行为)
            mvt.source_ = std::make_shared<VideoSourceImpl>(plugin_);
            mvt.track_source_ = rtc::make_ref_counted<VideoTrackSourceImpl>(plugin_, mvt.source_);
            // video/audio 必须挂同一 MediaStream id,否则 web 端若直接用
            // ontrack.streams[0] 赋值 srcObject,后到的轨会覆盖先到的(有画面无声)。
            auto video_track = peer_conn_factory_->CreateVideoTrack(mvt.track_source_, "video_track_source_1");
            auto rtc_error_or = peer_conn_->AddTrack(video_track, { kMediaStreamId });
            if (!rtc_error_or.ok()) {
                LOGE("peer connection add track failed. with {}", rtc_error_or.error().message());
                return;
            }
            video_tracks_.push_back(mvt);
        }

        // audio source
        audio_source_ = AudioSourceImpl::Create();
        auto audio_track = peer_conn_factory_->CreateAudioTrack("audio", audio_source_.get());
        // 多 track 模式下音频用独立 stream id,避免和多路 video 混在同一 stream;
        // 单 track 模式保持与 video 同 stream(web 端 srcObject 需要)
        if (multi_track_mode_) {
            peer_conn_->AddTrack(audio_track, { std::format("{}_audio", kMediaStreamId) });
        }
        else {
            peer_conn_->AddTrack(audio_track, { kMediaStreamId });
        }

        // BWE 初始种子:默认起始估计只有 300kbps,爬坡期 pacing 饿死视频码流,
        // 而 BWE 又依赖码流动起来才能探测上行——鸡生蛋死锁,表现为视频完全发不出来。
        // 给一个有意义的起点,后续仍由 BWE 按真实链路状况上下调整。
        //
        // 本地链路(loopback/局域网)直接把工作点钉住:实测 GCC 的延迟估计
        // 在客户端高负载(有头浏览器解码渲染)下会误判拥塞,目标码率/fps 在
        // 1M~15M / 14~44fps 之间秒级震荡——x264 每 3s 被迫重开、生产速率被压到
        // ~30fps,pacing 失配又反过来喂养延迟估计,形成延迟螺旋。
        // 本插件只服务本地链路,钉死 min=start=max 让 GCC 无震荡空间;
        // 链路侧其余自适应(IDR/pacing)不受影响。
        //
        // 钉值取 24M(原 12M):pacer 按此速率放包,钉值翻倍让 IDR 等大帧的
        // 排空时间减半(73KB: 49ms→24ms),直接削掉 pacing 段延迟;loopback/
        // 有线 LAN 容量充裕,Wi-Fi 直连也留有余量。实测双 track 总分配
        // ~11.3M 已到 12M 上限,24M 给双屏高动态场景留出头空间。
        static constexpr int kLocalLinkBitrateBps = 24 * 1000 * 1000;
        webrtc::BitrateSettings bitrate_settings;
        bitrate_settings.min_bitrate_bps = kLocalLinkBitrateBps;
        bitrate_settings.start_bitrate_bps = kLocalLinkBitrateBps;
        bitrate_settings.max_bitrate_bps = kLocalLinkBitrateBps;
        auto bitrate_err = peer_conn_->SetBitrate(bitrate_settings);
        LOGI("SetBitrate seed: pinned min=start=max={}M, ok: {}", kLocalLinkBitrateBps / 1000000, bitrate_err.ok());

        // 首帧加速:即将开始发流,此刻主动请求主管线产 IDR。
        // 建连前的旧帧无需清理:Encode 首次执行时会以当前产出序号引导
        // (consumed_seq_ = GetLatestEncodedSeq),只消费之后新产的帧,
        // 配合 mWaitIDRFrame 保证首帧必为关键帧。
        plugin_->InsertIdr();

        // set remote sdp
        LOGI("Will set remote offer sdp.");
        webrtc::SdpParseError error;
        webrtc::SessionDescriptionInterface* session_description(webrtc::CreateSessionDescription("offer", offer_sdp_, &error));
        peer_conn_->SetRemoteDescription(this->set_remote_offer_sdp_callback_.get(), session_description);
        if (!error.line.empty()) {
            LOGE("OnOfferSdpCallback, SetRemoteDescription error: {}, {}", error.line, error.description);
            return;
        }

        LOGI("Will create answer sdp.");
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
        options.offer_to_receive_audio = true;
        options.offer_to_receive_video = true;
        peer_conn_->CreateAnswer(this->create_answer_callback_.get(), options);

    }

    void RtcServer::OnRemoteIce(const std::string& ice, const std::string& mid, int sdp_mline_index) {
        LOGI("OnRemoteIce: {}", ice);
        webrtc::SdpParseError error;
        std::unique_ptr<webrtc::IceCandidateInterface> candidate(webrtc::CreateIceCandidate(mid, sdp_mline_index, ice, &error));
        if (!error.line.empty()) {
            LOGE("Create IceCandidate failed: {} - {}", error.line, error.description);
            return;
        }
        peer_conn_->AddIceCandidate(std::move(candidate), [](webrtc::RTCError error) {
            if (error.ok()) {
                LOGI("AddIceCandidate success.");
            } else {
                LOGE("AddIceCandidate failed: {}", error.message());
            }
        });
    }

    void RtcServer::SendIceToRemote(const std::string& ice, const std::string& mid, int sdp_mline_index) {
        auto event = std::make_shared<PxPluginRtcIceEvent>();
        event->stream_id_ = stream_id_;
        event->ice_ = ice;
        event->mid_ = mid;
        event->sdp_mline_index_ = sdp_mline_index;
        plugin_->CallbackEvent(event);
    }

    void RtcServer::OnRemoteAudioTrack(rtc::scoped_refptr<webrtc::AudioTrackInterface> track) {
        LOGI("OnRemoteAudioTrack: {}", track->id());
        // 已有 sink(理论上一条连接只有一条上行音频轨),先清理
        OnRemoteAudioTrackRemoved(remote_audio_track_);

        // Factory uses dummy ADM (no mic capture race). Remote track sink is
        // stats-only for now; browser-mic playout needs a dedicated WASAPI path.
        auto sink = RemoteAudioSink::Make();
        track->AddSink(sink.get());
        remote_audio_track_ = std::move(track);
        remote_audio_sink_ = sink;
        LOGI("Remote audio sink attached (stats only; dummy ADM, no auto playout).");
    }

    void RtcServer::OnRemoteAudioTrackRemoved(rtc::scoped_refptr<webrtc::AudioTrackInterface> track) {
        if (!remote_audio_sink_) {
            return;
        }
        if (track && remote_audio_track_ && track->id() != remote_audio_track_->id()) {
            return;
        }
        LOGI("OnRemoteAudioTrackRemoved");
        if (remote_audio_track_) {
            remote_audio_track_->RemoveSink(remote_audio_sink_.get());
            remote_audio_track_ = nullptr;
        }
        remote_audio_sink_ = nullptr;
    }

    void RtcServer::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {
        if (network_thread_ && media_data_channel_ && !exit_) {
            network_thread_->PostTask([=, this]() {
                media_data_channel_->SendData(msg);
            });
        }
    }

    bool RtcServer::PostTargetStreamProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        if (network_thread_ && media_data_channel_ && !exit_) {
            network_thread_->PostTask([=, this]() {
                media_data_channel_->SendData(msg);
            });
        }
        return true;
    }

    bool RtcServer::PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        // 必须投递到 WebRTC 网络线程再 Send,否则 data_channel_->Send() 跨线程调用
        // 会被 libwebrtc 静默丢弃(剪切板文件取数 kClipboardReqBuffer/kClipboardRespBuffer
        // 偶发 60s 超时即源于此)。与 media 通道 PostTargetStreamProtoMessage 对齐。
        if (network_thread_ && ft_data_channel_ && !exit_) {
            network_thread_->PostTask([=, this]() {
                ft_data_channel_->SendData(msg);
            });
        }
        return true;
    }

    bool RtcServer::IsDataChannelConnected() {
        return !exit_ && media_data_channel_ && media_data_channel_->IsConnected();
    }

    bool RtcServer::IsFtDataChannelConnected() {
        return !exit_ && ft_data_channel_ && ft_data_channel_->IsConnected();
    }

    uint32_t RtcServer::GetMediaPendingMessages() {
        return !exit_ && media_data_channel_ ? media_data_channel_->GetPendingDataCount() : 0;
    }

    uint32_t RtcServer::GetFtPendingMessages() {
        return !exit_ && ft_data_channel_ ? ft_data_channel_->GetPendingDataCount() : 0;
    }

    bool RtcServer::HasEnoughBufferForQueuingMediaMessages() {
        return !exit_ && media_data_channel_ && media_data_channel_->HasEnoughBufferForQueuingMessages();
    }

    bool RtcServer::HasEnoughBufferForQueuingFtMessages() {
        return !exit_ && ft_data_channel_ && ft_data_channel_->HasEnoughBufferForQueuingMessages();
    }

    void RtcServer::On100msTimeout() {
        if (!exit_ && ice_disconnected_since_ms_.load() != 0) {
            auto now = (int64_t)TimeUtil::GetCurrentTimestamp();
            if (now - ice_disconnected_since_ms_.load() >= kIceDisconnectedTimeoutMs) {
                LOGW("Rtc server ice disconnected timeout, conn_id: {}, will be swept.", conn_id_);
                exit_ = true;
                if (plugin_) {
                    plugin_->NotifyRtcServerTerminal(conn_id_, this);
                }
            }
        }
        if (ft_data_channel_ && !exit_) {
            ft_data_channel_->On100msTimeout();
        }
    }

    std::string RtcServer::GetAnswerSdp() {
        return answer_sdp_;
    }

    void RtcServer::SetOnAnswerCallback(std::function<void(const std::string& answer_sdp)>&& callback) {
        answer_sdp_callback_ = callback;
    }

    void RtcServer::OnNewFrameCaptured(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, uint64_t handle, int64_t adapter_id, uint64_t frame_format) {
        if (exit_) {
            return;
        }
        if (video_tracks_.empty()) {
            LOGE("Don't have video source");
            return;
        }
        if (handle == 0) {
            LOGE("Invalid texture handle");
            return;
        }
        DispatchCapturedFrameNotify(mon_name, frame_idx, frame_width, frame_height, handle, adapter_id, frame_format);
    }

    // CPU 采集(GDI/mock)的裸帧通知:没有共享纹理,handle 置 0。
    // NotifyFrameFrameBuffer 只是"有帧了"的载体,像素从不经 webrtc 传递
    // (RtcVideoEncoder 用预编码码流替换),所以 handle=0 无影响。
    void RtcServer::OnNewRawFrameCaptured(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height) {
        if (exit_) {
            return;
        }
        if (video_tracks_.empty()) {
            LOGE("Don't have video source");
            return;
        }
        DispatchCapturedFrameNotify(mon_name, frame_idx, frame_width, frame_height, 0, 0, 0);
    }

    void RtcServer::DispatchCapturedFrameNotify(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, uint64_t handle, int64_t adapter_id, uint64_t frame_format) {

        // 按屏路由:多 track 模式每条 track 只发自己那块屏的帧;
        // 单 track(动态)模式接收所有屏,编码器侧处理切屏
        MonitorVideoTrack* target = nullptr;
        if (!multi_track_mode_) {
            target = &video_tracks_[0];
        }
        else {
            for (auto& t : video_tracks_) {
                if (t.mon_name_ == mon_name) {
                    target = &t;
                    break;
                }
            }
            if (!target) {
                // 建连后新增的显示器没有对应 track(加 track 需重新协商),限速日志后丢弃
                static std::atomic_uint64_t unknown_mon_drops = 0;
                if (++unknown_mon_drops % 300 == 1) {
                    LOGW("OnNewFrameCaptured, no video track for monitor: {}", mon_name);
                }
                return;
            }
        }

        if (target->last_frame_index_ == 0) {
            target->last_frame_index_ = frame_idx;
        }
        auto diff = frame_idx - target->last_frame_index_;
        target->last_frame_index_ = frame_idx;
        if (diff > 1) {
            LOGE("OnNewFrameCaptured [{}], but diff size is: {}", mon_name, diff);
        }

        // timestamp_us = Unix us. Do NOT set ntp_time_ms here: WebRTC fills NTP
        // on the encode path; stuffing the wrong epoch caused DebugBreak crashes.
        // RtcSharedVideoEncoder normalizes EncodedImage.ntp_time_ms_ before send.
        const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto buffer = rtc::make_ref_counted<NotifyFrameFrameBuffer>(mon_name, frame_idx, frame_width, frame_height, handle, adapter_id, frame_format);
        webrtc::VideoFrame notify_frame = webrtc::VideoFrame::Builder()
                .set_video_frame_buffer(buffer)
                .set_timestamp_us(now_us)
                .set_id(static_cast<uint16_t>(frame_idx & 0xFFFF))
                .build();
        target->source_->OnNotifyFrame(notify_frame);
    }

    std::vector<std::string> RtcServer::GetVideoTrackMonitors() const {
        std::vector<std::string> names;
        names.reserve(video_tracks_.size());
        for (const auto& t : video_tracks_) {
            names.push_back(t.mon_name_);
        }
        return names;
    }

    void RtcServer::OnRawAudioData(const std::shared_ptr<Data>& data, int samples, int channels, int bits) {
        if (exit_ || !audio_source_ || !data || data->Size() == 0) {
            return;
        }
        audio_source_->SendAudio(data->DataAddr(), data->Size(), samples, channels, bits);
    }

    void RtcServer::Exit() {
        // 幂等:ICE 终态 Sweep、takeover 替换、插件销毁等路径可能重复调用
        if (cleaned_up_.exchange(true)) {
            return;
        }
        exit_ = true;
        OnRemoteAudioTrackRemoved(remote_audio_track_);
        if (media_data_channel_) {
            media_data_channel_->Close();
        }
        if (ft_data_channel_) {
            ft_data_channel_->Close();
        }
        if (input_data_channel_) {
            input_data_channel_->Close();
        }
        if (ping_data_channel_) {
            ping_data_channel_->Close();
        }
        if (peer_conn_) {
            peer_conn_->Close();
            peer_conn_ = nullptr;
        }
        peer_conn_factory_ = nullptr;

        if (network_thread_) {
            network_thread_->Stop();
        }
        if (worker_thread_) {
            worker_thread_->Stop();
        }
        if (sig_thread_) {
            sig_thread_->Stop();
        }

        // 打断 RtcServer <-> RtcDataChannel 的 shared_ptr 循环引用,避免泄漏
        media_data_channel_ = nullptr;
        ft_data_channel_ = nullptr;
        input_data_channel_ = nullptr;
        ping_data_channel_ = nullptr;

        rtc::CleanupSSL();
    }

}
