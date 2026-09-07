#include "media_record_sign_lab.h"
#include <QPointer>
#include <QTimer>
#include <qfont.h>
#include "ct_client_context.h"
#include "px_common/log.h"
#include "px_common/message_notifier.h"
#include "px_client_sdk/sdk_messages.h"

namespace px
{
    MediaRecordSignLab::MediaRecordSignLab(const std::shared_ptr<ClientContext>& context, QWidget* parent) : QWidget(parent) {
        context_ = context;
        setFixedSize(42, 24);
        setAttribute(Qt::WA_StyledBackground, true);
        this->setStyleSheet("background:#FFFFFFFF;");
        listener_ = context_->ObtainUIMessageListener();
        const QPointer<MediaRecordSignLab> guarded_self(this);
        listener_->Listen<SdkMsgTimer1000>([guarded_self](const SdkMsgTimer1000&) {
            if (!guarded_self || guarded_self->isHidden()) {
                return;
            }
            guarded_self->context_->PostUITask([guarded_self]() {
                if (guarded_self) {
                    guarded_self->update();
                    guarded_self->toggle_++;
                }
            });
        });
    }

    void MediaRecordSignLab::paintEvent(QPaintEvent* event) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        QPen pen(0x999999);
        pen.setWidth(2);
        painter.setPen(pen);
        QPen font_pen;
        if (toggle_ % 2 == 0) {
            painter.setBrush(QBrush(0xff0000));
            font_pen.setColor(QColor(0xffffff));
        }
        else {
            painter.setBrush(QBrush(0xffffff));
            font_pen.setColor(QColor(0xff0000));
        }
        painter.drawRoundedRect(this->rect(), 2, 2);
        painter.save();
        QFont font{ "Microsoft YaHei" };
        font.setPixelSize(13);
        font.setBold(true);
        painter.setPen(font_pen);
        painter.setFont(font);
        painter.drawText(this->rect(), Qt::AlignCenter, "REC");
        painter.restore();
    }

}
