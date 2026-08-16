//
// Created by RGAA on 2024/4/9.
//

#ifndef TC_SERVER_STEAM_TAB_SERVER_H
#define TC_SERVER_STEAM_TAB_SERVER_H

#include "tab_base.h"
#include "px_cms_client/cms_stream.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QStackedWidget>
#include <QLabel>
#include <QComboBox>
#include <QProcess>

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
    class RunningStreamManager;
    class StreamDBOperator;
    class TcPasswordInput;
    class RoundImageDisplay;
    class TcCircleIndicator;
    class PxStatistics;

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
        PxStatistics* stat_ = nullptr;
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
        TcPasswordInput* password_input_ = nullptr;
        QComboBox* remote_devices_ = nullptr;
        TcImageButton* btn_hide_random_pwd_ = nullptr;
        std::shared_ptr<RunningStreamManager> running_stream_mgr_ = nullptr;
        std::shared_ptr<StreamDBOperator> stream_db_mgr_ = nullptr;
        std::vector<std::shared_ptr<px_cms::CmsStream>> recent_streams_;
        TcCircleIndicator* cms_indicator_ = nullptr;
        TcCircleIndicator* relay_indicator_ = nullptr;
        TcCircleIndicator* relay_ft_indicator_ = nullptr;
        // last computed alive state, for transition-only logging
        bool last_relay_alive_ = false;
        bool last_relay_ft_alive_ = false;
    };
}

#endif //TC_SERVER_STEAM_TAB_SERVER_H
