//
// Created by RGAA on 2023-12-16.
//

#include "rd_app.h"
#include <windows.h>
#include <random>
#include <thread>
#include "rd_context.h"
#include "px_common_new/log.h"
#include "px_common_new/file.h"
#include "px_common_new/data.h"
#include "px_common_new/image.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/thread.h"
#include "px_common_new/process_util.h"
#include "px_common_new/string_util.h"
#include "px_common_new/time_util.h"
#include "px_encoder_new/video_encoder_factory.h"
#include "px_capture_new/capture_message.h"
#include "px_capture_new/capture_message_maker.h"
#include "px_capture_new/process_loopback_support.h"
#include "app/app_manager.h"
#include "app/app_manager_factory.h"
#include "app/app_messages.h"
#include "settings/rd_settings.h"
#include "render_panel/network/ws_panel_server.h"
#include "app/encoder_thread.h"
#include "network/net_message_maker.h"
#include "px_message.pb.h"
#include "px_render_panel_message.pb.h"
#include "app/app_timer.h"
#include "px_opus_codec_new/opus_codec.h"
#include "network/ws_panel_client.h"
#include "network/server_cast.h"
#include "app/app_shared_info.h"
#include "app/win/dx_address_loader.h"
#include "px_common_new/win32/win_helper.h"
#include "px_common_new/fft_32.h"
#include "px_common_new/hardware.h"
#include "px_common_new/shared_preference.h"
#include "px_controller/vigem/vigem_controller.h"
#include "px_controller/vigem_driver_manager.h"
#include "rd_statistics.h"
#include "network/render_service_client.h"
#include "px_render/plugins/plugin_manager.h"
#include "px_render/plugins/plugin_ids.h"
#include "px_render/plugin_interface/px_stream_plugin.h"
#include "px_render/plugin_interface/px_net_plugin.h"
#include "px_render/plugin_interface/px_monitor_capture_plugin.h"
#include "px_render/plugin_interface/px_data_provider_plugin.h"
#include "px_render/plugin_interface/px_audio_encoder_plugin.h"
#include "px_service_message.pb.h"
#include "app/win/win_desktop_manager.h"
#include "px_common_new/win32/d3d11_wrapper.h"
#include "px_message_new/proto_converter.h"
#include "px_message_new/rp_proto_converter.h"
#include "px_common_new/memory_stat.h"
#include "px_common_new/folder_util.h"
#include "px_common_new/virtual_display_limits.h"
#include "webview/webview_runtime.h"

