//
// Created by RGAA on 2024-06-10.
//

#ifndef PX_ST_NETWORK_H
#define PX_ST_NETWORK_H

#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QPointer>
#include <functional>
#include "tab_base.h"

namespace px
{

    class PxApplication;
    class MessageListener;
    class ConsoleAccessInfo;
    class PxAsyncScope;
    class LatestSerialRequestGate;

    class StNetwork : public TabBase {
    public:
        explicit StNetwork(const std::shared_ptr<PxApplication>& app, QWidget* parent = nullptr);
        ~StNetwork() override;

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
        std::reference_wrapper<PxSettings> network_settings_;
        QPointer<QTextEdit> edt_console_access_;
        QPointer<QLineEdit> edt_console_server_port_;
        QPointer<QLineEdit> edt_relay_server_port_;
        QPointer<QLineEdit> edt_node_access_host_;
        std::shared_ptr<PxAsyncScope> request_scope_;
        std::shared_ptr<LatestSerialRequestGate> verify_gate_;
        std::shared_ptr<LatestSerialRequestGate> save_gate_;
    };

}

#endif //PX_ST_ABOUT_ME_H
