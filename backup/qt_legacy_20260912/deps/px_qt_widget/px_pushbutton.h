//
// Created by RGAA on 22/03/2025.
//

#ifndef PX_TC_PUSHBUTTON_H
#define PX_TC_PUSHBUTTON_H

#include "translator/px_translator.h"
#include <QPushButton>

namespace px
{

    class TcPushButton : public QPushButton, public TcTranslator {
    public:
        TcPushButton(QWidget* parent = nullptr);
        void SetTextId(const QString &id) override;
        void OnTranslate(px::LanguageKind kind) override;
    };

}

#endif //PX_TC_PUSHBUTTON_H