namespace px
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
        Exit();
        LOGI("RdApplication dtor");
    }

    void RdApplication::Init(int argc, char** argv) {
        init_failed_ = false;
        init_error_.clear();



        // sp
        sp_ = SharedPreference::Instance();
        auto path = FolderUtil::GetProgramDataPath() + L"/px_data";
        std::string sp_name = std::format("pixels_render_{}.dat", settings_->transmission_.listening_port_);
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
        // Assign early so net_ws /ipc can late-bind OnIpcVideoFrame during plugin Start().
        rdApp = shared_from_this();
        plugin_manager_ = PluginManager::Make(shared_from_this());
        context_->SetPluginManager(plugin_manager_);

        plugin_manager_->LoadAllPlugins();
        plugin_manager_->RegisterPluginEventsCallback();
        plugin_manager_->DumpPluginInfo();

        // Game-hook first frames can arrive while the target process is still
        // bringing up its D3D device.  On some NVIDIA drivers, creating our
        // first device at exactly that moment may block indefinitely.  Create
        // and cache the default hardware device before starting/injecting the
        // game; GenerateD3DDevice resolves it to its actual adapter LUID.
        if (!GenerateD3DDevice(static_cast<uint64_t>(-1))) {
            LOGW("Early D3D11 device prewarm failed; will retry for the frame adapter.");
        }

        statistics_->SetApplication(shared_from_this());
        statistics_->StartMonitor();

        // connect to service
        LOGI("Will connect the service!");
        service_client_ = std::make_shared<RenderServiceClient>(shared_from_this());

        // connect panel
        LOGI("Will connect the panel!");
        ws_panel_client_ = std::make_shared<WsPanelClient>(context_);
        ws_panel_client_->Start();

        // app manager
        app_manager_ = AppManagerFactory::Make(context_);
        // encoder in thread
        encoder_thread_ = EncoderThread::Make(shared_from_this());
        // event bus listener
        msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kControl);
        state_msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kState);
        // app shared info
        app_shared_info_ = AppSharedInfo::Make(context_);

        // app timer
        InitAppTimer();
        // messages
        InitMessages();
        // Register the application listeners before the service connection can
        // publish MsgRenderConnected2Service.  The local service may accept the
        // socket immediately; starting it earlier could lose that edge-triggered
        // notification and skip restoration of persisted virtual-display state.
        service_client_->Start();
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
                    init_failed_ = true;
                    init_error_ = "no valid capture plugin";
                    Exit();
                    return -1;
                }

                // test only gdi begin
                //capture_plugin_ = gdi_capture_plugin_;
                // test only gdi end

                LOGI("Use capture fps: {}", settings_->encoder_.fps_);
                if (capture_plugin_ && capture_plugin_->IsPluginEnabled()) {
                    LOGI("Use dda capture plugin.");
                    capture_plugin_->SetCaptureFps(settings_->encoder_.fps_);
                    const auto weak_self = weak_from_this();
                    capture_plugin_->SetCaptureErrorCallback([weak_self](const MonitorCaptureError& err) {
                        const auto self = weak_self.lock();
                        if (!self || self->exit_app_) {
                            return;
                        }
                        LOGE("*** capture error: {}", (int)err);
                        // the callback runs on the capture thread, switching capture must be
                        // done on the main thread, otherwise stopping DDA would join itself.
                        self->PostGlobalTask([weak_self]() {
                            const auto self = weak_self.lock();
                            if (!self || self->exit_app_) {
                                return;
                            }
                            if (self->IsCurrentGdiCapture()) {
                                LOGI("Already use GDI capture, ignore the error.");
                                return;
                            }
                            if (self->monitor_changed_) {
                                LOGI("Maybe montor changed, ignore this error now.");
                                return;
                            }
                            // change to GDI
                            // capture_plugin_->DisablePlugin();
                            LOGI("Don't use DDA, will switch to GDI.");
                            if (!self->SwitchGdiCapture() || !self->capture_plugin_) {
                                LOGE("Switch to GDI failed or no capture plugin available.");
                                return;
                            }
                            self->capture_plugin_->StartCapturing();
                        });
                    });
                }
                else {
                    LOGI("Don't use DDA, will switch to GDI.");
                    SwitchGdiCapture();
                }
            }
        }

        if (settings_->capture_.enable_video_) {
            // application.mode in settings.toml decides path:
            // game-hook → start/inject game; desktop → screen capture (never launch game-path).
            if (settings_->IsWebViewMode()) {
                StartWebView();
            } else if (settings_->IsGameHookMode()) {
                StartProcessWithHook();
            } else {
                StartProcessWithScreenCapture();
            }
        }

        if (init_failed_) {
            LOGE("RdApplication abort after game-hook start failure: {}", init_error_);
            Exit();
            return -1;
        }

        // desktop manager
        desktop_mgr_ = WinDesktopManager::Make(context_);

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
        Exit();
        return 0;
    }

    void RdApplication::InitAppTimer() {
        app_timer_ = std::make_shared<AppTimer>(context_);
        app_timer_->StartTimers();
    }

    void RdApplication::InitMessages() {
        auto weak_self = weak_from_this();
        msg_listener_->Listen<MsgBeforeInject>([weak_self](const MsgBeforeInject& msg) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            // Prefer PrepareGameHookBoot() called synchronously before InjectDll.
            // This async path is a fallback only.
            if (self->settings_->capture_.IsVideoInnerCapture()) {
                self->PrepareGameHookBoot(msg.pid_);
            }
        });

        msg_listener_->Listen<MsgObsInjected>([weak_self](const MsgObsInjected& msg) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            // Game-hook audio: start/restart host capture as PID process-loopback (never device mix).
            if (!self->settings_->capture_.IsVideoInnerCapture() || msg.pid_ == 0) {
                return;
            }
            if (!PreferProcessLoopbackCapture()) {
                LOGI("MsgObsInjected pid={}: skip host PID loopback (force_hook={} os_supported={})",
                     msg.pid_, ForceInProcessHookAudio(), IsProcessLoopbackCaptureSupported());
                return;
            }
            // MUST NOT run MiniAudio/WASAPI ActivateAudioInterfaceAsync on the UI/message
            // thread: the async activation needs a pumping thread and will stall ~20s then
            // fail, producing no CaptureAudioFrame (video still works on other threads).
            self->PostGlobalTask([weak_self, pid = msg.pid_]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_ || !self->audio_capture_plugin_) {
                    LOGE("MsgObsInjected: cannot start PID audio (app/plugin missing) pid={}", pid);
                    return;
                }
                LOGI("MsgObsInjected: schedule PID process-loopback on worker pid={}", pid);
                if (self->audio_capture_thread_ && self->audio_capture_thread_->IsJoinable()) {
                    LOGI("MsgObsInjected: stopping previous audio worker before restart");
                    self->audio_capture_plugin_->StopProviding();
                    self->audio_capture_thread_->Join();
                }
                self->audio_capture_plugin_->SetAudioLoopbackProcessId(pid);
                self->audio_capture_thread_ = Thread::MakeOnceTask([weak_self, pid]() {
                    auto self = weak_self.lock();
                    if (!self || self->exit_app_ || !self->audio_capture_plugin_) {
                        return;
                    }
                    // MiniAudio manages COM itself: ma_context_init CoInitializeEx's the
                    // calling thread and ma_context_uninit balances it, and its WASAPI
                    // worker thread CoInitializeEx/CoUninitialize's itself (miniaudio.h).
                    // ProcessLoopbackAudioCapture also initializes COM on its own capture
                    // thread. So this thread's COM init is only for the duration of
                    // Stop/StartProviding and must be paired before the thread exits.
                    const HRESULT co_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                    const bool co_init = (co_hr == S_OK || co_hr == S_FALSE);
                    LOGI("PID audio worker: Stop+Start begin pid={} CoInitializeEx=0x{:08x}", pid,
                         static_cast<unsigned>(co_hr));
                    self->audio_capture_plugin_->StopProviding();
                    self->audio_capture_plugin_->SetAudioLoopbackProcessId(pid);
                    self->audio_capture_plugin_->StartProviding();
                    const bool ok = self->audio_capture_plugin_->IsProviding();
                    const int err = self->audio_capture_plugin_->GetLastStartError();
                    if (ok) {
                        LOGI("PID audio worker: StartProviding OK pid={}", pid);
                    } else {
                        LOGE("PID audio worker: StartProviding FAILED pid={} err={} "
                             "(no host capture; in-process hook disabled when loopback supported)",
                             pid, err);
                    }
                    if (co_init) {
                        CoUninitialize();
                    }
                }, "pid audio capture", false);
            });
        });

        state_msg_listener_->Listen<MsgTimer16>([weak_self](const MsgTimer16&) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            self->context_->PostTask([weak_self]() {
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

            self->PostGlobalTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                self->SendAudioSpectrumMessage();
            });
        });

        state_msg_listener_->Listen<MsgTimer100>([weak_self](const MsgTimer100&) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            self->PostGlobalTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                // If you want a much smoother spectrum, report it quicker, post it in MsgTimer16 callback
                self->ReportAudioSpectrum2Panel();
            });
        });

        state_msg_listener_->Listen<MsgTimer1000>([weak_self](const MsgTimer1000&) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            self->statistics_->IncreaseRunningTime();

            auto plugin_manager = self->context_->GetPluginManager();
            plugin_manager->On1Second();

#if MEMORY_STST_ON
            self->context_->PostTask([]() {
                auto info = MemoryStat::Instance()->GetStatInfo();
                LOGI("Memory usage: {}", info.Dump());
            });
#endif
        });

        msg_listener_->Listen<MsgClientConnected>([weak_self](const MsgClientConnected& msg) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            // A reconnect during the grace window invalidates any shutdown
            // scheduled by the previous "last client disconnected" event.
            // Only connections with a stable id participate in game lifetime.
            // The transport name is deliberately not filtered: current web
            // clients may negotiate Direct, UDP or another registered net
            // plugin while preserving the same connect/disconnect id.
            const bool tracked_game_client = self->settings_->IsGameHookMode()
                && !msg.conn_id_.empty();
            if (tracked_game_client) {
                self->game_hook_has_seen_client_ = true;
                std::lock_guard<std::mutex> lock(self->game_hook_clients_mutex_);
                self->game_hook_client_ids_.insert(msg.conn_id_);
            }
            ++self->client_disconnect_generation_;
            if (self->settings_->IsWebViewMode() && self->webview_runtime_) {
                self->webview_runtime_->SetActive(true);
                self->webview_runtime_->SendFocusEvent(true);
            }
        });

        msg_listener_->Listen<MsgClientHello>([weak_self](const MsgClientHello&) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            self->PostGlobalTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                // send configuration back to client
                self->SendConfigurationBack();
            });
        });

        msg_listener_->Listen<MsgClientDisconnected>([weak_self](const MsgClientDisconnected& msg) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            if (self->settings_->IsWebViewMode()) {
                const auto generation = ++self->client_disconnect_generation_;
                const auto weak_webview = weak_self;
                self->context_->PostDelayTask([weak_webview, generation]() {
                    const auto self = weak_webview.lock();
                    if (!self || self->exit_app_
                        || self->client_disconnect_generation_ != generation
                        || self->HasConnectedPeer() || !self->webview_runtime_) {
                        return;
                    }
                    self->webview_runtime_->SendFocusEvent(false);
                    self->webview_runtime_->SetActive(false);
                }, 100);
                return;
            }
            if (!self->settings_->IsGameHookMode()) {
                return;
            }
            bool removed_tracked_client = false;
            if (!msg.conn_id_.empty()) {
                std::lock_guard<std::mutex> lock(self->game_hook_clients_mutex_);
                removed_tracked_client = self->game_hook_client_ids_.erase(msg.conn_id_) > 0;
            }
            // Still update the tracked set during startup so short-lived setup
            // sockets cannot keep the process alive.  Only the stop decision
            // is suppressed until the embedded web listener is ready.
            if (!self->game_hook_startup_grace_complete_) {
                LOGI("Ignore game-hook client-disconnect during startup grace period.");
                return;
            }
            if (!removed_tracked_client) {
                LOGI("Ignore untracked game-hook client-disconnect event.");
                return;
            }
            if (self->HasConnectedPeer()) {
                LOGI("Still has connected clients");
                return;
            }
            if (!self->game_hook_has_seen_client_) {
                LOGW("Ignore game-hook client-disconnect before the first confirmed client connection.");
                return;
            }

            const auto generation = ++self->client_disconnect_generation_;
            LOGI("Last game-hook client disconnected; stop render in 5 seconds unless a client reconnects.");
            self->context_->PostDelayTask([weak_self, generation]() {
                const auto self = weak_self.lock();
                if (!self || self->exit_app_ || self->client_disconnect_generation_ != generation) {
                    return;
                }
                if (self->HasConnectedPeer()) {
                    return;
                }
                LOGI("Game-hook grace period elapsed with no clients; stopping render.");
                ProcessUtil::KillProcess(GetCurrentProcessId());
            }, 5000);
        });

        msg_listener_->Listen<ClipboardMessage>([weak_self](const ClipboardMessage& msg) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            self->PostGlobalTask([weak_self, msg]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                self->SendClipboardMessage(msg.msg_);
            });
        });

        // DDA Init failed
        msg_listener_->Listen<CaptureInitFailedMessage>([weak_self](const CaptureInitFailedMessage&) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            self->PostGlobalTask([weak_self]() {
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
        msg_listener_->Listen<CaptureMonitorInfoMessage>([weak_self](const CaptureMonitorInfoMessage&) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            self->PostGlobalTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                self->SendConfigurationBack();
                if (self->settings_->virtual_display_enabled_ &&
                    !self->settings_->IsGameHookMode()) {
                    // The display driver may publish the Windows topology
                    // notification before the Service operation response is
                    // delivered. Query the authoritative ownership state after
                    // that mutation has had a chance to commit, so a lost
                    // response cannot leave connected clients on a stale count.
                    self->context_->PostDelayTask([weak_self]() {
                        if (const auto self = weak_self.lock(); self && !self->exit_app_) {
                            self->RefreshVirtualDisplayStatus("render-monitor-query");
                        }
                    }, 500);
                }
            });
        });

        msg_listener_->Listen<MsgVirtualDisplayServiceResult>([weak_self](const MsgVirtualDisplayServiceResult& msg) {
            if (const auto self = weak_self.lock(); self && !self->exit_app_) {
                self->UpdateVirtualDisplayStatus(msg);
            }
        });

        msg_listener_->Listen<MsgRenderConnected2Service>([weak_self](const MsgRenderConnected2Service&) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_ || !self->settings_->virtual_display_enabled_ ||
                self->settings_->IsGameHookMode()) {
                return;
            }
            self->RefreshVirtualDisplayStatus("render-startup-query");
        });

        msg_listener_->Listen<MsgReCreateRefresher>([weak_self](const MsgReCreateRefresher&) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            // report to Panel
            // !! USELESS !! Just report it
            if (self->ws_panel_client_) {
                self->ws_panel_client_->ReportMonitorChanged();
            }

            self->monitor_changed_ = true;
            self->context_->PostDelayTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                self->monitor_changed_ = false;
            }, 5000);
        });

        msg_listener_->Listen<MsgModifyFps>([weak_self](const MsgModifyFps& msg) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            std::lock_guard<std::mutex> lk(self->capture_plugin_mtx_);
            if (self->capture_plugin_) {
                self->settings_->encoder_.fps_ = msg.fps_;
                self->capture_plugin_->SetCaptureFps(msg.fps_);
            }
        });

        // request from Remote Panel's context menu or same function
        msg_listener_->Listen<MsgPanelStreamLockScreen>([](const MsgPanelStreamLockScreen& msg) {
            LOGI(" ** Panel request LockScreen from device: {}", msg.from_device_);
            Hardware::LockScreen();
        });

        // request from Remote Panel's context menu or same function
        msg_listener_->Listen<MsgPanelStreamRestartDevice>([](const MsgPanelStreamRestartDevice& msg) {
            LOGI(" ** Panel request RestartDevice from device: {}", msg.from_device_);
            Hardware::RestartDevice();
        });

        // request from Remote Panel's context menu or same function
        msg_listener_->Listen<MsgPanelStreamShutdownDevice>([](const MsgPanelStreamShutdownDevice& msg) {
            LOGI(" ** Panel request ShutdownDevice from device: {}", msg.from_device_);
            Hardware::ShutdownDevice();
        });

        state_msg_listener_->Listen<MsgTimer20S>([weak_self](const MsgTimer20S&) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            self->context_->PostTask([weak_self]() {
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

        // Legacy desktop render watchdog.  A game-hook render owns a launched
        // game in a kill-on-close job, so using "no connected WebRTC client" as
        // a reason to restart it also terminates the game.  Game instances are
        // now lifecycle-managed by Console; without viewers they simply stop
        // capture/encode through the existing HasConnectedPeer gates.
        state_msg_listener_->Listen<MsgTimer1Minute>([weak_self](const MsgTimer1Minute&) {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            if (self->settings_->IsGameHookMode()) {
                return;
            }
            ++self->restart_counter_;
            if (self->restart_counter_ >= 60 * 6) {
                self->restart_counter_ = 0;

                if (self->HasConnectedPeer()) {
                    return;
                }
                LOGW("** Don't have connected clients, will restart render now.");
                ProcessUtil::KillProcess(GetCurrentProcessId());
            }
        });

    }

    void RdApplication::OnCapturedAudioFrame(const CaptureAudioFrame& frame) {
        auto weak_self = weak_from_this();
        if (exit_app_ || !audio_encoder_plugin_) {
            return;
        }

            if (!HasConnectedPeer()) {
                static thread_local uint64_t s_drop = 0;
                if (++s_drop == 1 || (s_drop % 500) == 0) {
                    LOGW("CaptureAudioFrame: no connected peer, drop n={} idx={}", s_drop,
                         frame.frame_index_);
                }
                return;
            }

            int samples = (int)frame.samples_;
            int channels = (int)frame.channels_;
            int bits = (int)frame.bits_;

            if (frame.full_data_) {
                static thread_local uint64_t s_enc = 0;
                if (++s_enc == 1 || (s_enc % 200) == 0) {
                    LOGI("CaptureAudioFrame→encode: n={} {}Hz {}ch {}bit bytes={}", s_enc, samples,
                         channels, bits, frame.full_data_->Size());
                }
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
                        self->plugin_manager_->VisitStreamPlugins([=](PxStreamPlugin *plugin) {
                            plugin->OnRawAudioData(data, samples, channels, bits);
                        });
                        // net_rtc_local consumes raw PCM for the WebRTC audio RTP track
                        // (encoded Opus over DataChannel is intentionally dropped there).
                        self->plugin_manager_->VisitNetPlugins([=](PxNetPlugin *plugin) {
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
                    self->plugin_manager_->VisitStreamPlugins([=](PxStreamPlugin *plugin) {
                        plugin->OnSplitRawAudioData(frame.left_ch_data_, frame.right_ch_data_, samples, channels, bits);
                        plugin->OnSplitFFTAudioData(self->fft_left_, self->fft_right_);
                    });
                });
            }
    }

    void RdApplication::InitAudioCapture() {
        auto weak_self = weak_from_this();
        audio_encoder_plugin_ = plugin_manager_->GetAudioEncoderPlugin();
        if (!audio_encoder_plugin_) {
            return;
        }

        // WebView audio is delivered by CefAudioHandler, never by the OS
        // default device or another process's loopback stream.
        if (settings_->IsWebViewMode()) {
            LOGI("WebView audio: use CEF stream callback");
            return;
        }
        if (settings_->capture_.capture_audio_type_ != Capture::CaptureAudioType::kAudioGlobal) {
            return;
        }
        audio_capture_plugin_ = plugin_manager_->GetAudioCapturePlugin();
        if (!audio_capture_plugin_) {
            return;
        }

        // Desktop: start default-device loopback immediately.
        // Game-hook: wait for MsgObsInjected → PID process-loopback (never device mix).
        // If OS lacks process-loopback, rely on in-process WASAPI hook only.
        if (settings_->capture_.IsVideoInnerCapture()) {
            if (PreferProcessLoopbackCapture()) {
                LOGI("game-hook audio: defer until inject (PID process-loopback)");
            } else {
                LOGI("game-hook audio: in-process hook path "
                     "(force_hook={} os_supported={}; do not start host device-mix)",
                     ForceInProcessHookAudio(), IsProcessLoopbackCaptureSupported());
            }
        } else {
            audio_capture_thread_ = Thread::MakeOnceTask([weak_self]() {
                const auto self = weak_self.lock();
                if (!self || self->exit_app_ || !self->audio_capture_plugin_) {
                    return;
                }
                self->audio_capture_plugin_->StartProviding();
            }, "global audio capture", false);
        }
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
        if (!settings_->capture_.IsVideoInnerCapture() || msg.empty()) {
            return;
        }
        auto data = Data::From(msg);
        // Host → injected DLL over /ipc (WsPlugin only). Do not VisitNetPlugins for this
        // virtual: unrebuilt net plugin DLLs lack the trailing vtable slot and crash.
        plugin_manager_->VisitNetPlugins([=](PxNetPlugin* plugin) {
            if (!plugin || plugin->GetPluginId() != kNetWsPluginId) {
                return;
            }
            plugin->PostIpcBinaryMessage(data);
        });
    }

    void RdApplication::PostNetMessage(std::shared_ptr<Data> msg) const {
        if (!msg) {
            return;
        }
        plugin_manager_->VisitNetPlugins([=](PxNetPlugin* plugin) {
            plugin->PostProtoMessage(msg, true);
        });
    }

    void RdApplication::StartProcessWithHook() {
        // Frames arrive via /ipc → PxPluginCapturedVideoFrameEvent → CaptureVideoFrame
        // on the app bus (same event as DDA). Encode → PluginStreamEventRouter → web.
        if (!settings_->IsGameHookMode()) {
            LOGI("StartProcessWithHook skipped: application.mode is desktop");
            return;
        }
        // Do not let a transient setup socket terminate the game before its
        // embedded web client can connect. After this window the normal
        // tracked-client disconnect path uses the requested five-second grace.
        auto weak_self = weak_from_this();
        context_->PostDelayTask([weak_self]() {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_ || !self->settings_->IsGameHookMode()) return;
            self->game_hook_startup_grace_complete_ = true;
            if (self->HasConnectedPeer()) return;
            LOGI("Game-hook startup grace elapsed with no clients; stopping render.");
            ProcessUtil::KillProcess(GetCurrentProcessId());
        }, 15000);
        LOGI("StartProcessWithHook: game_path={}, capture_method={}",
             settings_->app_.game_path_,
             (int)settings_->app_.inject_method_);
        if (settings_->app_.game_path_.empty()) {
            LOGE("StartProcessWithHook: game-path is empty, cannot start game.");
            init_failed_ = true;
            init_error_ = "game-path is empty";
            return;
        }
        bool ok = app_manager_->StartProcessWithHook();
        if (!ok) {
            LOGE("StartProcessWithHook failed for: {}", settings_->app_.game_path_);
            // Fail fast so Service can report to Console (no orphan Render without game).
            init_failed_ = true;
            init_error_ = std::format("StartProcessWithHook failed: {}", settings_->app_.game_path_);
        } else {
            LOGI("StartProcessWithHook requested OK, inject timer will attach px_gh.dll");
        }
    }

    void RdApplication::StartWebView() {
        if (!settings_->IsWebViewMode()) {
            return;
        }
        webview_runtime_ = std::make_unique<WebViewRuntime>();
        auto weak_self = weak_from_this();
        WebViewRuntimeConfig config{
            .url_b64 = settings_->webview_url_b64_,
            .instance_id = settings_->webview_instance_id_,
            .width = settings_->webview_width_,
            .height = settings_->webview_height_,
            .frame_rate = settings_->encoder_.fps_,
            .enable_audio = settings_->capture_.enable_audio_,
            .accelerated_paint = settings_->webview_gpu_,
        };
        WebViewRuntimeCallbacks callbacks{
            .on_video_frame = [weak_self](const CaptureVideoFrame& frame) {
                const auto self = weak_self.lock();
                // WebViewRuntime::SetActive is the production gate. Do not
                // re-check peer state here: room preparation and RTC data
                // channel callbacks are asynchronous, and the first repaint
                // used to be discarded in that short transition, leaving a
                // static page with no later paint and therefore a black RTC
                // track forever.
                if (!self || self->exit_app_) return;
                self->encoder_thread_->Encode(frame);
            },
            .on_audio_frame = [weak_self](const CaptureAudioFrame& frame) {
                const auto self = weak_self.lock();
                if (!self || self->exit_app_) return;
                self->OnCapturedAudioFrame(frame);
            },
            .on_cursor = [weak_self](const CaptureCursorBitmap& cursor) {
                const auto self = weak_self.lock();
                if (!self || self->exit_app_) return;
                self->PostNetMessage(NetMessageMaker::MakeCursorInfoSyncMsg(
                    cursor.x_, cursor.y_, cursor.hotspot_x_, cursor.hotspot_y_,
                    cursor.width_, cursor.height_, cursor.visible_, cursor.data_, cursor.type_));
            },
            .on_failed = [weak_self](const std::string& error) {
                const auto self = weak_self.lock();
                if (!self || self->exit_app_) return;
                LOGE("WebView runtime failure: {}", error);
                if (self->service_client_) {
                    self->service_client_->NotifyAppInstanceReady(
                        self->settings_->webview_instance_id_,
                        self->settings_->transmission_.listening_port_, false, error);
                }
            },
            .on_first_frame = [weak_self]() {
                LOGI("WebView first off-screen frame is ready");
                if (const auto self = weak_self.lock(); self && self->service_client_) {
                    self->service_client_->NotifyAppInstanceReady(
                        self->settings_->webview_instance_id_,
                        self->settings_->transmission_.listening_port_, true, "");
                }
                if (const auto self = weak_self.lock(); self && self->webview_runtime_ &&
                    !self->HasConnectedPeer() && !self->settings_->webview_smoke_test_) {
                    self->webview_runtime_->SetActive(false);
                }
            },
        };
        std::string error;
        if (!webview_runtime_->Start(GetModuleHandleW(nullptr), config, std::move(callbacks), error)) {
            init_failed_ = true;
            init_error_ = error.empty() ? "WebView runtime start failed" : error;
            LOGE("StartWebView failed: {}", init_error_);
            if (service_client_) {
                service_client_->NotifyAppInstanceReady(
                    settings_->webview_instance_id_,
                    settings_->transmission_.listening_port_, false, init_error_);
            }
            webview_runtime_.reset();
            return;
        }
        // Keep CEF nearly idle until the first viewer. Frames and audio are
        // additionally gated by HasConnectedPeer before encoding.
        // Render one probe frame so Service can distinguish BrowserReady from
        // merely finding the root process. The first-frame callback returns
        // CEF to 1 fps/inactive when no viewer is attached.
        webview_runtime_->SetActive(true);
    }

    void RdApplication::SendWebViewMouseEvent(const MouseEvent& event) {
        if (webview_runtime_) webview_runtime_->SendMouseEvent(event);
    }

    void RdApplication::SendWebViewKeyEvent(const KeyEvent& event) {
        if (webview_runtime_) webview_runtime_->SendKeyEvent(event);
    }

    void RdApplication::SendWebViewTextInput(const TextInput& event) {
        if (webview_runtime_) webview_runtime_->SendTextInput(event);
    }

    void RdApplication::SendWebViewFocusEvent(bool focused) {
        if (webview_runtime_) webview_runtime_->SendFocusEvent(focused);
    }

    void RdApplication::StartProcessWithScreenCapture() {
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

    void RdApplication::OnCapturedVideoFrame(const CaptureVideoFrame& frame) const {
        if (exit_app_) {
            return;
        }
        if (app_manager_) {
            app_manager_->OnCapturedVideoFrame();
        }
        if (!HasConnectedPeer()) {
            return;
        }
        if (!settings_->IsGameHookMode()) {
            bool only_audio_clients = true;
            plugin_manager_->VisitNetPlugins([&only_audio_clients](PxNetPlugin* plugin) { // NOLINT(gammaray-raw-pointer-boundary): plug-in visitor ABI
                if (plugin->IsWorking() && !plugin->IsOnlyAudioClients()) {
                    only_audio_clients = false;
                }
            });
            if (only_audio_clients) {
                LOGI("Only audio clients, ignore video frame.");
                return;
            }
        }
        encoder_thread_->Encode(frame);
    }

    void RdApplication::OnCapturedCursorBitmap(const CaptureCursorBitmap& cursor) const {
        if (exit_app_) {
            return;
        }
        PostNetMessage(NetMessageMaker::MakeCursorInfoSyncMsg(
            cursor.x_, cursor.y_, cursor.hotspot_x_, cursor.hotspot_y_, cursor.width_,
            cursor.height_, cursor.visible_, cursor.data_, cursor.type_));
    }

    void RdApplication::OnIpcVideoFrame(const std::shared_ptr<CaptureVideoFrame>& msg) const {
        if (!HasConnectedPeer()) {
            return;
        }
        OnCapturedVideoFrame(*msg);
    }

    void RdApplication::OnIpcAudioFrame(const CaptureAudioFrame& frame) {
        // In-process media path: do not enqueue high-rate PCM on the application bus.
        if (!context_) {
            LOGE("OnIpcAudioFrame: context_ null, drop frame idx={} pcm={}",
                 frame.frame_index_, frame.full_data_ ? frame.full_data_->Size() : 0);
            return;
        }
        if (!frame.full_data_ || frame.full_data_->Size() <= 0) {
            LOGE("OnIpcAudioFrame: empty pcm idx={} {}Hz {}ch", frame.frame_index_, frame.samples_,
                 frame.channels_);
            return;
        }
        static thread_local uint64_t s_n = 0;
        if (++s_n == 1 || (s_n % 200) == 0) {
            LOGI("OnIpcAudioFrame: n={} idx={} {}Hz {}ch {}bit bytes={} → media", s_n,
                 frame.frame_index_, frame.samples_, frame.channels_, frame.bits_,
                 frame.full_data_->Size());
        }
        OnCapturedAudioFrame(frame);
    }

    bool RdApplication::HasConnectedPeer() const {
        if (plugin_manager_->GetTotalMediaConsumersCount()) {
            return true;
        }
        if (!settings_->IsGameHookMode()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(game_hook_clients_mutex_);
        return !game_hook_client_ids_.empty();
    }

    void RdApplication::WriteBoostUpInfoForPid(uint32_t pid) {
        PrepareGameHookBoot(pid);
    }

    void RdApplication::PrepareGameHookBoot(uint32_t pid) {
        if (!app_shared_message_) {
            LOGE("PrepareGameHookBoot: no AppSharedMessage (offsets/port)");
            return;
        }
        if (!app_shared_info_) {
            LOGE("PrepareGameHookBoot: no AppSharedInfo writer");
            return;
        }
        app_shared_message_->ipc_port_ = settings_->transmission_.listening_port_;
        app_shared_message_->self_size_ = sizeof(AppSharedMessage);
        app_shared_message_->enable_hook_events_ = 1;
        // Prefer OS process-loopback when available; otherwise (or PIXELS_FORCE_HOOK_AUDIO=1)
        // enable in-process WASAPI/XAudio2 hook.
        const bool prefer_pid = PreferProcessLoopbackCapture();
        app_shared_message_->enable_hook_audio_ = prefer_pid ? 0u : 1u;
        LOGI("PrepareGameHookBoot pid={}: prefer_pid_loopback={}, force_hook={}, "
             "os_supported={}, enable_hook_audio={}",
             pid, prefer_pid, ForceInProcessHookAudio(), IsProcessLoopbackCaptureSupported(),
             app_shared_message_->enable_hook_audio_);

        std::string buffer;
        buffer.resize(sizeof(AppSharedMessage));
        memcpy(buffer.data(), app_shared_message_.get(), sizeof(AppSharedMessage));
        if (!app_shared_info_->WriteBootConfig(pid, buffer)) {
            LOGE("PrepareGameHookBoot failed for pid {}", pid);
            return;
        }
        // Allow this pid on /ipc (net_ws). Game restarts get here again with the new
        // pid, so each live game generation is re-registered; stale games injected by
        // dead renders are never registered and get rejected on connect.
        plugin_manager_->VisitNetPlugins([=](PxNetPlugin* plugin) {
            if (!plugin || plugin->GetPluginId() != kNetWsPluginId) {
                return;
            }
            plugin->RegisterIpcPid(pid);
        });
    }

    void RdApplication::SendAudioSpectrumMessage() const {
        auto st = RdStatistics::Instance();
        auto msg = std::make_shared<Message>();
        msg->set_type(px::kRendererAudioSpectrum);
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
        auto msg = std::make_shared<pxrp::RpMessage>();
        msg->set_type(pxrp::kRpServerAudioSpectrum);
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
        px::Message m;
        m.set_type(px::kClipboardInfo);
        m.mutable_clipboard_info()->set_msg(msg);
        auto buffer = ProtoAsData(&m);
        PostNetMessage(buffer);
    }

    void RdApplication::SendConfigurationBack() {
        PxMonitorCapturePlugin* capture_plugin = nullptr;
        {
            std::lock_guard<std::mutex> lk(capture_plugin_mtx_);
            capture_plugin = capture_plugin_;
        }

        std::vector<CaptureMonitorInfo> monitors;
        std::string capturing_name;
        const auto append_synthetic_monitor = [&monitors, &capturing_name](
            const std::string& name, int width, int height) {
            capturing_name = name;
            CaptureMonitorInfo monitor;
            monitor.name_ = name;
            monitor.primary_ = true;
            monitor.attached_desktop_ = true;
            monitor.right_ = width;
            monitor.bottom_ = height;
            monitor.supported_res_.push_back(SupportedResolution {
                .width_ = static_cast<unsigned long>(width),
                .height_ = static_cast<unsigned long>(height),
            });
            monitors.push_back(std::move(monitor));
        };
        if (capture_plugin) {
            monitors = capture_plugin->GetCaptureMonitorInfo();
            capturing_name = capture_plugin->GetCapturingMonitorName();
            this->UpdateCapturingMonitorInfo();
        }
        else if (settings_->IsGameHookMode()) {
            // Inner/game-hook capture has no desktop monitor plugin. It is a
            // single application surface, so expose a synthetic monitor to the
            // standard client configuration/decode pipeline.
            int width = settings_->encoder_.encode_width_;
            int height = settings_->encoder_.encode_height_;
            if (app_manager_) {
                const auto hwnd = static_cast<HWND>(app_manager_->GetWindowHandle());
                RECT client_rect{};
                if (hwnd && IsWindow(hwnd) && GetClientRect(hwnd, &client_rect)
                    && client_rect.right > client_rect.left
                    && client_rect.bottom > client_rect.top) {
                    width = client_rect.right - client_rect.left;
                    height = client_rect.bottom - client_rect.top;
                }
            }
            append_synthetic_monitor("Application", width, height);
            LOGI("Use synthetic game-hook monitor configuration: {}x{}", width, height);
        }
        else if (settings_->IsWebViewMode()) {
            append_synthetic_monitor(
                "webview", settings_->webview_width_, settings_->webview_height_);
            LOGI("Use synthetic WebView monitor configuration: {}x{}",
                 settings_->webview_width_, settings_->webview_height_);
        }
        else {
            LOGE("SendConfigurationBack failed, working monitor capture plugin is null.");
            return;
        }
        if (monitors.empty()) {
            LOGW("Ignore this sending configuration back, 'cause there's no monitors detected.");
            return;
        }

        px::Message m;
        m.set_type(px::kServerConfiguration);
        auto config = m.mutable_config();
        // screen info
        auto monitors_info = config->mutable_monitors_info();
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
        // FT 协议版本:rustdesk 语义 = 2(旧实现已删除,主控按此门控)
        config->set_ft_protocol_version(2);
        config->set_audio_enabled(settings_->audio_enabled_);
        config->set_can_be_operated(settings_->can_be_operated_);
        config->set_virtual_display_enabled(
            settings_->virtual_display_enabled_ && !settings_->IsGameHookMode());
        config->set_virtual_display_owned_count(virtual_display_owned_count_.load());
        config->set_virtual_display_max_count(kVirtualDisplayMaximumCount);
        config->set_topology_generation(virtual_display_topology_generation_.load());
        config->set_voice_call_enabled(settings_->voice_call_enabled_);
        config->set_voice_call_protocol_version(settings_->voice_call_enabled_ ? 1 : 0);
        config->set_voice_call_requires_headset(true);
        //
        auto buffer = ProtoAsData(&m);
        PostNetMessage(buffer);
    }

    void RdApplication::RequestRestartMe() const {
        pxrp::RpMessage m;
        m.set_type(pxrp::kRpRestartServer);
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

        px::Message m;
        m.set_type(px::kChangeMonitorResolutionResult);
        auto r = m.mutable_change_monitor_resolution_result();
        r->set_monitor_name(name);
        r->set_result(ok);
        auto buffer = ProtoAsData(&m);
        PostNetMessage(buffer);
    }

    std::shared_ptr<PluginManager> RdApplication::GetPluginManager() {
        return plugin_manager_;
    }

    px::PxMonitorCapturePlugin* RdApplication::GetWorkingMonitorCapturePlugin() {
        std::lock_guard<std::mutex> lk(capture_plugin_mtx_);
        return capture_plugin_;
    }

    std::map<std::string, PxVideoEncoderPlugin*> RdApplication::GetWorkingVideoEncoderPlugins() const {
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
            LOGI("D3D11CreateDevice begin for matched adapter uid={}", adapter_uid);
            res = D3D11CreateDevice(adapter.Get(),
                                D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                nullptr, 0, D3D11_SDK_VERSION,
                                &new_device_wrapper->d3d11_device_, &featureLevel, &new_device_wrapper->d3d11_device_context_);
            LOGI("D3D11CreateDevice end for matched adapter uid={}, hr={}", adapter_uid, res);
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
            uint64_t device_adapter_uid = adapter_uid;
            if (adapter_uid == static_cast<uint64_t>(-1)) {
                ComPtr<IDXGIDevice> dxgi_device;
                ComPtr<IDXGIAdapter> device_adapter;
                DXGI_ADAPTER_DESC device_desc{};
                if (SUCCEEDED(new_device_wrapper->d3d11_device_.As(&dxgi_device))
                    && dxgi_device
                    && SUCCEEDED(dxgi_device->GetAdapter(&device_adapter))
                    && device_adapter
                    && SUCCEEDED(device_adapter->GetDesc(&device_desc))) {
                    device_adapter_uid = device_desc.AdapterLuid.LowPart;
                    LOGI("D3D11 prewarm resolved adapter: {} (uid={})",
                         StringUtil::ToUTF8(device_desc.Description), device_adapter_uid);
                } else {
                    LOGW("D3D11 prewarm could not resolve the selected adapter LUID.");
                }
            }
            LOGI("D3D11CreateDevice mDevice = {}", (void *) new_device_wrapper->d3d11_device_.Get());
            new_device_wrapper->adapter_uid_ = device_adapter_uid;
            d3d11_devices_[device_adapter_uid] = new_device_wrapper;
            d3d11_device_failure_counts_[device_adapter_uid] = 0;
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
        plugin_manager_->VisitAllPlugins([adapter_uid](PxPluginInterface* plugin) {
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
        if (auto it = d3d11_devices_.find(adapter_uid); it != d3d11_devices_.end()) {
            return it->second ? it->second->d3d11_device_ : nullptr;
        }
        // GDI/raw capture has no DXGI adapter LUID and reports UINT64_MAX.
        // GenerateD3DDevice resolves a generic hardware device to its real
        // adapter LUID before caching it, so look up that resolved default
        // instead of immediately creating another device under the sentinel.
        if (adapter_uid == static_cast<uint64_t>(-1)) {
            for (const auto& [uid, wrapper] : d3d11_devices_) {
                if (wrapper && wrapper->d3d11_device_) {
                    return wrapper->d3d11_device_;
                }
            }
        }
        return nullptr;
    }

    ComPtr<ID3D11DeviceContext> RdApplication::GetD3DContext(uint64_t adapter_uid) {
        if (auto it = d3d11_devices_.find(adapter_uid); it != d3d11_devices_.end()) {
            return it->second ? it->second->d3d11_device_context_ : nullptr;
        }
        if (adapter_uid == static_cast<uint64_t>(-1)) {
            for (const auto& [uid, wrapper] : d3d11_devices_) {
                if (wrapper && wrapper->d3d11_device_context_) {
                    return wrapper->d3d11_device_context_;
                }
            }
        }
        return nullptr;
    }

    void RdApplication::ReqCtrlAltDelete(const std::string& device_id, const std::string& stream_id) const {
        if (!service_client_ || !service_client_->IsAlive()) {
            LOGE("Service client not connected, can't ReqCtrlAltDelete");
            return;
        }
        px::ServiceMessage m;
        m.set_type(ServiceMessageType::kSrvReqCtrlAltDelete);
        m.mutable_req_ctrl_alt_delete()->set_req_device_id(device_id);
        m.mutable_req_ctrl_alt_delete()->set_req_stream_id(stream_id);
        service_client_->PostNetMessage(m.SerializeAsString());
    }

    void RdApplication::RedeemConnectionTicket(
        const std::string& ticket,
        const std::string& client_nonce,
        const std::string& instance_id,
        std::function<void(bool, const std::string&, const std::vector<std::string>&,
                           const std::string&)>&& callback) const {
        if (!service_client_ || !service_client_->IsAlive()) {
            callback(false, "SERVICE_UNAVAILABLE", {}, "");
            return;
        }
        service_client_->RedeemConnectionTicket(
            ticket, client_nonce, instance_id, std::move(callback));
    }

    void RdApplication::RequestVirtualDisplay(
        const std::string& request_id,
        int operation,
        uint32_t width,
        uint32_t height,
        uint32_t refresh_hz,
        std::function<void(const MsgVirtualDisplayServiceResult&)>&& callback) {
        if (!service_client_ || !service_client_->IsAlive()) {
            MsgVirtualDisplayServiceResult result;
            result.request_id_ = request_id;
            result.error_code_ = "SERVICE_UNAVAILABLE";
            result.error_message_ = "px_service is unavailable";
            callback(result);
            return;
        }
        service_client_->RequestVirtualDisplay(
            request_id, operation, width, height, refresh_hz, std::move(callback));
    }

    void RdApplication::UpdateVirtualDisplayStatus(const MsgVirtualDisplayServiceResult& result) {
        if (!result.accepted_) {
            LOGW("Ignore failed virtual display status: request={}, code={}",
                 result.request_id_, result.error_code_);
            return;
        }
        const auto current_generation = virtual_display_topology_generation_.load();
        const auto current_owned_count = virtual_display_owned_count_.load();
        if (result.topology_generation_ < current_generation) {
            LOGW("Ignore stale virtual display status: request={}, incoming={}, current={}",
                 result.request_id_, result.topology_generation_, current_generation);
            return;
        }
        if (result.topology_generation_ == current_generation &&
            result.owned_display_count_ == current_owned_count) {
            return;
        }
        virtual_display_owned_count_.store(result.owned_display_count_);
        virtual_display_topology_generation_.store(result.topology_generation_);
        LOGI("Apply virtual display status: request={}, owned={}, generation={}",
             result.request_id_, result.owned_display_count_, result.topology_generation_);
        SendConfigurationBack();
    }

    void RdApplication::RefreshVirtualDisplayStatus(const std::string& request_prefix) {
        if (exit_app_ || !settings_->virtual_display_enabled_ || settings_->IsGameHookMode()) {
            return;
        }
        if (virtual_display_refresh_pending_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        const auto request_sequence =
            virtual_display_request_sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
        const auto request_id = std::format(
            "{}-{}-{}-{}",
            request_prefix,
            GetCurrentProcessId(),
            GetTickCount64(),
            request_sequence);
        const auto weak_self = weak_from_this();
        RequestVirtualDisplay(
            request_id, kVirtualDisplayQuery, 1920, 1080, 60,
            [weak_self](const MsgVirtualDisplayServiceResult& result) {
                const auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                self->virtual_display_refresh_pending_.store(false, std::memory_order_release);
                // Keep the callback as a topology-rebuild-safe fallback. The
                // message-bus copy can be discarded when the state lane is
                // rebuilt; UpdateVirtualDisplayStatus is idempotent for the
                // same generation/count, so callback and broadcast may race.
                self->PostGlobalTask([weak_self, result]() {
                    if (const auto self = weak_self.lock(); self && !self->exit_app_) {
                        self->UpdateVirtualDisplayStatus(result);
                    }
                });
            });
    }

    std::pair<uint32_t, uint64_t> RdApplication::GetVirtualDisplayStatusSnapshot() const {
        return {
            virtual_display_owned_count_.load(),
            virtual_display_topology_generation_.load(),
        };
    }

    void RdApplication::OnServiceRequestedStop() {
        LOGW("Service requested stop (Console stop instance), notify clients then exit.");
        // broadcast kInstanceStopped to all RTC clients, then leave some time
        // for the message to be flushed out before exiting by ourselves
        PostNetMessage(NetMessageMaker::MakeInstanceStopped("stopped by Console"));
        const auto weak_self = weak_from_this();
        context_->PostDelayTask([weak_self]() {
            if (const auto self = weak_self.lock(); self && !self->exit_app_) {
                self->Exit();
            }
        }, 400);
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
            capture_plugin_->DisablePlugin();
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
            capture_plugin_->DisablePlugin();
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

    bool RdApplication::PostPanelMessage(std::shared_ptr<Data> msg) {
        if (ws_panel_client_ && msg) {
            return ws_panel_client_->PostNetMessage(msg);
        }
        return false;
    }

    void RdApplication::PostUserProxyMessage(std::shared_ptr<Data> msg) {
        if (!msg || !plugin_manager_) {
            return;
        }
        plugin_manager_->VisitNetPlugins([&](PxNetPlugin* plugin) {
            plugin->PostUserProxyMessage(msg);
        });
    }

    void RdApplication::HandleForceGdiEvent(bool force_gdi) {
        // WebView frames come from CEF OSR and do not have a desktop capture
        // plugin. Relay's legacy RequestControl hint must not try to switch a
        // nonexistent DDA/GDI source.
        if (settings_->IsWebViewMode()) {
            return;
        }
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
        if (exit_app_.exchange(true)) {
            return;
        }
        // Flip the guard first so asynchronous callbacks stop touching the
        // pipeline while its owners are being released.
        if (msg_listener_) {
            msg_listener_->UnListenAll();
        }
        if (state_msg_listener_) {
            state_msg_listener_->UnListenAll();
        }
        // stop the statistics reporting at first: it runs on the context task pool
        // and reads net plugins, so it must be silent before capturing stops
        if (statistics_) {
            LOGI("RdApplication shutdown: statistics");
            statistics_->Exit();
        }
        if (app_timer_) {
            LOGI("RdApplication shutdown: timers");
            app_timer_->StopTimers();
        }
        if (ws_panel_client_) {
            LOGI("RdApplication shutdown: panel client");
            ws_panel_client_->Exit();
            ws_panel_client_ = nullptr;
        }
        if (service_client_) {
            LOGI("RdApplication shutdown: service client");
            service_client_->Exit();
            service_client_.reset();
        }
        if (webview_runtime_) {
            webview_runtime_->Stop();
            webview_runtime_.reset();
        }
        // stop capturing before tearing down other components.
        // NOTE: plugins are globally loaded and share the process lifetime;
        // do NOT call ReleaseAllPlugins() here — destroying/unloading them at exit
        // races with encoder/IPC threads that still hold raw plugin pointers.
        {
            std::lock_guard<std::mutex> lk(capture_plugin_mtx_);
            if (capture_plugin_) {
                capture_plugin_->StopCapturing();
            }
            if (dda_capture_plugin_) {
                dda_capture_plugin_->StopCapturing();
            }
            if (gdi_capture_plugin_) {
                gdi_capture_plugin_->StopCapturing();
            }
        }
        if (app_shared_info_) {
            LOGI("RdApplication shutdown: shared info");
            app_shared_info_->Exit();
        }
        // Stop audio capture before teardown: otherwise the capture thread keeps
        // invoking data callbacks into components that are being destroyed.
        // The plugin object itself is NOT destroyed (it shares the process lifetime).
        if (audio_capture_plugin_) {
            audio_capture_plugin_->StopProviding();
        }
        if (audio_capture_thread_ && audio_capture_thread_->IsJoinable()) {
            audio_capture_thread_->Join();
        }
        if (app_manager_) {
            LOGI("RdApplication shutdown: app manager");
            app_manager_->Exit();
        }
        if (encoder_thread_) {
            LOGI("RdApplication shutdown: encoder");
            encoder_thread_->Exit();
        }
        if (plugin_manager_) {
            LOGI("RdApplication shutdown: plug-in event routing");
            plugin_manager_->StopRouting();
        }
        LOGI("RdApplication shutdown: owners released");

        if (main_thread_id_ != 0) {
            PostThreadMessage(main_thread_id_, WM_QUIT, 0, 0);
        }
        if (rdApp.get() == this) {
            rdApp.reset();
        }
    }

    // ------------------------------------------------------ //
    // Windows
    WinApplication::WinApplication(const AppParams& args)
        : RdApplication(args) {

    }

    // The base destructor is invoked automatically after this destructor.
    // Calling it explicitly here destroyed the RdApplication subobject twice
    // and could terminate px_render with STATUS_HEAP_CORRUPTION on early exit.
    WinApplication::~WinApplication() {
        Exit();
    }

    int WinApplication::Run() {
        // WebView never injects the graphics hook and must not depend on the
        // auxiliary DXGI address probe executable being deployed.
        if (!settings_->IsWebViewMode()) {
            LoadDxAddress();
        }
        return RdApplication::Run();
    }

    static std::function<BOOL(DWORD)> s_ctrl_handler;
    static BOOL ConsoleHandler(DWORD signal) {
        if (s_ctrl_handler) {
            return s_ctrl_handler(signal);
        }
        return FALSE;
    }

    void WinApplication::Exit() {
        SetConsoleCtrlHandler(ConsoleHandler, FALSE);
        s_ctrl_handler = {};
        RdApplication::Exit();
    }

    void WinApplication::CaptureControlC() {
        const auto weak_self = weak_from_this();
        s_ctrl_handler = [weak_self](DWORD signal) -> BOOL {
            if (signal == CTRL_C_EVENT) {
                const auto self = weak_self.lock();
                if (!self) {
                    return FALSE;
                }
                std::cout << "CTRL+C detected, localVar value is " << "\n";
                self->Exit();
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
