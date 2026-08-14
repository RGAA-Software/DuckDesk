//
// Created by RGAA on 2023/12/26.
//

#include "thunder_sdk.h"

#include "tc_common_new/log.h"
#include "tc_common_new/file.h"
#include "tc_common_new/message_notifier.h"
#include "tc_common_new/thread.h"
#include "tc_common_new/time_util.h"
#include <atomic>
#include "tc_common_new/folder_util.h"
#include "tc_common_new/string_util.h"
#include "tc_client_sdk_new/gl/raw_image.h"
#include "tc_opus_codec_new/opus_codec.h"
#include "tc_message.pb.h"
#include "sdk_timer.h"
#include "sdk_messages.h"
#include "sdk_statistics.h"
#include "sdk_net_client.h"
#include "sdk_cast_receiver.h"
#include "sdk_video_decoder_factory.h"
#include "sdk_ffmpeg_soft_decoder.h"
#include "sdk_ffmpeg_decoder.h"
#include "sdk_ffmpeg_vulkan_decoder.h"
#include "video_decode_thread_task.h"
#include "tc_message_new/proto_converter.h"
#ifdef WIN32
#include "tc_common_new/hardware.h"
#endif

namespace tc
{

    std::atomic<uint64_t> g_last_mouse_send_us{0};

    namespace {
        // [LAT-decode] 客户端解码耗时统计(D3D11VA 硬解,含 GPU 等待)
        std::atomic<uint64_t> g_decode_frames{0};
        std::atomic<uint64_t> g_decode_us_sum{0};
        std::atomic<uint64_t> g_decode_us_max{0};

        void DumpDecodeLatencyIfDue() {
            static std::atomic<uint64_t> s_last_dump_us{0};
            auto now = TimeUtil::GetCurrentTimePointUS();
            auto last = s_last_dump_us.load();
            if (now - last < 5000000) {
                return;
            }
            if (!s_last_dump_us.compare_exchange_weak(last, now)) {
                return;
            }
            auto n = g_decode_frames.exchange(0);
            auto sum = g_decode_us_sum.exchange(0);
            auto mx = g_decode_us_max.exchange(0);
            LOGI("[LAT-decode] frames={} avg_us={} max_us={}",
                 n, n > 0 ? (sum / n) : 0, mx);
        }

        // [LAT-roundtrip] 操作往返延迟:最近一次鼠标发送 -> 本帧解码完成(含输入+DWM 垂直同步+视频链路)
        std::atomic<uint64_t> g_roundtrip_cnt{0};
        std::atomic<uint64_t> g_roundtrip_us_sum{0};
        std::atomic<uint64_t> g_roundtrip_us_max{0};

        void DumpRoundtripLatencyIfDue() {
            static std::atomic<uint64_t> s_last_dump_us{0};
            auto now = TimeUtil::GetCurrentTimePointUS();
            auto last = s_last_dump_us.load();
            if (now - last < 5000000) {
                return;
            }
            if (!s_last_dump_us.compare_exchange_weak(last, now)) {
                return;
            }
            auto n = g_roundtrip_cnt.exchange(0);
            auto sum = g_roundtrip_us_sum.exchange(0);
            auto mx = g_roundtrip_us_max.exchange(0);
            LOGI("[LAT-roundtrip] samples={} avg_us={} max_us={}",
                 n, n > 0 ? (sum / n) : 0, mx);
        }
    }

    std::shared_ptr<ThunderSdk> ThunderSdk::Make(const std::shared_ptr<MessageNotifier>& notifier) {
        return std::make_shared<ThunderSdk>(notifier);
    }

    ThunderSdk::ThunderSdk(const std::shared_ptr<MessageNotifier>& notifier) {
        this->msg_notifier_ = notifier;
    }

    ThunderSdk::~ThunderSdk() {

    }

