#include "round_img_display.h"

#include <QPainter>
#include <QPixmap>
#include <QBitmap>
#include <QDebug>
#include <QImage>
#include <QPainterPath>

namespace tc
{
    RoundImageDisplay::RoundImageDisplay(const QString &path, int width, int height, int radius, QWidget *parent)
            : QLabel(parent) {
        setFixedSize(width, height);
        this->radius = radius;
        pixmap = QPixmap(path);
        if (!pixmap.isNull()) {
            pixmap = pixmap.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }

    RoundImageDisplay::RoundImageDisplay(const QPixmap &pixmap, int radius, QWidget *parent) : QLabel(parent) {
        this->radius = radius;
        this->pixmap = pixmap;
    }

    RoundImageDisplay::~RoundImageDisplay() {

    }

    void RoundImageDisplay::resizeEvent(QResizeEvent *event) {
        auto size = event->size();
        mask = QBitmap(size);
        //qDebug() << "size :" << size;
    }

    void RoundImageDisplay::UpdatePixmap(const QPixmap& px) {
        this->pixmap = px;
        this->repaint();
    }

    void RoundImageDisplay::paintEvent(QPaintEvent *event) {
        QLabel::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing, true);
        painter.setRenderHints(QPainter::SmoothPixmapTransform, true);

        if (!pixmap.isNull() && painter.isActive()) {
            QPainterPath path;
            path.addRoundedRect(mask.rect(), radius, radius);
            painter.setClipPath(path);
            painter.drawPixmap(0, 0, pixmap);
        } else {
            painter.setBrush(QBrush(QColor(0x33, 0x33, 0x33)));
            painter.drawRoundedRect(0, 0, mask.width(), mask.height(), radius, radius);
        }

        if (border_width_ > 0) {
            painter.setBrush(Qt::NoBrush);
            QPen pen;
            pen.setWidth(border_width_);
            pen.setColor(border_color_);
            painter.setPen(pen);
            int offset = border_width_;
            painter.drawRoundedRect(this->rect(), this->width()/2, this->height()/2);
        }

    }

    void RoundImageDisplay::mousePressEvent(QMouseEvent *ev)  {
        QLabel::mousePressEvent(ev);
    }

    void RoundImageDisplay::mouseReleaseEvent(QMouseEvent *ev) {
        QLabel::mouseReleaseEvent(ev);
        if (click_listener_) {
            click_listener_(this);
        }
    }

    void RoundImageDisplay::enterEvent(QEnterEvent *event) {
        if (click_listener_) {
            cursor_ = cursor();
            setCursor(Qt::PointingHandCursor);
        }
        QLabel::enterEvent(event);
    }

    void RoundImageDisplay::leaveEvent(QEvent *event) {
        if (click_listener_) {
            setCursor(cursor_);
        }
        QLabel::leaveEvent(event);
    }

    void RoundImageDisplay::SetBorder(int width, int color) {
        border_width_ = width;
        border_color_ = color;
        update();
    }

}