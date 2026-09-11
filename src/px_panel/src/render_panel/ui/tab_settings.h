//
// Created by RGAA on 2024-04-09.
//

#ifndef TC_SERVER_STEAM_TABSETTINGS_H
#define TC_SERVER_STEAM_TABSETTINGS_H

#include "tab_base.h"
#include <QPointer>
#include <QStackedWidget>
#include <QPushButton>
#include <map>

namespace px
{

    enum class StTabName {
        kStGeneral,
        kStNetwork,
        kStSecurity,
        kStController,
        kStAboutMe,
    };

    class TabSettings : public TabBase {
    public:

        explicit TabSettings(const std::shared_ptr<PxApplication>& app, QWidget* parent = nullptr);
        ~TabSettings() override;

        void OnTabShow() override;
        void OnTabHide() override;

    private:
        void ChangeTab(const StTabName& tn);

    private:
        std::map<StTabName, QPointer<TabBase>> tabs_;
        QPointer<QStackedWidget> stacked_widget_;
        QPointer<QPushButton> btn_network_;
        QPointer<QPushButton> btn_security_;
        QPointer<QPushButton> btn_input_;
        QPointer<QPushButton> btn_controller_;
        QPointer<QPushButton> btn_about_me_;
    };

}

#endif //TC_SERVER_STEAM_TABGAME_H
