//
// Created by RGAA on 2023-12-27.
//
#include "px_client/ct_base_workspace.h"
#include <span>
#include <QHBoxLayout>
#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <QUuid>
#include <d3d10.h>
#include <dwmapi.h>
#include "thunder_sdk.h"
#include "px_client/ct_client_context.h"
#include "px_common/data.h"
#include "px_common/log.h"
#include "px_common/message_notifier.h"
#include "px_client/ct_audio_player.h"
#include "ui/float_controller.h"
#include "ui/float_controller_panel.h"
#include "px_client/ct_app_message.h"
#include "px_client/ct_settings.h"
#include "ui/float_notification_handle.h"
#include "ui/notification_panel.h"
#include "px_qt_widget/sized_msg_box.h"
#include "ui/ct_statistics_panel.h"
#include "ui/no_margin_layout.h"
#include "px_client_sdk/sdk_messages.h"
#include "px_common/process_util.h"
#include "ui/float_button_state_indicator.h"
#include "ct_main_progress.h"
#include "px_qt_widget/widgetframe/mainwindow_wrapper.h"
#include "px_dialog.h"
#include "px_label.h"
#include "px_render_view.h"
#include "ct_const_def.h"
#include "px_common/file.h"
#include "px_common/string_util.h"
#include "ui/retry_conn_dialog.h"
#include "network/ct_panel_client.h"
#include "px_common/md5.h"
#include "px_common/time_util.h"
#include "px_client/modules/client_module_manager.h"
#include "px_client/modules/file_transfer/file_transfer_module.h"
#include "px_client/modules/media_recording/media_recording_module.h"
#include "ct_virtual_display_protocol.h"
#include "ct_voice_call_protocol.h"
#include "px_voice_call/voice_audio_endpoint.h"
#include "px_qt_widget/notify/notifymanager.h"
#include "px_relay_client/relay_api.h"
#include "px_message/proto_converter.h"
#include "px_message/proto_message_maker.h"
#include "px_common/win32/d3d11_wrapper.h"
#include "front_render/opengl/ct_opengl_video_widget.h"
#include "front_render/vulkan/pl_vulkan.h"
#include "hw_info/hw_info.h"
#include "hw_info/hw_info_parser.h"
#include "hw_info/hw_info_widget.h"
#include "network/ct_console_client.h"
#include "skin/skin_loader.h"
#include "skin/interface/skin_interface.h"
#include "ct_game_overlay.h"

namespace px
{

    std::shared_ptr<BaseWorkspace> gWorkspace;

    BaseWorkspace::BaseWorkspace(const std::shared_ptr<ClientContext>& ctx, const std::shared_ptr<ThunderSdkParams>& params, QWidget* parent) : QMainWindow(parent) {
        this->context_ = ctx;
        this->context_->InitNotifyManager(this);
        this->settings_ = Settings::Instance();
        this->params_ = params;
        cursor_ = QCursor(Qt::ArrowCursor);
        retry_conn_dialog_ = std::make_shared<RetryConnDialog>(tcTr("id_warning"));
        const QPointer<BaseWorkspace> raise_target(this);
        QTimer::singleShot(1000, [raise_target]() {
            const auto self = raise_target;
            if (!self) {
                return;
            }
            self->raise();
            self->activateWindow();
        });
        if (!Settings::Instance()->file_transfer_only_) {
            pl_vulkan_ = PlVulkan::Make();
        }

        if (!Settings::Instance()->file_transfer_only_) {
        overlay_widget_ = new OverlayWidget(this);
        overlay_widget_->resize(this->size());
        overlay_widget_->SetOpacity(0.3);
        overlay_widget_->SetWatermarkCount(0);
        overlay_widget_->hide();
        const QPointer<BaseWorkspace> overlay_owner(this);
        QTimer::singleShot(1000, this, [overlay_owner]() {
            const auto self = overlay_owner;
            if (self && self->overlay_widget_) {
                self->UpdateOverlayWidgetPos();
                if (self->isHidden()) {
                    self->overlay_widget_->hide();
                }
                else {
                    self->overlay_widget_->show();
                }
            }
        });
        }

        //SetWindowDisplayAffinity((HWND)winId(), WDA_EXCLUDEFROMCAPTURE);
    }

    void BaseWorkspace::Init() {
        // shared_from_this() below requires this object to be created by Workspace::Make().
        gWorkspace = shared_from_this();
        InitModuleManager();

        // skin
        skin_ = SkinLoader::LoadSkin(settings_->skin_name_);

        auto beg = TimeUtil::GetCurrentTimestamp();

        InitTheme();

#ifdef WIN32
        if (!settings_->file_transfer_only_) {
            if (!settings_->force_software_) {
                gen_d3d11_device_ = GenerateD3DDevice();
            }
            if (gen_d3d11_device_) {
                for (const auto &[adapter_uid, wrapper]: d3d11_devices_) {
                    // TODO: find the primary or using d3d11 device
                    this->params_->d3d11_wrapper_ = wrapper;
                    LOGI("Using the D3D11Device, ID: {}", wrapper->adapter_uid_);
                    break;
                }
            }
            else {
                LOGW("!!Can't use D3D11 to render!!");
            }
        }
#endif

        sdk_ = ThunderSdk::Make(this->context_->GetMessageNotifier());
        sdk_->Init(this->params_, nullptr, DecoderRenderType::kFFmpegI420);

        // A Console ticket launch is already authenticated and lifecycle-managed
        // by Panel. The legacy device WebSocket cannot authenticate a guest or
        // user-session ticket and would otherwise retry a rejected handshake
        // every second for the lifetime of the application session.
        if (!settings_->file_transfer_only_
            && settings_->connection_ticket_.empty()
            && !settings_->device_id_.empty()
            && !settings_->console_host_.empty()
            && settings_->console_port_ > 0
            && !settings_->appkey_.empty()) {
            LOGI("Will start console client, device_id: {}, remote device_id: {}", settings_->device_id_, settings_->remote_device_id_);
            console_client_ = CtConsoleClient::Make(sdk_,
                                            context_,
                                            settings_->console_host_,
                                            settings_->console_port_,
                                            settings_->device_id_,
                                            settings_->remote_device_id_,
                                            settings_->host_,
                                            settings_->appkey_,
                                            settings_->console_ssl_);
            console_client_->Start();
        }

        // init game views
        if (!settings_->file_transfer_only_) {
            InitRenderViews(this->params_);

        // vulkan 
        if (this->params_->support_vulkan_) {
            this->params_->vulkan_hw_device_ctx_ = pl_vulkan_->GetHwDeviceCtx();
        }

            InitSampleWidget();
        }
        else {
            // Several long-lived base listeners own lightweight UI helpers
            // (progress/debug/indicator). Keep those helpers alive in the
            // hidden plugin host; they do not create capture/decoder/audio
            // resources, and avoid event-path null dereferences.
            InitSampleWidget();
        }

        // message listener
        InitListener();
        // connect to px_panel
        InitPanelClient();

        auto end = TimeUtil::GetCurrentTimestamp();
        LOGI("Init .3 used: {}ms", (end - beg));
    }

    void BaseWorkspace::InitModuleManager() {
        module_manager_ = ClientModuleManager::Make(shared_from_this());
        context_->SetModuleManager(module_manager_);
        module_manager_->Start();
    }

    void BaseWorkspace::InitSampleWidget() {
        main_progress_ = new MainProgress(sdk_, context_, this);
        main_progress_->show();

        // button indicator
        int shadow_color = 0x999999;
        btn_indicator_ = new FloatButtonStateIndicator(this->context_, this);
        btn_indicator_->hide();
        WidgetHelper::AddShadow(btn_indicator_, shadow_color);

        // debug panel
        st_panel_ = new CtStatisticsPanel(context_, nullptr);
        st_panel_->UpdateClientRenderTypeName(render_type_name_);
        st_panel_->resize(def_window_size_);
        st_panel_->hide();

        hw_info_widget_ = new HWInfoWidget(true, nullptr);
        hw_info_widget_->setWindowTitle(tcTr("id_remote_hw"));
        hw_info_widget_->resize(QSize(1260, 880));
        hw_info_widget_->hide();
    }

    void BaseWorkspace::InitTheme() {
        WidgetHelper::SetTitleBarColor(this, this->params_->titlebar_color_);

        if (this->params_->stream_name_.empty()) {
            origin_title_name_ = tcTr("id_gr_client");
        }
        else {
            origin_title_name_ = tcTr("id_gr_client") + "[" + this->params_->stream_name_.c_str() + "]";
        }
        setWindowTitle(origin_title_name_);
        auto notifier = this->context_->GetMessageNotifier();

        setAcceptDrops(true);
        QString app_dir = qApp->applicationDirPath();
        QString style_dir = app_dir + "/resources/";
        theme_ = new acss::QtAdvancedStylesheet(this);
        theme_->setStylesDirPath(style_dir);
        theme_->setOutputDirPath(app_dir + "/output");
        theme_->setCurrentStyle("qt_material");
        theme_->setCurrentTheme("light_blue");
        theme_->updateStylesheet();
        setWindowIcon(theme_->styleIcon());
        qApp->setStyleSheet(theme_->styleSheet());
    }

    void BaseWorkspace::InitListener() {
        msg_listener_ = context_->ObtainUIMessageListener();
        RegisterSdkMsgCallbacks();
        sdk_->Start();
        RegisterBaseListeners();
        RegisterControllerPanelListeners();
    }

    void BaseWorkspace::InitPanelClient() {
        panel_client_ = std::make_shared<CtPanelClient>(context_);
        panel_client_->Start();
    }

