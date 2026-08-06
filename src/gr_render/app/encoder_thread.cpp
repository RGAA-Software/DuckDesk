//
// Created by RGAA on 2023-12-24.
//

#include "encoder_thread.h"
#include <d3d11.h>
#include <memory>
#include <wrl/client.h>
#include "rd_app.h"
#include "rd_context.h"
#include "tc_common_new/data.h"
#include "tc_common_new/image.h"
#include "tc_common_new/thread.h"
#include "tc_common_new/file.h"
#include "tc_common_new/log.h"
#include "tc_common_new/time_util.h"
#include "tc_common_new/message_notifier.h"
#include "tc_encoder_new/video_encoder_factory.h"
#include "tc_encoder_new/video_encoder.h"
#include "tc_encoder_new/ffmpeg_video_encoder.h"
#include "tc_encoder_new/nvenc_video_encoder.h"
#include "settings/rd_settings.h"
#include "app/app_messages.h"
#include "rd_statistics.h"
#include "tc_common_new/win32/d3d_render.h"
#include "tc_common_new/win32/d3d_debug_helper.h"
#include "gr_render/plugins/plugin_manager.h"
#include "gr_render/plugins/plugin_ids.h"
#include "gr_render/plugin_interface/gr_stream_plugin.h"
#include "gr_render/plugin_interface/gr_net_plugin.h"
#include "gr_render/plugin_interface/gr_video_encoder_plugin.h"
#include "gr_render/plugin_interface/gr_frame_carrier_plugin.h"
#include "gr_render/plugin_interface/gr_frame_processor_plugin.h"
#include "network/net_message_maker.h"
#include "tc_message.pb.h"

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
            auto now = tc::TimeUtil::GetCurrentTimestamp();
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

namespace tc
{

    std::shared_ptr<EncoderThread> EncoderThread::Make(const std::shared_ptr<RdApplication>& app) {
        return std::make_shared<EncoderThread>(app);
    }

    EncoderThread::EncoderThread(const std::shared_ptr<RdApplication>& app) {
        app_ = app;
        stat_ = RdStatistics::Instance();
        context_ = app->GetContext();
        settings_ = RdSettings::Instance();
        plugin_manager_ = context_->GetPluginManager();
        // 队列过小会频繁丢弃未执行任务;丢弃时若已 ++in_flight 会泄漏并把 backlog 抬飞。
        // 32 足以吸收短时尖峰,同时仍会在持续过载时丢最旧帧保实时性。
        enc_thread_ = Thread::Make("encoder_thread", 32);
        enc_thread_->Poll();

        // frame carrier
        frame_carrier_plugin_ = plugin_manager_->GetFrameCarrierPlugin();

        msg_listener_ = context_->CreateMessageListener();
        msg_listener_->Listen<MsgInsertKeyFrame>([=, this](const MsgInsertKeyFrame& msg) {
            // plugins: InsertIdr
            plugin_manager_->VisitEncoderPlugins([=](GrVideoEncoderPlugin* plugin) {
                plugin->InsertIdr();
            });
        });
    }