    bool ThunderSdk::Init(const std::shared_ptr<ThunderSdkParams>& params, void* surface, const DecoderRenderType& drt) {
        sdk_params_ = params;
        drt_ = drt;
        render_surface_ = surface;
        last_heartbeat_callback_ = TimeUtil::GetCurrentTimestamp();

        auto fn_process_target_platform = [&]() {
            #if defined(_WIN32)
                sdk_params_->device_name_ = Hardware::GetDesktopName();
                sdk_params_->client_type_ = ClientType::kWindows;
                return ClientType::kWindows;
            #elif defined(__APPLE__)
                #include "TargetConditionals.h"
                #if TARGET_OS_IPHONE || TARGET_OS_SIMULATOR
                    // iOS或iOS模拟器
                    return ClientType::kiOS;
                #elif TARGET_OS_MAC
                    // macOS
                    return ClientType::kMacOS;
                #endif
            #elif defined(__ANDROID__)
                // Android
                return ClientType::kAndroid;
            #elif defined(__linux__)
                // Linux (not Android)
                return ClientType::kLinux;
            #else
                return ClientType::kUnknown;
            #endif
        };
        fn_process_target_platform();

        net_client_ = std::make_shared<NetClient>(sdk_params_,
                                      msg_notifier_,
                                      sdk_params_->ip_,
                                      sdk_params_->port_,
                                      sdk_params_->media_path_,
                                      sdk_params_->ft_path_,
                                      sdk_params_->nt_type_,
                                      sdk_params_->device_id_,
                                      sdk_params_->remote_device_id_,
                                      sdk_params_->ft_device_id_,
                                      sdk_params_->ft_remote_device_id_,
                                      sdk_params_->stream_id_);
        return true;
    }

