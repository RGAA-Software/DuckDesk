//
// Created by RGAA on 17/08/2024.
//

#include "float_sub_mode_panel.h"
#include "no_margin_layout.h"
#include "switch_button.h"
#include "background_widget.h"
#include "px_client/ct_settings.h"
#include "px_client/ct_client_context.h"
#include "px_client/ct_app_message.h"
#include "px_common_new/log.h"
#include <QLabel>

namespace px
{

    SubModePanel::SubModePanel(const std::shared_ptr<ClientContext>& ctx, QWidget* parent)
        : FloatOverlayWindow(ctx, parent, QSize(210, 138)) {
        int offset = 5;
        auto item_height = 38;
        auto border_spacing = 10;
        auto item_size = QSize(ContentWidth() - 2*offset, item_height);
        auto root_layout = new NoMarginVLayout();
        root_layout->setContentsMargins(kShadowMargin + offset, kShadowMargin + offset,
                                        kShadowMargin + offset, kShadowMargin + offset);

        settings_ = Settings::Instance();

        {
            auto layout = new NoMarginHLayout();
            auto widget = new QWidget(this);
            widget->setLayout(layout);
            widget->setFixedSize(item_size);
            layout->addWidget(widget);

            auto lbl = new QLabel();
            lbl->setText(tr("Work Mode"));
            lbl->setStyleSheet(R"(font-weight:bold;)");
            layout->addSpacing(border_spacing*2);
            layout->addWidget(lbl);

            layout->addStretch();

            auto sb = new SwitchButton(this);
            sb_work_ = sb;
            sb->setFixedSize(35, 20);
            sb->SetStatus(settings_->work_mode_ == SwitchWorkMode::kWork);
            layout->addWidget(sb);
            sb->SetClickCallback([=, this](bool enabled) {
                SwitchWorkMode::WorkMode mode = enabled ? SwitchWorkMode::kWork : SwitchWorkMode::kGame;
                settings_->SetWorkMode(mode);
                sb_game_->SetStatus(!enabled);
                context_->SendAppMessage(MsgClientSwitchWorkMode {
                    .mode_ = mode,
                });
                context_->SendAppMessage(MsgClientFloatControllerPanelUpdate{.update_type_ = MsgClientFloatControllerPanelUpdate::EUpdate::kWorkMode});
            });

            layout->addSpacing(border_spacing);

            root_layout->addSpacing(5);
            root_layout->addWidget(widget);
        }
        {
            auto layout = new NoMarginHLayout();
            auto widget = new QWidget(this);
            widget->setLayout(layout);
            widget->setFixedSize(item_size);
            layout->addWidget(widget);

            auto lbl = new QLabel();
            lbl->setText(tr("Game Mode"));
            lbl->setStyleSheet(R"(font-weight:bold;)");
            layout->addSpacing(border_spacing*2);
            layout->addWidget(lbl);

            layout->addStretch();

            auto sb = new SwitchButton(this);
            sb_game_ = sb;
            sb->setFixedSize(35, 20);
            sb->SetStatus(settings_->work_mode_ == SwitchWorkMode::kGame);
            layout->addWidget(sb);
            sb->SetClickCallback([=, this](bool enabled) {
                SwitchWorkMode::WorkMode mode = enabled ? SwitchWorkMode::kGame : SwitchWorkMode::kWork;
                settings_->SetWorkMode(mode);
                sb_work_->SetStatus(!enabled);
                context_->SendAppMessage(MsgClientSwitchWorkMode {
                    .mode_ = mode,
                });
                context_->SendAppMessage(MsgClientFloatControllerPanelUpdate{ .update_type_ = MsgClientFloatControllerPanelUpdate::EUpdate::kWorkMode });
            });

            layout->addSpacing(border_spacing);

            root_layout->addSpacing(5);
            root_layout->addWidget(widget);
        }

        root_layout->addStretch();
        setLayout(root_layout);
    }

    void SubModePanel::paintEvent(QPaintEvent *event) {
        FloatOverlayWindow::paintEvent(event);
    }

    void SubModePanel::UpdateStatus(const MsgClientFloatControllerPanelUpdate& msg) {
        if (MsgClientFloatControllerPanelUpdate::EUpdate::kWorkMode == msg.update_type_) {
            sb_game_->SetStatus(SwitchWorkMode::kGame == settings_->work_mode_);
            sb_work_->SetStatus(SwitchWorkMode::kWork == settings_->work_mode_);
        }
    }
}
