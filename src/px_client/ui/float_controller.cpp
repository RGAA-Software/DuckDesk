//
// Created by RGAA on 3/07/2024.
//

#include "float_controller.h"
#include "app_color_theme.h"
#include "px_common_new/log.h"
#include "px_client/ct_client_context.h"
#include <QPointer>
#include <QTimer>
#include <algorithm>

const static std::string kPosX = "float_button_pos_x";
const static std::string kPosY = "float_button_pos_y";

namespace px
{
    FloatController::FloatController(const std::shared_ptr<ClientContext>& ctx, QWidget* parent)
        : FloatOverlayWindow(ctx, parent, QSize(48, 48), 24, 6, 0) {
        setObjectName("remoteControlFloatingButton");
        setAccessibleName(tr("Remote control menu"));
        const QImage image(":resources/px_icon.png");
        pixmap_ = QPixmap::fromImage(image);
        pixmap_ = pixmap_.scaled(25, 25, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        setMouseTracking(true);
        this->setStyleSheet("background:#00000000;");
        const QPointer<FloatController> guarded_self(this);
        QTimer::singleShot(200, [guarded_self]() {
            if (guarded_self) {
                guarded_self->ReCalculatePosition();
            }
        });
    }

    void FloatController::paintEvent(QPaintEvent *event) {
        QPainter painter(this);
        const QColor background = pressed_ ? QColor(0xdfdfdf)
                                           : enter_ ? QColor(0xf5f5f5)
                                                    : QColor(0xffffff);
        PaintCard(painter, background, QColor(220, 224, 230));
        painter.setPen(Qt::NoPen);
        if (pressed_) {
            painter.setOpacity(0.88);
        } else if (enter_) {
            painter.setOpacity(0.95);
        }
        const QRect card = CardRect();
        painter.drawPixmap(card.center().x() - pixmap_.width() / 2,
                           card.center().y() - pixmap_.height() / 2,
                           pixmap_);
        BaseWidget::paintEvent(event);
    }

    void FloatController::mousePressEvent(QMouseEvent *event) {
        pressed_ = true;
        drag_position_ = event->globalPos() - frameGeometry().topLeft();
        press_global_position_ = event->globalPos();
        has_moved_ = false;
        event->accept();
        repaint();
    }

    void FloatController::mouseReleaseEvent(QMouseEvent *event) {
        pressed_ = false;
        event->accept();
        repaint();

        const QRect owner_rect = OwnerGlobalRect();
        const int available_width = std::max(1, owner_rect.width() - width());
        const int available_height = std::max(1, owner_rect.height() - height());
        const float x_pos_ratio = (this->pos().x() - owner_rect.left()) * 1.0f / available_width;
        const float y_pos_ratio = (this->pos().y() - owner_rect.top()) * 1.0f / available_height;
        context_->SaveKeyValue(kPosX, std::to_string(x_pos_ratio));
        context_->SaveKeyValue(kPosY, std::to_string(y_pos_ratio));

        if (!has_moved_ && click_listener_) {
            click_listener_();
        }

        has_moved_ = false;
    }

    void FloatController::mouseMoveEvent(QMouseEvent *event) {
        if (pressed_) {
            if ((event->globalPos() - press_global_position_).manhattanLength() >= 5) {
                has_moved_ = true;
            }
            if (has_moved_) {
                MoveClamped(event->globalPos() - drag_position_);
            }
        }
        if (move_listener_ && pressed_) {
            move_listener_();
        }
    }

    void FloatController::enterEvent(QEnterEvent *event) {
        enter_ = true;
        repaint();
    }

    void FloatController::leaveEvent(QEvent *event) {
        enter_ = false;
        repaint();
    }

    bool FloatController::HasMoved() const {
        return has_moved_;
    }

    void FloatController::ReCalculatePosition() {
        const QRect owner_rect = OwnerGlobalRect();
        if (!owner_rect.isValid()) {
            return;
        }
        const auto saved_x = context_->GetValueByKey(kPosX);
        const auto saved_y = context_->GetValueByKey(kPosY);
        float x_ratio = std::atof(saved_x.c_str());
        float y_ratio = std::atof(saved_y.c_str());
        if (saved_x.empty() || saved_y.empty()) {
            x_ratio = 0.02f;
            y_ratio = 0.08f;
        }
        x_ratio = std::clamp(x_ratio, 0.0f, 1.0f);
        y_ratio = std::clamp(y_ratio, 0.0f, 1.0f);
        const int x = owner_rect.left() + static_cast<int>((owner_rect.width() - width()) * x_ratio);
        const int y = owner_rect.top() + static_cast<int>((owner_rect.height() - height()) * y_ratio);
        MoveClamped(QPoint(x, y));
    }
}
