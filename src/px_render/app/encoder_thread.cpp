//
// Created by RGAA on 2023-12-24.
//

#include "encoder_thread.h"
#include <d3d11.h>
#include <memory>
#include <wrl/client.h>
#include "rd_app.h"
#include "rd_context.h"
#include "px_common/data.h"
#include "px_common/image.h"
#include "px_common/thread.h"
#include "px_common/file.h"
#include "px_common/log.h"
#include "px_common/time_util.h"
#include "px_common/message_notifier.h"
#include "px_encoder/video_encoder_factory.h"
#include "px_encoder/video_encoder.h"
#include "px_encoder/ffmpeg_video_encoder.h"
#include "px_encoder/nvenc_video_encoder.h"
#include "settings/rd_settings.h"
#include "app/app_messages.h"
#include "rd_statistics.h"
#include "px_common/win32/d3d_debug_helper.h"
#include "px_render/modules/render_module_registry.h"
#include "px_render/modules/module_ids.h"
#include "architecture/observers/frame_debugger_observer.h"
#include "architecture/processors/frame_carrier_processor.h"
#include "architecture/processors/frame_resizer_processor.h"
#include "architecture/encoders/video_encoder_module.h"
#include "network/net_message_maker.h"
#include "px_message.pb.h"

#define DEBUG_FILE 0

namespace
{
    // 生产侧诊断:每 5s 输出一次编码线程负载,用于定位"采集 60fps 但编码只产出 40fps"
    // 到底卡在哪一环:输入速率 / 主任务耗时 / 队列积压 / YUV->编码等待 / 纹理拷贝耗时
    struct EncThreadDiag {
        std::atomic_uint64_t in_frames{0};      // Encode() 调用次数(采集喂入速率)
        std::atomic_int64_t  in_flight{0};      // enc 线程队列积压(post 时 ++,执行完 --)
        std::atomic_uint64_t task_dones{0};     // enc 线程完成的主任务数
        std::atomic_int64_t  task_time_sum{0};  // 主任务执行耗时累计(ms)
        std::atomic_int64_t  task_time_max{0};
        std::atomic_int64_t  copy_tex_sum{0};   // CopyTexture 耗时累计(ms)
        std::atomic_uint64_t enc_posts{0};      // yuv 回调投递的编码任务数
        std::atomic_int64_t  enc_wait_sum{0};   // 编码任务 post->run 等待累计(ms)
        std::atomic_int64_t  enc_wait_max{0};
        int64_t window_beg = 0;

        void DumpIfDue() {
            auto now = px::TimeUtil::GetCurrentTimestamp();
            if (window_beg == 0) { window_beg = now; return; }
            auto wall = now - window_beg;
            if (wall < 5000) return;
            window_beg = now;
            auto n_task = task_dones.exchange(0);
            auto tsum = task_time_sum.exchange(0);
            auto tmax = task_time_max.exchange(0);
            auto csum = copy_tex_sum.exchange(0);
            auto ep = enc_posts.exchange(0);
            auto ewsum = enc_wait_sum.exchange(0);
            auto ewmax = enc_wait_max.exchange(0);
            auto in = in_frames.exchange(0);
            LOGI("enc diag: wall={}ms, in_fps={:.1f}, backlog={}, task_avg={}ms, task_max={}ms, "
                 "copy_tex_avg={}ms, enc_posts={}, enc_wait_avg={}ms, enc_wait_max={}ms",
                 wall, wall > 0 ? in * 1000.0 / wall : 0.0, in_flight.load(),
                 n_task > 0 ? tsum / (int64_t)n_task : 0, tmax,
                 n_task > 0 ? csum / (int64_t)n_task : 0,
                 ep, ep > 0 ? ewsum / (int64_t)ep : 0, ewmax);
        }
    };
    EncThreadDiag g_enc_diag;
}

namespace px
{

    std::shared_ptr<EncoderThread> EncoderThread::Make(const std::shared_ptr<RdApplication>& app) {
        auto encoder = std::make_shared<EncoderThread>(app);
        encoder->InitListener();
        return encoder;
    }

    EncoderThread::EncoderThread(const std::shared_ptr<RdApplication>& app)
        : settings_(*RdSettings::Instance()) {
        app_ = app;
        stat_ = RdStatistics::Instance();
        context_ = app->GetContext();
        module_registry_ = context_->GetRenderModuleRegistry();
        // 队列过小会频繁丢弃未执行任务;丢弃时若已 ++in_flight 会泄漏并把 backlog 抬飞。
        // 32 足以吸收短时尖峰,同时仍会在持续过载时丢最旧帧保实时性。
        enc_thread_ = Thread::Make("encoder_thread", 32);
        enc_thread_->Poll();

        frame_carrier_processor_ = context_->GetFrameCarrierProcessor();
        frame_resizer_processor_ = context_->GetFrameResizerProcessor();

    }

