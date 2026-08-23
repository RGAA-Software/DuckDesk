//
// Created by RGAA on 4/07/2024.
//

#include "float_controller_panel.h"
#include "px_label.h"
#include "px_dialog.h"
#include "ct_const_def.h"
#include "float_icon.h"
#include "sized_msg_box.h"
#include "computer_icon.h"
#include "ct_app_message.h"
#include "no_margin_layout.h"
#include "background_widget.h"
#include "px_common_new/log.h"
#include "px_client/ct_settings.h"
#include "px_client/ct_virtual_display_protocol.h"
#include "float_sub_mode_panel.h"
#include "float_sub_display_panel.h"
#include "float_sub_control_panel.h"
#include "px_client/ct_client_context.h"
#include "px_client/plugins/ct_plugin_manager.h"
#include "px_client_sdk_new/sdk_messages.h"
#include "px_common_new/message_notifier.h"
#include <QCoreApplication>
#include <QIcon>
#include <QPushButton>
#include <QTimer>

namespace px
{

    FloatControllerPanel::FloatControllerPanel(const std::shared_ptr<ClientContext>& ctx, QWidget* parent)
        : FloatOverlayWindow(ctx, parent, QSize(kInitialWidth, 412)) {
        auto root_layout = new QVBoxLayout();
        WidgetHelper::ClearMargins(root_layout);
        int border_spacing = 5;
        QSize btn_size = QSize(30, 30);
        int offset = 5;
        root_layout->setContentsMargins(kShadowMargin + offset, kShadowMargin + offset,
                                        kShadowMargin + offset, kShadowMargin + offset);
        root_layout->addSpacing(border_spacing);
        {
            auto layout = new QHBoxLayout();

            //屏幕索引切换按钮
            for (int i = 0; i < kMaxGameViewCount; i++) {
                auto ci = new ComputerIcon(ctx, i, this);
                ci->setFixedSize(QSize(26, 26));
                ci->UpdateSelectedState(true);
                ci->Hide();
                layout->addSpacing(5);
                layout->addWidget(ci);
                computer_icons_.push_back(ci);

                ci->SetOnClickListener([=, this](auto w) {
                    bool recording = context_->GetRecording();
                    if (recording) {
                        TcDialog dialog(tr("Tips"), tr("Currently, screen recording is in progress. Switching display is prohibited. If you want to switch displays, please stop the screen recording..."), nullptr);
                        const auto owner_rect = OwnerGlobalRect();
                        auto pos = owner_rect.center() - QPoint(dialog.width() / 2, dialog.height() / 2);
                        dialog.move(pos);
                        dialog.exec();
                        return;
                    }
                    HideAllSubPanels();
                    SwitchMonitor(ci);
                });
            }

            //分屏按钮
            {
                auto split_screen_btn = new FloatIcon(ctx, this);
                split_screen_btn_ = split_screen_btn;
                split_screen_btn->setFixedSize(btn_size);
                split_screen_btn->SetIcons(":resources/image/separate_monitor.svg", ":resources/image/separate_monitor.svg");
                layout->addWidget(split_screen_btn);

                split_screen_btn->SetOnClickListener([=, this](QWidget* w) {
                    if (!context_->full_functionality_) {
                        TcDialog dialog(tr("Tips"), tr("You need to upgrade to the Super Edition to use the multi-screen display feature."), nullptr);
                        const auto owner_rect = OwnerGlobalRect();
                        auto pos = owner_rect.center() - QPoint(dialog.width() / 2, dialog.height() / 2);
                        dialog.move(pos);
                        dialog.exec();
                        return;
                    }

                    bool recording = context_->GetRecording();
                    if (recording) {
                        TcDialog dialog(tr("Tips"), tr("Currently, screen recording is in progress. Switching display is prohibited. If you want to switch displays, please stop the screen recording.."), nullptr);
                        const auto owner_rect = OwnerGlobalRect();
                        auto pos = owner_rect.center() - QPoint(dialog.width() / 2, dialog.height() / 2);
                        dialog.move(pos);
                        dialog.exec();
                        return;
                    }
                    CaptureAllMonitor();
                });
            }

            layout->addStretch();

            //声音
            WidgetHelper::ClearMargins(layout);
            {
                auto btn = new FloatIcon(ctx, this);
                audio_btn_ = btn;
                btn->setFixedSize(btn_size);
                btn->SetIcons(":resources/image/ic_volume_off.svg", ":resources/image/ic_volume_on.svg");
                layout->addWidget(btn);

                auto settings = Settings::Instance();
                if (settings->IsAudioEnabled()) {
                    btn->SwitchToSelectedState();
                } else {
                    btn->SwitchToNormalState();
                }

                btn->SetOnClickListener([=, this](QWidget* w) {
                    if (settings->IsAudioEnabled()) {
                        btn->SwitchToNormalState();
                        settings->SetAudioEnabled(false);
                    } else {
                        btn->SwitchToSelectedState();
                        settings->SetAudioEnabled(true);
                    }
                    context_->SendAppMessage(MsgClientFloatControllerPanelUpdate{.update_type_ = MsgClientFloatControllerPanelUpdate::EUpdate::kAudioStatus});
                });
            }
            {
                auto btn = new FloatIcon(ctx, this);
                voice_call_btn_ = btn;
                btn->setFixedSize(btn_size);
                btn->setObjectName("voiceCallButton");
                btn->setAccessibleName(tcTr("id_voice_call"));
                btn->SetIcons(":resources/image/ic_voice_call.svg",
                              ":resources/image/ic_voice_call_active.svg");
                btn->setToolTip(tcTr("id_voice_call_unavailable"));
                btn->setEnabled(false);
                layout->addSpacing(border_spacing);
                layout->addWidget(btn);
                btn->SetOnClickListener([this](QWidget*) { ToggleVoiceCall(); });
            }
            {
                auto btn = new FloatIcon(ctx, this);
                btn->setFixedSize(btn_size);
                btn->SetIcons(":resources/image/ic_minimize.svg", "");
                layout->addSpacing(border_spacing);
                layout->addWidget(btn);
                btn->SetOnClickListener([=, this](QWidget* w) {
                    auto top_widget = OverlayOwner() ? OverlayOwner()->window() : nullptr;
                    if (top_widget) {
                        top_widget->showMinimized();
                    }
                });
            }
            {
                auto btn = new FloatIcon(ctx, this);
                full_screen_btn_ = btn;
                btn->setFixedSize(btn_size);
                btn->SetIcons(":resources/image/ic_fullscreen.svg", ":resources/image/ic_fullscreen_exit.svg");
                layout->addSpacing(border_spacing);
                layout->addWidget(btn);
                btn->SetOnClickListener([=, this](QWidget* w) {
                    auto top_widget = OverlayOwner() ? OverlayOwner()->window() : nullptr;
                    if (!top_widget) {
                        return;
                    }
                    if (top_widget->isFullScreen()) {
                        top_widget->showNormal();
                        context_->SendAppMessage(MsgClientExitFullscreen{});
                    } else {
                        top_widget->showFullScreen();
                        context_->SendAppMessage(MsgClientFullscreen{});
                        HideAllSubPanels();
                    }
                    this->hide();
                });
            }
            if (0) {
                auto btn = new FloatIcon(ctx, this);
                btn->setFixedSize(btn_size);
                btn->SetIcons(":resources/image/ic_close.svg", "");
                layout->addSpacing(border_spacing);
                layout->addWidget(btn);
                btn->SetOnClickListener([=, this](QWidget* w) {
//                    auto msg_box = SizedMessageBox::MakeOkCancelBox(tr("Stop"), tr("Do you want to STOP the control of remote PC ?"));
//                    if (msg_box->exec() == 0) {
//                        context_->SendAppMessage(MsgClientExitApp {});
//                    }

                    TcDialog dialog(tr("Warning"), tr("Do you want to stop the control of remote PC?"), nullptr);
                    dialog.exec();

                });
            }
            layout->addSpacing(border_spacing);
            root_layout->addLayout(layout);
        }

        auto icon_size = QSize(40, 40);
        int item_left_spacing = border_spacing;
        // work mode
#if 0   // 暂时不启用 work mode, 改用直接设置帧率
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setFixedSize(this->width(), icon_size.height());
            widget->setLayout(layout);
            root_layout->addSpacing(border_spacing);

            auto icon = new QLabel(this);
            icon->setFixedSize(icon_size);
            icon->setStyleSheet(R"( background-image: url(:resources/image/ic_mode.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addSpacing(item_left_spacing);
            layout->addWidget(icon);

            auto text = new QLabel();
            text->setText(tr("Mode"));
            text->setStyleSheet(R"(font-weight: bold;)");
            //layout->addSpacing(border_spacing);
            layout->addWidget(text);

            layout->addStretch();

            auto icon_right = new QLabel(this);
            icon_right->setFixedSize(icon_size);
            icon_right->setStyleSheet(R"( background-image: url(:resources/image/ic_arrow_right_2.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addWidget(icon_right);
            layout->addSpacing(border_spacing);

            root_layout->addWidget(widget);
            // click
            widget->SetOnClickListener([=, this](auto w) {
                auto panel = GetSubPanel(SubPanelType::kWorkMode);
                if (!panel) {
                    panel = (BaseWidget*)(new SubModePanel(ctx, OverlayOwner()));
                    sub_panels_[SubPanelType::kWorkMode] = panel;
                }
                HideAllSubPanels();
                ShowSubPanel(static_cast<FloatOverlayWindow*>(panel), w);
            });
        }
#endif
        // control
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setFixedSize(ContentWidth() - offset*2, icon_size.height());
            widget->setLayout(layout);

            auto icon = new QLabel(this);
            icon->setFixedSize(icon_size);
            icon->setStyleSheet(R"( background-image: url(:resources/image/ic_control.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addSpacing(item_left_spacing);
            layout->addWidget(icon);

            auto text = new TcLabel();
            text->SetTextId("id_control");
            text->setStyleSheet(R"(font-weight: bold;)");
            //layout->addSpacing(border_spacing);
            layout->addWidget(text);

            layout->addStretch();

            auto icon_right = new QLabel(this);
            icon_right->setFixedSize(icon_size);
            icon_right->setStyleSheet(R"( background-image: url(:resources/image/ic_arrow_right_2.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addWidget(icon_right);
            layout->addSpacing(border_spacing);

            root_layout->addSpacing(border_spacing);
            root_layout->addWidget(widget);

            // click
            widget->SetOnClickListener([=, this](auto w) {
                auto panel = GetSubPanel(SubPanelType::kControl);
                if (!panel) {
                    panel = (BaseWidget*)(new SubControlPanel(ctx, OverlayOwner()));
                    sub_panels_[SubPanelType::kControl] = panel;
                }
                HideAllSubPanels();
                ShowSubPanel(static_cast<FloatOverlayWindow*>(panel), w);
            });
        }
        // Display
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setFixedSize(ContentWidth() - offset*2, icon_size.height());
            widget->setLayout(layout);

            auto icon = new QLabel(this);
            icon->setFixedSize(icon_size);
            icon->setStyleSheet(R"( background-image: url(:resources/image/ic_desktop.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addSpacing(item_left_spacing);
            layout->addWidget(icon);

            auto text = new TcLabel();
            text->SetTextId("id_display");
            text->setStyleSheet(R"(font-weight: bold;)");
            //layout->addSpacing(border_spacing);
            layout->addWidget(text);
            layout->addStretch();

            auto icon_right = new QLabel(this);
            icon_right->setFixedSize(icon_size);
            icon_right->setStyleSheet(R"( background-image: url(:resources/image/ic_arrow_right_2.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addWidget(icon_right);
            layout->addSpacing(border_spacing);

            root_layout->addWidget(widget);

            // click
            widget->SetOnClickListener([=, this](auto w) {
                auto panel = GetSubPanel(SubPanelType::kDisplay);
                if (!panel) {
                    panel = (BaseWidget*)(new SubDisplayPanel(ctx, OverlayOwner()));
                    sub_panels_[SubPanelType::kDisplay] = panel;
                }
                HideAllSubPanels();
                ((SubDisplayPanel*)panel)->SetCaptureMonitorName(monitor_name_);
                ((SubDisplayPanel*)panel)->UpdateMonitorInfo(this->capture_monitor_);
                ShowSubPanel(static_cast<FloatOverlayWindow*>(panel), w);
            });

        }
        // RustDesk-style virtual display shortcut. Keep creation/removal on the
        // first-level floating panel so it is available without another submenu.
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setFixedSize(ContentWidth() - offset * 2, icon_size.height());
            widget->setLayout(layout);

            auto icon = new QLabel(this);
            icon->setFixedSize(icon_size);
            icon->setStyleSheet(R"( background-image: url(:resources/image/ic_desktop.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addSpacing(item_left_spacing);
            layout->addWidget(icon);

            widget->setObjectName("virtualDisplayControls");
            widget->setAccessibleName(tcTr("id_virtual_display"));

            virtual_display_label_ = new QLabel(tcTr("id_virtual_display"), this);
            virtual_display_label_->setObjectName("virtualDisplayStatusLabel");
            virtual_display_label_->setAccessibleName(tcTr("id_virtual_display"));
            virtual_display_label_->setStyleSheet(R"(font-weight:bold;)");
            layout->addWidget(virtual_display_label_);
            layout->addStretch();

            virtual_display_remove_btn_ = new QPushButton(this);
            virtual_display_add_btn_ = new QPushButton(this);
            virtual_display_remove_btn_->setObjectName("virtualDisplayRemoveButton");
            virtual_display_add_btn_->setObjectName("virtualDisplayAddButton");
            virtual_display_remove_btn_->setAccessibleName(tcTr("id_virtual_display_remove"));
            virtual_display_add_btn_->setAccessibleName(tcTr("id_virtual_display_add"));
            virtual_display_remove_btn_->setToolTip(tcTr("id_virtual_display_remove"));
            virtual_display_add_btn_->setToolTip(tcTr("id_virtual_display_add"));
            virtual_display_remove_btn_->setIcon(QIcon(":resources/image/ic_minimize.svg"));
            virtual_display_add_btn_->setIcon(QIcon(":resources/image/ic_add.svg"));
            for (auto* button : {virtual_display_remove_btn_, virtual_display_add_btn_}) {
                button->setFixedSize(28, 28);
                button->setIconSize(QSize(16, 16));
                button->setCursor(Qt::PointingHandCursor);
                button->setStyleSheet(R"(
                    QPushButton {
                        background: #ffffff;
                        border: 1px solid #d9dee7;
                        border-radius: 6px;
                        padding: 0;
                    }
                    QPushButton:hover {
                        background: #f2f6ff;
                        border-color: #8db5ff;
                    }
                    QPushButton:pressed {
                        background: #e5edff;
                        border-color: #5c96ff;
                    }
                    QPushButton:disabled {
                        background: #f7f8fa;
                        border-color: #e8ebf0;
                    }
                )");
            }
            layout->addWidget(virtual_display_remove_btn_);
            layout->addSpacing(4);
            layout->addWidget(virtual_display_add_btn_);
            layout->addSpacing(border_spacing);
            root_layout->addWidget(widget);

            virtual_display_timeout_timer_ = new QTimer(this);
            virtual_display_timeout_timer_->setSingleShot(true);
            connect(virtual_display_timeout_timer_, &QTimer::timeout, [this]() {
                const auto request_id = virtual_display_ui_state_.PendingRequestId();
                if (!virtual_display_ui_state_.Timeout(request_id)) {
                    return;
                }
                UpdateVirtualDisplayUi();
                context_->NotifyAppWarningMessage(
                    tcTr("id_warning"), tcTr("id_virtual_display_request_timeout"));
            });

            connect(virtual_display_add_btn_, &QPushButton::clicked, [this]() {
                StartVirtualDisplayRequest(VirtualDisplayUiOperation::kCreate);
            });
            connect(virtual_display_remove_btn_, &QPushButton::clicked, [this]() {
                StartVirtualDisplayRequest(VirtualDisplayUiOperation::kRemoveLast);
            });

            const auto settings = Settings::Instance();
            virtual_display_ui_state_.ApplyStatus(
                settings->render_virtual_display_enabled_,
                settings->render_virtual_display_owned_count_,
                settings->render_virtual_display_max_count_,
                settings->render_virtual_display_topology_generation_);
            UpdateVirtualDisplayUi();
        }
        // file transfer
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setFixedSize(ContentWidth() - offset*2, icon_size.height());
            widget->setLayout(layout);

            auto icon = new QLabel(this);
            icon->setFixedSize(icon_size);
            icon->setStyleSheet(R"( background-image: url(:resources/image/ic_file_transfer.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addSpacing(item_left_spacing);
            layout->addWidget(icon);

            auto text = new TcLabel();
            text->SetTextId("id_file_transfer");
            text->setStyleSheet(R"(font-weight: bold;)");
            layout->addWidget(text);

            layout->addStretch();
            root_layout->addWidget(widget);

            widget->SetOnClickListener([=, this](QWidget* w) {
                auto plugin_mgr = context_->GetPluginManager();
                if (auto plugin = plugin_mgr->GetFileTransferPlugin(); !plugin) {
                    context_->NotifyAppMessage("Warning", "Don't have file transfer plugin.");
                    return;
                }
                if (file_trans_listener_) {
                    file_trans_listener_(widget);
                }
            });
        }

        // media record
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setFixedSize(ContentWidth() - offset*2, icon_size.height());
            widget->setLayout(layout);

            auto icon = new QLabel(this);
            icon->setFixedSize(icon_size);
            icon->setStyleSheet(R"( background-image: url(:resources/image/ic_media_record.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addSpacing(item_left_spacing);
            layout->addWidget(icon);

            auto text = new TcLabel();
            text->SetTextId("id_screen_recording");
            media_record_lab_ = text;
            text->setStyleSheet(R"(font-weight: bold;)");
            layout->addWidget(text);

            layout->addStretch();
            root_layout->addWidget(widget);

            widget->SetOnClickListener([=, this](QWidget* w) {
                auto plugin_mgr = context_->GetPluginManager();
                if (auto plugin = plugin_mgr->GetMediaRecordPlugin(); !plugin) {
                    context_->NotifyAppMessage("Warning", "Don't have media record plugin.");
                    return;
                }

                bool res = context_->GetRecording();
                context_->SetRecording(!res);
                if (!res) {
                    media_record_lab_->setText(tcTr("id_stop_recording"));
                    text->setStyleSheet(R"(font-weight: bold; color: #dc3545;)");
                }
                else {
                    media_record_lab_->setText(tcTr("id_screen_recording"));
                    text->setStyleSheet(R"(font-weight: bold;)");
                }

                context_->SendAppMessage(MsgClientFloatControllerPanelUpdate{ .update_type_ = MsgClientFloatControllerPanelUpdate::EUpdate::kMediaRecordStatus });
                context_->SendAppMessage(MsgClientMediaRecord{});
                context_->PostTask([=, this]() {
                    context_->SendAppMessage(MsgClientHidePanel{});
                });
            });
        }

        // screenshot
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setFixedSize(ContentWidth() - offset*2, icon_size.height());
            widget->setLayout(layout);

            auto icon = new QLabel(this);
            icon->setFixedSize(icon_size);
            icon->setStyleSheet(R"( background-image: url(:resources/image/ic_screen_shot.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addSpacing(item_left_spacing);
            layout->addWidget(icon);

            auto text = new TcLabel();
            text->SetTextId("id_screenshot");
            text->setStyleSheet(R"(font-weight: bold;)");
            //layout->addSpacing(border_spacing);
            layout->addWidget(text);

            layout->addStretch();
            root_layout->addWidget(widget);

            widget->SetOnClickListener([=, this](QWidget* w) {
                Hide();
                context_->SendAppMessage(MsgStreamShot{});
            });
        }

        // statistics
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setFixedSize(ContentWidth() - offset*2, icon_size.height());
            widget->setLayout(layout);

            auto icon = new QLabel(this);
            icon->setFixedSize(icon_size);
            icon->setStyleSheet(R"( background-image: url(:resources/image/ic_statistics.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addSpacing(item_left_spacing);
            layout->addWidget(icon);

            auto text = new TcLabel();
            text->SetTextId("id_statistics");
            text->setStyleSheet(R"(font-weight: bold;)");
            //layout->addSpacing(border_spacing);
            layout->addWidget(text);

            layout->addStretch();
            root_layout->addWidget(widget);

            widget->SetOnClickListener([=, this](QWidget* w) {
                if (debug_listener_) {
                    debug_listener_(widget);
                }
            });
        }

        // hw info visibility
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setFixedSize(ContentWidth() - offset*2, icon_size.height());
            widget->setLayout(layout);

            auto icon = new QLabel(this);
            icon->setFixedSize(icon_size);
            icon->setStyleSheet(R"( background-image: url(:resources/image/ic_hw_cpu.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addSpacing(item_left_spacing);
            layout->addWidget(icon);

            auto text = new TcLabel();
            text->SetTextId("id_remote_hw");
            text->setStyleSheet(R"(font-weight: bold;)");
            //layout->addSpacing(border_spacing);
            layout->addWidget(text);

            layout->addStretch();
            root_layout->addWidget(widget);

            widget->SetOnClickListener([=, this](QWidget* w) {
                context_->SendAppMessage(MsgSetHWInfoPanelVisibility {
                    .visible_ = true,
                });
            });
        }

        // Close
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setFixedSize(ContentWidth() - offset*2, icon_size.height());
            widget->setLayout(layout);

            auto icon = new QLabel(this);
            icon->setFixedSize(icon_size);
            icon->setStyleSheet(R"( background-image: url(:resources/image/ic_close.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addSpacing(item_left_spacing);
            layout->addWidget(icon);

            auto text = new TcLabel();
            text->SetTextId("id_exit");
            text->setStyleSheet(R"(font-weight: bold;)");
            //layout->addSpacing(border_spacing);
            layout->addWidget(text);

            layout->addStretch();
            root_layout->addWidget(widget);

            widget->SetOnClickListener([=, this](QWidget* w) {
                context_->SendAppMessage(MsgClientExitApp {});
            });
        }
        root_layout->addStretch();
        setLayout(root_layout);

     
        msg_listener_->Listen<MsgClientMousePressed>([=, this](const MsgClientMousePressed& msg) {
            this->Hide();
        });

        msg_listener_->Listen<MsgClientCaptureMonitor>([=, this](const MsgClientCaptureMonitor& msg) {
            this->capture_monitor_ = msg;
            context_->PostUITask([=, this]() {
                UpdateCaptureMonitorInfo();
                //继续向下层panel传递显示器信息
                auto panel = GetSubPanel(SubPanelType::kDisplay);
                if (!panel) {
                    panel = (BaseWidget*)(new SubDisplayPanel(ctx, OverlayOwner()));
                    sub_panels_[SubPanelType::kDisplay] = panel;
                    ((SubDisplayPanel*)panel)->Hide();
                }
                ((SubDisplayPanel*)panel)->SetCaptureMonitorName(monitor_name_);
                ((SubDisplayPanel*)panel)->UpdateMonitorInfo(this->capture_monitor_);
            });
        });

        msg_listener_->Listen<MsgClientMonitorSwitched>([=, this](const MsgClientMonitorSwitched& msg) {
            context_->PostUITask([=, this]() {
                UpdateCapturingMonitor(msg.name_, msg.index_);
            });
        });

        msg_listener_->Listen<MsgClientFullscreen>([=, this](const MsgClientFullscreen& msg) {
            if (full_screen_btn_) {
                full_screen_btn_->SwitchToSelectedState();
            }
        });

        msg_listener_->Listen<MsgClientExitFullscreen>([=, this](const MsgClientExitFullscreen& msg) {
            if (full_screen_btn_) {
                full_screen_btn_->SwitchToNormalState();
            }
        });
        msg_listener_->Listen<MsgClientVirtualDisplayStatus>([this](const MsgClientVirtualDisplayStatus& msg) {
            context_->PostUITask([this, msg]() {
                virtual_display_ui_state_.ApplyStatus(
                    msg.enabled_, msg.owned_display_count_, msg.max_display_count_, msg.topology_generation_);
                if (!virtual_display_ui_state_.IsBusy() && virtual_display_timeout_timer_) {
                    virtual_display_timeout_timer_->stop();
                }
                UpdateVirtualDisplayUi();
            });
        });
        msg_listener_->Listen<MsgClientVirtualDisplayResult>([this](const MsgClientVirtualDisplayResult& msg) {
            context_->PostUITask([this, msg]() {
                const auto effect = virtual_display_ui_state_.ApplyResult(
                    msg.request_id_, msg.accepted_, msg.state_ == kVirtualDisplayNeedReconnect,
                    msg.owned_display_count_, msg.max_display_count_, msg.topology_generation_);
                UpdateVirtualDisplayUi();
                if (effect == VirtualDisplayUiResultEffect::kIgnored) {
                    return;
                }
                if (effect == VirtualDisplayUiResultEffect::kAwaitingReconnect) {
                    virtual_display_timeout_timer_->start(60000);
                    return;
                }
                virtual_display_timeout_timer_->stop();
                if (effect == VirtualDisplayUiResultEffect::kFailed) {
                    context_->NotifyAppErrMessage(
                        tcTr("id_virtual_display"),
                        tcTr("id_virtual_display_operation_failed") + QString("\n") +
                            QString::fromStdString(msg.error_code_ + ": " + msg.error_message_));
                }
            });
        });
        msg_listener_->Listen<MsgClientVoiceCallStatus>([this](const MsgClientVoiceCallStatus& msg) {
            context_->PostUITask([this, msg]() {
                voice_call_supported_ = msg.supported_;
                voice_call_requires_headset_ = msg.requires_headset_;
                voice_call_phase_ = msg.phase_;
                if (!voice_call_btn_) {
                    return;
                }
                voice_call_btn_->setEnabled(msg.supported_);
                if (msg.phase_ == VoiceCallPhase::kIdle) {
                    voice_call_btn_->SwitchToNormalState();
                } else {
                    voice_call_btn_->SwitchToSelectedState();
                }
                QString tooltip;
                switch (msg.phase_) {
                    case VoiceCallPhase::kOutgoingPending:
                        tooltip = tcTr("id_voice_call_waiting");
                        break;
                    case VoiceCallPhase::kIncomingPending:
                        tooltip = tcTr("id_voice_call_incoming");
                        break;
                    case VoiceCallPhase::kConnected:
                        tooltip = tcTr("id_voice_call_connected");
                        break;
                    default:
                        tooltip = msg.supported_ ? tcTr("id_voice_call_start")
                                                 : tcTr("id_voice_call_unavailable");
                        break;
                }
                voice_call_btn_->setToolTip(tooltip);
                voice_call_btn_->setAccessibleName(tooltip);
                if (!msg.reason_.empty() && msg.reason_ != "local_hangup" &&
                    msg.reason_ != "remote_hangup" && msg.reason_ != "disconnect") {
                    context_->NotifyAppWarningMessage(
                        tcTr("id_voice_call"),
                        tcTr("id_voice_call_failed") + QString::fromStdString(" (" + msg.reason_ + ")"));
                }
            });
        });
        msg_listener_->Listen<SdkMsgNetworkConnected>([this](const SdkMsgNetworkConnected&) {
            context_->PostUITask([this]() {
                if (!virtual_display_ui_state_.CompleteReconnect()) {
                    return;
                }
                virtual_display_timeout_timer_->stop();
                UpdateVirtualDisplayUi();
            });
        });
    }

    void FloatControllerPanel::ToggleVoiceCall() {
        LOGI("[VoiceCall] UI toggle, phase={}, supported={}",
             static_cast<int>(voice_call_phase_), voice_call_supported_);
        if (voice_call_phase_ == VoiceCallPhase::kIdle) {
            if (voice_call_requires_headset_ && !voice_call_warning_shown_) {
                auto box = SizedMessageBox::MakeOkCancelBox(
                    tcTr("id_voice_call"), tcTr("id_voice_call_headset_warning"));
                if (box->exec() != 0) {
                    return;
                }
                voice_call_warning_shown_ = true;
            }
        }
        context_->SendAppMessage(MsgClientVoiceCallCommand {
            .action_ = voice_call_phase_ == VoiceCallPhase::kIdle
                ? MsgClientVoiceCallCommand::Action::kStart
                : MsgClientVoiceCallCommand::Action::kHangUp,
        });
    }

    void FloatControllerPanel::paintEvent(QPaintEvent *event) {
        FloatOverlayWindow::paintEvent(event);
    }

    BaseWidget* FloatControllerPanel::GetSubPanel(const SubPanelType& type) {
        if (sub_panels_.count(type) > 0) {
            return sub_panels_[type];
        }
        return nullptr;
    }

    void FloatControllerPanel::ShowSubPanel(FloatOverlayWindow* panel, QWidget* anchor) {
        if (panel && anchor) {
            panel->ShowFlyout(this, anchor, true);
        }
    }

    void FloatControllerPanel::HideAllSubPanels() {
        for (const auto& [k, v] : sub_panels_) {
            v->Hide();
        }
    }

    void FloatControllerPanel::Hide() {
        this->hide();
        this->HideAllSubPanels();
    }

    void FloatControllerPanel::UpdateCaptureMonitorInfo() {
        if (capture_monitor_.monitors_.size() > 1) {
            split_screen_btn_->show();
        }
        else {
            split_screen_btn_->hide();
        }

        int default_appropriate_icons_count = 3;
        if (capture_monitor_.monitors_.size() <= default_appropriate_icons_count) {
            SetContentSize(QSize(kInitialWidth, ContentHeight()));
        }
        else {
            SetContentSize(QSize(kInitialWidth + (capture_monitor_.monitors_.size() - default_appropriate_icons_count) * 32,
                                 ContentHeight()));
        }
        int index = 0;
        for (const auto& mon : capture_monitor_.monitors_) {
            if (index >= kMaxGameViewCount) {
                break;
            }
            auto ci = computer_icons_[index];
            ci->Show();
            ci->SetMonitorName(mon.name_);
            if (mon.name_ == capture_monitor_.capturing_monitor_name_) {
                ci->UpdateSelectedState(true);
            }
            else {
                ci->UpdateSelectedState(false);
            }
            index++;
        }
        for (auto i = capture_monitor_.monitors_.size(); i < computer_icons_.size(); i++) {
            auto ci = computer_icons_[i];
            ci->Hide();
        }

        // 当远端只有一个屏幕的时候，屏幕图标始终为选中状态
        if (1 == capture_monitor_.monitors_.size()) {
            computer_icons_[0]->UpdateSelectedState(true);
        }
    }

    void FloatControllerPanel::SwitchMonitor(ComputerIcon* w) {
        context_->SendAppMessage(MsgClientSwitchMonitor {
            .name_ = w->GetMonitorName(),
        });
    }

    void FloatControllerPanel::UpdateCapturingMonitor(const std::string& name, int cur_cap_mon_index) {
        if (kCaptureAllMonitorsSign == name) {
            context_->SendAppMessage(MsgClientMultiMonDisplayMode{
                .mode_ = EMultiMonDisplayMode::kSeparate,
            });
            for (const auto& w : computer_icons_) {
                w->UpdateSelectedState(false);
            }
            return;
        }

        context_->SendAppMessage(MsgClientMultiMonDisplayMode{
            .mode_ = EMultiMonDisplayMode::kTab,
            .current_cap_mon_index_ = cur_cap_mon_index
        });

        for (const auto& w: computer_icons_) {
            if (w->GetMonitorName() == name) {
                w->UpdateSelectedState(true);
            } else {
                w->UpdateSelectedState(false);
            }
        }
    }

    void FloatControllerPanel::CaptureAllMonitor() {
        context_->SendAppMessage(MsgClientSwitchMonitor{
           .name_ = kCaptureAllMonitorsSign,
        });
    }

    void FloatControllerPanel::UpdateStatus(const MsgClientFloatControllerPanelUpdate& msg) {
        if (MsgClientFloatControllerPanelUpdate::EUpdate::kAudioStatus == msg.update_type_) {
            auto settings = Settings::Instance();
            if (settings->IsAudioEnabled()) {
                audio_btn_->SwitchToSelectedState();
            }
            else {
                audio_btn_->SwitchToNormalState();
            }
        }
        else if (MsgClientFloatControllerPanelUpdate::EUpdate::kMediaRecordStatus == msg.update_type_) {
            bool res = context_->GetRecording();
            if (res) {
                media_record_lab_->setText(tcTr("id_stop_recording"));
            }
            else {
                media_record_lab_->setText(tcTr("id_screen_recording"));
            }
        }
    }

    void FloatControllerPanel::StartVirtualDisplayRequest(VirtualDisplayUiOperation operation) {
        const auto request_id = NextNativeVirtualDisplayRequestId(
            static_cast<uint64_t>(QCoreApplication::applicationPid()));
        if (!virtual_display_ui_state_.BeginRequest(operation, request_id)) {
            return;
        }
        UpdateVirtualDisplayUi();
        virtual_display_timeout_timer_->start(40000);
        context_->SendAppMessage(MsgClientVirtualDisplayRequest {
            .request_id_ = request_id,
            .operation_ = operation == VirtualDisplayUiOperation::kCreate
                ? kRemoteVirtualDisplayCreate
                : kRemoteVirtualDisplayRemoveLast,
        });
    }

    void FloatControllerPanel::UpdateVirtualDisplayUi() {
        auto label = tcTr("id_virtual_display") +
            QString(" (%1/%2)")
                .arg(virtual_display_ui_state_.Owned())
                .arg(virtual_display_ui_state_.Maximum());
        if (virtual_display_ui_state_.Phase() == VirtualDisplayUiPhase::kRequesting) {
            label += " · " + tcTr("id_virtual_display_processing");
        }
        else if (virtual_display_ui_state_.Phase() == VirtualDisplayUiPhase::kReconnecting) {
            label += " · " + tcTr("id_virtual_display_reconnecting");
        }
        if (virtual_display_label_) {
            virtual_display_label_->setText(label);
            virtual_display_label_->setAccessibleName(label);
        }
        if (virtual_display_add_btn_) {
            virtual_display_add_btn_->setEnabled(virtual_display_ui_state_.CanAdd());
        }
        if (virtual_display_remove_btn_) {
            virtual_display_remove_btn_->setEnabled(virtual_display_ui_state_.CanRemove());
        }
    }

    void FloatControllerPanel::SetMainControl() {
        is_main_control_ = true;
    }

    void FloatControllerPanel::SetMonitorName(const std::string& mon_name) {
        monitor_name_ = mon_name;
    }
}
