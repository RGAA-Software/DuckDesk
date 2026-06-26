//
// Created by RGAA on 2023-12-16.
//

#include "rd_app.h"
#include <windows.h>
#include "rd_context.h"
#include "tc_common_new/log.h"
#include "tc_common_new/file.h"
#include "tc_common_new/data.h"
#include "tc_common_new/image.h"
#include "tc_common_new/message_notifier.h"
#include "tc_common_new/thread.h"
#include "tc_common_new/process_util.h"
#include "tc_common_new/string_util.h"
#include "tc_common_new/time_util.h"
#include "tc_encoder_new/video_encoder_factory.h"
#include "tc_capture_new/capture_message.h"
#include "tc_capture_new/capture_message_maker.h"
#include "app/app_manager.h"
#include "app/app_manager_factory.h"
#include "app/app_messages.h"
#include "settings/rd_settings.h"
#include "render_panel/network/ws_panel_server.h"
#include "app/encoder_thread.h"
#include "network/net_message_maker.h"
#include "tc_message.pb.h"
#include "tc_render_panel_message.pb.h"
#include "app/app_timer.h"
#include "tc_opus_codec_new/opus_codec.h"
#include "network/ws_panel_client.h"
#include "network/server_cast.h"
#include "app/app_shared_info.h"
#include "app/win/dx_address_loader.h"
#include "tc_common_new/win32/win_helper.h"
#include "tc_common_new/fft_32.h"
#include "tc_common_new/hardware.h"
#include "tc_common_new/shared_preference.h"
#include "tc_controller/vigem/vigem_controller.h"
#include "tc_controller/vigem_driver_manager.h"
#include "rd_statistics.h"
#include "network/render_service_client.h"
#include "gr_render/plugins/plugin_manager.h"
#include "gr_render/plugin_interface/gr_stream_plugin.h"
#include "gr_render/plugin_interface/gr_net_plugin.h"
#include "gr_render/plugin_interface/gr_monitor_capture_plugin.h"
#include "gr_render/plugin_interface/gr_data_provider_plugin.h"
#include "gr_render/plugin_interface/gr_audio_encoder_plugin.h"
#include "tc_service_message.pb.h"
#include "app/win/win_desktop_manager.h"
#include "tc_common_new/win32/d3d11_wrapper.h"
#include "tc_message_new/proto_converter.h"
#include "tc_message_new/rp_proto_converter.h"
#include "tc_common_new/memory_stat.h"
#include "tc_common_new/folder_util.h"

namespace tc
{

    std::shared_ptr<RdApplication> rdApp;

    static FpsStat timer_fps;

    std::shared_ptr<RdApplication> RdApplication::Make(const AppParams& args) {
        struct WinApplicationEnabler final : WinApplication {
            explicit WinApplicationEnabler(const AppParams& app_args) : WinApplication(app_args) {}
        };

        // By OS
        // Windows
        return std::make_shared<WinApplicationEnabler>(args);
        // Linux
    }

    RdApplication::RdApplication(const AppParams& args) {
        auto settings = RdSettings::Instance();
        settings_ = settings;

        // debug
        // MessageBoxA(0, "", "debug", 0);
    }

    RdApplication::~RdApplication() {
        LOGI("RdApplication dtor");
    }

    void RdApplication::Init(int argc, char** argv) {
        init_failed_ = false;
        init_error_.clear();



        // sp
        sp_ = SharedPreference::Instance();
        auto path = FolderUtil::GetProgramDataPath() + L"/gr_data";
        std::string sp_name = std::format("godesk_render_{}.dat", settings_->transmission_.listening_port_);
        if (!sp_->Init(path, sp_name)) {
            init_failed_ = true;
            init_error_ = std::format("Init render SharedPreference failed, path: {}, file: {}, error: {}",
                                      StringUtil::ToUTF8(path), sp_name, sp_->GetLastError());
            LOGE("{}", init_error_);
        }
    }

