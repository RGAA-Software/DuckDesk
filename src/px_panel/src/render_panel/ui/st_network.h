//
// Created by RGAA on 2024-06-10.
//

#ifndef PX_ST_NETWORK_H
#define PX_ST_NETWORK_H

#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QTextEdit>
#include "tab_base.h"

namespace px
{

    class PxApplication;
    class MessageListener;
    class ConsoleAccessInfo;

    class StNetwork : public TabBase {
    public:
        explicit StNetwork(const std::shared_ptr<PxApplication>& app, QWidget* parent = nullptr);
        ~StNetwork() override = default;

        void OnTabShow() override;
        void OnTabHide() override;

    private:
        std::shared_ptr<ConsoleAccessInfo> ParseConsoleAccessInfo(const std::string& info);
        void DisplayConsoleAccessInfo(const std::shared_ptr<ConsoleAccessInfo>& info);
        void SaveConsoleAccessInfo();
        void SearchAccessInfo(bool auto_restart_render);
        void VerifyAccessInfo();
        void Save(bool auto_restart_render);

    private:
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        QTextEdit* edt_console_access_ = nullptr;
        QLineEdit* edt_console_server_host_ = nullptr;
        QLineEdit* edt_console_server_port_ = nullptr;
        QLineEdit* edt_relay_server_host_ = nullptr;
        QLineEdit* edt_relay_server_port_ = nullptr;
        QCheckBox* cb_websocket_ = nullptr;
        QLineEdit* edt_websocket_ = nullptr;
        QCheckBox* cb_udp_kcp_ = nullptr;
        QLineEdit* edt_udp_kcp_ = nullptr;
        QCheckBox* cb_webrtc_ = nullptr;
        QLineEdit* edt_panel_port_ = nullptr;
    };

}

#endif //PX_ST_ABOUT_ME_H
