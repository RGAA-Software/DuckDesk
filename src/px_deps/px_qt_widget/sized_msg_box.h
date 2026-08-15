//
// Created by RGAA  on 2024/1/17.
//

#ifndef PLC_CONTROLLER_SIZED_MSG_BOX_H
#define PLC_CONTROLLER_SIZED_MSG_BOX_H

#include <QMessageBox>
#include <QString>
#include <QPushButton>
#include <QLabel>
#include <memory>

namespace tc
{
    class TcPushButton;
    class TcCustomTitleBar;

    class SizedMessageBox : public QDialog {
    public:

        static std::shared_ptr<SizedMessageBox> MakeOkBox(const QString& title, const QString& msg);
        static std::shared_ptr<SizedMessageBox> MakeOkCancelBox(const QString& title, const QString& msg);
        static std::shared_ptr<SizedMessageBox> Make2BtnsBox(const QString& title, const QString& msg,
                                                             const QString& btn_left_text, const QString& btn_right_text);
        static std::shared_ptr<SizedMessageBox> MakeErrorOkBox(const QString& title, const QString& msg);
        static std::shared_ptr<SizedMessageBox> MakeInfoOkBox(const QString& title, const QString& msg);

        explicit SizedMessageBox(const QString& title, bool ok, bool cancel, QWidget* parent = nullptr);

        void closeEvent(QCloseEvent *event) override {
            if (closed_callback_) {
                closed_callback_();
            }
            this->done(1);
        }

        void SetCloseCallback(std::function<void()>&& cbk) {
            closed_callback_ = std::move(cbk);
        }

        void paintEvent(QPaintEvent *event) override;
        void resizeEvent(QResizeEvent *) override;

    public:
        QLabel* lbl_message_ = nullptr;
        TcPushButton* btn_cancel_ = nullptr;
        TcPushButton* btn_ok_ = nullptr;
        std::function<void()> closed_callback_;
        TcCustomTitleBar* title_bar_ = nullptr;
        int shadow_offset = 15;
    };
}

#endif //PLC_CONTROLLER_SIZED_MSG_BOX_H