    void BaseWorkspace::RegisterBaseListeners() {
        const auto weak_self = weak_from_this();

        msg_listener_->Listen<MsgClientExitApp>([weak_self](const MsgClientExitApp&) {
            if (const auto self = weak_self.lock()) {
                self->context_->PostDelayUITask([weak_self]() {
                    if (const auto task_self = weak_self.lock()) {
                        task_self->ExitClientWithDialog();
                    }
                }, 10);
            }
        });

        msg_listener_->Listen<MsgClientClipboard>([weak_self](const MsgClientClipboard& msg) {
            if (const auto self = weak_self.lock()) {
                self->SendClipboardMessage(msg);
            }
        });

        msg_listener_->Listen<MsgClientRtcIceRestart>([weak_self](const MsgClientRtcIceRestart& msg) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            if (self->settings_->network_type_ != ClientNetworkType::kWebRtc) {
                LOGW("Ignore RTC ICE restart for non-standard RTC session");
                return;
            }
            if (!self->sdk_->RestartRtcIce(msg.ice_config_json_, msg.connection_ticket_,
                                           msg.client_nonce_, msg.instance_id_, msg.revision_)) {
                LOGE("RTC ICE restart request failed, revision={}", msg.revision_);
                return;
            }
            LOGI("RTC ICE restart started, revision={}", msg.revision_);
        });

