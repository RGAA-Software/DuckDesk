//
// Created by RGAA on 13/11/2025.
//

#ifndef GAMMARAYPREMIUM_TC_CIRCLE_INDICATOR_H
#define GAMMARAYPREMIUM_TC_CIRCLE_INDICATOR_H

#include <QWidget>
#include <QPainter>

namespace tc
{

    class TcCircleIndicator : public QWidget {
    public:

        enum class State {
            kOk,
            kWarn,
            kError,
            kUnknown
        };

        explicit TcCircleIndicator(QWidget* parent = nullptr);
        ~TcCircleIndicator() override;
        void SetState(State st);

        void paintEvent(QPaintEvent *event) override;

    private:
        State state_ = State::kUnknown;
        int ok_color_ = 0x00ee00;
        int warn_color_ = 0xeeee00;
        int err_color_ = 0xee0000;
    };

}

#endif //GAMMARAYPREMIUM_TC_CIRCLE_INDICATOR_H