    void ThunderSdk::Start() {
        statistics_ = SdkStatistics::Instance();
        statistics_->render_type_.Update(sdk_params_->render_type_name_);
        // threads
        video_thread_ = Thread::Make("video", 64);
        video_thread_->SetOnFrontTaskCallback([=, this](ThreadTaskPtr task_tr) ->void{
            if (video_frame_thread_discarded_cbk_) {
                video_frame_thread_discarded_cbk_();
                need_clear_video_tasks_ = true;
            }
            if (!task_tr) {
                return;
            }
            auto video_decode_task_ptr = std::dynamic_pointer_cast<VideoDecodeThreadTask>(task_tr);
            if (!video_decode_task_ptr) {
                return;
            }
            LOGW("the video task monitor_name: {}, frame_index: {}, discarded!", video_decode_task_ptr->monitor_name_, video_decode_task_ptr->frame_index_);
        });

        video_thread_->Poll();
        audio_thread_ = Thread::Make("audio", 32);
        audio_thread_->Poll();
        misc_thread_ = Thread::Make("misc", 32);
        misc_thread_->Poll();

        net_client_->SetOnConnectCallback([=, this]() {
            this->SendHelloMessage();
            msg_notifier_->SendAppMessage(SdkMsgNetworkConnected{});
        });

        net_client_->SetOnDisconnectedCallback([=, this]() {
            msg_notifier_->SendAppMessage(SdkMsgNetworkDisConnected{});
            this->ClearFirstFrameState();
        });

        net_client_->SetOnVideoFrameMsgCallback([=, this](std::shared_ptr<tc::Message> msg) {
            if (exit_) { return; }
            
            tc::VideoFrame frame = msg->video_frame();

            auto video_task = [=, this]() ->void {
                const auto& monitor_name = frame.mon_name();
                std::shared_ptr<VideoDecoder> video_decoder = nullptr;
                if (video_decoders_.contains(monitor_name)) {
                    video_decoder = video_decoders_[monitor_name];
                    bool rebuild = video_decoder->NeedReConstruct(frame.type(), frame.frame_width(), frame.frame_height(), frame.image_format());
                    if (rebuild) {
                        video_decoder->Release();
                        video_decoders_.erase(monitor_name);
                        video_decoder = nullptr;
                        LOGI("Rebuild video decoder, type: {}, {}x{}, image_format: {}", (int)frame.type(), frame.frame_width(), frame.frame_height(), (int)frame.image_format());
                    }
                }
                if (!video_decoder) {
#ifdef ANDROID
                    // Some Android devices can't decode 2 or more streams at the same time, so, re-create it .
                    if (!video_decoders_.empty()) {
                        for (auto& [mon_name, decoder] : video_decoders_) {
                            decoder->Release();
                        }
                        video_decoders_.clear();
                    }
#endif
                    //auto codec = (drt_ == DecoderRenderType::kMediaCodecSurface || drt_ == DecoderRenderType::kMediaCodecNv21) ? SupportedCodec::kMediaCodec : SupportedCodec::kFFmpeg;
                    //video_decoder = VideoDecoderFactory::Make(shared_from_this(), codec);
#if WIN32
                    // from now on, it's for d3d11va decoder
                    // we'll combine decoders into the same decoder class in the future
                    LOGI("We will try hardware decoder.");

                    if (sdk_params_->support_vulkan_) {
                        video_decoder = std::make_shared<FFmpegVulkanDecoder>(shared_from_this());
                    }
                    else {
                        video_decoder = std::make_shared<FFmpegDecoder>(shared_from_this());
                    }
                    auto r = video_decoder->Init(frame.mon_name(), frame.type(), frame.frame_width(), frame.frame_height(), frame.data(), render_surface_, frame.image_format(),
                        IsDisabledHardwareDecoder(frame.mon_name()));
                    if (r != 0) {
                        LOGE("Init D3D11VA decoder failed, will try software decoder");
                        video_decoder->Release();
                        video_decoder.reset();
                    }

                    if (!IsDisabledHardwareDecoder(frame.mon_name())) {
                    }
                    else {
                        LOGI("Hardware decoder for: {} is disabled, use software.", frame.mon_name());
                    }
#endif

                    if (!video_decoder) {
                        // Android
                        // Begin
#ifdef ANDROID
                        auto codec = (drt_ == DecoderRenderType::kMediaCodecSurface || drt_ == DecoderRenderType::kMediaCodecNv21) ? SupportedCodec::kMediaCodec : SupportedCodec::kFFmpeg;
                        video_decoder = VideoDecoderFactory::Make(shared_from_this(), codec);
                        LOGI("Create video decoder, codec: {}", (int)codec);
                        // Android
                        // End
#else
                        video_decoder = std::make_shared<FFmpegVideoDecoder>(shared_from_this());
#endif
                        bool ready = video_decoder->Ready();
                        if (!ready) {
                            auto result = video_decoder->Init(frame.mon_name(), frame.type(), frame.frame_width(),
                                frame.frame_height(), frame.data(), render_surface_, frame.image_format(), false);
                            if (result != 0) {
                                LOGE("Video decoder init failed, mon name: {}, frame type: {}, frame width: {}, frame height: {}, format: {}",
                                    frame.mon_name(), (int)frame.type(), frame.frame_width(), frame.frame_height(), (int)frame.image_format());
                                return;
                            }
                            LOGI("Create decoder success {}x{}, type: {}", frame.frame_width(), frame.frame_height(), (int)frame.type());
                        }
                    }
                    video_decoders_[monitor_name] = video_decoder;
                }

                auto current_time = TimeUtil::GetCurrentTimestamp();
                if (!last_received_video_timestamps_.contains(monitor_name)) {
                    last_received_video_timestamps_[monitor_name] = current_time;
                }
                auto diff = current_time - last_received_video_timestamps_[monitor_name];
                last_received_video_timestamps_[monitor_name] = current_time;

                this->PostMiscTask([=, this]() {
                    statistics_->AppendVideoRecvGap(frame.mon_name(), diff);
                    statistics_->TickVideoRecvFps(frame.mon_name());
                    statistics_->UpdateFrameSize(frame.mon_name(), frame.frame_width(), frame.frame_height());
                });

                SdkCaptureMonitorInfo cap_mon_info{
                    .mon_name_ = frame.mon_name(),
                    .mon_index_ = frame.mon_index(),
                    .mon_left_ = frame.mon_left(),
                    .mon_top_ = frame.mon_top(),
                    .mon_right_ = frame.mon_right(),
                    .mon_bottom_ = frame.mon_bottom(),
                    .frame_width_ = frame.frame_width(),
                    .frame_height_ = frame.frame_height(),
                    .update_time_ = TimeUtil::GetCurrentTimestamp()
                };

                auto mon_name = frame.mon_name();
                if (!last_frame_indices_.contains(mon_name)) {
                    last_frame_indices_.insert({mon_name, frame.frame_index()});
                }
                auto frame_diff = frame.frame_index() - last_frame_indices_[frame.mon_name()];
                if (frame_diff != 1) {
                    LOGI("Video frame came, mon: [{}], index: {}, diff: {}, last: {}, extra: [{}]", mon_name, frame.frame_index(), (int64_t)frame_diff, last_frame_indices_[frame.mon_name()], frame.extra());
                }
                last_frame_indices_[mon_name] = frame.frame_index();

                if (sdk_params_->debug_) {
                    if (!received_files_.contains(mon_name)) {
                        auto display_name = mon_name.size() > 4 ? mon_name.substr(4) : mon_name;
                        auto file_path = StringUtil::ToUTF8(FolderUtil::GetProgramDataPath()) + "/gr_data/client/recv_" + display_name + ".h264";
                        auto recv_video_file = File::OpenForWriteB(U8Path(file_path));
                        received_files_[mon_name] = recv_video_file;
                    }
                    received_files_[mon_name]->Append(frame.data());
                }
                // [LAT-decode] 计时单帧解码耗时
                auto dec_beg = TimeUtil::GetCurrentTimePointUS();
                auto ret = video_decoder->Decode(frame.data());
                auto dec_us = TimeUtil::GetCurrentTimePointUS() - dec_beg;
                ++g_decode_frames;
                g_decode_us_sum += dec_us;
                {
                    auto prev = g_decode_us_max.load();
                    while (dec_us > prev && !g_decode_us_max.compare_exchange_weak(prev, dec_us)) {}
                }
                DumpDecodeLatencyIfDue();
                if (!ret.has_value() && ret.error() != 0) {
                    IncreaseDecodeFailedCount(frame.mon_name());
                    if (GetDecodeFailedCount(frame.mon_name()) > 60) {
                        ResetDecodeFailedCount(frame.mon_name());
                        DisableHardwareDecoder(frame.mon_name());
                        LOGE("decode error: {}, will recreate the decoder", ret.error());
                        video_decoder->Release();
                        video_decoders_.erase(frame.mon_name());
                        LOGW("Video decoder for : {} is released.", frame.mon_name());
                    }
                    else if (GetDecodeFailedCount(frame.mon_name()) > 30) {
                        RequestIFrame();
                        LOGE("decode error: {}, will request Key Frame", ret.error());
                    }
                }

                // test
                if (false) {
                    static bool recreate_destroy_decoder = false;
                    IncreaseDecodeFailedCount(frame.mon_name());
                    if (!recreate_destroy_decoder) {
                        if (GetDecodeFailedCount(frame.mon_name()) > 150) {
                            recreate_destroy_decoder = true;
                            ResetDecodeFailedCount(frame.mon_name());
                            DisableHardwareDecoder(frame.mon_name());
                            LOGE("decode error: {}, will recreate the decoder", ret.error());
                            video_decoder->Release();
                            video_decoders_.erase(frame.mon_name());
                            LOGW("Video decoder for : {} is released.", frame.mon_name());
                        }
                    }
                }
                // test

                if (exit_ || !ret.has_value()) {
                    LOGE("Don't have value, exit? {}", exit_);
                    return;
                }
                auto raw_image = ret.value();
                if (!raw_image) {
                    LOGE("Don't have decoded image");
                    return;
                }

                //LOGI("decode image size {}x{}", raw_image->img_width, raw_image->img_height);
                // [LAT-roundtrip] 操作往返:最近一次鼠标发送 -> 本帧解码完成(只统计 100ms 内,过滤空闲期)
                {
                    auto last_mouse = g_last_mouse_send_us.load();
                    if (last_mouse != 0) {
                        auto now_us = TimeUtil::GetCurrentTimePointUS();
                        auto rt_us = now_us - last_mouse;
                        if (rt_us < 100000) {
                            ++g_roundtrip_cnt;
                            g_roundtrip_us_sum += rt_us;
                            auto prev = g_roundtrip_us_max.load();
                            while (rt_us > prev && !g_roundtrip_us_max.compare_exchange_weak(prev, rt_us)) {}
                            DumpRoundtripLatencyIfDue();
                        }
                    }
                }
                if (video_frame_cbk_) {
                    video_frame_cbk_(raw_image, cap_mon_info);
                }

                if (!has_video_frame_msg_) {
                    has_video_frame_msg_ = true;
                    SendFirstFrameMessage(raw_image, cap_mon_info);
                }
            };

            this->PostVideoTask(video_task, frame.frame_index(), frame.mon_name());
        });

        net_client_->SetOnAudioFrameMsgCallback([=, this](std::shared_ptr<tc::Message> msg) {
            if (exit_) { return; }
            this->PostAudioTask([=, this]() {
                auto frame = msg->audio_frame();
                auto beg = TimeUtil::GetCurrentTimestamp();
                if (!audio_decoder_) {
                    audio_decoder_ = std::make_shared<OpusAudioDecoder>(frame.samples(), frame.channels());
                }
                std::vector<opus_int16> pcm_data;
                if (frame.data().empty()) {
                    // UDP 丢帧信号(extra="udp_lost"):无码流可解,走 Opus PLC 补一帧 20ms
                    pcm_data = audio_decoder_->DecodeDummy(frame.frame_size());
                }
                else {
                    std::vector<unsigned char> buffer(frame.data().begin(), frame.data().end());
                    pcm_data = audio_decoder_->Decode(buffer, frame.frame_size(), false);
                }
                if (audio_frame_cbk_) {
                    auto data = Data::Make((char*)pcm_data.data(), pcm_data.size()*2);
                    audio_frame_cbk_(data, frame.samples(), frame.channels(), frame.bits());
                }
                //LOGI("opus data size: {}, frame size: {}, samples: {}, channel: {}, PCM data size in char : {}", frame.data().size(), frame.frame_size(), frame.samples(), frame.channels(), pcm_data.size()*2);
                if (debug_audio_decoder_) {
                    static FilePtr pcm_audio = File::OpenForWriteB(U8Path("1.test.pcm"));
                    pcm_audio->Append((char *) pcm_data.data(), pcm_data.size()*2);
                }
                auto end = TimeUtil::GetCurrentTimestamp();
                //LOGI("decode audio : {}", end-beg);
            });
        });

        net_client_->SetOnAudioSpectrumCallback([=, this](std::shared_ptr<tc::Message> msg) {
            if (exit_) { return; }
            this->PostMiscTask([=, this]() {
                if (audio_spectrum_cbk_) {
                    audio_spectrum_cbk_(msg);
                }
            });
        });

        // decoded video frames from the webrtc local(direct) connection
        net_client_->SetOnRtcLocalVideoFrameCallback([=, this](int w, int h, std::shared_ptr<Data> i420) {
            this->OnRtcLocalVideoFrame(w, h, i420);
        });

        // decoded audio(16-bit interleaved PCM) from the webrtc local(direct) connection:
        // already decoded by webrtc's built-in opus decoder, feed the player directly
        net_client_->SetOnRtcLocalAudioCallback([=, this](std::shared_ptr<Data> pcm, int sample_rate, int channels) {
            if (exit_) { return; }
            this->PostAudioTask([=, this]() {
                if (audio_frame_cbk_) {
                    audio_frame_cbk_(pcm, sample_rate, channels, 16);
                }
            });
        });

        net_client_->Start();

        // rtc local encoded-sink mode, old-render compat: when the answer carries no
        // "monitors" array, the single dynamic track is mapped to the capturing
        // monitor reported via ServerConfiguration
        net_client_->SetRtcLocalCapturingMonitorNameProvider([=, this]() {
            std::lock_guard<std::mutex> lk(rtc_cap_mon_mtx_);
            return rtc_capturing_monitor_name_;
        });

        // receiver
        // cast_receiver_ = CastReceiver::Make();
        // cast_receiver_->Start();

        sdk_timer_ = std::make_shared<SdkTimer>(msg_notifier_);
        sdk_timer_->StartTimers();

        RegisterEventListeners();

    }

