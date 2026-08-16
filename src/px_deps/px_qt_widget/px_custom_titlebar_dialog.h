//
// Created by RGAA on 23/03/2025.
//

#ifndef PX_TC_CUSTOM_TITLEBAR_DIALOG_H
#define PX_TC_CUSTOM_TITLEBAR_DIALOG_H

#include <QDialog>

namespace px
{

    class NoMarginVLayout;

    class TcCustomTitleBarDialog : public QDialog {
    public:
        explicit TcCustomTitleBarDialog(const QString& title, QWidget* parent = nullptr);
        void CenterDialog(QDialog* dialog);
    protected:
        NoMarginVLayout* root_layout_ = nullptr;
    };

}
#endif //PX_TC_CUSTOM_TITLEBAR_DIALOG_H
