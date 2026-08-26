//
// Created by RGAA on 17/08/2024.
//

#include "float_3rd_resolution_panel.h"
#include "no_margin_layout.h"
#include "switch_button.h"
#include "background_widget.h"
#include "px_client/ct_settings.h"
#include "px_client/ct_client_context.h"
#include "px_client/ct_app_message.h"
#include "single_selected_list.h"
#include "px_common_new/log.h"
#include "px_common_new/message_notifier.h"
#include <QLabel>
#include <QPointer>
#include <format>

namespace px
{

    ThirdResolutionPanel::ThirdResolutionPanel(const std::shared_ptr<ClientContext>& ctx, QWidget* parent)
        : FloatOverlayWindow(ctx, parent, QSize(210, 360)) {
        int offset = 5;
        auto item_height = 35;
        auto border_spacing = 10;
        auto item_size = QSize(this->width(), item_height);
        auto root_layout = new NoMarginVLayout();
        root_layout->setContentsMargins(kShadowMargin + offset, kShadowMargin + offset,
                                        kShadowMargin + offset, kShadowMargin + offset);
        settings_ = Settings::Instance();

        listview_ = new SingleSelectedList(this);
        listview_->setFixedSize(QSize(ContentWidth() - 2*offset, ContentHeight()-2*offset));
        const QPointer<ThirdResolutionPanel> guarded_self(this);

        listview_->SetOnItemClickListener([guarded_self](int idx, auto) {
            if (!guarded_self) {
                return;
            }
            auto item = guarded_self->listview_->GetItems().at(idx);
            auto split_size = item->name_.split("x");
            if (split_size.size() < 2) {
                return;
            }
            int width = split_size.at(0).toInt();
            int height = split_size.at(1).toInt();
            if (width <= 0 || height <= 0) {
                LOGE("Error monitor resolution size: {}", item->name_.toStdString());
                return;
            }

            auto monitor_name = guarded_self->monitor_.name_;
            if (monitor_name.empty()) {
                LOGE("Target monitor name is empty");
                return;
            }
            guarded_self->context_->SendAppMessage(MsgClientChangeMonitorResolution{
                .monitor_name_ = monitor_name,
                .width_ = width,
                .height_ = height,
            });
        });

        root_layout->addWidget(listview_);
        root_layout->addStretch();
        setLayout(root_layout);

        msg_listener_->Listen<MsgClientMonitorChanged>([guarded_self](const MsgClientMonitorChanged&) {
            if (guarded_self) {
                guarded_self->context_->PostUITask([guarded_self]() {
                    if (guarded_self) {
                        guarded_self->SelectCapturingMonitorSize();
                    }
                });
            }
        });
    }

    void ThirdResolutionPanel::paintEvent(QPaintEvent *event) {
        FloatOverlayWindow::paintEvent(event);
    }

    void ThirdResolutionPanel::Hide() {
        BaseWidget::Hide();
    }

    void ThirdResolutionPanel::Show() {
        BaseWidget::Show();
        this->SelectCapturingMonitorSize();
    }

    void ThirdResolutionPanel::SelectCapturingMonitorSize() {
        auto res_name = std::format("{}x{}", monitor_.current_width_, monitor_.current_height_);
        listview_->SelectByName(res_name);
        LOGI("Capturing monitor name is {}, size is {}x{}", monitor_.name_, monitor_.current_width_, monitor_.current_height_);
    }

    void ThirdResolutionPanel::UpdateMonitor(const MsgClientCaptureMonitor::CaptureMonitor& m) {
        monitor_ = m;
        std::vector<SingleItemPtr> items;
        for (const auto& res : monitor_.resolutions_) {
            items.push_back(std::make_shared<SingleItem>(SingleItem { .name_ = std::format("{}x{}", res.width_, res.height_).c_str() }));
        }
        listview_->UpdateItems(items);

        SelectCapturingMonitorSize();
    }

}