    void ThunderSdk::SendFirstFrameMessage(std::shared_ptr<RawImage> image, const SdkCaptureMonitorInfo& info) {
        SdkMsgFirstVideoFrameDecoded msg;
        msg.raw_image_ = image;
        msg.mon_info_ = info;
        msg_notifier_->SendAppMessage(msg);
    }

    void ThunderSdk::OnRtcLocalVideoFrame(int w, int h, std::shared_ptr<Data> i420) {
        if (exit_) {
            return;
        }
        if (!i420 || w <= 0 || h <= 0) {
            return;
        }

        // statistics, keep the speed chart alive
        statistics_->AppendRecvDataSize(i420->Size());

        auto raw_image = RawImage::MakeI420(i420->DataAddr(), (int)i420->Size(), w, h);
        // rtc mode carries a single video stream, report it as the capturing monitor.
        // use the REAL monitor name from ServerConfiguration: the render's event replayer
        // drops mouse events tagged with an unknown monitor name.
        const auto mon_name = [=, this]() {
            std::lock_guard<std::mutex> lk(rtc_cap_mon_mtx_);
            return !rtc_capturing_monitor_name_.empty()
                   ? rtc_capturing_monitor_name_
                   : std::string("rtc_local");
        }();
        SdkCaptureMonitorInfo cap_mon_info {
            .mon_name_ = mon_name,
            .mon_index_ = 0,
            .mon_left_ = 0,
            .mon_top_ = 0,
            .mon_right_ = w,
            .mon_bottom_ = h,
            .frame_width_ = w,
            .frame_height_ = h,
            .update_time_ = TimeUtil::GetCurrentTimestamp(),
        };

        this->PostMiscTask([=, this]() {
            statistics_->TickVideoRecvFps(cap_mon_info.mon_name_);
            statistics_->UpdateFrameSize(cap_mon_info.mon_name_, w, h);
        });

        if (video_frame_cbk_) {
            video_frame_cbk_(raw_image, cap_mon_info);
        }

        if (!has_video_frame_msg_) {
            has_video_frame_msg_ = true;
            SendFirstFrameMessage(raw_image, cap_mon_info);
        }
    }

