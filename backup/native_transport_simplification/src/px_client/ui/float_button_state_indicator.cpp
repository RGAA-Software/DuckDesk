//
// Created by RGAA on 2/09/2024.
//

#include "float_button_state_indicator.h"
#include "key_state_panel.h"
#include "no_margin_layout.h"
#include "px_client/ct_client_context.h"
#include <QPointer>

namespace px
{

    FloatButtonStateIndicator::FloatButtonStateIndicator(const std::shared_ptr<ClientContext>& ctx, QWidget* parent)
        : BaseWidget(ctx, parent) {
        this->setWindowFlags(Qt::FramelessWindowHint);
        this->setStyleSheet("background:#00000000;");
        auto layout = new NoMarginHLayout();
        auto size = QSize(60, 25);
        {
            auto item = new KeyItem("ALT");
            item->setFixedSize(size);
            alt_item_ = item;
            layout->addWidget(item);
        }
        {
            auto item = new KeyItem("SHIFT");
            item->setFixedSize(size);
            shift_item_ = item;
            layout->addWidget(item);
        }
        {
            auto item = new KeyItem("CTRL");
            item->setFixedSize(size);
            control_item_ = item;
            layout->addWidget(item);
        }
        {
            auto item = new KeyItem("WIN");
            item->setFixedSize(size);
            win_item_ = item;
            layout->addWidget(item);
        }
        setLayout(layout);
    }

    void FloatButtonStateIndicator::UpdateOnHeartBeat(std::shared_ptr<px::Message> msg) {
        const QPointer<FloatButtonStateIndicator> guarded_self(this);
        context_->PostUITask([guarded_self, msg = std::move(msg)]() {
            if (!guarded_self || !msg) {
                return;
            }
            auto hb = msg->on_heartbeat();
            if (!hb.alt_pressed() && !hb.shift_pressed() && !hb.control_pressed() && !hb.win_pressed()) {
                guarded_self->hide();
            } else {
                guarded_self->show();
            }
            if (hb.alt_pressed()) {
                guarded_self->alt_item_->show();
            } else {
                guarded_self->alt_item_->hide();
            }
            if (hb.shift_pressed()) {
                guarded_self->shift_item_->show();
            } else {
                guarded_self->shift_item_->hide();
            }
            if (hb.control_pressed()) {
                guarded_self->control_item_->show();
            } else {
                guarded_self->control_item_->hide();
            }
            if (hb.win_pressed()) {
                guarded_self->win_item_->show();
            } else {
                guarded_self->win_item_->hide();
            }
            guarded_self->alt_item_->UpdateState(hb.alt_pressed());
            guarded_self->shift_item_->UpdateState(hb.shift_pressed());
            guarded_self->control_item_->UpdateState(hb.control_pressed());
            guarded_self->win_item_->UpdateState(hb.win_pressed());

            guarded_self->setFixedSize(QSize(
                guarded_self->alt_item_->width() * guarded_self->GetPressedCount(),
                guarded_self->alt_item_->height()));
        });
    }

    void FloatButtonStateIndicator::paintEvent(QPaintEvent *event) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xffffff));
        int offset = 0;
        int radius = 5;
        painter.drawRoundedRect(offset, offset, this->width()-offset*2, this->height()-offset*2, radius, radius);
        BaseWidget::paintEvent(event);
    }

    int FloatButtonStateIndicator::GetPressedCount() {
        int count = 0;
        if (alt_item_->IsPressed()) {
            count++;
        }
        if (shift_item_->IsPressed()) {
            count++;
        }
        if (control_item_->IsPressed()) {
            count++;
        }
        if (win_item_->IsPressed()) {
            count++;
        }
        return count;
    }

}
