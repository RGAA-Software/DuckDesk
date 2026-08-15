//
// Created by RGAA on 22/03/2025.
//

#include "px_pushbutton.h"

namespace px
{

    TcPushButton::TcPushButton(QWidget* parent) : QPushButton(parent), TcTranslator(this) {

    }

    void TcPushButton::SetTextId(const QString& id) {
        if (id.isEmpty()) {
            return;
        }
        TcTranslator::SetTextId(id);
        this->setText(tcTr(text_id_));
    }

    void TcPushButton::OnTranslate(LanguageKind kind) {
        TcTranslator::OnTranslate(kind);
        if (text_id_.isEmpty()) {
            return;
        }
        this->setText(tcTr(text_id_));
    }
    
}