    void ThunderSdk::PostMediaMessage(std::shared_ptr<Data> msg) {
        if(net_client_ && msg) {
            net_client_->PostMediaMessage(msg);
        }
    }

    void ThunderSdk::PostFileTransferMessage(std::shared_ptr<Data> msg) {
        if(net_client_) {
            net_client_->PostFileTransferMessage(msg);
        }
    }

    void ThunderSdk::RegisterEventListeners() {
        msg_listener_ = msg_notifier_->CreateListener();

        // notify to net client
        msg_listener_->Listen<SdkMsgTimer16>([=, this](const auto& msg) {
            net_client_->On16msTimeout();
        });

        msg_listener_->Listen<SdkMsgTimer100>([=, this](const auto& msg) {

        });

        msg_listener_->Listen<SdkMsgTimer1000>([=, this](const auto& msg) {
            PostMiscTask([this]() {
                statistics_->CalculateDataSpeed();
                statistics_->CalculateVideoFrameFps();
                this->ReportStatistics();
            });
        });

        msg_listener_->Listen<SdkMsgTimer2000>([=, this](const auto& msg) {

        });

        // remote device offline
        msg_listener_->Listen<SdkMsgRelayRemoteDeviceOffline>([=, this](const SdkMsgRelayRemoteDeviceOffline& msg) {
            this->ClearFirstFrameState();
        });
    }

