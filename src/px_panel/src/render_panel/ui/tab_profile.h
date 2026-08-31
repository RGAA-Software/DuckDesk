//
// Created by RGAA on 22/03/2025.
//

#ifndef PX_TAB_PROFILE_H
#define PX_TAB_PROFILE_H

#include "tab_base.h"
#include <QHBoxLayout>
#include <QListWidget>
#include <QPointer>
#include <QStackedWidget>

namespace px
{

    class NoMarginHLayout;
    class NoMarginVLayout;
    class TabProfile : public TabBase {
    public:
        TabProfile(
            const std::shared_ptr<PxApplication>& app,
            QWidget* parent); // NOLINT(gammaray-raw-pointer-boundary) Qt parent API

        void OnTabShow() override;
        void OnTabHide() override;

        void dragEnterEvent(QDragEnterEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary) Qt event ABI
        void dragMoveEvent(QDragMoveEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary) Qt event ABI
        void dropEvent(QDropEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary) Qt event ABI

    private:
        void AddLeftProfileInfo();
        void AddRightDetailInfo();
        QPointer<QWidget> AddEmptyWidget();
        QPointer<QWidget> AddOnlineInfoWidget();
        QPointer<QWidget> AddOfflineInfoWidget();

    private:
        QPointer<QHBoxLayout> root_layout_;
        QPointer<QListWidget> list_widget_;
        QPointer<QStackedWidget> stacked_widget_;
    };

}

#endif //PX_TAB_PROFILE_H
