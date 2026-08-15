//
// Created by RGAA on 22/03/2025.
//

#ifndef GAMMARAY_TC_LABEL_H
#define GAMMARAY_TC_LABEL_H

#include <QLabel>
#include "click_listener.h"
#include "translator/tc_translator.h"

namespace tc
{

    class TcLabel : public QLabel, public TcTranslator {
    public:
        explicit TcLabel(QWidget* parent = nullptr);
        ~TcLabel() override;

        void SetTextId(const QString& id) override;
        void OnTranslate(LanguageKind kind) override;
        void enterEvent(QEnterEvent *event) override;
        void leaveEvent(QEvent *event) override;
        void mousePressEvent(QMouseEvent *ev) override;
        void mouseReleaseEvent(QMouseEvent *ev) override;
        void SetOnClickListener(OnClickListener&& listener);

    private:
        bool enter_ = false;
        bool pressed_ = false;
        OnClickListener listener_;
        QCursor cursor_;
    };

}

#endif //GAMMARAY_TC_LABEL_H