    void ThunderSdk::SendHelloMessage() {
        if (!net_client_) {
            return;
        }
        tc::Message msg;
        msg.set_type(tc::MessageType::kHello);
        msg.set_device_id(sdk_params_->device_id_);
        msg.set_stream_id(sdk_params_->stream_id_);
        auto hello = msg.mutable_hello();
        hello->set_enable_audio(sdk_params_->enable_audio_);
        hello->set_enable_video(sdk_params_->enable_video_);
        hello->set_client_type(sdk_params_->client_type_);
        hello->set_enable_controller(sdk_params_->enable_controller_);
        hello->set_device_name(sdk_params_->device_name_);
        if (auto buffer = tc::ProtoAsData(&msg); buffer) {
            net_client_->PostMediaMessage(buffer);
        }
    }

    void ThunderSdk::RequestIFrame() {
        if (!net_client_) {
            return;
        }
        tc::Message msg;
        msg.set_type(MessageType::kInsertKeyFrame);
        msg.set_device_id(sdk_params_->device_id_);
        msg.set_stream_id(sdk_params_->stream_id_);
        auto ack = msg.mutable_ack();
        ack->set_type(MessageType::kInsertKeyFrame);
        if (auto buffer = tc::ProtoAsData(&msg); buffer) {
            net_client_->PostMediaMessage(buffer);
        }
    }

