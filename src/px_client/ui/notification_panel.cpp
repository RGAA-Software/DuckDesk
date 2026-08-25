//
// Created by RGAA on 6/07/2024.
//

#include "notification_panel.h"
#include "no_margin_layout.h"
#include "notification_item.h"
#include "px_client/ct_client_context.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/log.h"

#include <QGraphicsDropShadowEffect>
#include <QPushButton>

namespace px
{

    NotificationPanel::NotificationPanel(const std::shared_ptr<ClientContext>& ctx, QWidget* parent) : BaseWidget(ctx, parent) {
        this->setObjectName("notification_panel");
        this->setStyleSheet(R"(#notification_panel {background-color:#00000000;}")");
        this->setFixedWidth(350);
        auto ps = new QGraphicsDropShadowEffect();
        ps->setBlurRadius(15);
        ps->setOffset(0, 0);
        ps->setColor(0x999999);
        this->setGraphicsEffect(ps);

        list_ = new QListWidget(this);
        list_->setResizeMode(QListView::ResizeMode::Fixed);
        list_->setMovement(QListView::Movement::Static);
        list_->setFlow(QListView::Flow::TopToBottom);
        list_->setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
        list_->setStyleSheet("QListWidget {background: #ffffff;}"
                             "QListWidget::item:selected { background-color: transparent; }");

        auto root_layout = new NoMarginVLayout();
        {
            auto layout = new NoMarginHLayout();
            layout->addStretch();
            auto btn = new QPushButton(this);
            btn->setFixedSize(170, 35);
            btn->setText(tr("Clear Notifications"));
            btn->setStyleSheet("font-size: 12px;");
            btn->setProperty("flat", true);
            btn->setProperty("class", "danger");
            connect(btn, &QPushButton::clicked, this, [=, this]() {
                this->ClearCompletedNotifications();
            });
            layout->addWidget(btn);
            layout->addSpacing(5);
            root_layout->addSpacing(5);
            root_layout->addLayout(layout);
        }
        root_layout->addWidget(list_);
        setLayout(root_layout);

        msg_listener_ = context_->ObtainUIMessageListener();
    }

    void NotificationPanel::paintEvent(QPaintEvent *event) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(0xffffff));
        int border_radius = 7;
        painter.drawRoundedRect(this->rect(), border_radius, border_radius);
    }

    void NotificationPanel::ClearCompletedNotifications() {
        auto it = notifications_.begin();
        while (it != notifications_.end()) {
            auto widget = (*it).second;
            if (widget->IsProgressOver()) {
                it = notifications_.erase(it);
                list_->removeItemWidget(widget->BelongToItem());
            } else {
                it++;
            }
        }
    }

}