        msg_listener_->Listen<MsgClientSwitchMonitor>([weak_self](const MsgClientSwitchMonitor& msg) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->SendSwitchMonitorMessage(msg.name_);
            self->SendUpdateDesktopMessage();
            self->context_->PostDelayTask([weak_self]() {
                if (const auto task_self = weak_self.lock()) {
                    task_self->SendUpdateDesktopMessage();
                }
            }, 200);
        });

        msg_listener_->Listen<MsgClientSwitchWorkMode>([](const MsgClientSwitchWorkMode&) {
        });

        msg_listener_->Listen<MsgClientSwitchScaleMode>([weak_self](const MsgClientSwitchScaleMode& msg) {
            if (const auto self = weak_self.lock()) {
                self->SwitchScaleMode(msg.mode_);
            }
        });

        msg_listener_->Listen<MsgClientSwitchFullColor>([weak_self](const MsgClientSwitchFullColor& msg) {
            if (const auto self = weak_self.lock()) {
                self->SendSwitchFullColorMessage(msg.enable_);
            }
        });

        // step 1t
        msg_listener_->Listen<SdkMsgNetworkConnected>([weak_self](const SdkMsgNetworkConnected&) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->SendUpdateDesktopMessage();
            self->main_progress_->ResetProgress();
            self->main_progress_->StepForward();
            LOGI("Step: MsgNetworkConnected, at: {}", self->main_progress_->GetCurrentProgress());

            self->DismissConnectingDialog();

            // The file manager can be shown before networking is ready. Its
            // first ReadDir may then be dropped, so let the FT plugin refresh
            // once the transport is established and after reconnects. The
            // plugin does nothing while its window is hidden.
            self->context_->PostUITask([weak_self]() {
                const auto task_self = weak_self.lock();
                if (!task_self) {
                    return;
                }
                if (task_self->module_manager_) {
                    LOGI("File-transfer transport connected; notify module");
                    task_self->module_manager_->OnTransportConnected();
                }
            });
        });

        // reconnection
        // relay mode now, already connected
        msg_listener_->Listen<SdkMsgReconnect>([weak_self](const SdkMsgReconnect&) {
            if (const auto self = weak_self.lock()) {
                self->main_progress_->ResetProgress();
                self->main_progress_->StepForward();
                LOGI("Step: SdkMsgReconnect, at: {}", self->main_progress_->GetCurrentProgress());
            }
        });

        msg_listener_->Listen<SdkMsgNetworkDisConnected>([weak_self](const SdkMsgNetworkDisConnected&) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->StopVoiceCall(false, "disconnect");
            if (self->remote_force_closed_) {
                return;
            }
            self->context_->PostUITask([weak_self]() {
                const auto task_self = weak_self.lock();
                if (!task_self || !task_self->retry_conn_dialog_->isHidden()) {
                    return;
                }
                WidgetHelper::SetTitleBarColor(task_self->retry_conn_dialog_.get());
                if (task_self->retry_conn_dialog_->Exec() == -1) {
                    ProcessUtil::KillProcess(QApplication::applicationPid());
                }
            });
        });

        // webrtc local: render rejected the device password(HTTP 403), tell the user and quit
        msg_listener_->Listen<SdkMsgRtcLocalAuthFailed>([weak_self](const SdkMsgRtcLocalAuthFailed&) {
            if (const auto self = weak_self.lock()) {
                self->context_->PostUITask([weak_self]() {
                    if (!weak_self.lock()) {
                        return;
                    }
                    auto box = SizedMessageBox::MakeErrorOkBox(
                        tcTr("id_warning"), tcTr("id_rtc_local_pwd_error"));
                    box->exec();
                    ProcessUtil::KillProcess(QApplication::applicationPid());
                });
            }
        });

        // A WebSocket transport can finish its HTTP upgrade before Render's
        // logical-session admission completes. Terminal business rejections
        // are delivered explicitly so they do not become a one-second
        // transport reconnect loop.
        msg_listener_->Listen<SdkMsgWsConnectionRejected>(
            [weak_self](const SdkMsgWsConnectionRejected& event) {
                const auto self = weak_self.lock();
                if (!self || self->remote_force_closed_) {
                    return;
                }
                self->remote_force_closed_ = true;
                const auto message_id = [rejection = event.rejection_]() -> std::string {
                    if (rejection == WsControlRejection::kOccupied) {
                        return "id_connection_occupied";
                    }
                    if (rejection == WsControlRejection::kAuthorization) {
                        return "id_connection_authorization_rejected";
                    }
                    return "id_connection_policy_rejected";
                }();
                self->context_->PostUITask([weak_self, message_id]() {
                    if (!weak_self.lock()) {
                        return;
                    }
                    auto box = SizedMessageBox::MakeErrorOkBox(
                        tcTr("id_warning"), tcTr(QString::fromStdString(message_id)));
                    box->exec();
                    ProcessUtil::KillProcess(QApplication::applicationPid());
                });
            });

        // render 主动断开本连接:被其它客户端接管。通知在通道关闭前到达,
        // 先置 remote_force_closed_ 抑制随后的断线重连弹窗
        msg_listener_->Listen<SdkMsgConnectionTakenOver>([weak_self](const SdkMsgConnectionTakenOver&) {
            const auto self = weak_self.lock();
            if (!self || self->remote_force_closed_) {
                return;
            }
            self->remote_force_closed_ = true;
            self->context_->PostUITask([weak_self]() {
                if (!weak_self.lock()) {
                    return;
                }
                auto box = SizedMessageBox::MakeErrorOkBox(tcTr("id_warning"), tcTr("id_connection_taken_over"));
                box->exec();
                ProcessUtil::KillProcess(QApplication::applicationPid());
            });
        });

        // step 2
        msg_listener_->Listen<SdkMsgFirstConfigInfoCallback>([weak_self](const SdkMsgFirstConfigInfoCallback& msg) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->main_progress_->StepForward();
            LOGI("Step: MsgFirstConfigInfoCallback, at: {}", self->main_progress_->GetCurrentProgress());
            if (msg.msg_ && msg.msg_->type() == px::kServerConfiguration) {
                const auto config = msg.msg_->config();

            }
        });

        // step 3
        msg_listener_->Listen<SdkMsgFirstVideoFrameDecoded>([weak_self](const SdkMsgFirstVideoFrameDecoded&) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->main_progress_->CompleteProgress();
            LOGI("Step: MsgFirstVideoFrameDecoded, at: {}", self->main_progress_->GetCurrentProgress());

            // process watermark
            // context_->PostUITask([=, this]() {
            //     if (settings_->force_direct_) {
            //         overlay_widget_->SetWatermarkText("Force Direct");
            //     }
            //     if (settings_->show_watermark_) {
            //         overlay_widget_->SetWatermarkText("Unlicensed Stream");
            //     }
            //     if (settings_->show_watermark_ || settings_->force_direct_) {
            //         overlay_widget_->SetWatermarkCount(15);
            //     }
            //     else {
            //         overlay_widget_->SetWatermarkCount(0);
            //     }
            // });

            self->DismissConnectingDialog();
        });

        msg_listener_->Listen<MsgClientChangeMonitorResolution>([weak_self](const MsgClientChangeMonitorResolution& msg) {
            if (const auto self = weak_self.lock()) {
                self->SendChangeMonitorResolutionMessage(msg);
            }
        });

        msg_listener_->Listen<MsgClientVirtualDisplayRequest>([weak_self](const MsgClientVirtualDisplayRequest& msg) {
            if (const auto self = weak_self.lock()) {
                self->SendVirtualDisplayRequest(msg);
            }
        });

        msg_listener_->Listen<MsgClientVoiceCallCommand>([weak_self](const MsgClientVoiceCallCommand& msg) {
            if (const auto self = weak_self.lock()) {
                self->SendVoiceCallCommand(msg);
            }
        });

        msg_listener_->Listen<MsgClientCtrlAltDelete>([weak_self](const MsgClientCtrlAltDelete&) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            if (auto buffer = ProtoMessageMaker::MakeCtrlAltDelete(
                    self->settings_->device_id_, self->settings_->stream_id_); buffer) {
                self->sdk_->PostMediaMessage(buffer);
            }
        });

        msg_listener_->Listen<MsgClientHardUpdateDesktop>([weak_self](const MsgClientHardUpdateDesktop&) {
            if (const auto self = weak_self.lock()) {
                self->SendHardUpdateDesktopMessage();
            }
        });

        msg_listener_->Listen<MsgExitControlledEndExe>([weak_self](const MsgExitControlledEndExe&) {
            if (const auto self = weak_self.lock()) {
                self->SendExitControlledEndMessage();
            }
        });

        msg_listener_->Listen<MsgSetHWInfoPanelVisibility>([weak_self](const MsgSetHWInfoPanelVisibility&) {
            if (const auto self = weak_self.lock()) {
                self->context_->PostUITask([weak_self]() {
                    if (const auto task_self = weak_self.lock()) {
                        task_self->hw_info_widget_->show();
                    }
                });
            }
        });

        msg_listener_->Listen<MsgHWInfo>([weak_self](const MsgHWInfo& msg) {
            if (!msg.info_) {
                return;
            }
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->context_->PostUITask([weak_self, msg]() {
                const auto task_self = weak_self.lock();
                if (!task_self) {
                    return;
                }
                task_self->hw_info_widget_->OnSysInfoCallback(msg.info_);
                if (!msg.info_->networks_.empty()) {
                    for (const auto& nt : msg.info_->networks_) {
                        auto name = StringUtil::ToLowerCpy(nt.name_);
                        if (name.find("wsl") != std::string::npos
                            || name.find("wmware") != std::string::npos
                            || name.find("virtualbox") != std::string::npos) {
                            continue;
                        }
                        task_self->settings_->max_transmit_speed_ = nt.max_transmit_speed_;
                        task_self->settings_->max_receive_speed_ = nt.max_receive_speed_;
                    }
                }
            });
        });

        msg_listener_->Listen<SdkMsgVideoDecodeInit>([weak_self](const SdkMsgVideoDecodeInit& msg) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->context_->PostUITask([weak_self, msg]() {
                const auto task_self = weak_self.lock();
                if (!task_self) {
                    return;
                }
                bool notify_user = false;
                QString video_info;
                if (msg.hard_ware_) {
                    return;
                }
                if (msg.width_ > 1920 || msg.format_ == EImageFormat::kI444) {
                    video_info = QString::number(msg.width_) + "x" + QString::number(msg.height_);
                    if (msg.format_ == EImageFormat::kI444) {
                        video_info = video_info + " YUV444";
                    }
                    notify_user = true;
                }
                if (!notify_user) {
                    return;
                }
                video_info = " (" + video_info + ") ";
                task_self->context_->NotifyAppWarningMessage(
                    tcTr("id_warning"), tcTr("id_cpu_decode_warning") + video_info);
            });
        });
    }

    BaseWorkspace::~BaseWorkspace() {
        if (msg_listener_) {
            msg_listener_->UnListenAll();
        }
        media_recording_module_.reset();
        if (module_manager_) {
            module_manager_->Stop();
            module_manager_.reset();
        }
    }

    void BaseWorkspace::RegisterSdkMsgCallbacks() {
        const auto weak_self = weak_from_this();

        // save pcm file , use ffplay.exe -ar 48000 -ac 2 -f s16le -i .\audio_48000_2.pcm
        sdk_->SetOnAudioFrameDecodedCallback(
            [weak_self](std::shared_ptr<Data> data, int samples, int channels, int bits) {
            const auto self = weak_self.lock();
            if (!self || !self->settings_->IsAudioEnabled() || self->remote_force_closed_) {
                return;
            }
            if (!self->audio_player_) {
                LOGI("Init audio player, freq: {}, channels: {}, bits: {}", samples, channels, bits);
                self->audio_player_ = std::make_shared<AudioPlayer>();
                self->context_->PostUITask([weak_self, samples, channels]() {
                    if (const auto task_self = weak_self.lock(); task_self && task_self->audio_player_) {
                        task_self->audio_player_->Init(samples, channels);
                    }
                });
                return;
            }
            self->audio_player_->Write(data);
        });

        sdk_->SetOnAudioSpectrumCallback([](std::shared_ptr<px::Message>) {
        });

        sdk_->SetOnCursorInfoCallback([weak_self](std::shared_ptr<px::Message> msg) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->context_->PostUITask([weak_self, msg = std::move(msg)]() {
                const auto task_self = weak_self.lock();
                if (!task_self || !msg) {
                    return;
                }
                const auto& cursor_info = msg->cursor_info_sync();
                const std::string bitmap_data = cursor_info.bitmap();
                if (bitmap_data.empty() || task_self->last_cursor_bitmap_data_ == bitmap_data) {
                    return;
                }
                task_self->cursor_bitmap_data_ = bitmap_data;
                task_self->last_cursor_bitmap_data_ = bitmap_data;
                const QImage image(
                    reinterpret_cast<const uchar*>(task_self->cursor_bitmap_data_.data()),
                    cursor_info.width(), cursor_info.height(), QImage::Format_RGBA8888);
                const QPixmap pixmap = QPixmap::fromImage(image);
                QCursor cursor(pixmap, cursor_info.hotspot_x(), cursor_info.hotspot_y());
                task_self->cursor_ = cursor;
                task_self->UpdateLocalCursor();
            });
        });

        sdk_->SetOnHeartBeatCallback([weak_self](std::shared_ptr<px::Message> msg) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->context_->PostUITask([weak_self, msg = std::move(msg)]() {
                const auto task_self = weak_self.lock();
                if (!task_self) {
                    return;
                }
                if (task_self->st_panel_) {
                    task_self->st_panel_->UpdateOnHeartBeat(msg);
                }
                if (task_self->btn_indicator_ && task_self->settings_->develop_mode_) {
                    task_self->btn_indicator_->UpdateOnHeartBeat(msg);
                }
            });
        });

        sdk_->SetOnClipboardCallback([](std::shared_ptr<px::Message>) {
            // See: RawMessageCallback
        });

        sdk_->SetOnServerConfigurationCallback([weak_self](std::shared_ptr<px::Message> in_msg) {
            const auto self = weak_self.lock();
            if (!self || !in_msg) {
                return;
            }
            self->monitor_index_map_name_.clear();
            const auto& config = in_msg->config();

            MsgClientCaptureMonitor msg;
            msg.capturing_monitor_name_ = config.capturing_monitor_name();
            LOGI("capturing monitor name: {}", msg.capturing_monitor_name_);
            int monitor_index = 0;
            for (const auto& item : config.monitors_info()) {
                const std::string& monitor_name = item.name();
                LOGI("monitor name: {}, width: {}, height: {}", item.name(), item.current_width(), item.current_height());
                self->monitor_index_map_name_[monitor_index] = monitor_name;
                std::vector<MsgClientCaptureMonitor::Resolution> resolutions;
                for (auto& res : item.resolutions()) {
                    resolutions.push_back(MsgClientCaptureMonitor::Resolution {
                        .width_ = res.width(),
                        .height_ = res.height(),
                    });
                }
                msg.monitors_.push_back(MsgClientCaptureMonitor::CaptureMonitor {
                    .name_ = item.name(),
                    .resolutions_ = resolutions,
                    //当前显示器分辨率
                    .current_width_ = item.current_width(),
                    .current_height_ = item.current_height(),
                });
                ++monitor_index;
            }
            LOGI("capturing monitors count: {}", monitor_index);

            //
            self->settings_->is_render_audio_capture_enabled_ = config.audio_enabled();
            self->settings_->is_render_be_operated_by_mk_ = config.can_be_operated();
            self->settings_->render_ft_protocol_version_ = config.ft_protocol_version();
            self->settings_->render_virtual_display_enabled_ = config.virtual_display_enabled();
            self->settings_->render_virtual_display_owned_count_ = config.virtual_display_owned_count();
            self->settings_->render_virtual_display_max_count_ = config.virtual_display_max_count();
            self->settings_->render_virtual_display_topology_generation_ = config.topology_generation();
            self->settings_->render_voice_call_enabled_ = config.voice_call_enabled();
            self->settings_->render_voice_call_protocol_version_ = config.voice_call_protocol_version();
            self->settings_->render_voice_call_requires_headset_ = config.voice_call_requires_headset();

            self->context_->SendAppMessage(MsgClientVirtualDisplayStatus {
                .enabled_ = self->settings_->render_virtual_display_enabled_,
                .owned_display_count_ = self->settings_->render_virtual_display_owned_count_,
                .max_display_count_ = self->settings_->render_virtual_display_max_count_,
                .topology_generation_ = self->settings_->render_virtual_display_topology_generation_,
            });
            self->NotifyVoiceCallStatus();

            self->context_->SendAppMessage(msg);

            int fps = config.fps();
            self->settings_->SetFps(fps);
            LOGI("capturing fps: {}", fps);
            self->context_->SendAppMessage(MsgClientFloatControllerPanelUpdate{
                .update_type_ = MsgClientFloatControllerPanelUpdate::EUpdate::kFps
            });

            int monitors_count = config.monitors_info().size();
            const auto monitor_name = config.capturing_monitor_name();
            self->context_->PostUITask([weak_self, monitor_name, monitors_count]() {
                if (const auto task_self = weak_self.lock()) {
                    task_self->OnGetCaptureMonitorName(monitor_name);
                    task_self->OnGetCaptureMonitorsCount(monitors_count);
                }
            });
        });

        sdk_->SetOnMonitorSwitchedCallback([weak_self](std::shared_ptr<px::Message> msg) {
            const auto self = weak_self.lock();
            if (!self || !msg) {
                return;
            }
            const auto& ms = msg->monitor_switched();
            self->context_->SendAppMessage(MsgClientMonitorSwitched {
                .name_ = ms.name(),
                .index_ = ms.index()
            });
        });

        sdk_->SetOnRawMessageCallback([weak_self](std::shared_ptr<px::Message> msg) {
            const auto self = weak_self.lock();
            if (!self || self->remote_force_closed_) {
                return;
            }
            if (self->module_manager_) {
                self->module_manager_->HandleMessage(msg);
            }

            // parse it
            self->ProcessNetworkMessage(msg);
        });

        sdk_->SetOnVideoFrameDecodeThreadDiscardedCallback([weak_self]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            bool need_handle = true;
            auto cur_time = TimeUtil::GetCurrentTimestamp();
            if (cur_time - self->last_reduce_fps_time_ < 3000) {
                need_handle = false;
            }
            self->last_reduce_fps_time_ = cur_time;
            if (!need_handle) {
                return;
            }
            if (!self->settings_) {
                return;
            }
            int cur_fps = self->settings_->GetFps();
            bool find = false;
            int index = 0;
            for (auto fps : self->fps_array_) {
                if (cur_fps == fps) {
                    find = true;
                    break;
                }
                ++index;
            }
            if (!find || index == 0) {
                return;
            }
            int new_fps = self->fps_array_[index - 1];
            LOGI("new fps is {}", new_fps);
            self->settings_->SetFps(new_fps);
            self->context_->SendAppMessage(MsgClientModifyFps{
                .fps_ = new_fps,
            });
            self->context_->SendAppMessage(MsgClientFloatControllerPanelUpdate{
                .update_type_ = MsgClientFloatControllerPanelUpdate::EUpdate::kFps
            });
            self->context_->PostUITask([weak_self, cur_fps, new_fps]() {
                if (const auto task_self = weak_self.lock()) {
                    task_self->context_->NotifyAppWarningMessage(
                        tcTr("id_warning"), tcTr("id_auto_reduce_fps_warning") + QString(" (")
                        + QString::number(cur_fps) + " => " + QString::number(new_fps) + QString(" )"));
                }
            });
        });

        media_recording_module_ = module_manager_->GetMediaRecordingModule();
        if (!media_recording_module_ && !settings_->file_transfer_only_) {
            LOGE("Client media-recording module is unavailable");
        }
        
        msg_listener_->Listen<SdkMsgChangeMonitorResolutionResult>(
            [weak_self](const SdkMsgChangeMonitorResolutionResult& msg) {
            if (const auto self = weak_self.lock()) {
                self->context_->PostUITask([weak_self, msg]() {
                const auto task_self = weak_self.lock();
                if (!task_self) {
                    return;
                }
                // to trigger re-layout
                if (msg.result) {
                    task_self->move(task_self->pos().x() + 1, task_self->pos().y());

                    TcDialog dialog(tcTr("id_tips"), tcTr("id_change_resolution_success"), task_self.get());
                    dialog.exec();

                } else {
                    TcDialog dialog(tcTr("id_tips"), tcTr("id_change_resolution_failed"), task_self.get());
                    dialog.exec();
                }
                });
            }
        });

        msg_listener_->Listen<SdkMsgTimer1000>([weak_self](const SdkMsgTimer1000&) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->force_update_cursor_ = true;

            const ClientModuleSettings module_settings {
                .clipboard_enabled_ = self->settings_->clipboard_on_,
                .device_id_ = self->settings_->device_id_.empty()
                    ? self->settings_->my_host_ : self->settings_->device_id_,
                .stream_id_ = self->settings_->stream_id_,
                .language_ = static_cast<int>(self->settings_->language_),
                .stream_name_ = self->settings_->stream_name_,
                .display_name_ = self->settings_->display_name_,
                .display_remote_name_ = self->settings_->display_remote_name_,
                .max_transmit_speed_ = self->settings_->max_transmit_speed_,
                .max_receive_speed_ = self->settings_->max_receive_speed_,
            };
            if (self->module_manager_) {
                self->module_manager_->UpdateSettings(module_settings);
            }
        });

        msg_listener_->Listen<MsgClientFullscreen>([weak_self](const MsgClientFullscreen&) {
            if (const auto self = weak_self.lock()) {
                self->context_->PostUITask([weak_self]() {
                    if (const auto task_self = weak_self.lock()) {
                        task_self->full_screen_ = true;
                        task_self->UpdateRenderViewsStatus(false);
                    }
                });
            }
        });

        msg_listener_->Listen<MsgClientExitFullscreen>([weak_self](const MsgClientExitFullscreen&) {
            if (const auto self = weak_self.lock()) {
                self->context_->PostUITask([weak_self]() {
                    if (const auto task_self = weak_self.lock()) {
                        task_self->full_screen_ = false;
                        task_self->UpdateRenderViewsStatus(false);
                    }
                });
            }
        });

        msg_listener_->Listen<MsgClientMediaRecord>([weak_self](const MsgClientMediaRecord&) {
            const auto self = weak_self.lock();
            if (!self || !self->sdk_ || !self->media_recording_module_) {
                return;
            }
            px::Message m;
            m.set_device_id(self->settings_->device_id_);
            m.set_stream_id(self->settings_->stream_id_);
            bool res = self->context_->GetRecording();
            if (res) {
                LOGI("StartRecord");
                m.set_type(px::kStartMediaRecordClientSide);
                self->media_recording_module_->StartRecording();
            }
            else {
                LOGI("EndRecord");
                m.set_type(px::kStopMediaRecordClientSide);
                self->media_recording_module_->StopRecording();
            }
            if (auto buffer = px::ProtoAsData(&m); buffer) {
                self->sdk_->PostMediaMessage(buffer);
            }
        });

        msg_listener_->Listen<MsgClientModifyFps>([weak_self](const MsgClientModifyFps&) {
            if (const auto self = weak_self.lock()) {
                self->context_->PostUITask([weak_self]() {
                    if (const auto task_self = weak_self.lock()) {
                        task_self->SendModifyFpsMessage();
                    }
                });
            }
        });

        msg_listener_->Listen<MsgClientMouseEnterView>([weak_self](const MsgClientMouseEnterView&) {
            if (const auto self = weak_self.lock()) {
                self->context_->PostUITask([weak_self]() {
                    if (const auto task_self = weak_self.lock()) {
                        task_self->UpdateLocalCursor();
                    }
                });
            }
        });

        msg_listener_->Listen<MsgClientMouseLeaveView>([weak_self](const MsgClientMouseLeaveView&) {
            if (const auto self = weak_self.lock()) {
                self->context_->PostUITask([weak_self]() {
                    if (const auto task_self = weak_self.lock()) {
                        task_self->UpdateLocalCursor();
                    }
                });
            }
        });

        msg_listener_->Listen<MsgClientFocusOutEvent>([weak_self](const MsgClientFocusOutEvent&) {
            const auto self = weak_self.lock();
            if (!self || !self->sdk_ || self->remote_force_closed_) {
                return;
            }
            px::Message m;
            m.set_type(px::kFocusOutEvent);
            m.set_device_id(self->settings_->device_id_);
            m.set_stream_id(self->settings_->stream_id_);
            if (auto buffer = px::ProtoAsData(&m); buffer) {
                self->sdk_->PostMediaMessage(buffer);
            }
        });

        // relay error callback
        msg_listener_->Listen<SdkMsgRelayError>([weak_self](const SdkMsgRelayError&) {
            const auto self = weak_self.lock();
            if (!self || self->remote_force_closed_) {
                return;
            }
            //TODO: record it in event center
            //context_->PostUITask([=, this]() {
            //    TcDialog dialog(tcTr("id_error"), msg.msg_.c_str());
            //    dialog.exec();
            //});
        });

        // remote device offline
        msg_listener_->Listen<SdkMsgRelayRemoteDeviceOffline>(
            [weak_self](const SdkMsgRelayRemoteDeviceOffline&) {
            const auto self = weak_self.lock();
            if (!self || self->remote_force_closed_) {
                return;
            }
            self->context_->PostDelayUITask([weak_self]() {
                const auto task_self = weak_self.lock();
                if (!task_self) {
                    return;
                }
                TcDialog dialog(tcTr("id_error"), tcTr("id_remote_device_offline"));
                if (dialog.exec() == kDoneOk) {
                    task_self->context_->PostTask([weak_self]() {
                        if (const auto reconnect_self = weak_self.lock()) {
                            reconnect_self->ReconnectInRelayMode();
                        }
                    });
                }
                else {
                    // exit
                    ProcessUtil::KillProcess(QApplication::applicationPid());
                }
            }, 1000);
        });
    }

    void BaseWorkspace::changeEvent(QEvent* event) {
        is_window_active_ = isActiveWindow() && !(windowState() & Qt::WindowMinimized);
        qDebug() << "window state: " << is_window_active_;
        QMainWindow::changeEvent(event);
    }

    bool BaseWorkspace::IsActiveNow() const {
        return is_window_active_;
    }

    void BaseWorkspace::closeEvent(QCloseEvent *event) {
        this->raise();             
        this->activateWindow();    
        this->showNormal();
        if (!close_event_occurred_widget_) {
            close_event_occurred_widget_ = this;
        }
        event->ignore();

        ExitClientWithDialog();
        close_event_occurred_widget_ = nullptr;
    }

    void BaseWorkspace::ExitClientWithDialog() {
        QString msg = tcTr("id_exit_client");
        if (module_manager_) {
            if (const auto module = module_manager_->GetFileTransferModule();
                module && module->HasProcessingTasks()) {
                msg = tcTr("id_file_transfer_busy") + msg;
            }
        }
        TcDialog dialog(tcTr("id_exit"), msg, this);
        if (dialog.exec() == kDoneOk) {
            if (media_recording_module_) {
                media_recording_module_->StopRecording();
            }
            Exit();
        }
    }

    void BaseWorkspace::dragEnterEvent(QDragEnterEvent *event) {
        event->accept();
        if (event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
        }
    }

    void BaseWorkspace::dragMoveEvent(QDragMoveEvent *event) {
        event->accept();
    }

    void BaseWorkspace::dropEvent(QDropEvent *event) {
        QList<QUrl> urls = event->mimeData()->urls();
        if (urls.isEmpty()) {
            return;
        }
        std::vector<QString> files;
        for (const auto& url : urls) {
            files.push_back(url.toLocalFile());
        }
    }

    void BaseWorkspace::SendWindowsKey(unsigned long vk, bool down) {

    }

    void BaseWorkspace::resizeEvent(QResizeEvent *event) {
        // The standalone file manager deliberately does not construct the
        // remote-desktop widgets. Qt can still deliver a resize event while
        // its hidden host window is being created.
        if (main_progress_) {
            main_progress_->setGeometry(0, title_bar_height_, event->size().width(), event->size().height());
        }
        UpdateDebugPanelPosition();
        UpdateVideoWidgetSize();
        UpdateFloatButtonIndicatorPosition();
        if (overlay_widget_) {
            overlay_widget_->resize(event->size());
        }
    }

    void BaseWorkspace::UpdateFloatButtonIndicatorPosition() {
        if (btn_indicator_) {
            btn_indicator_->setGeometry(0, 0, btn_indicator_->width(), btn_indicator_->height());
        }
    }

    Qt::CursorShape BaseWorkspace::ToQCursorShape(uint32_t cursor_type) {
        if (cursor_type == CursorInfoSync::kIdcArrow) {
            return Qt::ArrowCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcIBeam) {
            return Qt::IBeamCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcWait) {
            return Qt::WaitCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcCross) {
            return Qt::CrossCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcUpArrow) {
            return Qt::UpArrowCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcSize) {
            return Qt::SizeAllCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcIcon) {
            return Qt::BitmapCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcSizeNWSE) {
            return Qt::SizeFDiagCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcSizeNESW) {
            return Qt::SizeBDiagCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcSizeWE) {
            return Qt::SizeHorCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcSizeNS) {
            return Qt::SizeVerCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcSizeAll) {
            return Qt::SizeAllCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcHand) {
            return Qt::PointingHandCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcPin) {
            return Qt::PointingHandCursor;
        }
        else if (cursor_type == CursorInfoSync::kIdcHelp) {
            return Qt::WhatsThisCursor;
        }
        else {
            return Qt::BitmapCursor;
        }
    }

    void BaseWorkspace::UpdateLocalCursor() {
        const auto weak_self = weak_from_this();
        context_->PostUITask([weak_self]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            if (PxRenderView::s_mouse_in_) {
                if (QApplication::overrideCursor()) {
                    QApplication::changeOverrideCursor(self->cursor_);
                }
                else {
                    QApplication::setOverrideCursor(self->cursor_);
                }
            }
            else {
                QApplication::restoreOverrideCursor();
            }
        });
    }

    void BaseWorkspace::RegisterControllerPanelListeners() {
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<MsgClientOpenFiletrans>([weak_self](const MsgClientOpenFiletrans&) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->context_->PostUITask([weak_self]() {
                const auto task_self = weak_self.lock();
                if (!task_self) {
                    return;
                }
                // FT 协议版本门控:rustdesk 语义 = 2;旧被控(0/缺省)不兼容,提示而非静默失败
                if (task_self->settings_->render_ft_protocol_version_ != 2) {
                    task_self->context_->NotifyAppMessage(
                        "Warning", "Controlled side file transfer version incompatible.");
                    return;
                }
                if (task_self->module_manager_) {
                    if (const auto module =
                            task_self->module_manager_->GetFileTransferModule()) {
                        module->ShowWindow();
                    }
                }
            });
        });

        msg_listener_->Listen<MsgClientOpenDebugPanel>([weak_self](const MsgClientOpenDebugPanel&) {
            if (const auto self = weak_self.lock()) {
                self->context_->PostUITask([weak_self]() {
                    if (const auto task_self = weak_self.lock(); task_self && task_self->st_panel_) {
                        task_self->st_panel_->setHidden(false);
                    }
                });
            }
        });
    }

    void BaseWorkspace::UpdateDebugPanelPosition() {

    }

    void BaseWorkspace::SendClipboardMessage(const MsgClientClipboard& msg) const {
        if (!sdk_ || remote_force_closed_) {
            return;
        }
        px::Message m;
        m.set_type(px::kClipboardInfo);
        m.set_device_id(settings_->device_id_);
        m.set_stream_id(settings_->stream_id_);
        auto sub = m.mutable_clipboard_info();
        sub->set_type((ClipboardType)msg.type_);
        if (msg.type_ == ClipboardType::kClipboardText) {
            sub->set_msg(msg.msg_);
        }
        else if (msg.type_ == ClipboardType::kClipboardFiles) {
            for (const auto& file : msg.files_) {
                auto pf = sub->mutable_files()->Add();
                pf->set_file_name(file.file_name());
                pf->set_full_path(file.full_path());
                pf->set_ref_path(file.ref_path());
                pf->set_total_size(file.total_size());
                LOGI("SendClipboardMessage, file: {}", file.file_name());
            }
        }
        if (auto buffer = px::ProtoAsData(&m); buffer) {
            sdk_->PostMediaMessage(buffer);
        }
    }

    void BaseWorkspace::SendSwitchMonitorMessage(const std::string& name) const {
        if (!sdk_ || remote_force_closed_) {
            return;
        }
        px::Message m;
        m.set_type(px::kSwitchMonitor);
        m.set_device_id(settings_->device_id_);
        m.set_stream_id(settings_->stream_id_);
        m.mutable_switch_monitor()->set_name(name);
        if (const auto buffer = px::ProtoAsData(&m); buffer) {
            sdk_->PostMediaMessage(buffer);
        }
    }

    void BaseWorkspace::SendUpdateDesktopMessage() const {
        if (!sdk_ || remote_force_closed_) {
            return;
        }
        px::Message m;
        m.set_type(px::kUpdateDesktop);
        if (const auto buffer = px::ProtoAsData(&m); buffer) {
            sdk_->PostMediaMessage(buffer);
        }
    }

    void BaseWorkspace::SendModifyFpsMessage() const {
        if (!sdk_ || remote_force_closed_) {
            return;
        }
        int fps = settings_->fps_;
        px::Message m;
        m.set_type(px::kModifyFps);
        auto mf = m.mutable_modify_fps();
        mf->set_fps(fps);
        if (const auto buffer = px::ProtoAsData(&m); buffer) {
            sdk_->PostMediaMessage(buffer);
        }
    }

    void BaseWorkspace::SendExitControlledEndMessage() {
        if (!sdk_ || remote_force_closed_) {
            return;
        }
        px::Message m;
        m.set_type(px::kExitControlledEnd);
        if (auto buffer = px::ProtoAsData(&m); buffer) {
            sdk_->PostMediaMessage(buffer);
        }
    }

    void BaseWorkspace::SendHardUpdateDesktopMessage() {
        if (!sdk_ || remote_force_closed_) {
            return;
        }
        px::Message m;
        m.set_type(px::kHardUpdateDesktop);
        if (auto buffer = px::ProtoAsData(&m); buffer) {
            sdk_->PostMediaMessage(buffer);
        }
    }

    void BaseWorkspace::SendSwitchWorkModeMessage(SwitchWorkMode::WorkMode mode) {
#if 0 // Deprecated !!
        if (!sdk_) {
            return;
        }
        settings_->SetWorkMode(mode);
        px::Message m;
        m.set_type(px::kSwitchWorkMode);
        m.set_device_id(settings_->device_id_);
        m.set_stream_id(settings_->stream_id_);
        auto wm = m.mutable_work_mode();
        wm->set_mode(mode);
        sdk_->PostMediaMessage(m.SerializeAsString());
#endif
    }

    void BaseWorkspace::SendSwitchFullColorMessage(bool enable) {
        if (!sdk_ || remote_force_closed_) {
            return;
        }
        px::Message m;
        m.set_type(px::kSwitchFullColorMode);
        m.set_device_id(settings_->device_id_);
        m.set_stream_id(settings_->stream_id_);
        auto wm = m.mutable_switch_full_color_mode();
        wm->set_enable(enable);
        if (auto buffer = px::ProtoAsData(&m); buffer) {
            sdk_->PostMediaMessage(buffer);
        }
    }

    void BaseWorkspace::SwitchScaleMode(const px::ScaleMode& mode) {
        settings_->SetScaleMode(mode);
        if (mode == ScaleMode::kFillWindow) {
            SwitchToFillWindow();
        }
        else if (mode == ScaleMode::kKeepAspectRatio) {
            CalculateAspectRatio();
        }
    }

    void BaseWorkspace::CalculateAspectRatio() {

    }

    void BaseWorkspace::SwitchToFillWindow() {

    }

    void BaseWorkspace::SendChangeMonitorResolutionMessage(const MsgClientChangeMonitorResolution& msg) {
        if (!sdk_ || remote_force_closed_) {
            return;
        }
        px::Message m;
        m.set_type(px::kChangeMonitorResolution);
        m.set_device_id(settings_->device_id_);
        m.set_stream_id(settings_->stream_id_);
        auto cmr = m.mutable_change_monitor_resolution();
        cmr->set_monitor_name(msg.monitor_name_);
        cmr->set_target_width(msg.width_);
        cmr->set_target_height(msg.height_);
        if (auto buffer = px::ProtoAsData(&m); buffer) {
            sdk_->PostMediaMessage(buffer);
        }
    }

    void BaseWorkspace::SendVirtualDisplayRequest(const MsgClientVirtualDisplayRequest& request) {
        const auto request_id = request.request_id_.empty()
            ? std::format("client-{}-{}", QApplication::applicationPid(), ++virtual_display_request_seq_)
            : request.request_id_;
        const auto self = shared_from_this();
        const auto report_local_failure = [self, request_id](
            std::string error_code, std::string error_message) {
            self->context_->SendAppMessage(MsgClientVirtualDisplayResult {
                .enabled_ = self->settings_->render_virtual_display_enabled_,
                .owned_display_count_ = self->settings_->render_virtual_display_owned_count_,
                .max_display_count_ = self->settings_->render_virtual_display_max_count_,
                .topology_generation_ = self->settings_->render_virtual_display_topology_generation_,
                .request_id_ = request_id,
                .accepted_ = false,
                .state_ = kVirtualDisplayFailed,
                .error_code_ = std::move(error_code),
                .error_message_ = std::move(error_message),
            });
        };
        if (!sdk_ || remote_force_closed_) {
            report_local_failure("CLIENT_NOT_CONNECTED", "The remote media connection is unavailable.");
            return;
        }
        auto message = MakeVirtualDisplayRequestMessage(
            settings_->device_id_, settings_->stream_id_, request_id, request.operation_,
            request.width_, request.height_, request.refresh_hz_);
        if (const auto buffer = px::ProtoAsData(&message); buffer) {
            sdk_->PostMediaMessage(buffer);
            return;
        }
        report_local_failure("REQUEST_ENCODE_FAILED", "The virtual display request could not be encoded.");
    }

    void BaseWorkspace::SendVoiceCallCommand(const MsgClientVoiceCallCommand& command) {
        VoiceCallPhase core_phase;
        {
            std::scoped_lock lock(voice_call_mutex_);
            core_phase = voice_call_state_.Phase();
        }
        LOGI("[VoiceCall] command action={}, core_phase={}, enabled={}, protocol={}",
             static_cast<int>(command.action_), static_cast<int>(core_phase),
             settings_->render_voice_call_enabled_,
             settings_->render_voice_call_protocol_version_);
        if (command.action_ == MsgClientVoiceCallCommand::Action::kSelectAudioDevices) {
            {
                std::scoped_lock lock(voice_call_mutex_);
                if (voice_call_state_.Phase() != VoiceCallPhase::kIdle) {
                    return;
                }
                voice_capture_device_id_ = command.capture_device_id_;
                voice_playout_device_id_ = command.playout_device_id_;
            }
            LOGI("[VoiceCall] audio device selection updated, capture={}, playout={}",
                 command.capture_device_id_.empty() ? "default" : "explicit",
                 command.playout_device_id_.empty() ? "default" : "explicit");
            NotifyVoiceCallStatus();
            return;
        }
        if (command.action_ == MsgClientVoiceCallCommand::Action::kToggleMicrophoneMute ||
            command.action_ == MsgClientVoiceCallCommand::Action::kToggleSpeakerMute) {
            std::shared_ptr<VoiceAudioEndpoint> endpoint;
            bool microphone_muted = false;
            bool speaker_muted = false;
            {
                std::scoped_lock lock(voice_call_mutex_);
                if (voice_call_state_.Phase() != VoiceCallPhase::kConnected ||
                    !voice_audio_endpoint_) {
                    return;
                }
                endpoint = voice_audio_endpoint_;
                if (command.action_ ==
                    MsgClientVoiceCallCommand::Action::kToggleMicrophoneMute) {
                    voice_microphone_muted_ = !voice_microphone_muted_;
                } else {
                    voice_speaker_muted_ = !voice_speaker_muted_;
                }
                microphone_muted = voice_microphone_muted_;
                speaker_muted = voice_speaker_muted_;
            }
            endpoint->SetMicrophoneMuted(microphone_muted);
            endpoint->SetSpeakerMuted(speaker_muted);
            LOGI("[VoiceCall] local controls microphone_muted={}, speaker_muted={}",
                 microphone_muted, speaker_muted);
            NotifyVoiceCallStatus();
            return;
        }
        if (command.action_ == MsgClientVoiceCallCommand::Action::kHangUp) {
            StopVoiceCall(true, "local_hangup");
            return;
        }
        if (!settings_->render_voice_call_enabled_ ||
            settings_->render_voice_call_protocol_version_ != 1) {
            NotifyVoiceCallStatus("unsupported");
            return;
        }
        if (!sdk_ || remote_force_closed_) {
            NotifyVoiceCallStatus("not_connected");
            return;
        }

        const auto call_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        const auto request_id = NextNativeVoiceCallRequestId();
        {
            std::scoped_lock lock(voice_call_mutex_);
            if (!voice_call_state_.BeginOutgoing(
                    call_id, request_id,
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count()))) {
                return;
            }
        }
        auto request = MakeVoiceCallRequestMessage(
            settings_->device_id_, settings_->stream_id_, call_id, request_id, true);
        if (const auto data = ProtoAsData(&request); data) {
            sdk_->PostMediaMessage(data);
            NotifyVoiceCallStatus();
        } else {
            StopVoiceCall(false, "request_encode_failed");
            return;
        }

        const auto weak_self = weak_from_this();
        QTimer::singleShot(static_cast<int>(VoiceCallState::kRequestTimeoutMs), this,
            [weak_self, call_id, request_id]() {
                const auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                bool expired = false;
                {
                    std::scoped_lock lock(self->voice_call_mutex_);
                    if (self->voice_call_state_.CallId() == call_id &&
                        self->voice_call_state_.RequestId() == request_id) {
                        const auto now = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count());
                        expired = self->voice_call_state_.Expire(now);
                    }
                }
                if (expired) {
                    if (self->sdk_ && !self->remote_force_closed_) {
                        auto cancel = MakeVoiceCallRequestMessage(
                            self->settings_->device_id_, self->settings_->stream_id_,
                            call_id, request_id, false);
                        if (const auto data = ProtoAsData(&cancel); data) {
                            self->sdk_->PostMediaMessage(data);
                        }
                    }
                    LOGI("[VoiceCall] outgoing request timed out, remote cancel sent, call={}",
                         VoiceCallLogId(call_id));
                    self->NotifyVoiceCallStatus("timeout");
                }
            });
    }

    void BaseWorkspace::ProcessVoiceCallMessage(const std::shared_ptr<Message>& msg) {
        if (!msg) {
            return;
        }
        if (msg->device_id() != settings_->device_id_ ||
            msg->stream_id() != settings_->stream_id_) {
            LOGW("[VoiceCall] drop message for another session, stream={}", msg->stream_id());
            return;
        }
        if (msg->type() == kVoiceCallRequest) {
            const auto& request = msg->voice_call_request();
            if (!request.connect()) {
                bool matched = false;
                {
                    std::scoped_lock lock(voice_call_mutex_);
                    matched = voice_call_state_.CallId() == request.call_id();
                }
                if (matched) {
                    StopVoiceCall(false, "remote_hangup");
                }
                return;
            }
            // Native controller v1 does not expose an incoming-call surface.
            Message response;
            response.set_type(kVoiceCallResponse);
            response.set_device_id(settings_->device_id_);
            response.set_stream_id(settings_->stream_id_);
            auto* sub = response.mutable_voice_call_response();
            sub->set_call_id(request.call_id());
            sub->set_request_id(request.request_id());
            sub->set_accepted(false);
            sub->set_reason("unsupported_direction");
            if (const auto data = ProtoAsData(&response); data && sdk_) {
                sdk_->PostMediaMessage(data);
            }
            return;
        }
        if (msg->type() == kVoiceCallResponse) {
            const auto& response = msg->voice_call_response();
            bool matched = false;
            {
                std::scoped_lock lock(voice_call_mutex_);
                matched = voice_call_state_.ApplyResponse(
                    response.call_id(), response.request_id(), response.accepted());
            }
            if (!matched) {
                LOGW("[VoiceCall] stale/replayed response dropped, call={}",
                     VoiceCallLogId(response.call_id()));
                return;
            }
            if (!response.accepted()) {
                NotifyVoiceCallStatus(response.reason().empty() ? "rejected" : response.reason());
                return;
            }

            auto endpoint = std::make_shared<VoiceAudioEndpoint>();
            const std::weak_ptr<VoiceAudioEndpoint> weak_endpoint = endpoint;
            VoiceAudioBackendConfig backend_config;
            {
                std::scoped_lock lock(voice_call_mutex_);
                backend_config.capture_device_id = voice_capture_device_id_;
                backend_config.playout_device_id = voice_playout_device_id_;
            }
            std::string error;
            const auto weak_self = weak_from_this();
            if (!voice_packet_transport_.Start(
                    [weak_self, call_id = response.call_id()](const VoiceTransportPacket& packet) {
                        if (const auto self = weak_self.lock()) {
                            self->DispatchVoiceAudioFrame(
                                call_id, packet.sequence, packet.capture_time_ms, packet.opus);
                        }
                    })) {
                StopVoiceCall(true, "transport_unavailable");
                return;
            }
            if (!endpoint->Start(
                    [weak_self, call_id = response.call_id()](
                        uint32_t sequence, uint64_t capture_time_ms,
                        const std::vector<uint8_t>& opus) {
                        if (const auto self = weak_self.lock()) {
                            self->QueueVoiceAudioFrame(call_id, sequence, capture_time_ms, opus);
                        }
                    }, backend_config, error,
                    [weak_self, call_id = response.call_id(), weak_endpoint](
                        const std::string& reason) {
                        const auto self = weak_self.lock();
                        if (!self) {
                            return;
                        }
                        self->context_->PostUITask(
                            [weak_self, call_id, weak_endpoint, reason]() {
                                const auto task_self = weak_self.lock();
                                if (!task_self) {
                                    return;
                                }
                                const auto expected_endpoint = weak_endpoint.lock();
                                bool still_active = false;
                                {
                                    std::scoped_lock lock(task_self->voice_call_mutex_);
                                    still_active = expected_endpoint &&
                                        task_self->voice_call_state_.IsMediaAllowed(call_id) &&
                                        task_self->voice_audio_endpoint_ == expected_endpoint;
                                }
                                if (still_active) {
                                    task_self->StopVoiceCall(true,
                                        reason.empty() ? "device_lost" : reason);
                                }
                            });
                    })) {
                LOGE("[VoiceCall] local audio endpoint failed: {}", error);
                StopVoiceCall(true, "no_mic");
                return;
            }
            const auto backend_info = endpoint->BackendInfo();
            LOGI("[VoiceCall] local audio backend={}, capture={}, playout={}, apm=aec+ns+agc",
                 backend_info.backend, backend_info.capture_device,
                 backend_info.playout_device);
            bool keep_endpoint = false;
            {
                std::scoped_lock lock(voice_call_mutex_);
                keep_endpoint = voice_call_state_.IsMediaAllowed(response.call_id());
                if (keep_endpoint) {
                    voice_audio_endpoint_ = endpoint;
                    voice_microphone_muted_ = false;
                    voice_speaker_muted_ = false;
                }
            }
            if (!keep_endpoint) {
                endpoint->Stop();
                voice_packet_transport_.Stop();
                return;
            }
            auto config = MakeVoiceAudioConfigMessage(
                settings_->device_id_, settings_->stream_id_, response.call_id());
            if (const auto data = ProtoAsData(&config); data && sdk_) {
                sdk_->PostMediaMessage(data);
            }
            NotifyVoiceCallStatus();
            return;
        }
        if (msg->type() == kVoiceAudioConfig) {
            const auto& config = msg->voice_audio_config();
            bool active_call = false;
            {
                std::scoped_lock lock(voice_call_mutex_);
                active_call = voice_call_state_.IsMediaAllowed(config.call_id());
            }
            if (!active_call) {
                return;
            }
            if (config.sample_rate() != VoiceAudioEndpoint::kSampleRate ||
                config.channels() != VoiceAudioEndpoint::kChannels ||
                config.frame_ms() != VoiceAudioEndpoint::kFrameMs) {
                StopVoiceCall(true, "incompatible_audio_config");
                NotifyVoiceCallStatus("incompatible_audio_config");
            }
            return;
        }
        if (msg->type() == kVoiceAudioFrame) {
            const auto& frame = msg->voice_audio_frame();
            std::shared_ptr<VoiceAudioEndpoint> endpoint;
            {
                std::scoped_lock lock(voice_call_mutex_);
                if (!voice_call_state_.AcceptMedia(frame.call_id(), frame.sequence())) {
                    return;
                }
                endpoint = voice_audio_endpoint_;
            }
            if (endpoint && !frame.opus().empty()) {
                endpoint->ReceiveOpus(
                    frame.sequence(), frame.capture_time_ms(),
                    std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(frame.opus().data()),
                        frame.opus().size())); // NOLINT(gammaray-raw-pointer-boundary): protobuf byte-view boundary
            }
        }
    }

    void BaseWorkspace::StopVoiceCall(bool notify_remote, const std::string& reason) {
        std::shared_ptr<VoiceAudioEndpoint> endpoint;
        std::string call_id;
        uint64_t request_id = 0;
        {
            std::scoped_lock lock(voice_call_mutex_);
            if (voice_call_state_.Phase() == VoiceCallPhase::kIdle) {
                return;
            }
            call_id = voice_call_state_.CallId();
            request_id = voice_call_state_.RequestId();
            voice_call_state_.Reset();
            endpoint = std::move(voice_audio_endpoint_);
            voice_microphone_muted_ = false;
            voice_speaker_muted_ = false;
        }
        voice_packet_transport_.Stop();
        if (endpoint) {
            const auto stats = endpoint->Stats();
            const auto transport_stats = voice_packet_transport_.Stats();
            endpoint->Stop();
            LOGI("[VoiceCall] local end reason={}, tx={}, rx={}, underrun={}, plc={}, "
                 "jitter_peak={}, jitter_late={}, jitter_drop={}, apm_fail={}/{}, device_rebuilds={}, transport_drop={}",
                 reason, stats.encoded_packets, stats.decoded_packets,
                 stats.playout_underruns, stats.plc_packets,
                 stats.jitter_peak_packets, stats.jitter_late,
                 stats.jitter_overflow_drops, stats.apm_capture_failures,
                 stats.apm_render_failures, stats.device_rebuilds,
                 transport_stats.congestion_drops);
        }
        if (notify_remote && sdk_ && !remote_force_closed_ && !call_id.empty()) {
            auto request = MakeVoiceCallRequestMessage(
                settings_->device_id_, settings_->stream_id_, call_id, request_id, false);
            if (const auto data = ProtoAsData(&request); data) {
                sdk_->PostMediaMessage(data);
            }
        }
        NotifyVoiceCallStatus(reason);
    }

    void BaseWorkspace::NotifyVoiceCallStatus(const std::string& reason) {
        // Voice commands originate from MessageNotifier::process().  Dispatching
        // a status synchronously from that callback recursively enters the same
        // event bus and can spin the UI thread.  A queued Qt invocation also
        // serializes SDK/network-thread status changes onto the UI thread.
        const auto weak_self = weak_from_this();
        context_->PostUITask([weak_self, reason]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            VoiceCallPhase phase;
            bool microphone_muted = false;
            bool speaker_muted = false;
            std::string capture_device_id;
            std::string playout_device_id;
            {
                std::scoped_lock lock(self->voice_call_mutex_);
                phase = self->voice_call_state_.Phase();
                microphone_muted = self->voice_microphone_muted_;
                speaker_muted = self->voice_speaker_muted_;
                capture_device_id = self->voice_capture_device_id_;
                playout_device_id = self->voice_playout_device_id_;
            }
            self->context_->SendAppMessage(MsgClientVoiceCallStatus {
                .supported_ = self->settings_->render_voice_call_enabled_ &&
                              self->settings_->render_voice_call_protocol_version_ == 1,
                .requires_headset_ = self->settings_->render_voice_call_requires_headset_,
                .microphone_muted_ = microphone_muted,
                .speaker_muted_ = speaker_muted,
                .capture_device_id_ = std::move(capture_device_id),
                .playout_device_id_ = std::move(playout_device_id),
                .phase_ = phase,
                .reason_ = reason,
            });
        });
    }

    void BaseWorkspace::QueueVoiceAudioFrame(
        const std::string& call_id, uint32_t sequence, uint64_t capture_time_ms,
        const std::vector<uint8_t>& opus) {
        {
            std::scoped_lock lock(voice_call_mutex_);
            if (!voice_call_state_.IsMediaAllowed(call_id) || !sdk_ || remote_force_closed_) {
                return;
            }
        }
        voice_packet_transport_.Enqueue({
            .sequence = sequence,
            .capture_time_ms = capture_time_ms,
            .opus = opus,
        });
    }

    void BaseWorkspace::DispatchVoiceAudioFrame(
        const std::string& call_id, uint32_t sequence, uint64_t capture_time_ms,
        const std::vector<uint8_t>& opus) {
        {
            std::scoped_lock lock(voice_call_mutex_);
            if (!voice_call_state_.IsMediaAllowed(call_id) || !sdk_ || remote_force_closed_) {
                return;
            }
        }
        auto message = MakeVoiceAudioFrameMessage(
            settings_->device_id_, settings_->stream_id_, call_id,
            sequence, capture_time_ms, opus);
        if (const auto data = ProtoAsData(&message); data) {
            sdk_->PostMediaMessage(data);
        }
    }

    void BaseWorkspace::UpdateVideoWidgetSize() {
        if (settings_->file_transfer_only_) {
            return;
        }
        const auto weak_self = weak_from_this();
        context_->PostUITask([weak_self]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            auto scale_mode = self->settings_->scale_mode_;
            LOGI("UpdateVideoWidgetSize scale_mode: {}", (int)scale_mode);
            self->SwitchScaleMode(scale_mode);
        });
    }

    void BaseWorkspace::Exit() {
        if (media_recording_module_) {
            media_recording_module_->StopRecording();
            media_recording_module_.reset();
        }
        if (module_manager_) {
            module_manager_->Stop();
            module_manager_.reset();
        }
        if (panel_client_) {
            panel_client_->Exit();
            panel_client_ = nullptr;
        }
        if (console_client_) {
            console_client_->Exit();
            console_client_ = nullptr;
        }
        if (sdk_) {
            sdk_->Exit();
            sdk_ = nullptr;
        }
        if (context_) {
            context_->Exit();
            context_ = nullptr;
        }
        ProcessUtil::KillProcess(QApplication::applicationPid());
    }

    void BaseWorkspace::OnGetCaptureMonitorsCount(int monitors_count) {
        monitors_count_ = monitors_count;
    }

    void BaseWorkspace::OnGetCaptureMonitorName(std::string monitor_name) {

    }

    void BaseWorkspace::InitRenderViews(const std::shared_ptr<ThunderSdkParams>& params) {
        this->resize(def_window_size_);
    }

    bool BaseWorkspace::eventFilter(QObject* watched, QEvent* event) {
        return QMainWindow::eventFilter(watched, event);
    }

    std::shared_ptr<ThunderSdk> BaseWorkspace::GetThunderSdk() {
        return sdk_;
    }

    std::shared_ptr<ClientContext> BaseWorkspace::GetContext() {
        return context_;
    }

    void BaseWorkspace::WidgetSelectMonitor(QWidget* widget, QList<QScreen*>& screens) {
        QRect widget_geometry = widget->geometry();
        int max_widget_with_screen_visible_area = 0;
        QScreen* target_screen = nullptr;
        for (auto screen : screens) {
            QRect widget_screen_intersection = screen->availableGeometry().intersected(widget_geometry);
            int  widget_with_screen_visible_area = widget_screen_intersection.width() * widget_screen_intersection.height();
            if (widget_with_screen_visible_area > max_widget_with_screen_visible_area) {
                max_widget_with_screen_visible_area = widget_with_screen_visible_area;
                target_screen = screen;
            }
        }
        if (target_screen) {
            widget->windowHandle()->setScreen(target_screen);
        }
    }

    void BaseWorkspace::ReconnectInRelayMode() {
        if (!settings_->IsRelayMode() || remote_force_closed_) {
            return;
        }
        // Reconnect
        // 1. Can I connect relay server?
        {
            LOGI("will get device info in {}:{} for id: {}", settings_->relay_host_, settings_->relay_port_, settings_->full_device_id_);
            auto r = px_relay::RelayApi::GetRelayDeviceInfo(settings_->relay_host_, settings_->relay_port_, settings_->full_device_id_, settings_->relay_appkey_);
            if (!r.has_value()) {
                const auto weak_self = weak_from_this();
                context_->PostUITask([weak_self]() {
                    if (const auto self = weak_self.lock()) {
                        TcDialog dialog(tcTr("id_warning"),
                                        tcTr("id_cant_get_local_device_info"), self.get());
                        dialog.exec();
                    }
                });
                return;
            }
        }

        // 2. Can I get remote device info ?
        {
            LOGI("will get remote device info in {}:{} for id: {}", settings_->relay_host_, settings_->relay_port_, settings_->full_remote_device_id_);
            auto r = px_relay::RelayApi::GetRelayDeviceInfo(settings_->relay_host_, settings_->relay_port_, settings_->full_remote_device_id_, settings_->relay_appkey_);
            if (!r.has_value()) {
                const auto weak_self = weak_from_this();
                context_->PostUITask([weak_self]() {
                    if (const auto self = weak_self.lock()) {
                        TcDialog dialog(tcTr("id_warning"),
                                        tcTr("id_cant_get_remote_device_info"), self.get());
                        dialog.exec();
                    }
                });
                return;
            }
        }

        // 3. Start reconnecting
        sdk_->RetryConnection();

        // show dialog
        const auto weak_self = weak_from_this();
        context_->PostUITask([weak_self]() {
            const auto self = weak_self.lock();
            if (self && self->retry_conn_dialog_->isHidden()) {
                WidgetHelper::SetTitleBarColor(self->retry_conn_dialog_.get());
                self->retry_conn_dialog_->Exec();
            }
        });
    }

    void BaseWorkspace::DismissConnectingDialog() {
        const auto weak_self = weak_from_this();
        context_->PostUITask([weak_self]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            // dismiss dialog
            if (self->retry_conn_dialog_ && !self->retry_conn_dialog_->isHidden()) {
                self->retry_conn_dialog_->Done(0);
            }
        });
    }

    void BaseWorkspace::ProcessNetworkMessage(const std::shared_ptr<px::Message>& msg) {
        if (msg->type() == MessageType::kDisconnectConnection) {
            const auto& sub = msg->disconnect_connection();
            LOGI("DISCONNECT, device id: {}, stream id: {}", sub.device_id(), sub.stream_id());
            remote_force_closed_ = true;
            const auto weak_self = weak_from_this();
            context_->PostUITask([weak_self]() {
                if (const auto self = weak_self.lock()) {
                    TcDialog dialog(tcTr("id_warning"), tcTr("id_remote_disconnected"), self.get());
                    dialog.exec();
                    self->Exit();
                }
            });
            context_->SendAppMessage(MsgStopTheWorld{});
            ExitSdk();
        }
        else if (msg->type() == MessageType::kHardwareInfo) {
            const auto weak_self = weak_from_this();
            context_->PostTask([weak_self, msg]() {
                const auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                const auto& hw_info = msg->hw_info().hw_info();
                const auto freq = msg->hw_info().current_cpu_freq();
                auto sys_info = HWInfoParser::ParseHWInfo(msg->hw_info().hw_info(), freq);
                if (!sys_info) {
                    return;
                }
                self->context_->SendAppMessage(MsgHWInfo {
                    .info_ = sys_info,
                });
                //LOGI("SysInfo: {}", to_string(*sys_info.get()));
            });
        }
        else if (msg->type() == MessageType::kVirtualDisplayResponse) {
            const auto& response = msg->virtual_display_response();
            const auto native_state = NormalizeNativeVirtualDisplayResponseState(
                response.accepted(), response.state());
            settings_->render_virtual_display_owned_count_ = response.owned_display_count();
            settings_->render_virtual_display_topology_generation_ = response.topology_generation();
            context_->SendAppMessage(MsgClientVirtualDisplayResult {
                .enabled_ = settings_->render_virtual_display_enabled_,
                .owned_display_count_ = response.owned_display_count(),
                .max_display_count_ = settings_->render_virtual_display_max_count_,
                .topology_generation_ = response.topology_generation(),
                .request_id_ = response.request_id(),
                .accepted_ = response.accepted(),
                .state_ = native_state,
                .topology_changed_ = response.topology_changed(),
                .logical_display_id_ = response.logical_display_id(),
                .error_code_ = response.error_code(),
                .error_message_ = response.error_message(),
                .actual_virtual_display_count_ = response.actual_virtual_display_count(),
                .driver_installed_ = response.driver_installed(),
                .package_valid_ = response.package_valid(),
                .removal_safe_ = response.removal_safe(),
            });
            if (response.accepted() && response.state() == kVirtualDisplayNeedReconnect) {
                LOGI("Virtual display topology generation {} applied in-place for native transport",
                     response.topology_generation());
            }
        }
        else if (msg->type() == MessageType::kVoiceCallRequest ||
                 msg->type() == MessageType::kVoiceCallResponse ||
                 msg->type() == MessageType::kVoiceAudioConfig ||
                 msg->type() == MessageType::kVoiceAudioFrame) {
            ProcessVoiceCallMessage(msg);
        }
    }

    bool BaseWorkspace::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef WIN32
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_ACTIVATE) {
            if (LOWORD(msg->wParam) == WA_INACTIVE) {
                qDebug() << "Window lost focus!";
                const auto weak_self = weak_from_this();
                context_->PostTask([weak_self]() {
                    if (const auto self = weak_self.lock()) {
                        self->context_->SendAppMessage(MsgClientFocusOutEvent{});
                    }
                });
            }
            else {
                qDebug() << "Window gained focus!";
            }
        }
        else if (msg->message == WM_EXITSIZEMOVE) {
            UpdateOverlayWidgetPos();
        }