    int RdApplication::Run() {
        if (init_failed_) {
            LOGE("RdApplication init failed, abort run: {}", init_error_);
            return -1;
        }
        statistics_ = RdStatistics::Instance();

        // context
        context_ = std::make_shared<RdContext>();
        context_->Init();

        // shared_from_this() below requires this object to be created by RdApplication::Make().
        plugin_manager_ = PluginManager::Make(shared_from_this());
        context_->SetPluginManager(plugin_manager_);

        plugin_manager_->LoadAllPlugins();
        plugin_manager_->RegisterPluginEventsCallback();
        plugin_manager_->DumpPluginInfo();

        statistics_->SetApplication(shared_from_this());
        statistics_->StartMonitor();

        // connect to service
        LOGI("Will connect the service!");
        service_client_ = std::make_shared<RenderServiceClient>(shared_from_this());
        service_client_->Start();

        // connect panel
        LOGI("Will connect the panel!");
        ws_panel_client_ = std::make_shared<WsPanelClient>(context_);
        ws_panel_client_->Start();

        // app manager
        app_manager_ = AppManagerFactory::Make(context_);
        // encoder in thread
        encoder_thread_ = EncoderThread::Make(shared_from_this());
        // event bus listener
        msg_listener_ = context_->GetMessageNotifier()->CreateListener();
        // app shared info
        app_shared_info_ = AppSharedInfo::Make(context_);

        // app timer
        InitAppTimer();
        // messages
        InitMessages();
        // global audio capture
        if (settings_->capture_.enable_audio_) {
            InitAudioCapture();
        }

        // vigem control thread
        //control_thread_ = Thread::Make("control", 16);
        //control_thread_->Poll();
        // desktop capture
        if (settings_->capture_.mock_video_) {
            LOGI("Use mocking video plugin.");
            data_provider_plugin = plugin_manager_->GetMockVideoStreamPlugin();
        }
        else {
            if (settings_->capture_.IsVideoInnerCapture()) {
                LOGI("Use inner capture.");
            }
            else {
                dda_capture_plugin_ = plugin_manager_->GetDDACapturePlugin();
                gdi_capture_plugin_ = plugin_manager_->GetGdiCapturePlugin();
                if (dda_capture_plugin_) {
                    capture_plugin_ = dda_capture_plugin_;
                }
                else if (gdi_capture_plugin_) {
                    capture_plugin_ = gdi_capture_plugin_;
                }
                else {
                    LOGE("Don't have a valid capture plugin, will exit!");
                    return -1;
                }

                // test only gdi begin
                //capture_plugin_ = gdi_capture_plugin_;
                // test only gdi end

                LOGI("Use capture fps: {}", settings_->encoder_.fps_);
                if (capture_plugin_ && capture_plugin_->IsPluginEnabled()) {
                    LOGI("Use dda capture plugin.");
                    capture_plugin_->SetCaptureFps(settings_->encoder_.fps_);
                    capture_plugin_->SetCaptureErrorCallback([=, this](const MonitorCaptureError& err) {
                        LOGE("*** capture error: {}", (int)err);
                        if (IsCurrentGdiCapture()) {
                            LOGI("Already use GDI capture, ignore the error.");
                            return;
                        }
                        if (monitor_changed_) {
                            LOGI("Maybe montor changed, ignore this error now.");
                            return;
                        }
                        // change to GDI
                        // capture_plugin_->DisablePlugin();
                        LOGI("Don't use DDA, will switch to GDI.");
                        if (!SwitchGdiCapture() || !capture_plugin_) {
                            LOGE("Switch to GDI failed or no capture plugin available.");
                            return;
                        }
                        capture_plugin_->StartCapturing();
                    });
                }
                else {
                    LOGI("Don't use DDA, will switch to GDI.");
                    SwitchGdiCapture();
                }
            }
        }

        if (settings_->capture_.enable_video_) {
            if (settings_->capture_.capture_video_type_ == Capture::CaptureVideoType::kVideoInner) {
                StartProcessWithHook();
            }
            else if (settings_->capture_.capture_video_type_ == Capture::CaptureVideoType::kCaptureScreen) {
                StartProcessWithScreenCapture();
            }
        }

        // desktop manager
        desktop_mgr_ = WinDesktopManager::Make(context_);

        rdApp = shared_from_this();
        main_thread_id_ = GetCurrentThreadId();

        MSG msg{};
        while (!exit_app_) {
            BOOL ret = GetMessage(&msg, NULL, 0, 0);
            if (ret == 0 || ret == -1) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            // Execute pending UI tasks from RdContext
            if (context_) {
                context_->ExecutePendingUITasks();
            }

            // Execute pending global tasks
            std::queue<std::shared_ptr<AppMessage>> local;
            {
                std::lock_guard<std::mutex> lock(task_mutex_);
                local.swap(pending_tasks_);
            }
            while (!local.empty()) {
                auto& m = local.front();
                if (m->task_) {
                    m->task_();
                }
                local.pop();
            }
        }
        return 0;
    }

    void RdApplication::InitAppTimer() {
        app_timer_ = std::make_shared<AppTimer>(context_);
        app_timer_->StartTimers();
    }