    EncoderThread::~EncoderThread() {
        Exit();
    }

    void EncoderThread::InitListener() {
        msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kControl);
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<MsgInsertKeyFrame>([weak_self](const MsgInsertKeyFrame&) {
            const auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->module_registry_) {
                return;
            }
            self->module_registry_->InsertIdr();
        });
    }

    void EncoderThread::Encode(const CaptureVideoFrame& cap_video_msg) {
        if (exiting_ || !frame_carrier_processor_) {
            return;
        }
        ++g_enc_diag.in_frames;
        ++g_enc_diag.in_flight;
        // 捕获到 lambda 里:任务被队列挤掉未执行时,shared_ptr 析构也会 --in_flight,避免 backlog 泄漏。
        auto inflight_guard = std::shared_ptr<void>(nullptr, [](void*) {
            --g_enc_diag.in_flight;
        });
        const auto weak_self = weak_from_this();
        PostEncTask([weak_self, cap_video_msg, inflight_guard]() {
            const auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            self->EncodeOnWorker(cap_video_msg, inflight_guard);
        });
    }

    void EncoderThread::EncodeOnWorker(
        CaptureVideoFrame cap_video_msg, const std::shared_ptr<void>& inflight_guard) {
            // 保持 inflight_guard 存活到本任务结束;若任务被队列挤掉未执行,lambda 析构时仍会 --in_flight
            (void)inflight_guard;
            auto diag_task_beg = TimeUtil::GetCurrentTimestamp();
            struct DiagGuard {
                int64_t beg_;
                ~DiagGuard() {
                    g_enc_diag.task_time_sum += TimeUtil::GetCurrentTimestamp() - beg_;
                    auto d = TimeUtil::GetCurrentTimestamp() - beg_;
                    auto prev = g_enc_diag.task_time_max.load();
                    while (d > prev && !g_enc_diag.task_time_max.compare_exchange_weak(prev, d)) {}
                    ++g_enc_diag.task_dones;
                    g_enc_diag.DumpIfDue();
                }
            } diag_guard{static_cast<std::int64_t>(diag_task_beg)};
            if (clear_encoders_) {
                clear_encoders_ = false;
                LOGW("clear all encoders!!!");
                std::lock_guard<std::mutex> lk(encoder_modules_mtx_);
                encoders_.clear();
            }

            auto adapter_uid = cap_video_msg.adapter_uid_;

            // plugins: SharedTexture
            if (cap_video_msg.handle_ > 0) {

                const auto module_registry = module_registry_;
                context_->PostMediaTask([module_registry, cap_video_msg]() {
                    module_registry->SubmitRtcLocalSharedTexture(
                        cap_video_msg.display_name_,
                        cap_video_msg.frame_index_,
                        cap_video_msg.frame_width_,
                        cap_video_msg.frame_height_,
                        cap_video_msg.handle_,
                        cap_video_msg.adapter_uid_,
                        cap_video_msg.frame_format_);
                });
            }

            auto settings = RdSettings::Instance();
            auto frame_index = cap_video_msg.frame_index_;
            //auto adapter_uid = cap_video_msg.adapter_uid_;
            auto monitor_name = std::string(cap_video_msg.display_name_);
            bool frame_meta_info_changed = [&]() {
                auto last_video_frame_exists = last_video_frames_.contains(monitor_name);
                if (!last_video_frame_exists) {
                    return true;
                }
                auto last_video_frame = last_video_frames_[monitor_name];
                if (last_video_frame == std::nullopt) {
                    return true;
                }
                return last_video_frame.value().frame_width_ != cap_video_msg.frame_width_
                    || last_video_frame.value().frame_height_ != cap_video_msg.frame_height_;
            }();

            bool full_color_mode_changed = false;
            auto target_encoder = GetEncoderForMonitor(monitor_name);

            // Size thrash (windowed ↔ exclusive fullscreen) used to Exit/recreate NVENC every
            // few seconds and drop the WebRTC picture. Wait until the new size is stable.
            // Important: do NOT pause the whole encode path for the debounce window — that made
            // glass-to-glass lag feel like ~1s+ while fps stayed smooth on the last good size.
            // Only drop frames whose capture size != current encoder; keep streaming matches.
            constexpr int64_t kSizeChangeDebounceMs = 800;
            if (frame_meta_info_changed && target_encoder) {
                const auto new_size = std::make_pair(cap_video_msg.frame_width_, cap_video_msg.frame_height_);
                const auto now_ms = TimeUtil::GetCurrentTimestamp();
                auto& pending = pending_frame_size_[monitor_name];
                auto& since = pending_frame_size_since_ms_[monitor_name];
                if (pending != new_size) {
                    pending = new_size;
                    since = now_ms;
                    LOGW("Capture size change pending {}x{} (debounce {}ms), keep current encoder",
                         new_size.first, new_size.second, kSizeChangeDebounceMs);
                }
                if (now_ms - since < kSizeChangeDebounceMs) {
                    auto enc_cfg = target_encoder->Configuration(monitor_name);
                    const bool size_matches_encoder = enc_cfg.has_value()
                        && enc_cfg->width == cap_video_msg.frame_width_
                        && enc_cfg->height == cap_video_msg.frame_height_;
                    if (!size_matches_encoder) {
                        return;
                    }
                    // Same as current encoder: keep encoding, suppress recreate for now.
                    frame_meta_info_changed = false;
                } else {
                    LOGI("Capture size settled at {}x{}, recreating encoder", new_size.first, new_size.second);
                }
            } else if (!frame_meta_info_changed) {
                pending_frame_size_.erase(monitor_name);
                pending_frame_size_since_ms_.erase(monitor_name);
            }
            if (target_encoder) {
                auto encoder_config_res = target_encoder->Configuration(monitor_name);
                if (encoder_config_res.has_value()) {
                    const auto selected_encoder_config = encoder_config_res.value();
                    if (selected_encoder_config.enable_full_color_mode_ != settings_.EnableFullColorMode() ) {
                        full_color_mode_changed = true;
                        LOGI("full_color_mode_changed!!!");
                    }
                } else {

                    LOGI("EncoderThread encoder_config_res no value");
                }
            }
            else {
                LOGI("EncoderThread target_encoder is nullptr, will create encoder.");
            }

            // 全彩强制 HEVC 只影响本次有效格式,不得改写 settings 里用户/启动参数选定的 encoder_format_,
            // 否则关全彩后会永久卡在 HEVC(WebRTC/多数 Win 端按 H264 解 → 黑屏)。
            const auto effective_format = settings_.EnableFullColorMode()
                ? Encoder::EncoderFormat::kHEVC
                : settings->encoder_.encoder_format_;
            const bool switched_to_hevc = (effective_format == Encoder::EncoderFormat::kHEVC
                                          && encoder_format_ != Encoder::EncoderFormat::kHEVC);

            if (full_color_mode_changed || frame_meta_info_changed || encoder_format_ != effective_format
                || !target_encoder || !target_encoder->IsEnabled()) {
                if (target_encoder) {
                    // todo : Test it!
                    target_encoder->Remove(monitor_name);
                    target_encoder = nullptr;
                }
                px::EncoderConfig encoder_config;
                // WebView OSR frames arrive as CPU BGRA images without a
                // desktop-capture module. Route frames through the CPU-input
                // encoder chain just like GDI frames; texture-only encoders
                // cannot consume this buffer.
                const bool is_cpu_frame = cap_video_msg.raw_image_ != nullptr && cap_video_msg.handle_ == 0;
                bool is_gdi_capture = is_cpu_frame ||
                    module_registry_->IsGdiCapture(app_->GetWorkingMonitorCaptureSource());
                if (settings_.encoder_.encode_res_type_ == Encoder::EncodeResolutionType::kOrigin || is_gdi_capture) {
                    encoder_config.width = cap_video_msg.frame_width_;
                    encoder_config.height = cap_video_msg.frame_height_;
                    encoder_config.encode_width = cap_video_msg.frame_width_;
                    encoder_config.encode_height = cap_video_msg.frame_height_;
                    encoder_config.frame_resize = false;
                } else {
                    encoder_config.width = settings_.encoder_.encode_width_;
                    encoder_config.height = settings_.encoder_.encode_height_;
                    encoder_config.encode_width = settings_.encoder_.encode_width_;
                    encoder_config.encode_height = settings_.encoder_.encode_height_;
                    // resize will be enabled when dda capture working
                    encoder_config.frame_resize = true;
                }

                if (settings_.EnableFullColorMode()) {
                    LOGI("full color mode, use HEVC (settings format kept: {})", (int)settings->encoder_.encoder_format_);
                }

                encoder_config.codec_type = effective_format == Encoder::EncoderFormat::kH264
                    ? px::EVideoCodecType::kH264 : px::EVideoCodecType::kHEVC;
                encoder_config.enable_adaptive_quantization = true;
                encoder_config.gop_size = -1;
                encoder_config.quality_preset = 1;
                // MUST have a value > 0
                encoder_config.fps = settings_.encoder_.fps_;
                if (encoder_config.fps < 15 || encoder_config.fps > 120) {
                    encoder_config.fps = 60;
                }
                encoder_config.multi_pass = px::ENvdiaEncMultiPass::kMultiPassDisabled;
                encoder_config.rate_control_mode = px::ERateControlMode::kRateControlModeCbr;
                encoder_config.sample_desc_count = 1;
                encoder_config.supports_intra_refresh = true;
                // frame carrier 会把非 8bit 捕获格式(如 UE5 D3D12 的 R10G10B10A2)
                // 转成 B8G8R8A8 再送编码;编码器输入格式必须与转换后的纹理一致,
                // 否则 NVENC 按 ABGR10 初始化却收到 BGRA 纹理,编码失败甚至挂起 GPU。
                const auto cap_fmt = static_cast<DXGI_FORMAT>(cap_video_msg.frame_format_);
                encoder_config.texture_format =
                    (cap_fmt == DXGI_FORMAT_B8G8R8A8_UNORM
                     || cap_fmt == DXGI_FORMAT_B8G8R8X8_UNORM
                     || cap_fmt == DXGI_FORMAT_R8G8B8A8_UNORM)
                    ? cap_video_msg.frame_format_
                    : static_cast<int>(DXGI_FORMAT_B8G8R8A8_UNORM);
                encoder_config.bitrate = settings->encoder_.bitrate_ * 1000000;
                encoder_config.adapter_uid_ = cap_video_msg.adapter_uid_;
                encoder_config.enable_full_color_mode_ = settings_.EnableFullColorMode();

                PrintEncoderConfig(encoder_config);

                // generate d3d device/context
                auto d3d_device = app_->GetD3DDevice(adapter_uid);
                auto d3d_context = app_->GetD3DContext(adapter_uid);
                if (d3d_device) {
                    const auto removed_reason = d3d_device->GetDeviceRemovedReason();
                    if (removed_reason != S_OK) {
                        app_->HandleD3DDeviceFailure(adapter_uid, std::format("device removed/reset: {}", static_cast<int>(removed_reason)));
                        d3d_device = nullptr;
                        d3d_context = nullptr;
                    }
                }

                if (!d3d_device || !d3d_context) {
                    if (!app_->GenerateD3DDevice(adapter_uid)) {
                        LOGE("Generate D3DDevice failed!");
                        app_->HandleD3DDeviceFailure(adapter_uid, "GenerateD3DDevice failed");
                        return;
                    }
                    d3d_device = app_->GetD3DDevice(adapter_uid);
                    d3d_context = app_->GetD3DContext(adapter_uid);
                }
                else {
                    LOGI("We use d3d device from capture.");
                }

                if (!d3d_device || !d3d_context) {
                    app_->HandleD3DDeviceFailure(adapter_uid, "empty D3D device/context after generation");
                    return;
                }

                module_registry_->UpdateModuleD3DResources(
                    adapter_uid, d3d_device, d3d_context);

                // video frame carrier
                const auto r = frame_carrier_processor_->InitializeMonitor(
                    render::FrameCarrierParams{
                        .monitor_id = monitor_name,
                        .device = d3d_device,
                        .device_context = d3d_context,
                        .adapter_uid = static_cast<std::uint64_t>(
                            cap_video_msg.adapter_uid_),
                        .full_color = encoder_config.enable_full_color_mode_,
                    });
                if (!r) {
                    LOGE("Init Frame Carrier failed");
                }

                // Create the encoder module.

                auto select_encoder_with_capability_func =
                    [=, &target_encoder](
                        const std::shared_ptr<VideoEncoderModule>& encoder,
                        const std::string& monitor_name) {
                    if (!encoder_config.enable_full_color_mode_) {
                        target_encoder = encoder;
                    }
                    else {
                        auto cap_res = encoder->Capability(monitor_name);
                        if (cap_res.has_value()) {
                            auto cap = cap_res.value();
                            if (px::EVideoCodecType::kH264 == encoder_config.codec_type) {
                                if (cap.support_h264_yuv444_) {
                                    target_encoder = encoder;
                                }
                            }
                            else if (px::EVideoCodecType::kHEVC == encoder_config.codec_type) {
                                if (cap.support_hevc_yuv444_) {
                                    target_encoder = encoder;
                                }
                            }
                        }
                    }
                };

                if (!target_encoder) {
                    LOGI("Hardware disabled? {}", hardware_disabled_.load());
                    // GDI 采集产出 CPU 裸帧,走 Encode(Image) 路径;NVENC/AMF 只实现纹理编码,
                    // Encode(Image) 基类返回 kNotImplemented。选了它们会在编码失败→清空→重建
                    // 中原地死循环,永远出不了图,必须直接跳到 FFmpeg 链(其 kNvEnc/kQsv 硬编
                    // 由 ffmpeg 内部完成 CPU→GPU 上传)。
                    auto nvenc_encoder = module_registry_->GetNvencEncoder();
                    if (!is_gdi_capture && !hardware_disabled_ && nvenc_encoder && nvenc_encoder->IsEnabled() &&
                        nvenc_encoder->Initialize(encoder_config, monitor_name)) {
                        select_encoder_with_capability_func(nvenc_encoder, monitor_name);
                    }

                    if (!target_encoder) {
                        LOGW("Init NVENC {}failed, will try AMF.", is_gdi_capture ? "skipped(GDI raw frames), " : "");
                        auto amf_encoder = module_registry_->GetAmfEncoder();
                        if (!is_gdi_capture && !hardware_disabled_ && amf_encoder && amf_encoder->IsEnabled() &&
                            amf_encoder->Initialize(encoder_config, monitor_name)) {
                            select_encoder_with_capability_func(amf_encoder, monitor_name);
                        }
                    }

                    auto ffmpeg_encoder = module_registry_->GetFFmpegEncoder();
                    if (!target_encoder) {
                        LOGW("Init AMF failed, will try FFmpeg(kNvEnc).");
                        // 让ffmpeg尝试硬编码初始化
                        encoder_config.Hardware = EHardwareEncoder::kNvEnc;
                        if (ffmpeg_encoder && ffmpeg_encoder->IsEnabled() && ffmpeg_encoder->Initialize(encoder_config, monitor_name)) {
                            select_encoder_with_capability_func(ffmpeg_encoder, monitor_name);
                        }
                    }

                    if (!target_encoder) {
                        // Intel Quick Sync 兜底:无 N/A 卡的机器(如只有 Intel 核显)
                        // 用 QSV 硬编,把 1080p 编码从 x264 软编的一个多大核上卸下来,
                        // 否则软编与采集/同机浏览器抢 CPU,DDA 采集被压到 32~40fps。
                        // 注意必须插在 FFmpeg(kAmf) 之前:kAmf 分支在 ffmpeg 插件内
                        // 实际映射为 libx264 且总能初始化成功,排在它后面永远轮不到。
                        LOGW("Init FFmpeg(kNvEnc) failed, will try FFmpeg(kQsv).");
                        encoder_config.Hardware = EHardwareEncoder::kQsv;
                        if (!hardware_disabled_ && ffmpeg_encoder && ffmpeg_encoder->IsEnabled() &&
                            ffmpeg_encoder->Initialize(encoder_config, monitor_name)) {
                            select_encoder_with_capability_func(ffmpeg_encoder, monitor_name);
                        }
                    }

                    if (!target_encoder) {
                        LOGW("Init FFmpeg(kQsv) failed, will try FFmpeg(kAmf).");
                        // 让ffmpeg尝试硬编码初始化
                        encoder_config.Hardware = EHardwareEncoder::kAmf;
                        if (ffmpeg_encoder && ffmpeg_encoder->IsEnabled() && ffmpeg_encoder->Initialize(encoder_config, monitor_name)) {
                            select_encoder_with_capability_func(ffmpeg_encoder, monitor_name);
                        }
                    }

                    if (!target_encoder) {
                        LOGW("Init FFmpeg(kAmf) failed, will try FFmpeg(kNone).");
                        //让ffmpeg尝试软件编码初始化
                        encoder_config.Hardware = EHardwareEncoder::kNone;
                        if (ffmpeg_encoder && ffmpeg_encoder->IsEnabled() && ffmpeg_encoder->Initialize(encoder_config, monitor_name)) {
                            select_encoder_with_capability_func(ffmpeg_encoder, monitor_name);
                        }
                    }

                    if (!target_encoder) {
                        LOGW("Init FFmpeg(kAmf) failed, will try FFmpeg(kNone). without capability!");
                        //让ffmpeg尝试软件编码初始化
                        encoder_config.Hardware = EHardwareEncoder::kNone;
                        if (ffmpeg_encoder && ffmpeg_encoder->IsEnabled() && ffmpeg_encoder->Initialize(encoder_config, monitor_name)) {
                            target_encoder = ffmpeg_encoder;
                        }
                    }

                    if (!target_encoder) {
                        LOGE("Init FFmpeg failed, we can't encode frame in this machine!");
                        return;
                    }
                }

                {
                    std::lock_guard<std::mutex> lk(encoder_modules_mtx_);
                    encoders_[monitor_name] = target_encoder;
                }
                LOGI("Selected encoder module: {}, version: {} for monitor: {}",
                     target_encoder->Name(), target_encoder->VersionName(), monitor_name);

                auto video_type = [=]() -> EncodedVideoType {
                    if (effective_format == Encoder::EncoderFormat::kH264) {
                        return EncodedVideoType::kH264;
                    } else if (effective_format == Encoder::EncoderFormat::kHEVC) {
                        return EncodedVideoType::kH265;
                    } else {
                        return EncodedVideoType::kH264;
                    }
                } ();

                if (const auto observer =
                        context_->GetFrameDebuggerObserver()) {
                    static_cast<void>(observer->SubmitEncoderReady(
                        render::VideoEncoderReady{
                            .monitor_id = monitor_name,
                            .codec = video_type ==
                                             EncodedVideoType::kH264
                                         ? "h264"
                                         : "h265",
                            .width = static_cast<std::uint32_t>(
                                encoder_config.width),
                            .height = static_cast<std::uint32_t>(
                                encoder_config.height),
                        }));
                }

                stat_->video_encoder_format_ = effective_format;

                encoder_format_ = effective_format;
                last_video_frames_[monitor_name] = cap_video_msg;

                // Win 客户端可解 H265;WebRTC 只协商 H264。切到 H265 时若有 RTC 连接则下发提示。
                if (switched_to_hevc) {
                    const bool full_color = settings_.EnableFullColorMode();
                    const std::string reason = full_color ? "full_color" : "encoder_format";
                    auto tip = NetMessageMaker::MakeVideoCodecChanged(px::VideoType::kNetHevc, full_color, reason);
                    if (module_registry_->PostRtcLocalMessage(tip, false)) {
                        LOGW("Notify WebRTC clients: pipeline switched to H265 ({})", reason);
                    }
                }
            }

            // from texture handle
            if (cap_video_msg.handle_ > 0 && frame_carrier_processor_) {
                // 1. copy shared texture
                auto beg = TimeUtil::GetCurrentTimestamp();
                auto cp_result = frame_carrier_processor_->CopyTexture(
                    monitor_name, cap_video_msg.handle_, frame_index);
                if (!cp_result || !cp_result->texture) {
                    LOGE("CopyTexture failed: empty result or texture");
                    return;
                }

                ComPtr<ID3D11Texture2D> target_texture = cp_result->texture;
                // 2. resize ?
                if (auto opt_config = target_encoder->Configuration(monitor_name);
                    opt_config.has_value() && opt_config.value().frame_resize) {
                    auto config = opt_config.value();
                    if (frame_resizer_processor_) {
                        const auto resize_device = app_->GetD3DDevice(adapter_uid);
                        const auto resize_context = app_->GetD3DContext(adapter_uid);
                        if (!resize_device || !resize_context) {
                            LOGE("Resize failed: D3D device/context unavailable for adapter {}",
                                 adapter_uid);
                            return;
                        }
                        auto t = frame_resizer_processor_->Process(
                            cp_result->texture, resize_device, resize_context,
                            adapter_uid, monitor_name,
                            static_cast<std::uint32_t>(config.encode_width),
                            static_cast<std::uint32_t>(config.encode_height));
                        if (t) {
                            target_texture = std::move(t);
                        }
                        else {
                            LOGE("Resize failed!");
                            return;
                        }
                    }
                }

                auto copy_texture_end = TimeUtil::GetCurrentTimestamp();
                auto diff_copy_texture = copy_texture_end - beg;
                stat_->CaptureInfo(monitor_name)->AppendCopyTextureDuration((int32_t)diff_copy_texture);
                g_enc_diag.copy_tex_sum += diff_copy_texture;

                //video_encoder_->Encode(target_texture, frame_index);
                bool can_encode_texture = false;
                if (target_encoder && target_encoder->CanEncodeTexture()) {
                    can_encode_texture = true;
                    // plugins: EncodeTexture
                    auto encode_result = target_encoder->Encode(target_texture, frame_index, cap_video_msg);
                    if (!encode_result.Success()) {
                        if (encode_result.type_ == VideoEncoderErrorType::kEncodeFailed) {
                            LOGW("<!!> Encode failed, will release this encoder for display and disable hardware: {}", monitor_name);
                            target_encoder->Remove(monitor_name);
                            {
                                std::lock_guard<std::mutex> lk(encoder_modules_mtx_);
                                encoders_.erase(monitor_name);
                            }
                            // disable hardware encoder
                            hardware_disabled_ = true;
                        }
                        LOGE("event=encoder.frame component={} input=texture outcome=failed error={} detail={} monitor={}",
                             target_encoder->Name(), (int)encode_result.type_, encode_result.GetReadableType(), monitor_name);
                        return;
                    }
                }

                // TODO: Add Texture Mapping duration
                if (!can_encode_texture /*|| other configs*/) {
                    //Todo: TEST
                    //TimeDuration td("Measure Map Raw Texture");

                    auto beg_map_texture = TimeUtil::GetCurrentTimestamp();

                    D3D11_TEXTURE2D_DESC desc;
                    target_texture->GetDesc(&desc);
                    const auto weak_self = weak_from_this();
                    auto rgba_cbk = [weak_self, monitor_name, cap_video_msg](
                        const std::shared_ptr<Image>& image) {
                        const auto self = weak_self.lock();
                        if (!self || self->exiting_ || !self->context_ || !self->module_registry_) {
                            return;
                        }
                        self->ObserveRawFrame(
                            monitor_name,
                            cap_video_msg.frame_index_,
                            cap_video_msg.frame_width_,
                            cap_video_msg.frame_height_);
                    };
                    auto yuv_cbk = [weak_self, monitor_name, cap_video_msg, beg_map_texture,
                                     can_encode_texture, frame_index](
                        const std::shared_ptr<Image>& image) {
                        const auto self = weak_self.lock();
                        if (!self || self->exiting_ || !self->context_ || !self->module_registry_) {
                            return;
                        }
                        // calculate used time
                        auto end_map_cvt_texture = TimeUtil::GetCurrentTimestamp();
                        auto diff_map_cvt_texture = end_map_cvt_texture - beg_map_texture;
                        self->stat_->CaptureInfo(monitor_name)->AppendMapCvtTextureDuration((int32_t)diff_map_cvt_texture);

                        // callback in YUV converter thread
                        if (!can_encode_texture && self->HasEncoderForMonitor(monitor_name)) {
                            auto enc_post_ts = TimeUtil::GetCurrentTimestamp();
                            ++g_enc_diag.enc_posts;
                            ++g_enc_diag.in_flight;
                            auto yuv_inflight = std::shared_ptr<void>(nullptr, [](void*) {
                                --g_enc_diag.in_flight;
                            });
                            self->PostEncTask([weak_self, monitor_name, image, frame_index,
                                               cap_video_msg, enc_post_ts, yuv_inflight]() {
                                const auto self = weak_self.lock();
                                if (!self || self->exiting_) {
                                    return;
                                }
                                // yuv_inflight 捕获保活到任务结束(含被队列挤掉未执行)时再 --in_flight
                                (void)yuv_inflight;
                                auto enc_wait = TimeUtil::GetCurrentTimestamp() - enc_post_ts;
                                g_enc_diag.enc_wait_sum += enc_wait;
                                auto prev = g_enc_diag.enc_wait_max.load();
                                while (enc_wait > prev && !g_enc_diag.enc_wait_max.compare_exchange_weak(prev, enc_wait)) {}
                                const auto encoder = self->GetEncoderForMonitor(monitor_name);
                                if (!encoder) {
                                    return;
                                }
                                auto encode_result = encoder->Encode(image, frame_index, cap_video_msg);
                                if (!encode_result.Success()) {
                                    LOGE("event=encoder.frame component={} input=yuv outcome=failed error={} detail={} monitor={}",
                                         encoder->Name(), (int)encode_result.type_, encode_result.GetReadableType(), cap_video_msg.display_name_);
                                    return;
                                }
                            });
                        }
                    };
                    // map the texture from GPU -> CPU
                    static_cast<void>(frame_carrier_processor_->MapRawTexture(
                        monitor_name, target_texture, desc.Format,
                        static_cast<int>(desc.Height), std::move(rgba_cbk),
                        std::move(yuv_cbk)));
                }

                auto end = TimeUtil::GetCurrentTimestamp();
                auto diff = end - beg;
                //RdStatistics::Instance()->AppendEncodeDuration(diff);
            }
            else {
                ObserveRawFrame(monitor_name,
                                frame_index,
                                cap_video_msg.frame_width_,
                                cap_video_msg.frame_height_);
                auto beg_map_texture = TimeUtil::GetCurrentTimestamp();
                const auto weak_self = weak_from_this();

                auto rgba_cbk = [weak_self, monitor_name, cap_video_msg](
                    const std::shared_ptr<Image>& image) {
                    const auto self = weak_self.lock();
                    if (!self || self->exiting_ || !self->context_ || !self->module_registry_) {
                        return;
                    }
                    self->ObserveRawFrame(
                        monitor_name,
                        cap_video_msg.frame_index_,
                        cap_video_msg.frame_width_,
                        cap_video_msg.frame_height_);
                };

                auto yuv_cbk = [weak_self, monitor_name, cap_video_msg, frame_index,
                                 beg_map_texture](const std::shared_ptr<Image>& image) {
                    const auto self = weak_self.lock();
                    if (!self || self->exiting_ || !self->context_ || !self->module_registry_) {
                        return;
                    }
                    // calculate used time
                    auto end_map_cvt_texture = TimeUtil::GetCurrentTimestamp();
                    auto diff_map_cvt_texture = end_map_cvt_texture - beg_map_texture;
                    self->stat_->CaptureInfo(monitor_name)->AppendMapCvtTextureDuration((int32_t)diff_map_cvt_texture);

                    // callback in YUV converter thread
                    if (self->HasEncoderForMonitor(monitor_name)) {
                        self->PostEncTask([weak_self, monitor_name, image, frame_index,
                                           cap_video_msg]() {
                            const auto self = weak_self.lock();
                            if (!self || self->exiting_) {
                                return;
                            }
                            const auto encoder = self->GetEncoderForMonitor(monitor_name);
                            if (!encoder) {
                                return;
                            }
                            auto encode_result = encoder->Encode(image, frame_index, cap_video_msg);
                            if (!encode_result.Success()) {
                                LOGE("event=encoder.frame component={} input=yuv outcome=failed error={} detail={} monitor={}",
                                     encoder->Name(), (int)encode_result.type_, encode_result.GetReadableType(), cap_video_msg.display_name_);
                                self->clear_encoders_ = true;
                                return;
                            }
                        });
                    }
                    const auto module_registry = self->module_registry_;
                    self->context_->PostMediaTask(
                        [module_registry, monitor_name, cap_video_msg, image]() {
                        module_registry->SubmitRtcLocalYuv(
                            monitor_name, cap_video_msg.frame_index_,
                            cap_video_msg.frame_width_,
                            cap_video_msg.frame_height_, image);
                    });
                };
                static_cast<void>(frame_carrier_processor_->ConvertRawImage(
                    monitor_name, cap_video_msg.raw_image_,
                    std::move(rgba_cbk), std::move(yuv_cbk)));
            }
    }

    void EncoderThread::Exit() {
        if (exiting_.exchange(true)) {
            return;
        }
        if (msg_listener_) {
            msg_listener_->UnListenAll();
            msg_listener_.reset();
        }
        if (enc_thread_) {
            enc_thread_->Exit();
            enc_thread_->Clear();
            enc_thread_.reset();
        }
        app_.reset();
        context_.reset();
        module_registry_.reset();
        stat_.reset();
        frame_carrier_processor_.reset();
        frame_resizer_processor_.reset();
    }

    void EncoderThread::HandleD3DDeviceFailure(uint64_t adapter_uid) {
        const auto weak_self = weak_from_this();
        PostEncTask([weak_self, adapter_uid]() {
            const auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            LOGW("Reset encoder pipeline after D3D device failure, adapter_uid={}", adapter_uid);
            std::map<std::string, std::shared_ptr<VideoEncoderModule>>
                working_encoders;
            {
                std::lock_guard<std::mutex> lk(self->encoder_modules_mtx_);
                working_encoders.swap(self->encoders_);
            }
            for (const auto& [monitor_name, encoder] : working_encoders) {
                if (encoder) {
                    encoder->Remove(monitor_name);
                }
            }
            self->last_video_frames_.clear();
            if (self->frame_resizer_processor_) {
                self->frame_resizer_processor_->ClearAdapter(adapter_uid);
            }
            if (self->frame_carrier_processor_) {
                self->frame_carrier_processor_->ClearAdapter(adapter_uid);
            }
            self->clear_encoders_ = false;
        });
    }

    void EncoderThread::PostEncTask(std::function<void()>&& task) {
        if (!exiting_ && enc_thread_ && task) {
            enc_thread_->Post(std::move(task));
        }
    }

    std::map<std::string, std::shared_ptr<VideoEncoderModule>>
    EncoderThread::GetWorkingVideoEncoders() {
        std::lock_guard<std::mutex> lk(encoder_modules_mtx_);
        return encoders_;
    }

    bool EncoderThread::HasEncoderForMonitor(const std::string& monitor_name) {
        return GetEncoderForMonitor(monitor_name) != nullptr;
    }

    std::shared_ptr<VideoEncoderModule>
    EncoderThread::GetEncoderForMonitor(const std::string& monitor_name) {
        std::lock_guard<std::mutex> lk(encoder_modules_mtx_);
        for (const auto& [name, encoder] : encoders_) {
            if (name == monitor_name) {
                return encoder;
            }
        }
        return {};
    }

    void EncoderThread::ObserveRawFrame(const std::string& monitor_name,
                                        const std::uint64_t frame_index,
                                        const std::uint32_t width,
                                        const std::uint32_t height) const {
        if (!context_) {
            return;
        }
        if (const auto observer = context_->GetFrameDebuggerObserver()) {
            observer->ObserveRawFrame(render::RawVideoFrameObservation{
                .monitor_id = monitor_name,
                .frame_index = frame_index,
                .width = width,
                .height = height,
            });
        }
    }

    void EncoderThread::PrintEncoderConfig(const px::EncoderConfig& config) {
        LOGI("---------------------------------------------------");
        LOGI("Encoder configs:");
        LOGI("width x height:{}x{}", config.width, config.height);
        LOGI("gop size: {}", config.gop_size);
        LOGI("gop bitrate: {}", config.bitrate);
        LOGI("enable full color: {}", config.enable_full_color_mode_);
        LOGI("encoder codec_type: {}", static_cast<int>(config.codec_type));
        LOGI("***************************************************");
    }

}
