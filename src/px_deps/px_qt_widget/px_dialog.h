//
// Created by RGAA on 23/03/2025.
//

#ifndef PX_TC_DIALOG_H
#define PX_TC_DIALOG_H

#include "px_custom_titlebar_dialog.h"

namespace px
{

    constexpr auto kDoneCancel = 0;
    constexpr auto kDoneOk = 1;

    class TcDialog : public TcCustomTitleBarDialog {
    public:
        TcDialog(const QString& title, const QString& msg, QWidget* parent = nullptr);

    private:

    };
}

#endif //PX_TC_DIALOG_H
