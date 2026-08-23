//
// Created by RGAA on 17/08/2024.
//

#include "float_3rd_scale_panel.h"
#include "no_margin_layout.h"
#include "switch_button.h"
#include "background_widget.h"
#include "px_client/ct_settings.h"
#include "px_client/ct_client_context.h"
#include "px_client/ct_app_message.h"
#include "single_selected_list.h"
#include "px_label.h"
#include <memory>

namespace px
{

    ThirdScalePanel::ThirdScalePanel(const std::shared_ptr<ClientContext>& ctx, QWidget* parent)
        : FloatOverlayWindow(ctx, parent, QSize(210, 140)) {
        auto root_layout = new NoMarginVLayout();
        int offset = 5;
        root_layout->setContentsMargins(kShadowMargin + offset, kShadowMargin + offset,
                                        kShadowMargin + offset, kShadowMargin + offset);

        settings_ = Settings::Instance();

        listview_ = new SingleSelectedList(this);
        listview_->setFixedSize(QSize(ContentWidth() - 2*offset, ContentHeight() - 2*offset));
        listview_->UpdateItems({
            std::make_shared<SingleItem>(SingleItem {
                   .name_ = tcTr("id_keep_aspect_ratio"),
                   .icon_path_ = "",
            }),
            std::make_shared<SingleItem>(SingleItem {
                   .name_ = tcTr("id_full_window"),
                   .icon_path_ = "",
            }),
            // std::make_shared<SingleItem>(SingleItem {
            //        .name_ = "Original Size",
            //        .icon_path_ = "",
            // }),
        });
        root_layout->addWidget(listview_);
        setLayout(root_layout);
        UpdateStatus(MsgClientFloatControllerPanelUpdate{
            .update_type_ = MsgClientFloatControllerPanelUpdate::EUpdate::kImageScaleMode
        });

        listview_->SetOnItemClickListener([=, this](int idx, QWidget* w) {
            ScaleMode mode = ScaleMode::kFillWindow;
            if (idx == 0) { 
                mode = ScaleMode::kKeepAspectRatio;
            } else if (idx == 1) { 
                mode = ScaleMode::kFillWindow;
            } else if (idx == 2) { 
                mode = ScaleMode::kOriginSize; 
            }
            UpdateScaleMode(mode);
            MsgClientSwitchScaleMode scale_mode_msg{.mode_ = mode};
            context_->SendAppMessage(scale_mode_msg);
            context_->SendAppMessage(MsgClientFloatControllerPanelUpdate{
                .update_type_ = MsgClientFloatControllerPanelUpdate ::EUpdate::kImageScaleMode
            });
        });
    }

    void ThirdScalePanel::paintEvent(QPaintEvent *event) {
        FloatOverlayWindow::paintEvent(event);
    }

    void ThirdScalePanel::UpdateScaleMode(ScaleMode mode) {
        settings_->SetScaleMode(mode);
    }

    void ThirdScalePanel::UpdateStatus(const MsgClientFloatControllerPanelUpdate& msg) {
        if (MsgClientFloatControllerPanelUpdate::EUpdate::kImageScaleMode == msg.update_type_) {
            int target_index = 0;
            if (ScaleMode::kKeepAspectRatio == settings_->scale_mode_) {
                target_index = 0;
            }
            else if (ScaleMode::kFillWindow == settings_->scale_mode_) {
                target_index = 1;
            }
            else if (ScaleMode::kOriginSize == settings_->scale_mode_) {
                target_index = 2;
            }
            listview_->Select(target_index);
        }
    }
}
