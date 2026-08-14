//
// Created by RGAA on 1/04/2025.
//

#include "tc_tooltip.h"
#include "widget_helper.h"
#include "no_margin_layout.h"
#include "tc_label.h"

namespace tc
{

    TcToolTip::TcToolTip(QWidget* parent) : QWidget(parent) {
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint | Qt::Tool);
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_TranslucentBackground);
        WidgetHelper::AddShadow(this, 0xaaaaaa, 4);
        auto root_layout = new NoMarginVLayout();
        root_layout->addStretch();

        auto content_layout = new NoMarginHLayout();
        auto label = new QLabel(this);
        label_ = label;
        label->setStyleSheet(R"(color: #333333; font-weight: 500; font-size:12px;)");
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft);
        content_layout->addSpacing(15);
        content_layout->addWidget(label);
        content_layout->addSpacing(15);

        root_layout->addLayout(content_layout);
        root_layout->addStretch();

        setLayout(root_layout);
    }

    void TcToolTip::paintEvent(QPaintEvent *event) {
        QPainter painter(this);
        auto offset = 5;
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QColor(0xeeeeee));
        painter.setBrush(QBrush(QColor(0xffffff)));
        painter.drawRoundedRect(QRect(offset, offset, this->width()-offset*2, this->height()-offset*2), 3, 3);
        //QWidget::paintEvent(event);
    }

    void TcToolTip::SetText(const QString &text) {
        label_->setText(text);
        repaint();
    }

}