    void ThunderSdk::PostVideoTask(std::function<void()>&& task, int64_t frame_index, const std::string& monitor_name) {
        auto video_task = VideoDecodeThreadTask::Make(std::move(task));
        video_task->frame_index_ = frame_index;
        video_task->monitor_name_ = monitor_name;
        video_thread_->Post(video_task);
        if (need_clear_video_tasks_) {
            need_clear_video_tasks_ = false;
            RequestIFrame();
            video_thread_->Clear();
        }
    }

    void ThunderSdk::PostAudioTask(std::function<void()>&& task) {
        audio_thread_->Post(SimpleThreadTask::Make(std::move(task)));
    }

    void ThunderSdk::PostMiscTask(std::function<void()>&& task) {
        misc_thread_->Post(SimpleThreadTask::Make(std::move(task)));
    }

    void ThunderSdk::SetOnAudioSpectrumCallback(OnAudioSpectrumCallback&& cbk) {
        audio_spectrum_cbk_ = std::move(cbk);
    }

    void ThunderSdk::SetOnCursorInfoCallback(tc::OnCursorInfoSyncMsgCallback&& cbk) {
        if (net_client_) {
            net_client_->SetOnCursorInfoSyncMsgCallback(std::move(cbk));
        }
    }

    void ThunderSdk::SetOnHeartBeatCallback(tc::OnHeartBeatInfoCallback&& cbk) {
        if (net_client_) {
            net_client_->SetOnHeartBeatCallback([this, callback = std::move(cbk)](auto m) {
                last_heartbeat_callback_ = TimeUtil::GetCurrentTimestamp();
                callback(m);
            });
        }
    }

    void ThunderSdk::SetOnClipboardCallback(OnClipboardInfoCallback&& cbk) {
        if (net_client_) {
            net_client_->SetOnClipboardCallback(std::move(cbk));
        }
    }

    void ThunderSdk::SetOnServerConfigurationCallback(OnConfigCallback&& cbk) {
        if (net_client_) {
            net_client_->SetOnServerConfigurationCallback([=, this](std::shared_ptr<tc::Message> msg) {
                if (msg && msg->has_config() && !msg->config().capturing_monitor_name().empty()) {
                    std::lock_guard<std::mutex> lk(rtc_cap_mon_mtx_);
                    rtc_capturing_monitor_name_ = msg->config().capturing_monitor_name();
                }
                cbk(std::move(msg));
                if (!has_config_msg_) {
                    has_config_msg_ = true;
                    msg_notifier_->SendAppMessage(SdkMsgFirstConfigInfoCallback {
                        .msg_ = msg,
                    });
                }
            });
        }
    }

    void ThunderSdk::SetOnMonitorSwitchedCallback(OnMonitorSwitchedCallback&& cbk) {
        if (net_client_) {
            net_client_->SetOnMonitorSwitchedCallback(std::move(cbk));
        }
    }

