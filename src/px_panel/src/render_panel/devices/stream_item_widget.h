//
// Created by RGAA on 2023/8/19.
//

#ifndef SAILFISH_CLIENT_PC_STREAMITEMWIDGET_H
#define SAILFISH_CLIENT_PC_STREAMITEMWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QPen>
#include <QBrush>
#include <QPaintEvent>
#include <QPixmap>
#include <QPushButton>
#include <QLabel>
#include <memory>

namespace px_console
{
    class ConsoleStream;
}

namespace px
{

    class TcPushButton;
    class TcLabel;

    using OnConnectListener = std::function<void()>;
    using OnMenuListener = std::function<void()>;

    class StreamItemWidget : public QWidget {
    public:

        explicit StreamItemWidget(const std::shared_ptr<px_console::ConsoleStream>& item, int bg_color, QWidget* parent = nullptr);
        ~StreamItemWidget() override;

        void paintEvent(QPaintEvent *event) override;
        void enterEvent(QEnterEvent *event) override;
        void leaveEvent(QEvent *event) override;
        void resizeEvent(QResizeEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;

        void SetOnConnectListener(OnConnectListener&& listener);
        void SetOnMenuListener(OnMenuListener&& listener);
        void SetDirectConnectedState(bool connected);
        void SetRelayConnectedState(bool connected);
        void SetConsoleConnectedState(bool connected);
        void Update();

        std::string GetStreamId();
        void ShowConnecting();

    private:
        std::shared_ptr<px_console::ConsoleStream> item_;
        int bg_color_ = 0;
        QPixmap icon_;
        QPixmap bg_pixmap_;
        bool enter_ = false;
        QBitmap mask_;
        int radius_ = 10;
        TcPushButton* btn_conn_ = nullptr;
        TcLabel* lbl_connecting_ = nullptr;
        QWidget* btn_option_ = nullptr;
        OnConnectListener conn_listener_;
        OnMenuListener menu_listener_;
        bool direct_connected_ = false;
        bool relay_connected_ = false;
        bool console_connected_ = false;
        TcLabel* work_mode_ = nullptr;
        QLabel* state_tooltip_ = nullptr;
        QWidget* state_tooltip_container_ = nullptr;
    };

}

#endif //SAILFISH_CLIENT_PC_STREAMITEMWIDGET_H