#endif
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    bool BaseWorkspace::GenerateD3DDevice() {
        auto new_device_wrapper = std::make_shared<D3D11DeviceWrapper>();
        const D3D_FEATURE_LEVEL supportedFeatureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

        ComPtr<IDXGIFactory1> factory1 = nullptr;
        ComPtr<IDXGIAdapter1> adapter = nullptr;

        HRESULT res = NULL;
        int adapter_index = 0;
        res = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory1.GetAddressOf()));
        if (res != S_OK) {
            LOGE("CreateDXGIFactory1 failed");
            return false;
        }

        ComPtr<IDXGIOutput> output;
        UINT adapterIndex = 0;
        uint32_t my_adapter_uid = 0;

        while(factory1->EnumAdapters1(adapterIndex, adapter.GetAddressOf()) != DXGI_ERROR_NOT_FOUND) {
            UINT outputIndex = 0;
            while(adapter->EnumOutputs(outputIndex, output.GetAddressOf()) != DXGI_ERROR_NOT_FOUND) {
                DXGI_OUTPUT_DESC desc;
                output->GetDesc(&desc);

                // 判断 hwnd 是否属于这个输出
                auto hwnd = (HWND)this->winId();
                RECT rect;
                GetWindowRect(hwnd, &rect);
                RECT intersect;
                if (IntersectRect(&intersect, &rect, &desc.DesktopCoordinates)) {
                    // 找到窗口所在的显示器输出
                    // 这里 adapterIndex 对应的 Adapter 就是窗口的 GPU
                    DXGI_ADAPTER_DESC adapter_desc;
                    adapter->GetDesc(&adapter_desc);
                    my_adapter_uid = adapter_desc.AdapterLuid.LowPart;
                    LOGI("Found the display adapter...: {}", my_adapter_uid);
                    break;
                }
                ++outputIndex;
            }
            ++adapterIndex;
        }

        while (true) {
            res = factory1->EnumAdapters1(adapter_index, adapter.GetAddressOf());
            if (res != S_OK) {
                LOGE("EnumAdapters1 index:{} failed\n", adapter_index);
                return !d3d11_devices_.empty();
            }

            DXGI_ADAPTER_DESC1 desc1;
            adapter->GetDesc1(&desc1);

            if (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                LOGI("Skip software adapter: {}", StringUtil::ToUTF8(desc1.Description).c_str());
                ++adapter_index;
                continue;
            }

            auto adapter_uid = desc1.AdapterLuid.LowPart;

            D3D_FEATURE_LEVEL featureLevel;
            res = D3D11CreateDevice(adapter.Get(),
                                    D3D_DRIVER_TYPE_UNKNOWN,
                                    nullptr,
                                    D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    supportedFeatureLevels,
                                    ARRAYSIZE(supportedFeatureLevels),
                                    D3D11_SDK_VERSION,
                                    &new_device_wrapper->d3d11_device_, &featureLevel, &new_device_wrapper->d3d11_device_context_);

            if (res != S_OK || !new_device_wrapper->d3d11_device_) {
                LOGE("D3D11CreateDevice failed: {}", res);
                return !d3d11_devices_.empty();
            } else {
                // Decoding runs on the SDK video thread, while QWidget/D3D output
                // must be driven by the GUI thread. Protect the shared immediate
                // context before handing this device to FFmpeg and the renderer.
                ComPtr<ID3D10Multithread> multithread;
                if (SUCCEEDED(new_device_wrapper->d3d11_device_.As(&multithread))) {
                    multithread->SetMultithreadProtected(TRUE);
                }
                else {
                    LOGW("D3D11 device does not expose ID3D10Multithread");
                }

                if (featureLevel < D3D_FEATURE_LEVEL_11_0) {
                    LOGE("Skip, Feature level < 11 {}");
                    ++adapter_index;
                    continue;
                }
                if (adapter_uid != my_adapter_uid) {
                    LOGE("Skip, I want the: {}, but now: {}", my_adapter_uid, adapter_uid);
                    ++adapter_index;
                    continue;
                }

                auto driver_name = StringUtil::ToUTF8(desc1.Description);
                if (driver_name.find("Microsoft Basic") != std::string::npos) {
                    LOGW("Skip, this is a microsoft basic render: {}", driver_name);
                    ++adapter_index;
                    continue;
                }

                new_device_wrapper->adapter_uid_ = adapter_uid;
                LOGI("++ Adapter Index:{}, UID: {}, Name: {}", adapter_index,  adapter_uid, driver_name);
                LOGI("++ D3D11CreateDevice mDevice = {}", (void *) new_device_wrapper->d3d11_device_.Get());
                d3d11_devices_[adapter_uid] = new_device_wrapper;
            }
            ++adapter_index;
        }

        if (d3d11_devices_.empty()) {
            LOGW("Can't create any D3D11Device/D3D11DeviceContext!");
        }
        return !d3d11_devices_.empty();
    }

    std::shared_ptr<D3D11DeviceWrapper> BaseWorkspace::GetD3D11DeviceWrapper(uint64_t adapter_uid) {
        if (d3d11_devices_.find(adapter_uid) == d3d11_devices_.end()) {
            return nullptr;
        }
        return d3d11_devices_[adapter_uid];
    }

    void BaseWorkspace::PostMediaMessage(std::shared_ptr<Data> msg) {
        sdk_->PostMediaMessage(msg);
    }

    void BaseWorkspace::PostFileTransferMessage(std::shared_ptr<Data> msg) {
        static_cast<void>(sdk_->PostFileTransferMessage(std::move(msg)));
    }

    SkinInterface* BaseWorkspace::GetSkin() {
        return skin_;
    }

    void BaseWorkspace::ExitSdk() {
        StopVoiceCall(false, "client_exit");
        if (sdk_) {
            sdk_->Exit();
            sdk_ = nullptr;
        }
    }

    void BaseWorkspace::moveEvent(QMoveEvent* event) {
        QMainWindow::moveEvent(event);
        if (overlay_widget_) {
            overlay_widget_->move(this->pos());
        }
    }

    void BaseWorkspace::showEvent(QShowEvent* event) {
        QMainWindow::showEvent(event);
        if (overlay_widget_) {
            overlay_widget_->show();
        }
    }

    void BaseWorkspace::hideEvent(QHideEvent* event) {
        QMainWindow::hideEvent(event);
        if (overlay_widget_) {
            overlay_widget_->hide();
        }
    }

    void BaseWorkspace::mouseReleaseEvent(QMouseEvent* event) {
        QMainWindow::mouseReleaseEvent(event);
    }

    void BaseWorkspace::UpdateOverlayWidgetPos() {
        if (overlay_widget_) {
            QPoint global_pos = mapToGlobal(QPoint(0, 0));
            overlay_widget_->resize(this->size());
            overlay_widget_->move(global_pos);
        }
    }

    void BaseWorkspace::enterEvent(QEnterEvent *event) {
        QMainWindow::enterEvent(event);
        //LOGI("workspace: enterEvent");
    }

    void BaseWorkspace::leaveEvent(QEvent *event) {
        QMainWindow::leaveEvent(event);
        //LOGI("workspace: leaveEvent");
    }
}