    void ThunderSdk::SetOnRawMessageCallback(OnRawMessageCallback&& cbk) {
        if (net_client_) {
            net_client_->SetOnRawMessageCallback(std::move(cbk));
        }
    }

    void ThunderSdk::SetOnVideoFrameDecodeThreadDiscardedCallback(OnVideoFrameDecodeThreadDiscardedCallback&& cbk) {
        video_frame_thread_discarded_cbk_ = cbk;
    }

    int ThunderSdk::GetProgressSteps() const {
        if (sdk_params_->nt_type_ == ClientNetworkType::kWebsocket) {
            return 3;
        }
        else if (sdk_params_->nt_type_ == ClientNetworkType::kUdpKcp) {
            return 3;
        }
        else if (sdk_params_->nt_type_ == ClientNetworkType::kWebRtc) {
            return 3;
        }
        else {
            return 3;
        }
    }

    std::shared_ptr<ThunderSdkParams> ThunderSdk::GetSdkParams() {
        return sdk_params_;
    }

    std::shared_ptr<MessageNotifier> ThunderSdk::GetMessageNotifier() {
        return msg_notifier_;
    }

    int64_t ThunderSdk::GetQueuingMediaMsgCount() {
        if (net_client_) {
            return net_client_->GetQueuingMediaMsgCount();
        }
        return 0;
    }

    int64_t ThunderSdk::GetQueuingFtMsgCount() {
        if (net_client_) {
            return net_client_->GetQueuingFtMsgCount();
        }
        return 0;
    }

    void ThunderSdk::RetryConnection() {
        if (net_client_) {
            net_client_->RetryConnection();

            // notify reconnecting
            msg_notifier_->SendAppMessage(SdkMsgReconnect{});
        }
    }

    uint64_t ThunderSdk::GetLastHeartbeatTimestamp() {
        return last_heartbeat_callback_;
    }

    void ThunderSdk::ReportStatistics() {

    }

    void ThunderSdk::ClearFirstFrameState() {
        has_config_msg_ = false;
        has_video_frame_msg_ = false;
    }

    void ThunderSdk::IncreaseDecodeFailedCount(const std::string& mon_name) {
        auto count = decode_failed_counts_[mon_name];
        decode_failed_counts_[mon_name] = count + 1;
    }

    int ThunderSdk::GetDecodeFailedCount(const std::string& mon_name) {
        if (decode_failed_counts_.contains(mon_name)) {
            return decode_failed_counts_[mon_name];
        }
        return 0;
    }

    void ThunderSdk::ResetDecodeFailedCount(const std::string& mon_name) {
        decode_failed_counts_[mon_name] = 0;
    }

    void ThunderSdk::DisableHardwareDecoder(const std::string& mon_name) {
        hw_disabled_states_[mon_name] = true;
    }

    bool ThunderSdk::IsDisabledHardwareDecoder(const std::string& mon_name) {
        if (hw_disabled_states_.contains(mon_name)) {
            return hw_disabled_states_[mon_name];
        }
        return false;
    }

    void ThunderSdk::Exit() {
        LOGI("ThunderSdk start exiting.");
        exit_ = true;
        if (cast_receiver_) {
            cast_receiver_->Exit();
        }

        LOGI("Will exit app timer.");
        if (sdk_timer_) {
            sdk_timer_->Exit();
        }

        LOGI("Will exit ws client.");
        if (net_client_) {
            net_client_->Exit();
        }

        LOGI("will exit video decoder.");
        for (const auto& [mon_name, video_decoder] : video_decoders_) {
            if (video_decoder) {
                video_decoder->Release();
            }
        }

        LOGI("Will exit video thread.");
        if (video_thread_) {
            video_thread_->Exit();
        }
        LOGI("Will exit audio thread.");
        if (audio_thread_) {
            audio_thread_->Exit();
        }
        LOGI("Will exit audio_spectrum_thread thread");
        if (misc_thread_) {
            misc_thread_->Exit();
        }

        LOGI("ThunderSdk exited");
    }
}
