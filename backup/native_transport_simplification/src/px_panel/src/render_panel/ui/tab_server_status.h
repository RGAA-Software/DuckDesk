//
// Created by RGAA on 4/02/2025.
//

#ifndef PX_TAB_SERVER_STATUS_H
#define PX_TAB_SERVER_STATUS_H

#include "tab_base.h"

#include <QPointer>
#include <QStackedWidget>

namespace px
{

    class RnApp;
    class RnEmpty;
    class MessageListener;
    class QtCircle;
    class QtVertical;
    class PxStatistics;
    class TcLabel;

    class TabServerStatus : public TabBase {
    public:
        explicit TabServerStatus(const std::shared_ptr<PxApplication>& app, QWidget *parent);
        ~TabServerStatus() override;

        void OnTabShow() override;
        void OnTabHide() override;

    private:
        void RefreshVigemState(bool ok);
        void RefreshServerState(bool ok);
        void RefreshServiceState(bool ok);
        static void RefreshIndicatorState(
            const QPointer<TcLabel>& indicator, bool ok);
        void RefreshUIEverySecond();

    private:
        QPointer<TcLabel> lbl_vigem_state_;
        QPointer<TcLabel> lbl_renderer_state_;
        QPointer<TcLabel> lbl_service_state_;
        QPointer<TcLabel> lbl_audio_format_;
        QPointer<QtVertical> spectrum_vertical_;
        QPointer<QStackedWidget> rn_stack_;
        QPointer<RnApp> rn_app_;

    };

}

#endif //PX_TAB_SERVER_STATUS_H
