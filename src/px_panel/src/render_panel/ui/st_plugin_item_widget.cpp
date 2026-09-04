//
// Created by RGAA on 30/04/2025.
//

#include "st_plugin_item_widget.h"
#include "no_margin_layout.h"
#include "st_plugins.h"
#include "px_render_panel_message.pb.h"
#include "render_panel/px_application.h"
#include "render_panel/ui/qt_lifetime_guard.h"
#include "px_message_new/rp_proto_converter.h"
#include "px_render/modules/module_ids.h"
#include <QPointer>
#include <QLabel>
#include <QPushButton>

namespace px
{

    const QString kDisplayPluginEnabled = "Enabled";
    const QString kDisplayPluginDisabled = "Disabled";

    StPluginItemWidget::StPluginItemWidget(const std::shared_ptr<PxApplication>& app,
                       const std::shared_ptr<PluginItemInfo>& item_info,
                       int index,
                       QWidget* parent) : QWidget(parent) {
        app_ = app;
        item_info_ = item_info;
        const QPointer<StPluginItemWidget> self(this);

        this->setObjectName("StPluginItemWidget");
        this->setStyleSheet("#StPluginItemWidget {background:#ffffffff;}");

        auto root_layout = new NoMarginVLayout();

        auto content_layout = new NoMarginHLayout();
        root_layout->addStretch();
        root_layout->addLayout(content_layout);
        root_layout->addStretch();

        content_layout->addSpacing(20);

        {
            auto lbl = new QLabel(this);
            lbl->setStyleSheet(R"(font-weight: 700;)");
            lbl->setFixedWidth(20);
            lbl->setText(std::to_string(index+1).c_str());
            content_layout->addWidget(lbl);
            content_layout->addSpacing(20);
        }

        // icon
        {
            auto icon = new QLabel(this);
            icon->setFixedSize(30, 30);
            QString style = R"(background-image: url(:resources/image/ic_plugin.svg);
                        background-repeat: no-repeat;
                        background-position: center;
                    )";
            icon->setStyleSheet(style);
            content_layout->addWidget(icon);
            content_layout->addSpacing(20);
        }

        {
            auto lbl = new QLabel(this);
            lbl->setStyleSheet(R"(font-weight: 700;)");
            lbl->setFixedWidth(180);
            lbl->setText(item_info->info_->name().c_str());
            content_layout->addWidget(lbl);
            content_layout->addSpacing(20);
        }

        {
            auto lbl = new QLabel(this);
            lbl->setStyleSheet(R"(font-size: 12px;)");
            lbl->setFixedWidth(400);
            lbl->setText(item_info->info_->desc().c_str());
            content_layout->addWidget(lbl);
            content_layout->addSpacing(20);
        }

        {
            auto lbl = new QLabel(this);
            lbl_enabled_ = lbl;
            lbl->setFixedWidth(120);
            content_layout->addWidget(lbl);
            content_layout->addSpacing(20);
            UpdatePluginStatus(item_info->info_->enabled());
        }

        content_layout->addStretch();

        auto size = QSize(75, 30);
        {
            auto btn = new QPushButton(this);
            btn->setProperty("class", "danger");
            btn->setFixedSize(size);
            btn->setText("Disable");
            content_layout->addWidget(btn);
            content_layout->addSpacing(10);
            connect(btn, &QPushButton::clicked, this,
                    MakeQtLifetimeAction(
                        self,
                        [](const QPointer<StPluginItemWidget>& item) {
                            item->SwitchPluginStatusInner(false);
                        }));
        }
        {
            auto btn = new QPushButton(this);
            btn->setFixedSize(size);
            btn->setText("Enable");
            content_layout->addWidget(btn);
            content_layout->addSpacing(10);
            connect(btn, &QPushButton::clicked, this,
                    MakeQtLifetimeAction(
                        self,
                        [](const QPointer<StPluginItemWidget>& item) {
                            item->SwitchPluginStatusInner(true);
                        }));
        }

        // server-side screen recording: 只有 media_recorder 插件显示录制按钮
        if (item_info_->id_ == kMediaRecorderPluginId) {
            auto btn = new QPushButton(this);
            btn->setFixedSize(size);
            btn->setText("Start Record");
            content_layout->addWidget(btn);
            content_layout->addSpacing(10);
            const QPointer<QPushButton> button(btn);
            connect(btn, &QPushButton::clicked, this,
                    MakeQtLifetimeAction(
                        self,
                        [button](const QPointer<StPluginItemWidget>& item) {
                            if (!button) {
                                return;
                            }
                            const bool start = button->text() == "Start Record";
                            button->setText(start ? "Stop Record" : "Start Record");
                            item->SendRecordCommand(start);
                        }));
        }

        content_layout->addSpacing(10);

        setLayout(root_layout);
    }

    void StPluginItemWidget::paintEvent(QPaintEvent *event) {
        QPainter painter(this);
        QPen pen;
        pen.setStyle(Qt::PenStyle::DashDotDotLine);
        pen.setColor(0xdddddd);
        painter.setPen(pen);
        int offset = 3;
        painter.drawRoundedRect(QRect(offset, offset, this->width()-offset*2, this->height()-offset*2), 5, 5);

        QWidget::paintEvent(event);
    }

    void StPluginItemWidget::UpdateStatus() {
        if (!lbl_enabled_) {
            return;
        }

        bool need_update = false;
        auto enabled = item_info_->info_->enabled();
        if (enabled) {
            if (lbl_enabled_->text() != kDisplayPluginEnabled) {
                need_update = true;
            }
        }
        else {
            if (lbl_enabled_->text() != kDisplayPluginDisabled) {
                need_update = true;
            }
        }
        if (need_update) {
            UpdatePluginStatus(enabled);
        }

    }

    void StPluginItemWidget::UpdatePluginStatus(bool enabled) {
        if (enabled) {
            lbl_enabled_->setText(kDisplayPluginEnabled);
            lbl_enabled_->setStyleSheet(R"(font-weight: 700; color: #555555;)");
        }
        else {
            lbl_enabled_->setText(kDisplayPluginDisabled);
            lbl_enabled_->setStyleSheet(R"(font-weight: 700; color: #ff2200;)");
        }
    }

    void StPluginItemWidget::SwitchPluginStatusInner(bool enabled) {
        pxrp::RpMessage pt_msg;
        pt_msg.set_type(pxrp::RpMessageType::kRpCommandRenderer);
        auto sub = pt_msg.mutable_command_renderer();
        sub->set_command(enabled ? pxrp::RpPanelCommand::kEnablePlugin : pxrp::RpPanelCommand::kDisablePlugin);
        sub->set_plugin_id(item_info_->id_);
        app_->PostMessage2Renderer(px::RpProtoAsData(&pt_msg));
    }

    void StPluginItemWidget::SendRecordCommand(bool start) {
        pxrp::RpMessage pt_msg;
        pt_msg.set_type(pxrp::RpMessageType::kRpCommandRenderer);
        auto sub = pt_msg.mutable_command_renderer();
        sub->set_command(start ? pxrp::RpPanelCommand::kStartMediaRecordServerSide
                               : pxrp::RpPanelCommand::kStopMediaRecordServerSide);
        sub->set_plugin_id(item_info_->id_);
        app_->PostMessage2Renderer(px::RpProtoAsData(&pt_msg));
    }

}
