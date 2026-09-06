//
// Created by RGAA on 2023/12/26.
//

#include "thunder_sdk.h"

#include "px_common/log.h"
#include "px_common/file.h"
#include "px_common/message_notifier.h"
#include "px_common/thread.h"
#include "px_common/time_util.h"
#include <atomic>
#include <span>
#include "px_common/folder_util.h"
#include "px_common/string_util.h"
#include "px_client_sdk/gl/raw_image.h"
#include "px_opus_codec/opus_codec.h"
#include "px_message.pb.h"
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
#include "px_message/proto_converter.h"
#ifdef WIN32
#include "px_common/hardware.h"
#endif

namespace px
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
        Exit();
    }

    bool ThunderSdk::Init(const std::shared_ptr<ThunderSdkParams>& params, void* surface, const DecoderRenderType& drt) {
        sdk_params_ = params;
        drt_ = drt;
        render_surface_handle_.store(reinterpret_cast<std::uintptr_t>(surface));
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

    void ThunderSdk::UpdateRenderSurface(const std::uintptr_t surface_handle, OnRenderSurfaceUpdated&& completion) {
        render_surface_handle_.store(surface_handle);
        render_surface_update_pending_.store(true, std::memory_order_release);
        const auto thread = video_thread_;
        if (!thread || exit_) {
            render_surface_update_pending_.store(false, std::memory_order_release);
            if (completion) completion();
            return;
        }
        thread->Clear();
        need_clear_video_tasks_.store(false, std::memory_order_release);
        const auto weak_self = weak_from_this();
        thread->Post(SimpleThreadTask::Make([weak_self, surface_handle, completion = std::move(completion)]() mutable {
            const auto self = weak_self.lock();
            if (self) {
                if (!self->exit_) {
                    bool surface_updated = surface_handle != 0U && !self->video_decoders_.empty();
                    for (const auto& [monitor_name, decoder] : self->video_decoders_) {
                        static_cast<void>(monitor_name);
                        if (!decoder->UpdateRenderSurface(surface_handle)) {
                            surface_updated = false;
                            break;
                        }
                    }
                    if (!surface_updated) {
                        for (const auto& [monitor_name, decoder] : self->video_decoders_) {
                            static_cast<void>(monitor_name);
                            decoder->Release();
                        }
                        self->video_decoders_.clear();
                    }
                }
                self->render_surface_update_pending_.store(false, std::memory_order_release);
                if (!self->exit_ && surface_handle != 0U) self->RequestIFrame();
            }
            if (completion) completion();
        }));
    }

    void ThunderSdk::Start() {
        const auto weak_self = weak_from_this();
        statistics_ = SdkStatistics::Instance();
        statistics_->render_type_.Update(sdk_params_->render_type_name_);
        // threads
        video_thread_ = Thread::Make("video", 64);
        video_thread_->SetOnFrontTaskCallback([weak_self](ThreadTaskPtr task_tr) ->void{
            const auto self = weak_self.lock();
            if (!self) return;
            if (self->video_frame_thread_discarded_cbk_) {
                self->video_frame_thread_discarded_cbk_();
                self->need_clear_video_tasks_ = true;
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

        net_client_->SetOnConnectCallback([weak_self]() {
            if (const auto self = weak_self.lock()) {
                self->SendHelloMessage();
                self->msg_notifier_->SendAppMessage(SdkMsgNetworkConnected{});
            }
        });

        net_client_->SetOnDisconnectedCallback([weak_self]() {
            if (const auto self = weak_self.lock()) {
                self->msg_notifier_->SendAppMessage(SdkMsgNetworkDisConnected{});
                self->ClearFirstFrameState();
            }
        });

        net_client_->SetOnVideoFrameMsgCallback([weak_self](std::shared_ptr<px::Message> msg) {
            const auto owner = weak_self.lock();
            if (!owner || owner->exit_) { return; }
            if (owner->encoded_video_frame_cbk_) {
                owner->encoded_video_frame_cbk_(msg);
            }
            
            px::VideoFrame frame = msg->video_frame();

            auto video_task = [weak_self, frame]() ->void {
                const auto self = weak_self.lock();
                if (!self || self->exit_) return;
                auto& video_decoders_ = self->video_decoders_;
                auto& last_received_video_timestamps_ = self->last_received_video_timestamps_;
                auto& last_frame_indices_ = self->last_frame_indices_;
                auto& received_files_ = self->received_files_;
                auto sdk_params_ = self->sdk_params_;
                auto statistics_ = self->statistics_;
                auto render_surface = reinterpret_cast<void*>(self->render_surface_handle_.load()); // NOLINT(gammaray-raw-pointer-boundary)
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
                        video_decoder = std::make_shared<FFmpegVulkanDecoder>(self);
                    }
                    else {
                        video_decoder = std::make_shared<FFmpegDecoder>(self);
                    }
                    auto r = video_decoder->Init(frame.mon_name(), frame.type(), frame.frame_width(), frame.frame_height(), frame.data(), render_surface, frame.image_format(),
                        self->IsDisabledHardwareDecoder(frame.mon_name()));
                    if (r != 0) {
                        LOGE("Init D3D11VA decoder failed, will try software decoder");
                        video_decoder->Release();
                        video_decoder.reset();
                    }

                    if (!self->IsDisabledHardwareDecoder(frame.mon_name())) {
                    }
                    else {
                        LOGI("Hardware decoder for: {} is disabled, use software.", frame.mon_name());
                    }
#endif

                    if (!video_decoder) {
                        // Android
                        // Begin
#ifdef ANDROID
                        auto codec = (self->drt_ == DecoderRenderType::kMediaCodecSurface || self->drt_ == DecoderRenderType::kMediaCodecNv21) ? SupportedCodec::kMediaCodec : SupportedCodec::kFFmpeg;
                        video_decoder = VideoDecoderFactory::Make(self, codec);
                        LOGI("Create video decoder, codec: {}", (int)codec);
                        // Android
                        // End
#else
                        video_decoder = std::make_shared<FFmpegVideoDecoder>(self);
#endif
                        bool ready = video_decoder->Ready();
                        if (!ready) {
                            auto result = video_decoder->Init(frame.mon_name(), frame.type(), frame.frame_width(),
                                frame.frame_height(), frame.data(), render_surface, frame.image_format(), false);
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

                self->PostMiscTask([statistics_, frame, diff]() {
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
                const auto previous_frame_index = last_frame_indices_[mon_name];
                const auto current_frame_index = static_cast<int64_t>(frame.frame_index());
                if (current_frame_index <= previous_frame_index) {
                    LOGI("Video frame stream reset, mon: [{}], index: {}, last: {}, extra: [{}]",
                         mon_name, current_frame_index, previous_frame_index, frame.extra());
                }
                else {
                    const auto frame_diff = current_frame_index - previous_frame_index;
                    if (frame_diff != 1) {
                        LOGI("Video frame came, mon: [{}], index: {}, diff: {}, last: {}, extra: [{}]",
                             mon_name, current_frame_index, frame_diff, previous_frame_index, frame.extra());
                    }
                }
                last_frame_indices_[mon_name] = current_frame_index;

                if (sdk_params_->debug_) {
                    if (!received_files_.contains(mon_name)) {
                        auto display_name = mon_name.size() > 4 ? mon_name.substr(4) : mon_name;
                        auto file_path = StringUtil::ToUTF8(FolderUtil::GetProgramDataPath()) + "/px_data/client/recv_" + display_name + ".h264";
                        auto recv_video_file = File::OpenForWriteB(PathFromUTF8(file_path));
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
                    self->IncreaseDecodeFailedCount(frame.mon_name());
                    if (self->GetDecodeFailedCount(frame.mon_name()) > 60) {
                        self->ResetDecodeFailedCount(frame.mon_name());
                        self->DisableHardwareDecoder(frame.mon_name());
                        LOGE("decode error: {}, will recreate the decoder", ret.error());
                        video_decoder->Release();
                        video_decoders_.erase(frame.mon_name());
                        LOGW("Video decoder for : {} is released.", frame.mon_name());
                    }
                    else if (self->GetDecodeFailedCount(frame.mon_name()) > 30) {
                        self->RequestIFrame();
                        LOGE("decode error: {}, will request Key Frame", ret.error());
                    }
                }

                // test
                if (false) {
                    static bool recreate_destroy_decoder = false;
                    self->IncreaseDecodeFailedCount(frame.mon_name());
                    if (!recreate_destroy_decoder) {
                        if (self->GetDecodeFailedCount(frame.mon_name()) > 150) {
                            recreate_destroy_decoder = true;
                            self->ResetDecodeFailedCount(frame.mon_name());
                            self->DisableHardwareDecoder(frame.mon_name());
                            LOGE("decode error: {}, will recreate the decoder", ret.error());
                            video_decoder->Release();
                            video_decoders_.erase(frame.mon_name());
                            LOGW("Video decoder for : {} is released.", frame.mon_name());
                        }
                    }
                }
                // test

                if (self->exit_) {
                    return;
                }
                if (!ret.has_value()) {
                    if (ret.error() != 0) LOGE("Video decoder produced an error: {}", ret.error());
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
                if (self->video_frame_cbk_) {
                    self->video_frame_cbk_(raw_image, cap_mon_info);
                }

                if (!self->has_video_frame_msg_) {
                    self->has_video_frame_msg_ = true;
                    self->SendFirstFrameMessage(raw_image, cap_mon_info);
                }
            };

            owner->PostVideoTask(std::move(video_task), frame.frame_index(), frame.mon_name());
        });

        net_client_->SetOnAudioFrameMsgCallback([weak_self](std::shared_ptr<px::Message> msg) {
            const auto owner = weak_self.lock();
            if (!owner || owner->exit_) { return; }
            if (owner->encoded_audio_frame_cbk_) {
                owner->encoded_audio_frame_cbk_(msg);
            }
            owner->PostAudioTask([weak_self, msg = std::move(msg)]() {
                const auto self = weak_self.lock();
                if (!self || self->exit_) return;
                auto frame = msg->audio_frame();
                auto beg = TimeUtil::GetCurrentTimestamp();
                if (!self->audio_decoder_) {
                    self->audio_decoder_ = std::make_shared<OpusAudioDecoder>(frame.samples(), frame.channels());
                }
                std::vector<opus_int16> pcm_data;
                if (frame.data().empty()) {
                    // UDP 丢帧信号(extra="udp_lost"):无码流可解,走 Opus PLC 补一帧 20ms
                    pcm_data = self->audio_decoder_->DecodeDummy(frame.frame_size());
                }
                else {
                    std::vector<unsigned char> buffer(frame.data().begin(), frame.data().end());
                    pcm_data = self->audio_decoder_->Decode(buffer, frame.frame_size(), false);
                }
                if (self->audio_frame_cbk_) {
                    auto data = Data::Copy(
                        std::span<const char>{reinterpret_cast<const char*>(pcm_data.data()), pcm_data.size() * sizeof(pcm_data.front())});
                    self->audio_frame_cbk_(data, frame.samples(), frame.channels(), frame.bits());
                }
                //LOGI("opus data size: {}, frame size: {}, samples: {}, channel: {}, PCM data size in char : {}", frame.data().size(), frame.frame_size(), frame.samples(), frame.channels(), pcm_data.size()*2);
                if (self->debug_audio_decoder_) {
                    static FilePtr pcm_audio = File::OpenForWriteB(PathFromUTF8("1.test.pcm"));
                    pcm_audio->Append(std::span<const char>{reinterpret_cast<const char*>(pcm_data.data()),
                                                           pcm_data.size() * sizeof(int16_t)});
                }
                auto end = TimeUtil::GetCurrentTimestamp();
                //LOGI("decode audio : {}", end-beg);
            });
        });

        net_client_->SetOnAudioSpectrumCallback([weak_self](std::shared_ptr<px::Message> msg) {
            const auto owner = weak_self.lock();
            if (!owner || owner->exit_) { return; }
            owner->PostMiscTask([weak_self, msg = std::move(msg)]() {
                if (const auto self = weak_self.lock(); self && !self->exit_ && self->audio_spectrum_cbk_) {
                    self->audio_spectrum_cbk_(msg);
                }
            });
        });

        // Decoded I420 callbacks run on a libwebrtc decoder thread. Never do
        // application event dispatch or UI handoff on that thread: a blocked
        // EventBus/UI path would stop the decoder after its first frame and make
        // the native client appear frozen. Reuse the bounded video worker used
        // by the encoded-frame decode path.
        net_client_->SetOnRtcLocalVideoFrameCallback([weak_self](int w, int h, std::shared_ptr<Data> i420) {
            const auto owner = weak_self.lock();
            if (!owner || owner->exit_) return;
            const auto frame_index = ++owner->rtc_video_frame_index_;
            if (frame_index == 1) {
                LOGI("WebRTC first decoded frame queued on SDK video worker: {}x{}", w, h);
            }
            owner->PostVideoTask([weak_self, w, h, i420 = std::move(i420), frame_index]() {
                const auto self = weak_self.lock();
                if (!self || self->exit_) return;
                if (frame_index == 1) {
                    LOGI("WebRTC first decoded frame executing on SDK video worker");
                }
                self->OnRtcLocalVideoFrame(w, h, i420);
            }, frame_index, "rtc");
        });

        // decoded audio(16-bit interleaved PCM) from the webrtc local(direct) connection:
        // already decoded by webrtc's built-in opus decoder, feed the player directly
        net_client_->SetOnRtcLocalAudioCallback([weak_self](std::shared_ptr<Data> pcm, int sample_rate, int channels) {
            const auto owner = weak_self.lock();
            if (!owner || owner->exit_) { return; }
            owner->PostAudioTask([weak_self, pcm = std::move(pcm), sample_rate, channels]() {
                if (const auto self = weak_self.lock(); self && !self->exit_ && self->audio_frame_cbk_) {
                    self->audio_frame_cbk_(pcm, sample_rate, channels, 16);
                }
            });
        });

        net_client_->Start();

        // rtc local encoded-sink mode, old-render compat: when the answer carries no
        // "monitors" array, the single dynamic track is mapped to the capturing
        // monitor reported via ServerConfiguration
        net_client_->SetRtcLocalCapturingMonitorNameProvider([weak_self]() {
            if (const auto self = weak_self.lock()) {
                std::lock_guard<std::mutex> lk(self->rtc_cap_mon_mtx_);
                return self->rtc_capturing_monitor_name_;
            }
            return std::string{};
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

        if (!has_video_frame_msg_) {
            LOGI("WebRTC first decoded frame entered ThunderSdk processing: {}x{}", w, h);
        }

        // statistics, keep the speed chart alive
        statistics_->AppendRecvDataSize(i420->Size());
        const bool first_rtc_frame = !has_video_frame_msg_;
        if (first_rtc_frame) {
            LOGI("WebRTC first decoded frame statistics updated, bytes={}", i420->Size());
        }

        auto raw_image = RawImage::MakeI420(i420->MutableBytes().data(), (int)i420->Size(), w, h);
        if (first_rtc_frame) {
            LOGI("WebRTC first decoded frame copied into RawImage");
        }
        // rtc mode carries a single video stream, report it as the capturing monitor.
        // use the REAL monitor name from ServerConfiguration: the render's event replayer
        // drops mouse events tagged with an unknown monitor name.
        const auto mon_name = [&]() {
            std::lock_guard<std::mutex> lk(rtc_cap_mon_mtx_);
            return !rtc_capturing_monitor_name_.empty()
                   ? rtc_capturing_monitor_name_
                   : std::string("rtc_local");
        }();
        if (first_rtc_frame) {
            LOGI("WebRTC first decoded frame monitor resolved: {}", mon_name);
        }
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

        auto statistics = statistics_;
        PostMiscTask([statistics, cap_mon_info, w, h]() {
            statistics->TickVideoRecvFps(cap_mon_info.mon_name_);
            statistics->UpdateFrameSize(cap_mon_info.mon_name_, w, h);
        });
        if (first_rtc_frame) {
            LOGI("WebRTC first decoded frame statistics task queued");
        }

        if (video_frame_cbk_) {
            if (first_rtc_frame) {
                LOGI("WebRTC first decoded frame dispatching to workspace");
            }
            video_frame_cbk_(raw_image, cap_mon_info);
            if (first_rtc_frame) {
                LOGI("WebRTC first decoded frame workspace dispatch returned");
            }
        }

        if (!has_video_frame_msg_) {
            has_video_frame_msg_ = true;
            LOGI("WebRTC first decoded frame notifying application listeners");
            SendFirstFrameMessage(raw_image, cap_mon_info);
            LOGI("WebRTC first decoded frame application notification returned");
        }
    }

    void ThunderSdk::PostMediaMessage(std::shared_ptr<Data> msg) {
        if(net_client_ && msg) {
            net_client_->PostMediaMessage(msg);
        }
    }

    FileTransferSendResult ThunderSdk::PostFileTransferMessage(std::shared_ptr<Data> msg) {
        if (!msg) {
            return FileTransferSendResult::TransportError("file-transfer message is empty");
        }
        if (!net_client_) {
            return FileTransferSendResult::Disconnected("network client is unavailable");
        }
        return net_client_->PostFileTransferMessage(std::move(msg));
    }

    void ThunderSdk::RegisterEventListeners() {
        const auto weak_self = weak_from_this();
        msg_listener_ = msg_notifier_->CreateListener(MessageExecutionLane::kControl);
        state_msg_listener_ = msg_notifier_->CreateListener(MessageExecutionLane::kState);

        // notify to net client
        msg_listener_->Listen<SdkMsgTimer16>([weak_self](const auto&) {
            if (const auto self = weak_self.lock(); self && self->net_client_) {
                self->net_client_->On16msTimeout();
            }
        });

        state_msg_listener_->Listen<SdkMsgTimer1000>([weak_self](const auto&) {
            if (const auto owner = weak_self.lock()) {
                owner->PostMiscTask([weak_self]() {
                    if (const auto self = weak_self.lock(); self && !self->exit_) {
                        self->statistics_->CalculateDataSpeed();
                        self->statistics_->CalculateVideoFrameFps();
                        self->ReportStatistics();
                    }
                });
            }
        });

        // remote device offline
        msg_listener_->Listen<SdkMsgRelayRemoteDeviceOffline>([weak_self](const SdkMsgRelayRemoteDeviceOffline&) {
            if (const auto self = weak_self.lock()) {
                self->ClearFirstFrameState();
            }
        });
    }

    void ThunderSdk::SendHelloMessage() {
        if (!net_client_) {
            return;
        }
        px::Message msg;
        msg.set_type(px::MessageType::kHello);
        msg.set_device_id(sdk_params_->device_id_);
        msg.set_stream_id(sdk_params_->stream_id_);
        auto hello = msg.mutable_hello();
        hello->set_enable_audio(sdk_params_->enable_audio_);
        hello->set_enable_video(sdk_params_->enable_video_);
        hello->set_client_type(sdk_params_->client_type_);
        hello->set_enable_controller(sdk_params_->enable_controller_);
        hello->set_device_name(sdk_params_->device_name_);
        if (auto buffer = px::ProtoAsData(&msg); buffer) {
            net_client_->PostMediaMessage(buffer);
        }
    }

    void ThunderSdk::RequestIFrame() {
        if (!net_client_) {
            return;
        }
        px::Message msg;
        msg.set_type(MessageType::kInsertKeyFrame);
        msg.set_device_id(sdk_params_->device_id_);
        msg.set_stream_id(sdk_params_->stream_id_);
        auto ack = msg.mutable_ack();
        ack->set_type(MessageType::kInsertKeyFrame);
        if (auto buffer = px::ProtoAsData(&msg); buffer) {
            net_client_->PostMediaMessage(buffer);
        }
    }

    void ThunderSdk::PostVideoTask(std::function<void()>&& task, int64_t frame_index, const std::string& monitor_name) {
        if (!video_thread_ || exit_ || render_surface_update_pending_.load(std::memory_order_acquire)) return;
        auto video_task = VideoDecodeThreadTask::Make(std::move(task));
        video_task->frame_index_ = frame_index;
        video_task->monitor_name_ = monitor_name;
        if (need_clear_video_tasks_.exchange(false, std::memory_order_acq_rel)) {
            RequestIFrame();
            video_thread_->Clear();
        }
        video_thread_->Post(video_task);
    }

    void ThunderSdk::PostAudioTask(std::function<void()>&& task) {
        if (audio_thread_ && !exit_) {
            audio_thread_->Post(SimpleThreadTask::Make(std::move(task)));
        }
    }

    void ThunderSdk::PostMiscTask(std::function<void()>&& task) {
        if (misc_thread_ && !exit_) {
            misc_thread_->Post(SimpleThreadTask::Make(std::move(task)));
        }
    }

    void ThunderSdk::SetOnAudioSpectrumCallback(OnAudioSpectrumCallback&& cbk) {
        audio_spectrum_cbk_ = std::move(cbk);
    }

    void ThunderSdk::SetOnCursorInfoCallback(px::OnCursorInfoSyncMsgCallback&& cbk) {
        if (net_client_) {
            net_client_->SetOnCursorInfoSyncMsgCallback(std::move(cbk));
        }
    }

    void ThunderSdk::SetOnHeartBeatCallback(px::OnHeartBeatInfoCallback&& cbk) {
        if (net_client_) {
            const auto weak_self = weak_from_this();
            net_client_->SetOnHeartBeatCallback([weak_self, callback = std::move(cbk)](auto m) {
                if (const auto self = weak_self.lock()) {
                    self->last_heartbeat_callback_ = TimeUtil::GetCurrentTimestamp();
                    callback(std::move(m));
                }
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
            const auto weak_self = weak_from_this();
            net_client_->SetOnServerConfigurationCallback(
                [weak_self, callback = std::move(cbk)](std::shared_ptr<px::Message> msg) mutable {
                const auto self = weak_self.lock();
                if (!self) return;
                if (msg && msg->has_config() && !msg->config().capturing_monitor_name().empty()) {
                    std::lock_guard<std::mutex> lk(self->rtc_cap_mon_mtx_);
                    self->rtc_capturing_monitor_name_ = msg->config().capturing_monitor_name();
                }
                auto first_config_message = msg;
                callback(std::move(msg));
                if (!self->has_config_msg_.exchange(true)) {
                    self->msg_notifier_->SendAppMessage(SdkMsgFirstConfigInfoCallback {
                        .msg_ = std::move(first_config_message),
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

    bool ThunderSdk::RestartRtcIce(const std::string& ice_config_json,
                                   const std::string& connection_ticket,
                                   const std::string& client_nonce,
                                   const std::string& instance_id,
                                   std::uint64_t revision) {
        return net_client_ && net_client_->RestartRtcIce(
            ice_config_json, connection_ticket, client_nonce, instance_id, revision);
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
        if (exit_.exchange(true)) {
            return;
        }
        LOGI("ThunderSdk start exiting.");
        msg_listener_.reset();
        state_msg_listener_.reset();
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
