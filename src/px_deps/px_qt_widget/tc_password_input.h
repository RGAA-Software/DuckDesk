//
// Created by RGAA on 29/03/2025.
//

#ifndef GAMMARAY_TC_PASSWORD_INPUT_H
#define GAMMARAY_TC_PASSWORD_INPUT_H

#include <QWidget>
#include <QLineEdit>

namespace tc
{

    class TcImageButton;

    class TcPasswordInput : public QWidget {
    public:
        explicit TcPasswordInput(QWidget* parent);
        void resizeEvent(QResizeEvent *event) override;
        void SetPassword(const QString& password);
        QString GetPassword();

    private:
        QLineEdit* remote_password_ = nullptr;
        TcImageButton* btn_password_echo_change_ = nullptr;
    };

}

#endif //GAMMARAY_TC_PASSWORD_INPUT_H
