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
#include "sdk_stream_helper.h"
#include "sdk_video_decoder.h"
#include "video_decode_thread_task.h"
#include "px_message/proto_converter.h"

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

    bool ThunderSdk::Init(const std::shared_ptr<ThunderSdkParams>& params, std::shared_ptr<VideoDecoderFactory> decoder_factory) {
        if (!params || !decoder_factory || net_client_ || exit_) return false;
        sdk_params_ = params;
        decoder_factory_ = std::move(decoder_factory);
        last_heartbeat_callback_ = TimeUtil::GetCurrentTimestamp();

        // Device identity is supplied by the host; the shared session does not query desktop APIs.

        // The session composition boundary projects only transport values. The
        // network runtime must not retain renderer devices or mutable UI params.
        net_client_ = std::make_shared<NetClient>(
            SdkConnectionParams{
                .ssl_ = params->ssl_,
                .enable_audio_ = params->enable_audio_,
                .enable_video_ = params->enable_video_,
                .file_transfer_only_ = params->file_transfer_only_,
                .ip_ = params->ip_,
                .port_ = params->port_,
                .udp_port_ = params->udp_port_,
                .media_path_ = params->media_path_,
                .ft_path_ = params->ft_path_,
                .device_id_ = params->device_id_,
                .stream_id_ = params->stream_id_,
                .connection_ticket_ = params->connection_ticket_,
                .connection_nonce_ = params->connection_nonce_,
                .connection_instance_id_ = params->connection_instance_id_,
                .udp_media_association_ = params->udp_media_association_,
            },
            msg_notifier_);
        return true;
    }

    void ThunderSdk::RefreshVideoOutput(const bool output_available, OnRenderSurfaceUpdated&& completion, std::function<void()> configure_output) {
        output_available_.store(output_available, std::memory_order_release);
        render_surface_update_pending_.store(true, std::memory_order_release);
        const auto thread = video_thread_;
        if (!thread || exit_) {
            if (!exit_ && configure_output) configure_output();
            render_surface_update_pending_.store(false, std::memory_order_release);
            if (completion) completion();
            return;
        }
        thread->Clear();
        need_clear_video_tasks_.store(false, std::memory_order_release);
        const auto weak_self = weak_from_this();
        thread->Post(SimpleThreadTask::Make([weak_self, output_available, completion = std::move(completion),
                                            configure_output = std::move(configure_output)]() mutable {
            const auto self = weak_self.lock();
            if (self) {
                if (!self->exit_) {
                    if (configure_output) configure_output();
                    bool surface_updated = output_available && !self->video_decoders_.empty();
                    for (const auto& [monitor_name, decoder] : self->video_decoders_) {
                        static_cast<void>(monitor_name);
                        if (!decoder->RefreshOutput()) {
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
                if (!self->exit_ && output_available) self->RequestIFrame();
            }
            if (completion) completion();
        }));
    }

    void ThunderSdk::Start() {
        if (!net_client_ || !decoder_factory_ || exit_ || started_.exchange(true)) return;
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
                if (!self || self->exit_ || !self->output_available_.load(std::memory_order_acquire)) return;
                auto& video_decoders_ = self->video_decoders_;
                auto& last_received_video_timestamps_ = self->last_received_video_timestamps_;
                auto& last_frame_indices_ = self->last_frame_indices_;
                auto& received_files_ = self->received_files_;
                auto sdk_params_ = self->sdk_params_;
                auto statistics_ = self->statistics_;
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
                    const auto configured = StreamHelper::HasDecoderConfiguration(frame.type() == px::kNetHevc, frame.data());
                    const auto decision = self->decoder_startup_gates_[monitor_name].Observe(
                        frame.key(), configured, std::chrono::steady_clock::now());
                    if (decision != DecoderStartupGate::Decision::kDecode) {
                        if (decision == DecoderStartupGate::Decision::kRequestKeyFrame) {
                            LOGI("Waiting for decoder startup key frame and complete parameter sets");
                            self->RequestIFrame();
                        }
                        return;
                    }
                    if (!self->decoder_factory_->SupportsMultipleStreams()) {
                        for (const auto& [name, decoder] : video_decoders_) {
                            static_cast<void>(name);
                            decoder->Release();
                        }
                        video_decoders_.clear();
                    }
                    auto created = self->decoder_factory_->Create(self, frame, self->IsDisabledHardwareDecoder(monitor_name));
                    if (created.disable_hardware) self->DisableHardwareDecoder(monitor_name);
                    video_decoder = std::move(created.decoder);
                    if (!video_decoder) {
                        self->NotifyDecoderUnavailable();
                        return;
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
                        const auto hardware_was_enabled = !self->IsDisabledHardwareDecoder(frame.mon_name());
                        self->DisableHardwareDecoder(frame.mon_name());
                        LOGE("decode error: {}, will recreate the decoder", ret.error());
                        video_decoder->Release();
                        video_decoders_.erase(frame.mon_name());
                        LOGW("Video decoder for : {} is released.", frame.mon_name());
                        if (!hardware_was_enabled) self->NotifyDecoderUnavailable();
                    }
                    else if (self->GetDecodeFailedCount(frame.mon_name()) > 30) {
                        self->RequestIFrame();
                        LOGE("decode error: {}, will request Key Frame", ret.error());
                    }
                } else if (ret.has_value()) {
                    self->ResetDecodeFailedCount(frame.mon_name());
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

        net_client_->Start();

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
                    }
                });
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
        if (!video_thread_ || exit_ || !output_available_.load(std::memory_order_acquire) ||
            render_surface_update_pending_.load(std::memory_order_acquire)) return;
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

    uint64_t ThunderSdk::GetLastHeartbeatTimestamp() {
        return last_heartbeat_callback_;
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

    void ThunderSdk::NotifyDecoderUnavailable() {
        if (decoder_failure_notified_.exchange(true) || !video_decoder_failure_cbk_) return;
        video_decoder_failure_cbk_();
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

        // Stop the video worker before touching its decoder map. Release after
        // draining, so initialization/decoding cannot race shutdown.
        LOGI("will exit video decoder.");
        for (const auto& [mon_name, video_decoder] : video_decoders_) {
            if (video_decoder) {
                video_decoder->Release();
            }
        }

        video_decoders_.clear();
        decoder_factory_.reset();

        LOGI("ThunderSdk exited");
    }
}