    void EncoderThread::Encode(const CaptureVideoFrame& cap_video_msg) {
        if (!frame_carrier_plugin_) {
            return;
        }
        ++g_enc_diag.in_frames;
        ++g_enc_diag.in_flight;
        // 捕获到 lambda 里:任务被队列挤掉未执行时,shared_ptr 析构也会 --in_flight,避免 backlog 泄漏。
        auto inflight_guard = std::shared_ptr<void>(nullptr, [](void*) {
            --g_enc_diag.in_flight;
        });
        PostEncTask([=, this]() {
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
            } diag_guard{diag_task_beg};
            if (clear_encoders_) {
                clear_encoders_ = false;
                LOGW("clear all encoders!!!");
                std::lock_guard<std::mutex> lk(encoder_plugins_mtx_);
                encoder_plugins_.clear();
            }

            auto adapter_uid = cap_video_msg.adapter_uid_;

            // plugins: SharedTexture
            if (cap_video_msg.handle_ > 0) {

                // all plugins   // to do 这里要不要考虑adapter_uid不合法的情况
                //plugin_manager_->VisitAllPlugins([=, this](GrPluginInterface* plugin) {
                //    plugin->d3d11_devices_[adapter_uid] = app_->GetD3DDevice(adapter_uid);
                //    plugin->d3d11_devices_context_[adapter_uid] = app_->GetD3DContext(adapter_uid);
                //});

                context_->PostStreamPluginTask([=, this]() {
                    plugin_manager_->VisitAllPlugins([=](GrPluginInterface* plugin) {
                        plugin->OnRawVideoFrameSharedTexture(cap_video_msg.display_name_,
                                                             cap_video_msg.frame_index_,
                                                             cap_video_msg.frame_width_,
                                                             cap_video_msg.frame_height_,
                                                             cap_video_msg.handle_,
                                                             cap_video_msg.adapter_uid_,
                                                             cap_video_msg.frame_format_
                                                            );
                    });
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
            auto target_encoder_plugin = GetEncoderPluginForMonitor(monitor_name);
            if (target_encoder_plugin) {
                auto encoder_config_res = target_encoder_plugin->GetEncoderConfig(monitor_name);
                if (encoder_config_res.has_value()) {
                    const auto selected_encoder_config = encoder_config_res.value();
                    if (selected_encoder_config.enable_full_color_mode_ != settings_->EnableFullColorMode() ) {
                        full_color_mode_changed = true;
                        LOGI("full_color_mode_changed!!!");
                    }
                } else {

                    LOGI("EncoderThread encoder_config_res no value");
                }
            }
            else {
                LOGI("EncoderThread target_encoder_plugin is nullptr, will create encoder.");
            }

            // 全彩强制 HEVC 只影响本次有效格式,不得改写 settings 里用户/启动参数选定的 encoder_format_,
            // 否则关全彩后会永久卡在 HEVC(WebRTC/多数 Win 端按 H264 解 → 黑屏)。
            const auto effective_format = settings_->EnableFullColorMode()
                ? Encoder::EncoderFormat::kHEVC
                : settings->encoder_.encoder_format_;
            const bool switched_to_hevc = (effective_format == Encoder::EncoderFormat::kHEVC
                                          && encoder_format_ != Encoder::EncoderFormat::kHEVC);

            if (full_color_mode_changed || frame_meta_info_changed || encoder_format_ != effective_format
                || !target_encoder_plugin || !target_encoder_plugin->IsPluginEnabled()) {
                if (target_encoder_plugin) {
                    // todo : Test it!
                    target_encoder_plugin->Exit(monitor_name);
                    target_encoder_plugin = nullptr;
                }
                tc::EncoderConfig encoder_config;
                bool is_gdi_capture = plugin_manager_->IsGDIMonitorCapturePlugin(app_->GetWorkingMonitorCapturePlugin());
                if (settings_->encoder_.encode_res_type_ == Encoder::EncodeResolutionType::kOrigin || is_gdi_capture) {
                    encoder_config.width = cap_video_msg.frame_width_;
                    encoder_config.height = cap_video_msg.frame_height_;
                    encoder_config.encode_width = cap_video_msg.frame_width_;
                    encoder_config.encode_height = cap_video_msg.frame_height_;
                    encoder_config.frame_resize = false;
                } else {
                    encoder_config.width = settings_->encoder_.encode_width_;
                    encoder_config.height = settings_->encoder_.encode_height_;
                    encoder_config.encode_width = settings_->encoder_.encode_width_;
                    encoder_config.encode_height = settings_->encoder_.encode_height_;
                    // resize will be enabled when dda capture working
                    encoder_config.frame_resize = true;
                }

                if (settings_->EnableFullColorMode()) {
                    LOGI("full color mode, use HEVC (settings format kept: {})", (int)settings->encoder_.encoder_format_);
                }

                encoder_config.codec_type = effective_format == Encoder::EncoderFormat::kH264
                    ? tc::EVideoCodecType::kH264 : tc::EVideoCodecType::kHEVC;
                encoder_config.enable_adaptive_quantization = true;
                encoder_config.gop_size = -1;
                encoder_config.quality_preset = 1;
                // MUST have a value > 0
                encoder_config.fps = settings_->encoder_.fps_;
                if (encoder_config.fps < 15 || encoder_config.fps > 120) {
                    encoder_config.fps = 60;
                }
                encoder_config.multi_pass = tc::ENvdiaEncMultiPass::kMultiPassDisabled;
                encoder_config.rate_control_mode = tc::ERateControlMode::kRateControlModeCbr;
                encoder_config.sample_desc_count = 1;
                encoder_config.supports_intra_refresh = true;
                encoder_config.texture_format = cap_video_msg.frame_format_;
                encoder_config.bitrate = settings->encoder_.bitrate_ * 1000000;
                encoder_config.adapter_uid_ = cap_video_msg.adapter_uid_;
                encoder_config.enable_full_color_mode_ = settings_->EnableFullColorMode();

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

                // all plugins
                plugin_manager_->VisitAllPlugins([=, this](GrPluginInterface* plugin) {
                    plugin->d3d11_devices_[adapter_uid] = d3d_device;
                    plugin->d3d11_devices_context_[adapter_uid] = d3d_context;
                });

                // video frame carrier
                auto r = frame_carrier_plugin_->InitFrameCarrier(GrCarrierParams {
                    .mon_name_ = monitor_name,
                    .d3d_device_ = d3d_device,
                    .d3d_device_context_ = d3d_context,
                    .adapter_uid_ = cap_video_msg.adapter_uid_,
                    .enable_full_color_mode_ = encoder_config.enable_full_color_mode_,
                });
                if (!r) {
                    LOGE("Init Frame Carrier failed");
                }

                // plugins: Create encoder plugin
                // To use FFmpeg encoder if mocking video stream or to implement the hardware encoder to encode raw frame(RGBA)
                bool is_mocking = settings_->capture_.mock_video_;

                auto select_encoder_with_capability_func = [=, &target_encoder_plugin](tc::GrVideoEncoderPlugin* encoder_plugin, const std::string& monitor_name) {
                    if (!encoder_config.enable_full_color_mode_) {
                        target_encoder_plugin = encoder_plugin;
                    }
                    else {
                        auto cap_res = encoder_plugin->GetEncoderCapability(monitor_name);
                        if (cap_res.has_value()) {
                            auto cap = cap_res.value();
                            if (tc::EVideoCodecType::kH264 == encoder_config.codec_type) {
                                if (cap.support_h264_yuv444_) {
                                    target_encoder_plugin = encoder_plugin;
                                }
                            }
                            else if (tc::EVideoCodecType::kHEVC == encoder_config.codec_type) {
                                if (cap.support_hevc_yuv444_) {
                                    target_encoder_plugin = encoder_plugin;
                                }
                            }
                        }
                    }
                };

                if (!target_encoder_plugin) {
                    LOGI("Hardware disabled? {}", hardware_disabled_.load());
                    auto nvenc_encoder_plugin = plugin_manager_->GetNvencEncoderPlugin();
                    if (!is_mocking && !hardware_disabled_ && nvenc_encoder_plugin && nvenc_encoder_plugin->IsPluginEnabled() && nvenc_encoder_plugin->Init(encoder_config, monitor_name)) {
                        select_encoder_with_capability_func(nvenc_encoder_plugin, monitor_name);
                    }

                    if (!target_encoder_plugin) {
                        LOGW("Init NVENC failed, will try AMF.");
                        auto amf_encoder_plugin = plugin_manager_->GetAmfEncoderPlugin();
                        if (!is_mocking && !hardware_disabled_  && amf_encoder_plugin && amf_encoder_plugin->IsPluginEnabled() && amf_encoder_plugin->Init(encoder_config, monitor_name)) {
                            select_encoder_with_capability_func(amf_encoder_plugin, monitor_name);
                        }
                    }

                    auto ffmpeg_encoder_plugin = plugin_manager_->GetFFmpegEncoderPlugin();
                    if (!target_encoder_plugin) {
                        LOGW("Init AMF failed, will try FFmpeg(kNvEnc).");
                        // 让ffmpeg尝试硬编码初始化
                        encoder_config.Hardware = EHardwareEncoder::kNvEnc;
                        if (ffmpeg_encoder_plugin && ffmpeg_encoder_plugin->IsPluginEnabled() && ffmpeg_encoder_plugin->Init(encoder_config, monitor_name)) {
                            select_encoder_with_capability_func(ffmpeg_encoder_plugin, monitor_name);
                        }
                    }

                    if (!target_encoder_plugin) {
                        // Intel Quick Sync 兜底:无 N/A 卡的机器(如只有 Intel 核显)
                        // 用 QSV 硬编,把 1080p 编码从 x264 软编的一个多大核上卸下来,
                        // 否则软编与采集/同机浏览器抢 CPU,DDA 采集被压到 32~40fps。
                        // 注意必须插在 FFmpeg(kAmf) 之前:kAmf 分支在 ffmpeg 插件内
                        // 实际映射为 libx264 且总能初始化成功,排在它后面永远轮不到。
                        LOGW("Init FFmpeg(kNvEnc) failed, will try FFmpeg(kQsv).");
                        encoder_config.Hardware = EHardwareEncoder::kQsv;
                        if (!is_mocking && !hardware_disabled_ && ffmpeg_encoder_plugin && ffmpeg_encoder_plugin->IsPluginEnabled() && ffmpeg_encoder_plugin->Init(encoder_config, monitor_name)) {
                            select_encoder_with_capability_func(ffmpeg_encoder_plugin, monitor_name);
                        }
                    }

                    if (!target_encoder_plugin) {
                        LOGW("Init FFmpeg(kQsv) failed, will try FFmpeg(kAmf).");
                        // 让ffmpeg尝试硬编码初始化
                        encoder_config.Hardware = EHardwareEncoder::kAmf;
                        if (ffmpeg_encoder_plugin && ffmpeg_encoder_plugin->IsPluginEnabled() && ffmpeg_encoder_plugin->Init(encoder_config, monitor_name)) {
                            select_encoder_with_capability_func(ffmpeg_encoder_plugin, monitor_name);
                        }
                    }

                    if (!target_encoder_plugin) {
                        LOGW("Init FFmpeg(kAmf) failed, will try FFmpeg(kNone).");
                        //让ffmpeg尝试软件编码初始化
                        encoder_config.Hardware = EHardwareEncoder::kNone;
                        if (ffmpeg_encoder_plugin && ffmpeg_encoder_plugin->IsPluginEnabled() && ffmpeg_encoder_plugin->Init(encoder_config, monitor_name)) {
                            select_encoder_with_capability_func(ffmpeg_encoder_plugin, monitor_name);
                        }
                    }

                    if (!target_encoder_plugin) {
                        LOGW("Init FFmpeg(kAmf) failed, will try FFmpeg(kNone). without capability!");
                        //让ffmpeg尝试软件编码初始化
                        encoder_config.Hardware = EHardwareEncoder::kNone;
                        if (ffmpeg_encoder_plugin && ffmpeg_encoder_plugin->IsPluginEnabled() && ffmpeg_encoder_plugin->Init(encoder_config, monitor_name)) {
                            target_encoder_plugin = ffmpeg_encoder_plugin;
                        }
                    }

                    if (!target_encoder_plugin) {
                        LOGE("Init FFmpeg failed, we can't encode frame in this machine!");
                        return;
                    }
                }

                {
                    std::lock_guard<std::mutex> lk(encoder_plugins_mtx_);
                    encoder_plugins_[monitor_name] = target_encoder_plugin;
                }
                LOGI("Finally, we use encoder plugin: {}, version: {} for monitor: {}",
                     target_encoder_plugin->GetPluginName(), target_encoder_plugin->GetVersionName(), monitor_name);

                auto video_type = [=]() -> GrPluginEncodedVideoType {
                    if (effective_format == Encoder::EncoderFormat::kH264) {
                        return GrPluginEncodedVideoType::kH264;
                    } else if (effective_format == Encoder::EncoderFormat::kHEVC) {
                        return GrPluginEncodedVideoType::kH265;
                    } else {
                        return GrPluginEncodedVideoType::kH264;
                    }
                } ();

                // plugins: VideoEncoderCreated
                context_->PostStreamPluginTask([=, this]() {
                    plugin_manager_->VisitStreamPlugins([=, this](GrStreamPlugin *plugin) {
                        plugin->OnVideoEncoderCreated(monitor_name, video_type, encoder_config.width, encoder_config.height);
                    });
                });

                stat_->video_encoder_format_ = effective_format;

                encoder_format_ = effective_format;
                last_video_frames_[monitor_name] = cap_video_msg;

                // Win 客户端可解 H265;WebRTC 只协商 H264。切到 H265 时若有 RTC 连接则下发提示。
                if (switched_to_hevc) {
                    const bool full_color = settings_->EnableFullColorMode();
                    const std::string reason = full_color ? "full_color" : "encoder_format";
                    auto tip = NetMessageMaker::MakeVideoCodecChanged(tc::VideoType::kNetHevc, full_color, reason);
                    plugin_manager_->VisitNetPlugins([=](GrNetPlugin* plugin) {
                        if (!plugin || plugin->GetPluginId() != kNetRtcLocalPluginId) {
                            return;
                        }
                        if (plugin->GetConnectedClientsCount() <= 0) {
                            return;
                        }
                        LOGW("Notify WebRTC clients: pipeline switched to H265 ({})", reason);
                        plugin->PostProtoMessage(tip, false);
                    });
                }
            }

            // from texture handle
            if (cap_video_msg.handle_ > 0 && frame_carrier_plugin_) {
                // 1. copy shared texture
                auto beg = TimeUtil::GetCurrentTimestamp();
                auto cp_result = frame_carrier_plugin_->CopyTexture(monitor_name, cap_video_msg.handle_, frame_index);
                if (!cp_result || cp_result->texture_ == nullptr) {
                    LOGE("CopyTexture failed: empty result or texture");
                    return;
                }

                ComPtr<ID3D11Texture2D> target_texture = cp_result->texture_;
                // 2. resize ?
                if (auto opt_config = target_encoder_plugin->GetEncoderConfig(monitor_name);
                    opt_config.has_value() && opt_config.value().frame_resize) {
                    auto config = opt_config.value();
                    if (auto resize_plugin = plugin_manager_->GetFrameResizePlugin(); resize_plugin) {
                        auto t = resize_plugin->Process(cp_result->texture_, adapter_uid, monitor_name, config.encode_width, config.encode_height);
                        if (t) {
                            target_texture = t;
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
                if (target_encoder_plugin && target_encoder_plugin->CanEncodeTexture()) {
                    can_encode_texture = true;
                    // plugins: EncodeTexture
                    auto encode_result = target_encoder_plugin->Encode(target_texture, frame_index, cap_video_msg);
                    if (!encode_result.Success()) {
                        if (encode_result.type_ == VideoEncoderErrorType::kEncodeFailed) {
                            LOGW("<!!> Encode failed, will release this encoder for display and disable hardware: {}", monitor_name);
                            target_encoder_plugin->Exit(monitor_name);
                            {
                                std::lock_guard<std::mutex> lk(encoder_plugins_mtx_);
                                encoder_plugins_.erase(monitor_name);
                            }
                            // disable hardware encoder
                            hardware_disabled_ = true;
                        }
                        LOGE("<!!> Encode texture failed, encoder plugin: {}, error: {}->{}, monitor: {}",
                             target_encoder_plugin->GetPluginName(), (int)encode_result.type_, encode_result.GetReadableType(), monitor_name);
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
                    auto rgba_cbk = [=, this](const std::shared_ptr<Image> &image) {
                        // callback in Enc thread
                        context_->PostStreamPluginTask([=, this]() {
                            plugin_manager_->VisitAllPlugins([=, this](GrPluginInterface* plugin) {
                                plugin->OnRawVideoFrameRgba(monitor_name, cap_video_msg.frame_index_, cap_video_msg.frame_width_, cap_video_msg.frame_height_, image);
                            });
                        });
                    };
                    auto yuv_cbk = [=, this](const std::shared_ptr<Image> &image) {
                        // notify yuv
                        context_->PostStreamPluginTask([=, this]() {
                            plugin_manager_->VisitAllPlugins([=, this](GrPluginInterface *plugin) {
                                plugin->OnRawVideoFrameYuv(monitor_name, cap_video_msg.frame_index_, cap_video_msg.frame_width_, cap_video_msg.frame_height_, image);
                            });
                        });

                        // calculate used time
                        auto end_map_cvt_texture = TimeUtil::GetCurrentTimestamp();
                        auto diff_map_cvt_texture = end_map_cvt_texture - beg_map_texture;
                        stat_->CaptureInfo(monitor_name)->AppendMapCvtTextureDuration((int32_t)diff_map_cvt_texture);

                        // callback in YUV converter thread
                        if (target_encoder_plugin && !can_encode_texture) {
                            auto enc_post_ts = TimeUtil::GetCurrentTimestamp();
                            ++g_enc_diag.enc_posts;
                            ++g_enc_diag.in_flight;
                            auto yuv_inflight = std::shared_ptr<void>(nullptr, [](void*) {
                                --g_enc_diag.in_flight;
                            });
                            PostEncTask([=, this]() {
                                // yuv_inflight 捕获保活到任务结束(含被队列挤掉未执行)时再 --in_flight
                                (void)yuv_inflight;
                                auto enc_wait = TimeUtil::GetCurrentTimestamp() - enc_post_ts;
                                g_enc_diag.enc_wait_sum += enc_wait;
                                auto prev = g_enc_diag.enc_wait_max.load();
                                while (enc_wait > prev && !g_enc_diag.enc_wait_max.compare_exchange_weak(prev, enc_wait)) {}
                                auto encode_result = target_encoder_plugin->Encode(image, frame_index, cap_video_msg);
                                if (!encode_result.Success()) {
                                    LOGE("<!!> Encode YUV failed, encoder plugin: {}, error: {}->, monitor: {}",
                                         target_encoder_plugin->GetPluginName(), (int)encode_result.type_, encode_result.GetReadableType(), cap_video_msg.display_name_);
                                    return;
                                }
                            });
                        }
                    };
                    // map the texture from GPU -> CPU
                    frame_carrier_plugin_->MapRawTexture(monitor_name, target_texture, desc.Format, (int)desc.Height, rgba_cbk, yuv_cbk);
                }

                auto end = TimeUtil::GetCurrentTimestamp();
                auto diff = end - beg;
                //RdStatistics::Instance()->AppendEncodeDuration(diff);
            }
            else {
                context_->PostStreamPluginTask([=, this]() {
                    plugin_manager_->VisitStreamPlugins([=, this](GrStreamPlugin *plugin) {
                        plugin->OnRawVideoFrameRgba(monitor_name, frame_index, cap_video_msg.frame_width_, cap_video_msg.frame_height_, cap_video_msg.raw_image_);
                    });
                });

                auto beg_map_texture = TimeUtil::GetCurrentTimestamp();

                auto rgba_cbk = [=, this](const std::shared_ptr<Image>& image) {
                    // callback in Enc thread
                    context_->PostStreamPluginTask([=, this]() {
                        plugin_manager_->VisitStreamPlugins([=, this](GrStreamPlugin* plugin) {
                            plugin->OnRawVideoFrameRgba(monitor_name, cap_video_msg.frame_index_, cap_video_msg.frame_width_, cap_video_msg.frame_height_, image);
                        });
                    });
                };

                auto yuv_cbk = [=, this](const std::shared_ptr<Image>& image) {
                    // calculate used time
                    auto end_map_cvt_texture = TimeUtil::GetCurrentTimestamp();
                    auto diff_map_cvt_texture = end_map_cvt_texture - beg_map_texture;
                    stat_->CaptureInfo(monitor_name)->AppendMapCvtTextureDuration((int32_t)diff_map_cvt_texture);

                    // callback in YUV converter thread
                    if (target_encoder_plugin) {
                        PostEncTask([=, this]() {
                            auto encode_result = target_encoder_plugin->Encode(image, frame_index, cap_video_msg);
                            if (!encode_result.Success()) {
                                LOGE("<!!> Encode YUV failed, encoder plugin: {}, error: {}->{}, monitor: {}",
                                     target_encoder_plugin->GetPluginName(), (int)encode_result.type_, encode_result.GetReadableType(), cap_video_msg.display_name_);
                                clear_encoders_ = true;
                                return;
                            }
                        });
                    }
                    context_->PostStreamPluginTask([=, this]() {
                        plugin_manager_->VisitStreamPlugins([=, this](GrStreamPlugin* plugin) {
                            plugin->OnRawVideoFrameYuv(monitor_name, cap_video_msg.frame_index_, cap_video_msg.frame_width_, cap_video_msg.frame_height_, image);
                        });
                    });
                };
                frame_carrier_plugin_->ConvertRawImage(monitor_name, cap_video_msg.raw_image_, rgba_cbk, yuv_cbk);
            }
        });
    }

    void EncoderThread::Exit() {
        if (enc_thread_) {
            enc_thread_->Exit();
        }
    }

    void EncoderThread::HandleD3DDeviceFailure(uint64_t adapter_uid) {
        PostEncTask([=, this]() {
            LOGW("Reset encoder pipeline after D3D device failure, adapter_uid={}", adapter_uid);
            std::map<std::string, GrVideoEncoderPlugin*> working_plugins;
            {
                std::lock_guard<std::mutex> lk(encoder_plugins_mtx_);
                working_plugins.swap(encoder_plugins_);
            }
            for (const auto& [monitor_name, plugin] : working_plugins) {
                if (plugin) {
                    plugin->Exit(monitor_name);
                }
            }
            last_video_frames_.clear();
            clear_encoders_ = false;
        });
    }

    void EncoderThread::PostEncTask(std::function<void()>&& task) {
        enc_thread_->Post(std::move(task));
    }

    std::map<std::string, GrVideoEncoderPlugin*> EncoderThread::GetWorkingVideoEncoderPlugins() {
        std::lock_guard<std::mutex> lk(encoder_plugins_mtx_);
        return encoder_plugins_;
    }

    bool EncoderThread::HasEncoderForMonitor(const std::string& monitor_name) {
        return GetEncoderPluginForMonitor(monitor_name) != nullptr;
    }

    GrVideoEncoderPlugin* EncoderThread::GetEncoderPluginForMonitor(const std::string& monitor_name) {
        std::lock_guard<std::mutex> lk(encoder_plugins_mtx_);
        for (const auto& [name, plugin] : encoder_plugins_) {
            if (name == monitor_name) {
                return plugin;
            }
        }
        return nullptr;
    }

    void EncoderThread::PrintEncoderConfig(const tc::EncoderConfig& config) {
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
