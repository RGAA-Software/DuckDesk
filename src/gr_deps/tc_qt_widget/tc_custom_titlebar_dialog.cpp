//
// Created by RGAA on 23/03/2025.
//

#include <dwmapi.h>
#include "tc_custom_titlebar_dialog.h"
#include "tc_custom_titlebar.h"
#include "no_margin_layout.h"
#include "tc_qt_widget/tc_pushbutton.h"
#include "tc_qt_widget/widget_helper.h"
#include "tc_qt_widget/tc_custom_titlebar.h"
#include "tc_qt_widget/widgetframe/mainwindow_wrapper.h"
#include <QApplication>
#include <QScreen>

namespace tc
{

    TcCustomTitleBarDialog::TcCustomTitleBarDialog(const QString& title, QWidget* parent) : QDialog(parent) {
        //setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
        WidgetHelper::SetTitleBarColor(this);
        setContentsMargins(0, 0, 0, 0);
        setWindowTitle(title);
        root_layout_ = new NoMarginVLayout();
        setLayout(root_layout_);
    }

    void TcCustomTitleBarDialog::CenterDialog(QDialog* dialog) {
        QRect screenGeometry = QApplication::primaryScreen()->geometry();
        int x = (screenGeometry.width() - dialog->width()) / 2;
        int y = (screenGeometry.height() - dialog->height()) / 2;
        dialog->move(x, y);
    }
}