//
// Created by RGAA on 22/03/2025.
//

#ifndef GAMMARAY_TC_PUSHBUTTON_H
#define GAMMARAY_TC_PUSHBUTTON_H

#include "translator/tc_translator.h"
#include <QPushButton>

namespace tc
{

    class TcPushButton : public QPushButton, public TcTranslator {
    public:
        TcPushButton(QWidget* parent = nullptr);
        void SetTextId(const QString &id) override;
        void OnTranslate(tc::LanguageKind kind) override;
    };

}

#endif //GAMMARAY_TC_PUSHBUTTON_H
