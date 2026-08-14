#pragma once

#include <QWidget>
#include <QLabel>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QBitmap>
#include <QPixmap>

#include <memory>
#include <mutex>

namespace tc
{
    class RoundImageDisplay : public QLabel {

    public:
        RoundImageDisplay(const QString &path, int width, int height, int radius, QWidget *parent = nullptr);
        RoundImageDisplay(const QPixmap &pixmap, int radius, QWidget *parent = nullptr);
        ~RoundImageDisplay() override;
        void paintEvent(QPaintEvent *event) override;
        void resizeEvent(QResizeEvent *event) override;
        void mousePressEvent(QMouseEvent *ev) override;
        void mouseReleaseEvent(QMouseEvent *ev) override;
        void enterEvent(QEnterEvent *event) override;
        void leaveEvent(QEvent *event) override;
        void UpdatePixmap(const QPixmap& pixmap);
        void SetBorder(int width, int color);
        void SetOnClickListener(std::function<void(QWidget* w)>&& l) {
            click_listener_ = l;
        }

    private:
        QBitmap mask;
        QPixmap pixmap;
        int radius;
        std::function<void(QWidget* w)> click_listener_;
        QCursor cursor_;
        int border_width_ = 0;
        int border_color_ = 0;
    };

}