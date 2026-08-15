#pragma once

#include <QFont>
#include <QEvent>
#include <QColor>
#include <QPushButton>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPixmap>
#include <QLabel>
#include "translator/tc_translator.h"

namespace tc
{

    class CustomTabBtn : public QPushButton, public TcTranslator {
    public:
        explicit CustomTabBtn(const QColor& inactive_color, const QColor& hover_color, QWidget *parent = 0);
        ~CustomTabBtn() override;

        void SetSelectedFontColor(const std::string &color);
        void SetBorderRadius(int r) { border_radius_ = r; }

        void ToActiveStatus();
        void ToInActiveStatus();

        void AddIcon(const QString& selected_uri, const QString& normal_uri, int size, int offset_x);
        void SetTextId(const QString &id) override;
        void OnTranslate(tc::LanguageKind kind) override;

        void mousePressEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;
        void paintEvent(QPaintEvent *event) override;
        void enterEvent(QEnterEvent *event) override;
        void leaveEvent(QEvent *event) override;
        void resizeEvent(QResizeEvent *event) override;

    private:
        void SetIconSelected();
        void SetIconNormal();

    private:
        QColor hover_color_ = QColor(255, 160, 90);
        QColor inactive_color_ = QColor(160, 160, 160);
        bool active_ = false;
        bool enter_ = false;
        QString text_;
        int border_radius_ = 0;
        QString selected_img_uri_;
        QString normal_img_uri_;
        QLabel* lbl_icon_ = nullptr;
        int offset_x_ = 0;
    };

}