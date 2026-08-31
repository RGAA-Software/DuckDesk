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
#include <functional>

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
    class PxAsyncScope;
    class LatestSerialRequestGate;

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
        void RefreshRandomPassword();
        void SaveDeviceName(const QString& device_name);
        void PublishDesktopLink(
            std::string desktop_link,
            std::string desktop_link_raw);

    private:
        std::reference_wrapper<PxSettings> server_settings_;
        QPixmap qr_pixmap_;
        QPointer<QLabel> lbl_machine_code_;
        QPointer<QLabel> lbl_machine_random_pwd_;
        QPointer<QLineEdit> edt_machine_name_;
        QPointer<QLineEdit> lbl_detailed_info_;
        QPointer<QLineEdit> edt_web_client_url_;
        QPointer<TcQRWidget> lbl_qr_code_;
        QPointer<RoundImageDisplay> qr_avatar_;
        QPointer<StreamContent> stream_content_;
        QPointer<QComboBox> remote_devices_;
        QPointer<TcImageButton> btn_hide_random_pwd_;
        std::shared_ptr<StreamDBOperator> stream_db_mgr_ = nullptr;
        std::vector<std::shared_ptr<px_console::ConsoleStream>> recent_streams_;
        QPointer<TcCircleIndicator> console_indicator_;
        std::shared_ptr<PxAsyncScope> device_request_scope_;
        std::shared_ptr<LatestSerialRequestGate> random_password_gate_;
        std::shared_ptr<LatestSerialRequestGate> device_name_gate_;
        std::shared_ptr<LatestSerialRequestGate> desktop_link_gate_;
    };
}

#endif //TC_SERVER_STEAM_TAB_SERVER_H
