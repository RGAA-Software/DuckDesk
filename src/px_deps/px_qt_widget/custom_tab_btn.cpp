#include "custom_tab_btn.h"

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QRadialGradient>
#include <QLabel>

namespace tc
{
    
    CustomTabBtn::CustomTabBtn(const QColor& inactive_color, const QColor& hover_color, QWidget *parent)
        : QPushButton(parent), TcTranslator(this) {
        this->inactive_color_ = inactive_color;
        this->hover_color_ = hover_color;
    }

    CustomTabBtn::~CustomTabBtn() {

    }

    void CustomTabBtn::SetSelectedFontColor(const std::string &color) {
        auto style = std::format(R"(
            QPushButton {{
                background:none;
                border: none;
                color: {};
            }}

            QPushButton:hover {{

            }}
            QPushButton:pressed{{

            }}
        )", color);
        this->setStyleSheet(style.c_str());
    }

    void CustomTabBtn::mousePressEvent(QMouseEvent *event) {
        QPushButton::mousePressEvent(event);
    }

    void CustomTabBtn::mouseReleaseEvent(QMouseEvent *event) {
        QPushButton::mouseReleaseEvent(event);
    }

    void CustomTabBtn::SetTextId(const QString &id) {
        if (id.isEmpty()) {
            return;
        }
        TcTranslator::SetTextId(id);
        this->text_ = tcTr(text_id_);
        repaint();
    }

    void CustomTabBtn::OnTranslate(tc::LanguageKind kind) {
        TcTranslator::OnTranslate(kind);
        if (text_id_.isEmpty()) {
            return;
        }
        this->text_ = tcTr(text_id_);
        repaint();
    }

    void CustomTabBtn::paintEvent(QPaintEvent *event) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

        auto font = painter.font();
        //font.setBold(true);
        font.setPixelSize(13);
        painter.setFont(font);

        if (!active_) {
            QRadialGradient gradient(width() / 2, height() / 2, width() * 1.2, width() / 3, height() / 2);
            gradient.setColorAt(0, hover_color_);
            gradient.setColorAt(1, 0xffffff);

            int font_color = 0;
            QBrush brush(Qt::BrushStyle::SolidPattern);
            if (enter_) {
                brush = QBrush(gradient);
                font_color = 0xffffff;
            } else {
                brush.setColor(enter_ ? hover_color_ : inactive_color_);
                font_color = 0x333333;
            }

            painter.setBrush(brush);
            painter.setPen(Qt::NoPen);
            int offset = 0;
            painter.drawRoundedRect(0 + offset, 0 + offset, width() - 2 * offset, height() - 2 * offset, border_radius_, border_radius_);

            QFontMetrics fm = painter.fontMetrics();
            QSize s = fm.size(0, text_);
            int width = s.width();//fm.width(text_);
            int height = fm.descent() + fm.ascent();
            painter.setPen(QPen(QColor(font_color)));
            if (!lbl_icon_) {
                painter.drawText((this->width() - width) / 2, (this->height() - height) / 2, width, height, 0, text_);
            }
            else {
                painter.drawText(offset_x_ + lbl_icon_->width() + 10, (this->height() - height) / 2, width, height, 0, text_);
            }
        }
        else {
            //QRect rect(0, 0, width(), height());
            //painter.drawPixmap(rect, active_bg);
            QRadialGradient gradient(width() / 2, height() / 2, width() * 1.2, width() / 4, height() / 2);
            gradient.setColorAt(0, hover_color_);
            gradient.setColorAt(1, QColor::fromRgbF(0, 0, 0, 0));
            auto brush = QBrush(gradient);
            painter.setBrush(brush);
            painter.setPen(Qt::NoPen);
            int offset = 0;
            painter.drawRoundedRect(0 + offset, 0 + offset, width() - 2 * offset, height() - 2 * offset, border_radius_, border_radius_);

            QFontMetrics fm = painter.fontMetrics();
            QSize s = fm.size(0, text_);
            int width = s.width();//fm.width(text_);
            int height = fm.descent() + fm.ascent();
            painter.setPen(QPen(QColor(0xffffff)));
            if (!lbl_icon_) {
                painter.drawText((this->width() - width) / 2, (this->height() - height) / 2, width, height, 0, text_);
            }
            else {
                painter.drawText(offset_x_ + lbl_icon_->width() + 10, (this->height() - height) / 2, width, height, 0, text_);
            }
        }

        QPushButton::paintEvent(event);
    }

    void CustomTabBtn::enterEvent(QEnterEvent *event) {
        QPushButton::enterEvent(event);
        enter_ = true;
        SetIconSelected();
        repaint();
    }

    void CustomTabBtn::leaveEvent(QEvent *event) {
        QPushButton::leaveEvent(event);
        enter_ = false;
        if (!active_) {
            SetIconNormal();
        }
        repaint();
    }

    void CustomTabBtn::ToActiveStatus() {
        active_ = true;
        SetIconSelected();
        repaint();
    }

    void CustomTabBtn::ToInActiveStatus() {
        active_ = false;
        SetIconNormal();
        repaint();
    }

    void CustomTabBtn::AddIcon(const QString& selected_uri, const QString& normal_uri, int size, int offset_x) {
        this->selected_img_uri_ = selected_uri;
        this->normal_img_uri_ = normal_uri;
        this->offset_x_ = offset_x;
        lbl_icon_ = new QLabel(this);
        lbl_icon_->setFixedSize(size, size);
    }

    void CustomTabBtn::resizeEvent(QResizeEvent *event) {
        if (!lbl_icon_) {
            return;
        }
        lbl_icon_->setGeometry(offset_x_, (this->height()-lbl_icon_->height())/2, lbl_icon_->width(), lbl_icon_->height());
    }

    void CustomTabBtn::SetIconSelected() {
        QString style = R"(background-image: url(%1);
                        background-repeat: no-repeat;
                        background-position: center;
                    )";
        style = style.arg(selected_img_uri_);
        if (lbl_icon_) {
            lbl_icon_->setStyleSheet(style);
        }
    }

    void CustomTabBtn::SetIconNormal() {
        QString style = R"(background-image: url(%1);
                        background-repeat: no-repeat;
                        background-position: center;
                    )";
        style = style.arg(normal_img_uri_);
        if (lbl_icon_) {
            lbl_icon_->setStyleSheet(style);
        }
    }

}