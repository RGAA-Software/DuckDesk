//
// Created by RGAA on 23/03/2025.
//

#ifndef GAMMARAY_TC_DIALOG_H
#define GAMMARAY_TC_DIALOG_H

#include "tc_custom_titlebar_dialog.h"

namespace tc
{

    constexpr auto kDoneCancel = 0;
    constexpr auto kDoneOk = 1;

    class TcDialog : public TcCustomTitleBarDialog {
    public:
        TcDialog(const QString& title, const QString& msg, QWidget* parent = nullptr);

    private:

    };
}

#endif //GAMMARAY_TC_DIALOG_H
