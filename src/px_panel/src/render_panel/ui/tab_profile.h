//
// Created by RGAA on 22/03/2025.
//

#ifndef PX_TAB_PROFILE_H
#define PX_TAB_PROFILE_H

#include "tab_base.h"
#include <QListWidget>
#include <QStackedWidget>

namespace px
{

    class NoMarginHLayout;
    class NoMarginVLayout;
    class AccountSdk;
    class AccountDevice;
    class AccountProfile;

    class TabProfile : public TabBase {
    public:
        TabProfile(const std::shared_ptr<PxApplication>& app, QWidget *parent);

        void OnTabShow() override;
        void OnTabHide() override;

        void dragEnterEvent(QDragEnterEvent *event) override;
        void dragMoveEvent(QDragMoveEvent *event) override;
        void dropEvent(QDropEvent *event) override;

    private:
        void AddLeftProfileInfo();
        void AddRightDetailInfo();
        void QueryMyDevices();
        QListWidgetItem* AddItem(const std::shared_ptr<AccountDevice>& item_info, int index);
        QWidget* AddEmptyWidget();
        QWidget* AddOnlineInfoWidget();
        QWidget* AddOfflineInfoWidget();

    private:
        NoMarginHLayout* root_layout_ = nullptr;
        QListWidget* list_widget_ = nullptr;
        QStackedWidget* stacked_widget_ = nullptr;
        std::shared_ptr<AccountSdk> acc_sdk_ = nullptr;
    };

}

#endif //PX_TAB_PROFILE_H
