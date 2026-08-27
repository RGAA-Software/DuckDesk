//
// Created by RGAA on 23/03/2025.
//

#include "px_dialog.h"
#include "no_margin_layout.h"
#include "px_pushbutton.h"
#include "px_label.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPointer>
#include <QScreen>

namespace px
{

    TcDialog::TcDialog(const QString& title, const QString& msg, QWidget* parent) : TcCustomTitleBarDialog(title, parent) {
        constexpr auto kCompactWidth = 410;
        constexpr auto kCompactHeight = 220;
        constexpr auto kExpandedWidth = 640;
        constexpr auto kExpandedMinHeight = 320;
        constexpr auto kScreenMargin = 80;
        constexpr auto kMessageHorizontalMargins = 58;
        constexpr auto kDialogVerticalChrome = 145;
        const bool expanded = msg.count('\n') >= 2 || msg.size() >= 120;
        const auto available = QApplication::primaryScreen()->availableGeometry().size();
        const auto maximum = QSize(qMax(kCompactWidth, available.width() - kScreenMargin),
                                   qMax(kCompactHeight, available.height() - kScreenMargin));
        const auto target_width = qMin(expanded ? kExpandedWidth : kCompactWidth, maximum.width());

        root_layout_->addSpacing(20);
        {
            auto item_layout = new NoMarginHLayout();
            auto lbl_message = new TcLabel(this);
            lbl_message->setText(msg);
            lbl_message->setWordWrap(true);
            lbl_message->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            lbl_message->setStyleSheet("font-size: 15px;");
            item_layout->addSpacing(29);
            item_layout->addWidget(lbl_message);
            item_layout->addSpacing(29);
            root_layout_->addLayout(item_layout);

            const auto text_width = qMax(1, target_width - kMessageHorizontalMargins);
            const auto text_bounds = QFontMetrics(lbl_message->font()).boundingRect(
                QRect(0, 0, text_width, maximum.height()),
                Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, msg);
            const auto target_height = qMin(
                qMax(expanded ? kExpandedMinHeight : kCompactHeight,
                     text_bounds.height() + kDialogVerticalChrome),
                maximum.height());
            if (expanded) {
                setMinimumSize(target_width, target_height);
                setMaximumSize(maximum);
                resize(target_width, target_height);
            }
            else {
                setFixedSize(target_width, target_height);
            }
        }
        root_layout_->addStretch();
        {
            auto item_layout = new NoMarginHLayout();
            auto btn_size = QSize(90, 28);
            item_layout->addStretch();
            auto btn_cancel = new TcPushButton(this);
            btn_cancel->SetTextId("id_cancel");
            btn_cancel->setProperty("class", "danger");
            btn_cancel->setFixedSize(btn_size);
            item_layout->addWidget(btn_cancel);
            item_layout->addSpacing(20);

            const QPointer<TcDialog> guarded_self(this);
            connect(btn_cancel, &QPushButton::clicked, this, [guarded_self]() {
                if (guarded_self) guarded_self->done(kDoneCancel);
            });

            auto btn_ok = new TcPushButton(this);
            btn_ok->SetTextId("id_ok");
            btn_ok->setFixedSize(btn_size);
            item_layout->addWidget(btn_ok);
            item_layout->addStretch();

            connect(btn_ok, &QPushButton::clicked, this, [guarded_self]() {
                if (guarded_self) guarded_self->done(kDoneOk);
            });

            root_layout_->addLayout(item_layout);
        }
        root_layout_->addSpacing(30);

    }

}