    void RdApplication::InitMessages() {
        auto weak_self = weak_from_this();
        msg_listener_->Listen<MsgBeforeInject>([=, this](const MsgBeforeInject& msg) {
            if (settings_->capture_.IsVideoInnerCapture()) {
                this->WriteBoostUpInfoForPid(msg.pid_);
            }
        });

        msg_listener_->Listen<MsgObsInjected>([=, this](const MsgObsInjected& msg) {

        });

        msg_listener_->Listen<MsgTimer16>([=, this](const MsgTimer16& msg) {
            context_->PostTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                // notify dda capture
                auto plugin = self->plugin_manager_->GetDDACapturePlugin();
                if (!plugin) {
                    return;
                }
                plugin->On16MilliSecond();
                if (++self->timer_count_16ms_ % 2 == 0) {
                    plugin->On33MilliSecond();
                    timer_fps.Tick();
                }
            });

            this->PostGlobalTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                self->SendAudioSpectrumMessage();
            });
        });

        msg_listener_->Listen<MsgTimer100>([=, this](const MsgTimer100& msg) {
            this->PostGlobalTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                // If you want a much smoother spectrum, report it quicker, post it in MsgTimer16 callback
                self->ReportAudioSpectrum2Panel();
            });
        });

        msg_listener_->Listen<MsgTimer1000>([=, this](const MsgTimer1000& msg) {
            statistics_->IncreaseRunningTime();

            auto plugin_manager = context_->GetPluginManager();
            plugin_manager->On1Second();

#if MEMORY_STST_ON
            context_->PostTask([]() {
                auto info = MemoryStat::Instance()->GetStatInfo();
                LOGI("Memory usage: {}", info.Dump());
            });
#endif
        });

        msg_listener_->Listen<MsgClientConnected>([=, this](const MsgClientConnected& msg) {
            this->PostGlobalTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }

            });
        });

        msg_listener_->Listen<MsgClientHello>([=, this](const MsgClientHello& msg) {
            this->PostGlobalTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                // send configuration back to client
                self->SendConfigurationBack();
            });
        });

        msg_listener_->Listen<MsgClientDisconnected>([=, this](const MsgClientDisconnected& msg) {
            if (HasConnectedPeer()) {
                LOGI("Still has connected clients");
                return;
            }
            // LOGW("Don't have connected clients, maybe restart render in 10S");
            // // check UTC time
            // this->context_->PostDelayTask([=, this]() {
            //     if (!HasConnectedPeer()) {
            //         LOGW("** Don't have connected clients, will restart render now.");
            //         ProcessUtil::KillProcess(qApp->applicationPid());
            //     }
            // }, 10000);
        });

        msg_listener_->Listen<ClipboardMessage>([=, this](const ClipboardMessage& msg) {
            this->PostGlobalTask([weak_self, msg]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                self->SendClipboardMessage(msg.msg_);
            });
        });

        // DDA Init failed
        msg_listener_->Listen<CaptureInitFailedMessage>([=, this](const CaptureInitFailedMessage& msg) {
            this->PostGlobalTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                self->statistics_->IncreaseDDAFailedCount();
                // tell UI process to restart me
                self->RequestRestartMe();
            });
        });

        // CaptureMonitorInfoMessage
        msg_listener_->Listen<CaptureMonitorInfoMessage>([=, this](const CaptureMonitorInfoMessage& msg) {
            this->PostGlobalTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                self->SendConfigurationBack();
            });
        });

        msg_listener_->Listen<MsgReCreateRefresher>([=, this](const MsgReCreateRefresher& msg) {
            // report to Panel
            // !! USELESS !! Just report it
            if (ws_panel_client_) {
                ws_panel_client_->ReportMonitorChanged();
            }

            monitor_changed_ = true;
            context_->PostDelayTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                self->monitor_changed_ = false;
            }, 5000);
        });

        msg_listener_->Listen<MsgModifyFps>([=, this](const MsgModifyFps& msg) {
            std::lock_guard<std::mutex> lk(capture_plugin_mtx_);
            if (capture_plugin_) {
                settings_->encoder_.fps_ = msg.fps_;
                capture_plugin_->SetCaptureFps(msg.fps_);
            }
        });

        // request from Remote Panel's context menu or same function
        msg_listener_->Listen<MsgPanelStreamLockScreen>([=, this](const MsgPanelStreamLockScreen& msg) {
            LOGI(" ** Panel request LockScreen from device: {}", msg.from_device_);
            Hardware::LockScreen();
        });

        // request from Remote Panel's context menu or same function
        msg_listener_->Listen<MsgPanelStreamRestartDevice>([=, this](const MsgPanelStreamRestartDevice& msg) {
            LOGI(" ** Panel request RestartDevice from device: {}", msg.from_device_);
            Hardware::RestartDevice();
        });

        // request from Remote Panel's context menu or same function
        msg_listener_->Listen<MsgPanelStreamShutdownDevice>([=, this](const MsgPanelStreamShutdownDevice& msg) {
            LOGI(" ** Panel request ShutdownDevice from device: {}", msg.from_device_);
            Hardware::ShutdownDevice();
        });

        msg_listener_->Listen<MsgTimer20S>([=, this](const MsgTimer20S& msg) {
            context_->PostTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                if (self->IsCurrentGdiCapture() && !self->force_gdi_) {
                    if (auto r = self->TryInitDdaCapture(); !r) {
                        LOGI("===> Try init dda capture result failed!");
                        return;
                    }
                    LOGI("Will switch to DDA");
                    if (auto r = self->SwitchDdaCapture(); r && self->IsCurrentDdaCapture()) {
                        LOGI("Will start DDA capturing");
                        self->capture_plugin_->StartCapturing();
                    }
                }
            });
        });

        // Restart MySelf
        msg_listener_->Listen<MsgTimer1Minute>([=, this](const MsgTimer1Minute&) {
            ++restart_counter_;
            if (restart_counter_ >= 60 * 6) {
                restart_counter_ = 0;

                if (HasConnectedPeer()) {
                    return;
                }
                LOGW("** Don't have connected clients, will restart render now.");
                ProcessUtil::KillProcess(GetCurrentProcessId());
            }
        });

    }

    void RdApplication::InitAudioCapture() {
        if (settings_->capture_.capture_audio_type_ != Capture::CaptureAudioType::kAudioGlobal) {
            return;
        }

        auto weak_self = weak_from_this();
        audio_capture_plugin_ = plugin_manager_->GetAudioCapturePlugin();
        audio_encoder_plugin_ = plugin_manager_->GetAudioEncoderPlugin();
        if (!audio_capture_plugin_ || !audio_encoder_plugin_) {
            return;
        }

        msg_listener_->Listen<CaptureAudioFrame>([=, this] (const CaptureAudioFrame& frame) {
            if (!HasConnectedPeer()) {
                return;
            }

            int samples = (int)frame.samples_;
            int channels = (int)frame.channels_;
            int bits = (int)frame.bits_;

            if (frame.full_data_) {
                audio_encoder_plugin_->Encode(frame.full_data_, samples, channels, bits);

                auto stat = RdStatistics::Instance();
                stat->audio_samples_ = samples;
                stat->audio_channels_ = channels;
                stat->audio_bits_ = bits;

                // plugins
                {
                    auto data = frame.full_data_;
                    context_->PostStreamPluginTask([weak_self, data, samples, channels, bits]() {
                        auto self = weak_self.lock();
                        if (!self || self->exit_app_ || !self->plugin_manager_) {
                            return;
                        }
                        self->plugin_manager_->VisitStreamPlugins([=](GrStreamPlugin *plugin) {
                            plugin->OnRawAudioData(data, samples, channels, bits);
                        });
                    });
                }
                // statistics
                {
                    auto current_time = TimeUtil::GetCurrentTimestamp();
                    if (last_post_audio_time_ == 0) {
                        last_post_audio_time_ = current_time;
                    }
                    auto diff = current_time - last_post_audio_time_;
                    last_post_audio_time_ = current_time;
                    statistics_->AppendAudioFrameGap(diff);
                }
            }
            else if (frame.left_ch_data_ && frame.right_ch_data_) {
                PostGlobalTask([weak_self, frame]() {
                    auto self = weak_self.lock();
                    if (!self || self->exit_app_) {
                        return;
                    }
                    auto bytes = 960;
                    auto single_bytes = bytes/2;
                    if (self->fft_left_.size() != single_bytes) {
                        self->fft_left_.resize(single_bytes);
                    }
                    if (self->fft_right_.size() != single_bytes) {
                        self->fft_right_.resize(single_bytes);
                    }
                    FFT32::DoFFT(self->fft_left_, frame.left_ch_data_, 960, true);
                    FFT32::DoFFT(self->fft_right_, frame.right_ch_data_, 960, true);
                    int cpy_size = 150;
                    if (self->fft_left_.size() < cpy_size || self->fft_right_.size() < cpy_size) {
                        return;
                    }

                    self->statistics_->CopyLeftSpectrum(self->fft_left_, cpy_size);
                    self->statistics_->CopyRightSpectrum(self->fft_right_, cpy_size);
                });

                context_->PostStreamPluginTask([weak_self, frame, samples, channels, bits]() {
                    auto self = weak_self.lock();
                    if (!self || self->exit_app_ || !self->plugin_manager_) {
                        return;
                    }
                    self->plugin_manager_->VisitStreamPlugins([=](GrStreamPlugin *plugin) {
                        plugin->OnSplitRawAudioData(frame.left_ch_data_, frame.right_ch_data_, samples, channels, bits);
                        plugin->OnSplitFFTAudioData(self->fft_left_, self->fft_right_);
                    });
                });
            }
        });

        audio_capture_thread_ = Thread::MakeOnceTask([=, this]() {
            audio_capture_plugin_->StartProviding();
        }, "global audio capture", false);
    }

    void RdApplication::PostGlobalAppMessage(std::shared_ptr<AppMessage>&& msg) {
        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            pending_tasks_.push(std::move(msg));
        }
        PostThreadMessage(main_thread_id_, WM_NULL, 0, 0);
    }

    void RdApplication::PostGlobalTask(std::function<void()>&& task) {
        PostGlobalAppMessage(AppMessageMaker::MakeTaskMessage(std::move(task)));
    }

    void RdApplication::PostIpcMessage(std::shared_ptr<Data>&& msg) {

    }

    void RdApplication::PostIpcMessage(const std::string& msg) const {
        if (settings_->capture_.IsVideoInnerCapture()) {
            PostNetMessage(Data::From(msg));
        }
    }

    void RdApplication::PostNetMessage(std::shared_ptr<Data> msg) const {
        if (!msg) {
            return;
        }
        plugin_manager_->VisitNetPlugins([=](GrNetPlugin* plugin) {
            plugin->PostProtoMessage(msg, true);
        });
    }

    void RdApplication::StartProcessWithHook() {
#if 0
        msg_listener_->Listen<MsgVideoFrameEncoded>([=, this](const MsgVideoFrameEncoded& msg) {
            auto net_msg = NetMessageMaker::MakeVideoFrameMsg([=]() -> tc::VideoType {
                return (Encoder::EncoderFormat)msg.frame_encode_type_ == Encoder::EncoderFormat::kH264 ? tc::VideoType::kNetH264 : tc::VideoType::kNetHevc;
            } (), msg.data_, msg.frame_index_, msg.frame_width_, msg.frame_height_, msg.key_frame_, msg.monitor_name_,
            msg.monitor_left_, msg.monitor_top_, msg.monitor_right_, msg.monitor_bottom_);

            if (settings_->app_.debug_enabled_) {
                if (!debug_encode_file_) {
                    debug_encode_file_ = File::OpenForWriteB("1.debug_after_encode.h264");
                }
                debug_encode_file_->Append(msg.data_->AsString());
                LOGI("encoded frame callback, size: {}x{}, buffer size: {}", msg.frame_width_, msg.frame_height_, msg.data_->Size());
            }
            PostNetMessage(net_msg);
        });

        auto fn_start_process = [=, this]() {
            bool ok = app_manager_->StartProcessWithHook();
            if (!ok) {
                LOGE("StartProcessWithHook failed.");
            }
        };

        bool is_steam_app = settings_->app_.IsSteamUrl();
        // steam app
        if (is_steam_app) {
            fn_start_process();
            return;
        }
        fn_start_process();
#endif
    }

    void RdApplication::StartProcessWithScreenCapture() {
        msg_listener_->Listen<CaptureVideoFrame>([=, this](const CaptureVideoFrame& msg) {
            // todo: RtcLocal process
            //

            if (!HasConnectedPeer()) {
                return;
            }
            bool only_audio_clients = true;
            plugin_manager_->VisitNetPlugins([&](GrNetPlugin* plugin) {
                if (plugin->IsWorking() && !plugin->IsOnlyAudioClients()) {
                    only_audio_clients = false;
                }
            });
            if (only_audio_clients) {
                LOGI("Only audio clients, ignore video frame.");
                return;
            }

            // calculate gaps between 2 captured frames.
            //{
            //    auto current_time = TimeUtil::GetCurrentTimestamp();
            //    if (last_capture_screen_time_ == 0) {
            //        last_capture_screen_time_ = current_time;
            //    }
            //    auto gap = current_time - last_capture_screen_time_;
            //    last_capture_screen_time_ = current_time;
            //    statistics_->AppendFrameGap(gap);
            //}

            // to encode
            encoder_thread_->Encode(msg);
        });

        msg_listener_->Listen<CaptureCursorBitmap>([=, this](const CaptureCursorBitmap& cursor_msg) {
            auto net_msg = NetMessageMaker::MakeCursorInfoSyncMsg(cursor_msg.x_, cursor_msg.y_, cursor_msg.hotspot_x_,
                                                                  cursor_msg.hotspot_y_, cursor_msg.width_, cursor_msg.height_,
                                                                  cursor_msg.visible_, cursor_msg.data_, cursor_msg.type_);
            PostNetMessage(net_msg);
        });

        if (capture_plugin_) {
            LOGI("Will start capturing by using: {}", capture_plugin_->GetPluginName());
            auto r = capture_plugin_->StartCapturing();
            if (!r) {
                LOGE("StartCapturing failed in : {}", capture_plugin_->GetPluginName());
                if (capture_plugin_->GetPluginId() == kDdaCapturePluginId) {
                    LOGW("The failed capture is DDA, will change to GDI");
                    if (SwitchGdiCapture() && capture_plugin_) {
                        capture_plugin_->StartCapturing();
                    }
                }
            }
        }
        if (data_provider_plugin) {
            data_provider_plugin->StartProviding();
        }
        app_manager_->StartProcess();
    }

    void RdApplication::OnIpcVideoFrame(const std::shared_ptr<CaptureVideoFrame>& msg) const {
        if (!HasConnectedPeer()) {
            return;
        }
        encoder_thread_->Encode(*msg);
    }

    bool RdApplication::HasConnectedPeer() const {
        return plugin_manager_->GetTotalConnectedClientsCount();
    }

    void RdApplication::WriteBoostUpInfoForPid(uint32_t pid) const {
        if (!app_shared_message_) {
            LOGE("Don't have app_shared_message_");
            return;
        }
        auto shm_name = std::format("application_shm_{}", pid);
        std::string shm_buffer;
        shm_buffer.resize(sizeof(AppSharedMessage));
        memcpy(shm_buffer.data(), app_shared_message_.get(), sizeof(AppSharedMessage));
        app_shared_info_->WriteData(shm_name, shm_buffer);
    }

    void RdApplication::SendAudioSpectrumMessage() const {
        auto st = RdStatistics::Instance();
        auto msg = std::make_shared<Message>();
        msg->set_type(tc::kRendererAudioSpectrum);
        auto sas = msg->mutable_renderer_audio_spectrum();
        sas->set_samples(st->audio_samples_);
        sas->set_bits(st->audio_bits_);
        sas->set_channels(st->audio_channels_);
        auto left_spectrum = st->GetLeftSpectrum();
        auto right_spectrum = st->GetRightSpectrum();
        sas->mutable_left_spectrum()->Add(left_spectrum.begin(), left_spectrum.end());
        sas->mutable_right_spectrum()->Add(right_spectrum.begin(), right_spectrum.end());
        auto net_msg = ProtoAsData(msg);

        // audio spectrum
        PostNetMessage(net_msg);
    }

    void RdApplication::ReportAudioSpectrum2Panel() {
        auto st = RdStatistics::Instance();
        auto msg = std::make_shared<tcrp::RpMessage>();
        msg->set_type(tcrp::kRpServerAudioSpectrum);
        auto sas = msg->mutable_renderer_audio_spectrum();
        sas->set_samples(st->audio_samples_);
        sas->set_bits(st->audio_bits_);
        sas->set_channels(st->audio_channels_);
        auto left_spectrum = st->GetLeftSpectrum();
        auto right_spectrum = st->GetRightSpectrum();
        sas->mutable_left_spectrum()->Add(left_spectrum.begin(), left_spectrum.end());
        sas->mutable_right_spectrum()->Add(right_spectrum.begin(), right_spectrum.end());
        auto buffer = RpProtoAsData(msg);
        PostPanelMessage(buffer);
    }

    void RdApplication::SendClipboardMessage(const std::string& msg) const {
        tc::Message m;
        m.set_type(tc::kClipboardInfo);
        m.mutable_clipboard_info()->set_msg(msg);
        auto buffer = ProtoAsData(&m);
        PostNetMessage(buffer);
    }

    void RdApplication::SendConfigurationBack() {
        {
            std::lock_guard<std::mutex> lk(capture_plugin_mtx_);
            if (!capture_plugin_) {
                LOGE("SendConfigurationBack failed, working monitor capture plugin is null.");
                return;
            }
        }

        std::vector<CaptureMonitorInfo> monitors = [this]() {
            std::lock_guard<std::mutex> lk(capture_plugin_mtx_);
            return capture_plugin_->GetCaptureMonitorInfo();
        }();
        if (monitors.empty()) {
            LOGW("Ignore this sending configuration back, 'cause there's no monitors detected.");
            return;
        }

        // update capturing monitor info
        this->UpdateCapturingMonitorInfo();

        tc::Message m;
        m.set_type(tc::kServerConfiguration);
        auto config = m.mutable_config();
        // screen info
        auto monitors_info = config->mutable_monitors_info();
        auto capturing_name = [this]() {
            std::lock_guard<std::mutex> lk(capture_plugin_mtx_);
            return capture_plugin_->GetCapturingMonitorName();
        }();

        LOGI("Will send configuration back, monitor size: {}", monitors.size());
        for (int i = 0; i < monitors.size(); i++) {
            auto monitor = monitors[i];
            MonitorInfo info;
            info.set_name(monitor.name_);
            for (const auto& res : monitor.supported_res_) {
                MonitorResolution mr;
                mr.set_width(res.width_);
                mr.set_height(res.height_);
                info.mutable_resolutions()->Add(std::move(mr));
            }
            info.set_current_width(monitor.Width());
            info.set_current_height(monitor.Height());
            monitors_info->Add(std::move(info));
        }
        LOGI("Will send configuration back, fps: {}", settings_->encoder_.fps_);
        config->set_fps(settings_->encoder_.fps_);
        config->set_capturing_monitor_name(capturing_name);
        config->set_file_transfer_enabled(settings_->file_transfer_enabled_);
        config->set_audio_enabled(settings_->audio_enabled_);
        config->set_can_be_operated(settings_->can_be_operated_);
        //
        auto buffer = ProtoAsData(&m);
        PostNetMessage(buffer);
    }

    void RdApplication::RequestRestartMe() const {
        tcrp::RpMessage m;
        m.set_type(tcrp::kRpRestartServer);
        m.mutable_restart_server()->set_reason("restart");
        auto buffer = RpProtoAsData(&m);
        ws_panel_client_->PostNetMessage(buffer);
    }

    void RdApplication::ResetMonitorResolution(const std::string& name, int w, int h) {
        DEVMODE dm;
        dm.dmSize = sizeof(dm);
        dm.dmPelsWidth = w;
        dm.dmPelsHeight = h;
        dm.dmBitsPerPel = 32;
        dm.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
        auto deviceName = StringUtil::ToWString(name);//L"\\\\.\\DISPLAY1";
        LONG result = ChangeDisplaySettingsExW(deviceName.c_str(), &dm, nullptr, CDS_FULLSCREEN, nullptr);
        bool ok = result == DISP_CHANGE_SUCCESSFUL;

        tc::Message m;
        m.set_type(tc::kChangeMonitorResolutionResult);
        auto r = m.mutable_change_monitor_resolution_result();
        r->set_monitor_name(name);
        r->set_result(ok);
        auto buffer = ProtoAsData(&m);
        PostNetMessage(buffer);
    }

    std::shared_ptr<PluginManager> RdApplication::GetPluginManager() {
        return plugin_manager_;
    }

    tc::GrMonitorCapturePlugin* RdApplication::GetWorkingMonitorCapturePlugin() {
        std::lock_guard<std::mutex> lk(capture_plugin_mtx_);
        return capture_plugin_;
    }

    std::map<std::string, GrVideoEncoderPlugin*> RdApplication::GetWorkingVideoEncoderPlugins() const {
        if (encoder_thread_) {
            return encoder_thread_->GetWorkingVideoEncoderPlugins();
        }
        return {};
    }

    bool RdApplication::GenerateD3DDevice(uint64_t adapter_uid) {
        LOGI("GenerateD3DDevice, adapter_uid = {}", adapter_uid);
        ClearD3DDevice(adapter_uid);
        ClearPluginD3DState(adapter_uid);

        auto new_device_wrapper = std::make_shared<D3D11DeviceWrapper>();

        ComPtr<IDXGIFactory1> factory1;
        ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC desc;
        HRESULT res = NULL;
        int adapter_index = 0;
        bool adapter_found = false;
        res = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory1.GetAddressOf()));
        if (res != S_OK) {
            LOGE("CreateDXGIFactory1 failed");
            return false;
        }
        if (adapter_uid != static_cast<uint64_t>(-1)) {
            while (true) {
                adapter.Reset();
                res = factory1->EnumAdapters1(adapter_index, adapter.GetAddressOf());
                if (res != S_OK) {
                    LOGW("EnumAdapters1 index:{} failed, adapter_uid={}", adapter_index, adapter_uid);
                    break;
                }

                adapter->GetDesc(&desc);
                if (adapter_uid == desc.AdapterLuid.LowPart) {
                    LOGI("Adapter Index:{} Name: {}", adapter_index, StringUtil::ToUTF8(desc.Description).c_str());
                    LOGI("find adapter");
                    adapter_found = true;
                    break;
                }
                ++adapter_index;
            }
        }

        D3D_FEATURE_LEVEL featureLevel;
        if (adapter_found) {
            res = D3D11CreateDevice(adapter.Get(),
                                D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                nullptr, 0, D3D11_SDK_VERSION,
                                &new_device_wrapper->d3d11_device_, &featureLevel, &new_device_wrapper->d3d11_device_context_);
        }
        else {
            LOGW("Adapter uid {} not found or virtual/RDP path, fallback to generic D3D device creation", adapter_uid);
            res = D3D11CreateDevice(nullptr,
                                D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                nullptr, 0, D3D11_SDK_VERSION,
                                &new_device_wrapper->d3d11_device_, &featureLevel, &new_device_wrapper->d3d11_device_context_);
            if (res != S_OK || !new_device_wrapper->d3d11_device_ || !new_device_wrapper->d3d11_device_context_) {
                LOGW("Fallback hardware D3D11CreateDevice failed: {}, try WARP", res);
                new_device_wrapper->d3d11_device_.Reset();
                new_device_wrapper->d3d11_device_context_.Reset();
                res = D3D11CreateDevice(nullptr,
                                    D3D_DRIVER_TYPE_WARP, nullptr,
                                    D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    nullptr, 0, D3D11_SDK_VERSION,
                                    &new_device_wrapper->d3d11_device_, &featureLevel, &new_device_wrapper->d3d11_device_context_);
            }
        }

        if (res != S_OK || !new_device_wrapper->d3d11_device_ || !new_device_wrapper->d3d11_device_context_) {
            LOGE("D3D11CreateDevice failed: {}", res);
            ClearD3DDevice(adapter_uid);
            return false;
        } else {
            LOGI("D3D11CreateDevice mDevice = {}", (void *) new_device_wrapper->d3d11_device_.Get());
            new_device_wrapper->adapter_uid_ = adapter_uid;
            d3d11_devices_[adapter_uid] = new_device_wrapper;
            d3d11_device_failure_counts_[adapter_uid] = 0;
            return true;
        }
    }

    void RdApplication::ClearD3DDevice(uint64_t adapter_uid) {
        if (!d3d11_devices_.contains(adapter_uid)) {
            return;
        }
        if (d3d11_devices_[adapter_uid]) {
            d3d11_devices_[adapter_uid]->Release();
        }
        d3d11_devices_.erase(adapter_uid);
    }

    void RdApplication::ClearPluginD3DState(uint64_t adapter_uid) {
        if (!plugin_manager_) {
            return;
        }
        plugin_manager_->VisitAllPlugins([adapter_uid](GrPluginInterface* plugin) {
            plugin->d3d11_devices_.erase(adapter_uid);
            plugin->d3d11_devices_context_.erase(adapter_uid);
        });
    }

    void RdApplication::HandleD3DDeviceFailure(uint64_t adapter_uid, const std::string& reason) {
        LOGE("HandleD3DDeviceFailure adapter_uid={}, reason={}", adapter_uid, reason);
        ClearD3DDevice(adapter_uid);
        ClearPluginD3DState(adapter_uid);
        if (encoder_thread_) {
            encoder_thread_->HandleD3DDeviceFailure(adapter_uid);
        }

        const auto fail_count = ++d3d11_device_failure_counts_[adapter_uid];
        if (fail_count < 2) {
            return;
        }

        auto weak_self = weak_from_this();
        context_->PostTask([weak_self, adapter_uid, fail_count]() {
            auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            if (!self->IsCurrentDdaCapture() || self->force_gdi_) {
                return;
            }
            LOGW("D3D device generation failed repeatedly, downgrade capture to GDI. adapter_uid={}, fail_count={}", adapter_uid, fail_count);
            if (self->SwitchGdiCapture() && self->capture_plugin_) {
                self->capture_plugin_->StartCapturing();
            }
        });
    }

    ComPtr<ID3D11Device> RdApplication::GetD3DDevice(uint64_t adapter_uid) {
        if (!d3d11_devices_.contains(adapter_uid)) {
            return nullptr;
        }
        return d3d11_devices_[adapter_uid]->d3d11_device_;
    }

    ComPtr<ID3D11DeviceContext> RdApplication::GetD3DContext(uint64_t adapter_uid) {
        if (!d3d11_devices_.contains(adapter_uid)) {
            return nullptr;
        }
        return d3d11_devices_[adapter_uid]->d3d11_device_context_;
    }

    void RdApplication::ReqCtrlAltDelete(const std::string& device_id, const std::string& stream_id) const {
        if (!service_client_ || !service_client_->IsAlive()) {
            LOGE("Service client not connected, can't ReqCtrlAltDelete");
            return;
        }
        tc::ServiceMessage m;
        m.set_type(ServiceMessageType::kSrvReqCtrlAltDelete);
        m.mutable_req_ctrl_alt_delete()->set_req_device_id(device_id);
        m.mutable_req_ctrl_alt_delete()->set_req_stream_id(stream_id);
        service_client_->PostNetMessage(m.SerializeAsString());
    }

    std::shared_ptr<WinDesktopManager> RdApplication::GetDesktopManager() {
        return desktop_mgr_;
    }

    bool RdApplication::SwitchGdiCapture() {
        if (IsCurrentGdiCapture()) {
            return true;
        }

        std::lock_guard<std::mutex> lk(capture_plugin_mtx_);
        if (capture_plugin_) {
            capture_plugin_->StopCapturing();
        }
        if (!gdi_capture_plugin_) {
            LOGE("Don't have gdi plugin, ignore!");
            return false;
        }
        capture_plugin_ = gdi_capture_plugin_;
        capture_plugin_->SetCaptureFps(settings_->encoder_.fps_);
        capture_plugin_->EnablePlugin();
        LOGI("Use gdi capture plugin.");
        return true;
    }

    bool RdApplication::SwitchDdaCapture() {
        if (IsCurrentDdaCapture()) {
            return true;
        }

        std::lock_guard<std::mutex> lk(capture_plugin_mtx_);
        if (capture_plugin_) {
            capture_plugin_->StopCapturing();
        }
        if (!dda_capture_plugin_) {
            LOGE("Don't have gdi plugin, ignore!");
            return false;
        }
        capture_plugin_ = dda_capture_plugin_;
        capture_plugin_->SetCaptureFps(settings_->encoder_.fps_);
        capture_plugin_->EnablePlugin();
        LOGI("Use dda capture plugin.");
        return true;
    }

    bool RdApplication::IsCurrentGdiCapture() {
        std::lock_guard<std::mutex> lk(capture_plugin_mtx_);
        return capture_plugin_ && capture_plugin_->GetPluginId() == kGdiCapturePluginId;
    }

    bool RdApplication::IsCurrentDdaCapture() {
        std::lock_guard<std::mutex> lk(capture_plugin_mtx_);
        return capture_plugin_ && capture_plugin_->GetPluginId() == kDdaCapturePluginId;
    }

    bool RdApplication::TryInitDdaCapture() {
        if (!dda_capture_plugin_) {
            return false;
        }
        return dda_capture_plugin_->TryInitSpecificCapture();
    }

    void RdApplication::PostPanelMessage(std::shared_ptr<Data> msg) {
        if (ws_panel_client_ && msg) {
            ws_panel_client_->PostNetMessage(msg);
        }
    }

    void RdApplication::HandleForceGdiEvent(bool force_gdi) {
        force_gdi_ = force_gdi;
        auto weak_self = weak_from_this();
        context_->PostTask([weak_self, force_gdi]() {
            auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            if (force_gdi) {
                self->SwitchGdiCapture();
            }
            else {
                self->SwitchDdaCapture();
            }
            if (self->capture_plugin_) {
                self->capture_plugin_->StartCapturing();
            }
        });
    }

    void RdApplication::UpdateCapturingMonitorInfo() {
        const auto plugin = this->GetWorkingMonitorCapturePlugin();
        if (!plugin) {
            LOGE("ProcessCapturingMonitorInfoEvent failed, plugin is null.");
            return;
        }
        const auto cm_msg = CaptureMonitorInfoMessage {
            .monitors_ = plugin->GetCaptureMonitorInfo(),
            .capturing_monitor_name_ = plugin->GetCapturingMonitorName(),
            .virtual_desktop_bound_rectangle_info_ = plugin->GetVirtualDesktopBoundRectangleInfo()
        };

        LOGI("Config Monitors size: {}", cm_msg.monitors_.size());
        if (cm_msg.monitors_.empty()) {
            LOGE("Don't have monitors, ignore the event replayer updating.");
            return;
        }

        // to event replayer
        if (const auto erp_plugin = plugin_manager_->GetEventsReplayerPlugin(); erp_plugin) {
            erp_plugin->UpdateCaptureMonitorInfo(cm_msg);
            LOGI("Update CaptureMonitorInfo to replayer plugin finished.");
        }
    }

    void RdApplication::Exit() {
        if (app_timer_) {
            app_timer_->StopTimers();
        }
        if (app_shared_info_) {
            app_shared_info_->Exit();
        }
        if (audio_capture_thread_ && audio_capture_thread_->IsJoinable()) {
            audio_capture_thread_->Join();
        }
        if (app_manager_) {
            app_manager_->Exit();
        }
        if (encoder_thread_) {
            encoder_thread_->Exit();
        }
        if (ws_panel_client_) {
            ws_panel_client_->Exit();
            ws_panel_client_ = nullptr;
        }

        exit_app_ = true;
        PostThreadMessage(main_thread_id_, WM_QUIT, 0, 0);
    }

    // ------------------------------------------------------ //
    // Windows
    WinApplication::WinApplication(const AppParams& args)
        : RdApplication(args) {

    }

    WinApplication::~WinApplication() {
        RdApplication::~RdApplication();
    }

    int WinApplication::Run() {
        LoadDxAddress();
        RdApplication::Run();
        return 0;
    }

    void WinApplication::Exit() {
        RdApplication::Exit();
    }

    static std::function<BOOL(DWORD)> s_ctrl_handler;
    static BOOL ConsoleHandler(DWORD signal) {
        if (s_ctrl_handler) {
            return s_ctrl_handler(signal);
        }
        return FALSE;
    }

    void WinApplication::CaptureControlC() {
        s_ctrl_handler = [this](DWORD signal) -> BOOL {
            if (signal == CTRL_C_EVENT) {
                std::cout << "CTRL+C detected, localVar value is " << "\n";
                this->Exit();
                return TRUE;
            }
            return FALSE;
        };
        if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
            LOGE("ERROR: Could not set control handler");
        }
    }

    void WinApplication::LoadDxAddress() {
        app_shared_message_ = DxAddressLoader::LoadDxAddress();
        if (app_shared_message_) {
            app_shared_message_->ipc_port_ = settings_->transmission_.listening_port_;
            app_shared_message_->self_size_ = sizeof(AppSharedMessage);
            app_shared_message_->enable_hook_events_ = 1;
        } else {
            LOGE("LoadDxAddress failed.");
        }
    }

    // Windows
    // ------------------------------------------------------ //
}
