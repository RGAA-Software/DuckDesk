//
// Created by RGAA on 17/08/2024.
//

#include "float_sub_display_panel.h"
#include "no_margin_layout.h"
#include "switch_button.h"
#include "background_widget.h"
#include "px_client/ct_settings.h"
#include "float_3rd_scale_panel.h"
#include "float_3rd_resolution_panel.h"
#include "px_client/ct_client_context.h"
#include "px_common_new/log.h"
#include "float_sub_fps_panel.h"
#include "px_label.h"
#include <QLabel>
#include <QTimer>

namespace px
{

    SubDisplayPanel::SubDisplayPanel(const std::shared_ptr<ClientContext>& ctx, QWidget* parent)
        : FloatOverlayWindow(ctx, parent, QSize(210, 183)) {
        int offset = 5;
        auto item_height = 38;
        int border_spacing = 10;
        auto item_size = QSize(ContentWidth() - 2*offset, item_height);
        auto root_layout = new NoMarginVLayout();
        root_layout->setContentsMargins(kShadowMargin + offset, kShadowMargin + offset,
                                        kShadowMargin + offset, kShadowMargin + offset);
        auto icon_size = QSize(40, 40);
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setLayout(layout);
            widget->setFixedSize(item_size);
            layout->addWidget(widget);

            auto lbl = new TcLabel();
            lbl->SetTextId("id_scale");
            lbl->setStyleSheet(R"(font-weight:bold;)");
            layout->addSpacing(border_spacing*2);
            layout->addWidget(lbl);

            layout->addStretch();

            auto icon_right = new QLabel(this);
            icon_right->setFixedSize(icon_size);
            icon_right->setStyleSheet(R"( background-image: url(:resources/image/ic_arrow_right_2.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addWidget(icon_right);
            layout->addSpacing(border_spacing);
            layout->addSpacing(border_spacing);

            root_layout->addSpacing(5);
            root_layout->addWidget(widget);
            //
            widget->SetOnClickListener([=, this](auto w) {
                auto panel = GetSubPanel(SubDisplayType::kScale);
                if (!panel) {
                    panel = (BaseWidget*)(new ThirdScalePanel(ctx, OverlayOwner()));
                    sub_panels_[SubDisplayType::kScale] = panel;
                }
                HideAllSubPanels();
                ShowSubPanel(static_cast<FloatOverlayWindow*>(panel), w);
            });
        }
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setLayout(layout);
            widget->setFixedSize(item_size);
            layout->addWidget(widget);

            auto lbl = new TcLabel();
            lbl->SetTextId("id_resolution");
            lbl->setStyleSheet(R"(font-weight:bold;)");
            layout->addSpacing(border_spacing*2);
            layout->addWidget(lbl);

            layout->addStretch();

            auto icon_right = new QLabel(this);
            icon_right->setFixedSize(icon_size);
            icon_right->setStyleSheet(R"( background-image: url(:resources/image/ic_arrow_right_2.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addWidget(icon_right);
            layout->addSpacing(border_spacing);
            layout->addSpacing(border_spacing);

            root_layout->addSpacing(5);
            root_layout->addWidget(widget);

            widget->SetOnClickListener([=, this](auto w) {
                auto panel = GetSubPanel(SubDisplayType::kResolution);
                if (!panel) {
                    panel = (BaseWidget*)(new ThirdResolutionPanel(ctx, OverlayOwner()));
                    sub_panels_[SubDisplayType::kResolution] = panel;
                }
                HideAllSubPanels();
                if (cap_monitors_info_.monitors_.empty()) {
                    LOGE("Error monitor index, can not get MsgClientCaptureMonitor.");
                    context_->PostUITask([=, this]() {
                        context_->NotifyAppWarningMessage(tcTr("id_warning"), tcTr("id_modify_display_settings_of_controlled"));
                    });
                    return;
                }
                //auto capture_monitor = cap_monitors_info_.GetCaptureMonitorByName(capturing_monitor_name);
                auto capture_monitor = cap_monitors_info_.GetCaptureMonitorByName(capture_monitor_name_);
                if (capture_monitor.IsValid()) {
                    ((ThirdResolutionPanel *) panel)->UpdateMonitor(capture_monitor);
                    ShowSubPanel(static_cast<FloatOverlayWindow*>(panel), w);
                }
            });

        }

        //全彩模式
        {
            auto layout = new NoMarginHLayout();
            auto widget = new QWidget(this);
            widget->setLayout(layout);
            widget->setFixedSize(item_size);
            layout->addWidget(widget);

            auto lbl = new TcLabel();
            lbl->SetTextId("id_full_color");
            lbl->setStyleSheet(R"(font-weight:bold;)");
            layout->addSpacing(border_spacing * 2);
            layout->addWidget(lbl);

            layout->addStretch();

            auto sb = new SwitchButton(this);
            full_color_btn_ = sb;
            sb->setFixedSize(35, 20);
            sb->SetStatus(Settings::Instance()->IsFullColorEnabled());
            layout->addWidget(sb);

            auto timer = new QTimer(this);
            connect(timer, &QTimer::timeout, [=]() {
                sb->setEnabled(true);
            });
            timer->setInterval(3000);
            timer->setSingleShot(true);

            sb->SetClickCallback([=, this](bool enabled) {
                sb->setEnabled(false);
                timer->start();
                context_->SendAppMessage(MsgClientSwitchFullColor {
                  .enable_ = enabled,
                });
            });

            layout->addSpacing(border_spacing);

            root_layout->addSpacing(5);
            root_layout->addWidget(widget);
        }

        // fps set
        {
            auto layout = new NoMarginHLayout();
            auto widget = new BackgroundWidget(ctx, this);
            widget->setLayout(layout);
            widget->setFixedSize(item_size);
            layout->addWidget(widget);

            auto lbl = new TcLabel();
            lbl->SetTextId("id_fps");
            lbl->setStyleSheet(R"(font-weight:bold;)");
            layout->addSpacing(border_spacing * 2);
            layout->addWidget(lbl);

            layout->addStretch();

            auto icon_right = new QLabel(this);
            icon_right->setFixedSize(icon_size);
            icon_right->setStyleSheet(R"( background-image: url(:resources/image/ic_arrow_right_2.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
            layout->addWidget(icon_right);
            layout->addSpacing(border_spacing);
            layout->addSpacing(border_spacing);

            root_layout->addSpacing(5);
            root_layout->addWidget(widget);

            widget->SetOnClickListener([=, this](auto w) {
                auto panel = GetSubPanel(SubDisplayType::kFps);
                if (!panel) {
                    panel = (BaseWidget*)(new SubFpsPanel(ctx, OverlayOwner()));
                    sub_panels_[SubDisplayType::kFps] = panel;
                }
                HideAllSubPanels();
                ShowSubPanel(static_cast<FloatOverlayWindow*>(panel), w);
            });
        }

        root_layout->addStretch();
        setLayout(root_layout);

        msg_listener_ = context_->ObtainMessageListener();
        msg_listener_->Listen<MsgClientFullscreen>([=, this](const MsgClientFullscreen& msg) {
            HideAllSubPanels();
        });
    }

    void SubDisplayPanel::paintEvent(QPaintEvent *event) {
        FloatOverlayWindow::paintEvent(event);
    }

    BaseWidget* SubDisplayPanel::GetSubPanel(const SubDisplayType& type) {
        if (sub_panels_.count(type) > 0) {
            return sub_panels_[type];
        }
        return nullptr;
    }

    void SubDisplayPanel::ShowSubPanel(FloatOverlayWindow* panel, QWidget* anchor) {
        if (panel && anchor) {
            panel->ShowFlyout(this, anchor, true);
        }
    }

    void SubDisplayPanel::HideAllSubPanels() {
        for (const auto& [k, v] : sub_panels_) {
            v->Hide();
        }
    }

    void SubDisplayPanel::Show() {
        BaseWidget::Show();
    }

    void SubDisplayPanel::Hide() {
        BaseWidget::Hide();
        HideAllSubPanels();
    }

    void SubDisplayPanel::UpdateMonitorInfo(const MsgClientCaptureMonitor& m) {
        cap_monitors_info_ = m;
        // sort it
        for (auto& monitor : cap_monitors_info_.monitors_) {
            std::sort(monitor.resolutions_.begin(), monitor.resolutions_.end(), [](const auto& left, const auto& right) {
                return left.width_ > right.width_;
            });
        }

        //将当前分辨率同步给分辨率面板,这样才会更新显示出正确的当前分辨率
        auto panel = GetSubPanel(SubDisplayType::kResolution);
        if (!panel) {
            panel = (BaseWidget*)(new ThirdResolutionPanel(context_, OverlayOwner()));
            sub_panels_[SubDisplayType::kResolution] = panel;
            panel->Hide();
        }
        if (cap_monitors_info_.monitors_.empty()) {
            LOGE("Error monitor index, can not get MsgClientCaptureMonitor.");
            return;
        }
        auto capture_monitor = cap_monitors_info_.GetCaptureMonitorByName(capture_monitor_name_);
        if (capture_monitor.IsValid()) {
            ((ThirdResolutionPanel*)panel)->UpdateMonitor(capture_monitor);
        }
    }

    void SubDisplayPanel::SetCaptureMonitorName(const std::string& name) {
        capture_monitor_name_ = name;
    }

    void SubDisplayPanel::UpdateStatus(const MsgClientFloatControllerPanelUpdate& msg) {
        if (MsgClientFloatControllerPanelUpdate::EUpdate::kFullColorStatus == msg.update_type_) {
            full_color_btn_->SetStatus(Settings::Instance()->IsFullColorEnabled());
        }
    }

}
