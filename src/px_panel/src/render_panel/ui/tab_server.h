//
// Created by RGAA on 2024/4/9.
//

#ifndef TC_SERVER_STEAM_TAB_SERVER_H
#define TC_SERVER_STEAM_TAB_SERVER_H

#include "tab_base.h"
#include "px_console_client/console_stream.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QStackedWidget>
#include <QLabel>
#include <QComboBox>
#include <QProcess>
#include <QPointer>

namespace px
{
    class RnApp;
    class RnEmpty;
    class MessageListener;
    class QtCircle;
    class PxSettings;
    class TcQRWidget;
    class StreamContent;
    class TcImageButton;
    class StreamDBOperator;
    class RoundImageDisplay;
    class TcCircleIndicator;

    class TabServer : public TabBase {
    public:
        explicit TabServer(const std::shared_ptr<PxApplication>& app, QWidget *parent);
        ~TabServer() override;
        void OnTabShow() override;
        void OnTabHide() override;
        void RegisterMessageListener();
        void resizeEvent(QResizeEvent *event) override;

    private:
        void UpdateQRCode();
        void UpdateWebClientUrl();
        void SetDeviceRandomPwdVisibility();
        void UpdateServerState();

    private:
        PxSettings* settings_ = nullptr;
        QPixmap qr_pixmap_;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        QLabel* lbl_machine_code_ = nullptr;
        QLabel* lbl_machine_random_pwd_ = nullptr;
        QLineEdit* edt_machine_name_ = nullptr;
        QLineEdit* lbl_detailed_info_ = nullptr;
        QLineEdit* edt_web_client_url_ = nullptr;
        TcQRWidget* lbl_qr_code_ = nullptr;
        RoundImageDisplay* qr_avatar_ = nullptr;
        StreamContent* stream_content_ = nullptr;
        QComboBox* remote_devices_ = nullptr;
        TcImageButton* btn_hide_random_pwd_ = nullptr;
        std::shared_ptr<StreamDBOperator> stream_db_mgr_ = nullptr;
        std::vector<std::shared_ptr<px_console::ConsoleStream>> recent_streams_;
        QPointer<TcCircleIndicator> console_indicator_;
    };
}

#endif //TC_SERVER_STEAM_TAB_SERVER_H
